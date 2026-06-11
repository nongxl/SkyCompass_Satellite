#ifndef HAL_IMU_H
#define HAL_IMU_H

#include <Arduino.h>

enum ImuStatus {
    IMU_STATUS_IDLE,
    IMU_STATUS_READY,
    IMU_STATUS_CALIBRATING,
    IMU_STATUS_ERROR
};

enum ImuFilterMode {
    IMU_FILTER_COMPLEMENTARY,
    IMU_FILTER_KALMAN,
    IMU_FILTER_MADGWICK
};

typedef struct {
    float accelX;
    float accelY;
    float accelZ;
    float gyroX;
    float gyroY;
    float gyroZ;
    float roll;
    float pitch;
    float yaw;
    float temperature;
    float heading;
    float headingAccuracy;
    bool isMoving;
    ImuStatus status;
} ImuData;

typedef struct {
    float alpha;
    float gyroWeight;
    float accelWeight;
    float deadZone;
    float motionThreshold;
    float driftCorrectionRate;
    unsigned long calibrationTime;
    ImuFilterMode filterMode;
} ImuConfig;

class HalImu {
public:
    virtual bool begin() = 0;
    virtual bool update() = 0;
    virtual ImuData getData() = 0;
    virtual ImuStatus getStatus() = 0;
    virtual void enable() = 0;
    virtual void disable() = 0;
    virtual bool hasNewData() = 0;
    virtual bool calibrate() = 0;
    
    virtual void setConfig(const ImuConfig& config) = 0;
    virtual ImuConfig getConfig() const = 0;
    virtual void setFilterMode(ImuFilterMode mode) = 0;
    virtual void setAlpha(float alpha) = 0;
    virtual void setDeadZone(float deadZone) = 0;
    virtual void resetOrientation() = 0;
    virtual void setReferenceOrientation(float roll, float pitch, float yaw) = 0;
    virtual void clearReferenceOrientation() = 0;
    virtual bool isMoving() const = 0;
    virtual float getTemperature() = 0;
    virtual void enableVirtualHeading(bool enable) = 0;
    virtual float getVirtualHeading() const = 0;
    virtual void setVirtualHeading(float heading) = 0;
    virtual void calibrateVirtualHeading() = 0;
};

#endif
