#ifndef HAL_GNSS_H
#define HAL_GNSS_H

#include <Arduino.h>

enum GnssStatus {
    GNSS_STATUS_IDLE,
    GNSS_STATUS_SEARCHING,
    GNSS_STATUS_LOCKED,
    GNSS_STATUS_STANDBY,
    GNSS_STATUS_ERROR
};

typedef struct {
    double latitude;
    double longitude;
    double altitude;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t satellites;
    float accuracy;
    float speed;
    float course;
    float hdop;
    GnssStatus status;
    bool isValid;
    bool dateValid;
    bool timeValid;
} GnssData;

typedef struct {
    int rxPin;
    int txPin;
    long baudRate;
    int timezoneOffset;
    bool autoTimezone;
    unsigned long updateInterval;
    bool standbyEnabled;
} GnssConfig;

class HalGnss {
public:
    virtual bool begin() = 0;
    virtual bool update() = 0;
    virtual GnssData getData() = 0;
    virtual GnssStatus getStatus() = 0;
    virtual void enable() = 0;
    virtual void disable() = 0;
    virtual bool isEnabled() = 0;
    virtual bool hasNewData() = 0;
    
    virtual void setConfig(const GnssConfig& config) = 0;
    virtual GnssConfig getConfig() const = 0;
    
    virtual int getSatelliteCount() = 0;
    virtual float getHDOP() = 0;
    virtual float getSpeed() = 0;
    virtual float getCourse() = 0;
    
    virtual int getTimezoneOffset() const = 0;
    virtual void setTimezoneOffset(int offsetHours) = 0;
    virtual void calculateTimezoneFromLocation() = 0;
    virtual void enableAutoTimezone(bool enable) = 0;
    
    virtual int getLocalHour() = 0;
    virtual int getLocalMinute() = 0;
    virtual int getLocalSecond() = 0;
    virtual int getLocalDay() = 0;
    virtual int getLocalMonth() = 0;
    virtual int getLocalYear() = 0;
    
    virtual void enterStandbyMode() = 0;
    virtual void exitStandbyMode() = 0;
    virtual bool isInStandbyMode() const = 0;
    
    virtual uint32_t getGpsChars() = 0;
    virtual uint32_t getGpsSentences() = 0;
    virtual bool isModuleInitialized() = 0;
    
    virtual bool feed(char c) = 0;
    virtual int available() = 0;
    virtual char read() = 0;
};

#endif
