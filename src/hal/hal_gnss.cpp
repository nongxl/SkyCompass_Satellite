#include "hal_gnss.h"
#include "core/log_manager.h"
#include <M5Cardputer.h>
#include "MultipleSatellite.h"

class M5Gnss : public HalGnss {
private:
    GnssData _data;
    GnssConfig _config;
    bool _enabled;
    bool _newData;
    bool _isInitialized;
    bool _isInStandby;
    bool _isProbing;
    MultipleSatellite* _gps;
    unsigned long _lastNmeaTime;
    
    static const int DEFAULT_RX_PIN = 15;
    static const int DEFAULT_TX_PIN = 13;
    static const uint32_t DEFAULT_BAUD = 115200;
    
    static uint32_t _gpsChars;
    static uint32_t _gpsSentences;

public:
    M5Gnss() : _enabled(false), _newData(false), _isInitialized(false), _isInStandby(false), _isProbing(false), _gps(nullptr), _lastNmeaTime(0) {
        _data.latitude = 0.0;
        _data.longitude = 0.0;
        _data.altitude = 0.0;
        _data.year = 0;
        _data.month = 0;
        _data.day = 0;
        _data.hour = 0;
        _data.minute = 0;
        _data.second = 0;
        _data.satellites = 0;
        _data.accuracy = 0.0;
        _data.speed = 0.0;
        _data.course = 0.0;
        _data.hdop = 99.0;
        _data.status = GNSS_STATUS_IDLE;
        _data.isValid = false;
        _data.dateValid = false;
        _data.timeValid = false;
        
        _config.rxPin = DEFAULT_RX_PIN;
        _config.txPin = DEFAULT_TX_PIN;
        _config.baudRate = DEFAULT_BAUD;
        _config.timezoneOffset = 0;
        _config.autoTimezone = true;
        _config.updateInterval = 1000;
        _config.standbyEnabled = true;
        _config.enableGroveProbe = true;
    }

    void enableCapLoRa1262Power() {
        // 1. 显式开启 M5 设备 5V 外设总线电源
        M5Cardputer.Power.setExtOutput(true);
        delay(30);
        
        // 2. 使用 M5Unified 安全线程化 I2C 接口探查并激活 PI4IOE5V6408 IO 扩展芯片 (I2C 0x43/0x44)
        // PI4IOE5V6408 寄存器映射：
        // 0x07: Output High-Impedance Register (0x00 = 输出使能/禁用高阻)
        // 0x03: I/O Direction Register (0xFF = 全部配置为 Output 输出模式)
        // 0x05: Output Port Register (0xFF = 输出全部 HIGH 高电平，使能 GPS 电源并拉高解除 RST)
        uint8_t addrs[] = {0x43, 0x44};
        for (uint8_t addr : addrs) {
            // 通过 M5.In_I2C (Cardputer 板载/Cap I2C 总线) 开启
            if (M5.In_I2C.writeRegister8(addr, 0x07, 0x00, 100000)) {
                LOG_I("GNSS", "Found Cap LoRa-1262 IO Expander via M5.In_I2C at 0x%02X, enabling power...", addr);
                M5.In_I2C.writeRegister8(addr, 0x03, 0xFF, 100000); // 0x03: Direction = Output
                M5.In_I2C.writeRegister8(addr, 0x05, 0xFF, 100000); // 0x05: Output = HIGH (Power ON & RST High)
            }
            
            // 通过 M5.Ex_I2C (外设 I2C 总线) 开启
            if (M5.Ex_I2C.writeRegister8(addr, 0x07, 0x00, 100000)) {
                LOG_I("GNSS", "Found Cap LoRa-1262 IO Expander via M5.Ex_I2C at 0x%02X, enabling power...", addr);
                M5.Ex_I2C.writeRegister8(addr, 0x03, 0xFF, 100000);
                M5.Ex_I2C.writeRegister8(addr, 0x05, 0xFF, 100000);
            }
        }
        delay(200); // 给予 GNSS 芯片上电解冻并启动 UART 输出的缓冲时间
    }

    bool begin() override {
        if (_isInitialized) return true;
        return begin(_config.rxPin, _config.txPin, _config.baudRate);
    }

    bool begin(int rxPin, int txPin, uint32_t baudRate = 115200) override {
        _config.rxPin = rxPin;
        _config.txPin = txPin;
        _config.baudRate = baudRate;
        
        LOG_I("GNSS", "Starting GNSS on RX=%d, TX=%d @ %u...", rxPin, txPin, baudRate);
        
        // 确保 M5 设备 5V 外设供电开启 (使能 Grove 口与 Cap 供电)
        M5Cardputer.Power.setExtOutput(true);
        delay(20);

        if (rxPin == 15 && txPin == 13) {
            enableCapLoRa1262Power();
        } else if ((rxPin == 1 && txPin == 2) || (rxPin == 2 && txPin == 1)) {
            Wire.end(); // 确保 Grove 端口释放 I2C 引脚占用
        }
        
        // 开启 RX 引脚内部上拉电阻
        gpio_pullup_en((gpio_num_t)rxPin);
        
        // 启动串口
        Serial1.end();
        Serial1.begin(baudRate, SERIAL_8N1, rxPin, txPin);
        delay(50);
        
        // 清空串口缓冲区
        while (Serial1.available() > 0) {
            Serial1.read();
        }
        
        // 发送唤醒指令与 GPS + 北斗 BDS 双模搜星配置指令
        Serial1.print("\r\n$PCAS04,3*1A\r\n");
        Serial1.print("$PCAS00*01\r\n");
        Serial1.flush();
        
        if (_gps) {
            delete _gps;
            _gps = nullptr;
        }
        _gps = new MultipleSatellite(Serial1, baudRate, SERIAL_8N1, rxPin, txPin);
        if (!_gps) {
            LOG_E("GNSS", "Failed to allocate MultipleSatellite!");
            _isInitialized = false;
            _enabled = false;
            return false;
        }
        
        _gps->begin();
        _isInitialized = true;
        _enabled = true;
        _isInStandby = false;
        _data.status = GNSS_STATUS_SEARCHING;
        LOG_I("GNSS", "GNSS module initialized successfully on RX=%d, TX=%d @ %u", rxPin, txPin, baudRate);
        return true;
    }

    bool update() override {
        if (!_enabled || !_gps) {
            return false;
        }

        bool hadChars = _gps->available();
        uint32_t prevChars = _gps->charsProcessed();
        _gps->updateGPS();
        if (hadChars || _gps->charsProcessed() > prevChars) {
            _lastNmeaTime = millis();
        }
        
        static unsigned long lastDebugLog = 0;
        if (millis() - lastDebugLog > 5000) {
            lastDebugLog = millis();
            if (!_isInStandby) {
                LOG_I("GNSS", "[DEBUG] UART: %u chars, %u good, %u bad checksum. Sats: %d, HDOP: %.1f, Fix: %s",
                      _gps->charsProcessed(), _gps->passedChecksum(), _gps->failedChecksum(),
                      _gps->satellites.value(), _gps->hdop.hdop(), _gps->location.isValid() ? "YES" : "NO");
                
                if (_gps->charsProcessed() < 50) {
                    LOG_I("GNSS", "[DEBUG] WARNING: Very little UART data received. Is the module wired correctly? (Baud: %d, RX: %d, TX: %d)",
                          _config.baudRate, _config.rxPin, _config.txPin);
                } else if (_gps->satellites.value() == 0) {
                    LOG_I("GNSS", "[DEBUG] Module is communicating but sees 0 satellites. Keep it under open sky.");
                }
            }
        }
        
        bool updated = false;
        
        if (_gps->location.isUpdated()) {
            _data.latitude = _gps->location.lat();
            _data.longitude = _gps->location.lng();
            _data.isValid = true;
            updated = true;
            
            if (_config.autoTimezone && _data.isValid) {
                calculateTimezoneFromLocation();
            }
        }
        
        if (_gps->altitude.isUpdated()) {
            _data.altitude = _gps->altitude.meters();
        }
        
        if (_gps->time.isUpdated()) {
            _data.hour = _gps->time.hour();
            _data.minute = _gps->time.minute();
            _data.second = _gps->time.second();
            _data.timeValid = _gps->time.isValid();
        }
        
        if (_gps->date.isUpdated()) {
            uint16_t gnssYear = _gps->date.year();
            uint8_t gnssMonth = _gps->date.month();
            uint8_t gnssDay = _gps->date.day();
            
            // 更严格的日期验证
            bool monthValid = gnssMonth >= 1 && gnssMonth <= 12;
            bool dayValid = gnssDay >= 1 && gnssDay <= 31;
            
            if (monthValid && dayValid) {
                _data.year = gnssYear;
                _data.month = gnssMonth;
                _data.day = gnssDay;
                _data.dateValid = _gps->date.isValid();
                
                log_i("[GNSS] Date updated: %04d-%02d-%02d, valid: %d, dateValid: %d\n", _data.year, _data.month, _data.day, _data.dateValid, _gps->date.isValid());
            } else {
                //忽略无效数据
                //LOG_I("GNSS", "Invalid date received: %04d-%02d-%02d (monthValid=%d, dayValid=%d), ignoring", gnssYear, gnssMonth, gnssDay, monthValid, dayValid);
                _data.dateValid = false;
            }
        }
        
        _data.satellites = _gps->satellites.value();
        _data.speed = _gps->speed.kmph();
        _data.course = _gps->course.deg();
        _data.hdop = _gps->hdop.hdop();
        _data.accuracy = _data.hdop * 2.5;
        
        if (_gps->location.isValid()) {
            _data.status = GNSS_STATUS_LOCKED;
        } else if (_enabled) {
            _data.status = GNSS_STATUS_SEARCHING;
        }
        
        if (updated) {
            _newData = true;
        }
        
        return updated;
    }

    GnssData getData() override {
        _newData = false;
        return _data;
    }

    GnssStatus getStatus() override {
        return _data.status;
    }

    void enable() override {
        _enabled = true;
        if (_isInitialized) {
            _data.status = GNSS_STATUS_SEARCHING;
        }
    }

    void disable() override {
        _enabled = false;
        _data.status = GNSS_STATUS_IDLE;
    }
    
    bool isEnabled() override {
        return _enabled;
    }
    
    bool hasNewData() override {
        return _newData;
    }
    
    void setConfig(const GnssConfig& config) override {
        _config = config;
    }
    
    GnssConfig getConfig() const override {
        return _config;
    }
    
    int getSatelliteCount() override {
        return _data.satellites;
    }
    
    float getHDOP() override {
        return _data.hdop;
    }
    
    float getSpeed() override {
        return _data.speed;
    }
    
    float getCourse() override {
        return _data.course;
    }
    
    int getTimezoneOffset() const override {
        return _config.timezoneOffset;
    }
    
    void setTimezoneOffset(int offsetHours) override {
        _config.timezoneOffset = offsetHours;
        _config.autoTimezone = false;
    }
    
    void calculateTimezoneFromLocation() override {
        if (_data.isValid) {
            _config.timezoneOffset = (int)round(_data.longitude / 15.0);
            if (_data.latitude > 18.0 && _data.latitude < 54.0 && 
                _data.longitude > 73.0 && _data.longitude < 135.0) {
                _config.timezoneOffset = 8;
            }
            LOG_I("GNSS", "Timezone calculated from longitude %.2f: UTC%+d", _data.longitude, _config.timezoneOffset);
        }
    }
    
    void enableAutoTimezone(bool enable) override {
        _config.autoTimezone = enable;
    }
    
    int getLocalHour() override {
        if (!_data.timeValid) return 0;
        
        int localHour = _data.hour + _config.timezoneOffset;
        
        if (localHour < 0) localHour += 24;
        else if (localHour >= 24) localHour -= 24;
        
        return localHour;
    }
    
    int getLocalMinute() override {
        return _data.minute;
    }
    
    int getLocalSecond() override {
        return _data.second;
    }
    
    int getLocalDay() override {
        if (!_data.dateValid) return 0;
        
        int hour = _data.hour + _config.timezoneOffset;
        int day = _data.day;
        int month = _data.month;
        int year = _data.year;
        
        if (hour < 0) {
            hour += 24;
            day--;
            if (day < 1) {
                month--;
                if (month < 1) {
                    month = 12;
                    year--;
                }
                day = getDaysInMonth(month, year);
            }
        } else if (hour >= 24) {
            hour -= 24;
            day++;
            if (day > getDaysInMonth(month, year)) {
                day = 1;
                month++;
                if (month > 12) {
                    month = 1;
                }
            }
        }
        
        return day;
    }
    
    int getLocalMonth() override {
        if (!_data.dateValid) return 0;
        
        int hour = _data.hour + _config.timezoneOffset;
        int month = _data.month;
        int year = _data.year;
        
        if (hour < 0) {
            month--;
            if (month < 1) {
                month = 12;
            }
        } else if (hour >= 24) {
            int day = _data.day;
            if (day > getDaysInMonth(month, year)) {
                month++;
                if (month > 12) {
                    month = 1;
                }
            }
        }
        
        return month;
    }
    
    int getLocalYear() override {
        if (!_data.dateValid) return 0;
        
        int hour = _data.hour + _config.timezoneOffset;
        int year = _data.year;
        
        if (hour < 0) {
            int month = _data.month;
            if (month == 1) {
                year--;
            }
        } else if (hour >= 24) {
            int month = _data.month;
            int day = _data.day;
            if (day > getDaysInMonth(month, year)) {
                month++;
                if (month > 12) {
                    year++;
                }
            }
        }
        
        return year;
    }
    
    void enterStandbyMode() override {
        if (_gps && _isInitialized && _config.standbyEnabled) {
            _gps->StandbyMode();
            _isInStandby = true;
            _data.status = GNSS_STATUS_STANDBY;
            LOG_I("GNSS", "Entered standby mode");
        }
    }
    
    void exitStandbyMode() override {
        if (_gps && _isInitialized) {
            _gps->write("\r\n$PCAS00*01\r\n");
            _isInStandby = false;
            _data.status = GNSS_STATUS_SEARCHING;
            LOG_I("GNSS", "Exited standby mode");
        }
    }
    
    bool isInStandbyMode() const override {
        return _isInStandby;
    }
    
    uint32_t getGpsChars() override {
        if (_gps) return _gps->charsProcessed();
        return _gpsChars;
    }
    
    uint32_t getGpsSentences() override {
        return _gpsSentences;
    }
    
    bool isModuleInitialized() override {
        return _isInitialized;
    }
    
    unsigned long getLastNmeaTime() override {
        return _lastNmeaTime;
    }
    
    bool isHeartbeatActive(uint32_t pulseDurationMs = 250) override {
        if (!_isInitialized || _isInStandby || !_enabled) {
            return false;
        }
        if (_lastNmeaTime == 0) return false;
        return (millis() - _lastNmeaTime < pulseDurationMs);
    }
    
    bool feed(char c) override {
        if (!_gps) return false;
        
        _lastNmeaTime = millis();
        _gpsChars++;
        if (_gps->encode(c)) {
            _gpsSentences++;
            return true;
        }
        return false;
    }
    
    int available() override {
        if (!_isInitialized) return 0;
        return Serial1.available();
    }
    
    char read() override {
        if (!_isInitialized) return 0;
        char c = Serial1.read();
        _lastNmeaTime = millis();
        _gpsChars++;
        return c;
    }

    bool probeGrove() override {
        return begin(1, 2, 115200);
    }

    bool isGroveMode() const override {
        return (_config.rxPin == 1 && _config.txPin == 2) || (_config.rxPin == 2 && _config.txPin == 1);
    }

    bool isProbing() const override {
        return _isProbing;
    }

private:
    int getDaysInMonth(int month, int year) {
        int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (month == 2 && (year % 400 == 0 || (year % 100 != 0 && year % 4 == 0))) {
            return 29;
        }
        return daysInMonth[month - 1];
    }
};

uint32_t M5Gnss::_gpsChars = 0;
uint32_t M5Gnss::_gpsSentences = 0;

HalGnss* gnss = new M5Gnss();
