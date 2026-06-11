#include "hal_gnss.h"
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
    MultipleSatellite* _gps;
    
    static const int DEFAULT_RX_PIN = 15;
    static const int DEFAULT_TX_PIN = 13;
    static const uint32_t DEFAULT_BAUD = 115200;
    
    static uint32_t _gpsChars;
    static uint32_t _gpsSentences;

public:
    M5Gnss() : _enabled(false), _newData(false), _isInitialized(false), _isInStandby(false), _gps(nullptr) {
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
    }

    bool begin() override {
        Serial.println(F("[GNSS] Creating MultipleSatellite instance..."));
        Serial.flush();
        
        _gps = new MultipleSatellite(Serial1, _config.baudRate, SERIAL_8N1, _config.rxPin, _config.txPin);
        if (!_gps) {
            Serial.println(F("[GNSS] Failed to create MultipleSatellite!"));
            return false;
        }
        Serial.println(F("[GNSS] MultipleSatellite created, calling begin()..."));
        Serial.flush();
        
        _gps->begin();
        Serial.println(F("[GNSS] begin() complete, setting boot mode..."));
        Serial.flush();
        
        _gps->setSystemBootMode(BOOT_FACTORY_START);
        Serial.println(F("[GNSS] Boot mode set, initialization complete"));
        Serial.flush();
        
        _enabled = true;
        _isInitialized = true;
        _data.status = GNSS_STATUS_SEARCHING;
        return true;
    }

    bool update() override {
        if (!_enabled || !_gps) {
            return false;
        }

        _gps->updateGPS();
        
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
                
                Serial.printf("[GNSS] Date updated: %04d-%02d-%02d, valid: %d, dateValid: %d\n", _data.year, _data.month, _data.day, _data.dateValid, _gps->date.isValid());
            } else {
                //忽略无效数据
                //Serial.printf("[GNSS] Invalid date received: %04d-%02d-%02d (monthValid=%d, dayValid=%d), ignoring\n", gnssYear, gnssMonth, gnssDay, monthValid, dayValid);
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
            Serial.printf("[GNSS] Timezone calculated from longitude %.2f: UTC%+d\n",
                         _data.longitude, _config.timezoneOffset);
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
            Serial1.println("$PCAS10,0*1C");
            _isInStandby = true;
            _data.status = GNSS_STATUS_STANDBY;
            Serial.println("[GNSS] Entered standby mode");
        }
    }
    
    void exitStandbyMode() override {
        if (_gps && _isInitialized) {
            Serial1.println("$PCAS10,0*1C");
            _isInStandby = false;
            _data.status = GNSS_STATUS_SEARCHING;
            Serial.println("[GNSS] Exited standby mode");
        }
    }
    
    bool isInStandbyMode() const override {
        return _isInStandby;
    }
    
    uint32_t getGpsChars() override {
        return _gpsChars;
    }
    
    uint32_t getGpsSentences() override {
        return _gpsSentences;
    }
    
    bool isModuleInitialized() override {
        return _isInitialized;
    }
    
    bool feed(char c) override {
        if (!_gps) return false;
        
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
        _gpsChars++;
        return c;
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
