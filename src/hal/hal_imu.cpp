#include "hal_imu.h"
#include <M5Cardputer.h>
#include <math.h>

class M5Imu : public HalImu {
private:
    ImuData _data;
    ImuConfig _config;
    bool _enabled;
    bool _newData;
    
    float _roll;
    float _pitch;
    unsigned long _lastUpdate;
    float _dt;
    
    bool _isMoving;
    float _lastAccelX, _lastAccelY, _lastAccelZ;
    
    bool _hasReferenceOrientation;
    float _refRoll, _refPitch;
    
    float _temperature;
    
    static constexpr float FILTER_ALPHA = 0.1;
    static constexpr float MAX_ANGLE = 90.0f;

public:
    M5Imu() : _enabled(false), _newData(false), _roll(0), _pitch(0), 
              _lastUpdate(0), _dt(0), _isMoving(false),
              _lastAccelX(0), _lastAccelY(0), _lastAccelZ(1.0),
              _hasReferenceOrientation(false), _refRoll(0), _refPitch(0),
              _temperature(25.0) {
        
        _data.accelX = 0.0;
        _data.accelY = 0.0;
        _data.accelZ = 1.0;
        _data.gyroX = 0.0;
        _data.gyroY = 0.0;
        _data.gyroZ = 0.0;
        _data.roll = 0.0;
        _data.pitch = 0.0;
        _data.yaw = 0.0;
        _data.temperature = 25.0;
        _data.heading = 0.0;
        _data.headingAccuracy = 0.0;
        _data.isMoving = false;
        _data.status = IMU_STATUS_IDLE;
        
        _config.alpha = 0.95;
        _config.gyroWeight = 0.98;
        _config.accelWeight = 0.02;
        _config.deadZone = 0.5;
        _config.motionThreshold = 0.3;
        _config.driftCorrectionRate = 0.001;
        _config.calibrationTime = 2000;
        _config.filterMode = IMU_FILTER_COMPLEMENTARY;
    }

    bool begin() override {
        Serial.println("[IMU] Starting IMU initialization...");
        
        bool imuOk = M5.Imu.begin();
        
        if (imuOk) {
            Serial.println("[IMU] IMU initialized successfully");
            _enabled = true;
            _data.status = IMU_STATUS_READY;
            _lastUpdate = millis();
            return true;
        } else {
            Serial.println("[IMU] IMU initialization FAILED!");
            _data.status = IMU_STATUS_ERROR;
            return false;
        }
    }

    bool update() override {
        if (!_enabled) {
            return false;
        }

        unsigned long currentTime = millis();
        _dt = (currentTime - _lastUpdate) / 1000.0;
        if (_dt <= 0) _dt = 0.01;
        _lastUpdate = currentTime;

        auto imu_update = M5.Imu.update();
        if (!imu_update) {
            return false;
        }
        
        auto imu_data = M5.Imu.getImuData();
        
        _data.accelX = imu_data.accel.x;
        _data.accelY = imu_data.accel.y;
        _data.accelZ = imu_data.accel.z;
        _data.gyroX = imu_data.gyro.x;
        _data.gyroY = imu_data.gyro.y;
        _data.gyroZ = imu_data.gyro.z;
        
        float currentPitch = atan2(_data.accelY, _data.accelZ);
        float currentRoll = atan2(-_data.accelX, sqrt(_data.accelY * _data.accelY + _data.accelZ * _data.accelZ));
        
        float filteredPitch = _pitch * (1.0 - FILTER_ALPHA * 2) + currentPitch * FILTER_ALPHA * 2;
        float filteredRoll = _roll * (1.0 - FILTER_ALPHA * 2) + currentRoll * FILTER_ALPHA * 2;
        
        // 极致性能优化：移除了人为的角度变化 MAX_CHANGE 限制。
        // 原先的限制会导致快速旋转时出现“粘滞感”和“假性卡顿”。
        // 现在系统将更即时地响应 IMU 的原始变化，通过 FILTER_ALPHA 保证平滑性。
        
        _pitch = filteredPitch;
        _roll = filteredRoll;
        
        float accelDiff = sqrt(pow(_data.accelX - _lastAccelX, 2) + 
                               pow(_data.accelY - _lastAccelY, 2) + 
                               pow(_data.accelZ - _lastAccelZ, 2));
        _isMoving = accelDiff > _config.motionThreshold;
        _data.isMoving = _isMoving;
        
        _lastAccelX = _data.accelX;
        _lastAccelY = _data.accelY;
        _lastAccelZ = _data.accelZ;
        
        float deltaPitch = _pitch;
        float deltaRoll = _roll;
        
        if (_hasReferenceOrientation) {
            deltaPitch = _pitch - _refPitch;
            deltaRoll = _roll - _refRoll;
        }
        
        deltaRoll = -deltaRoll;
        
        if (deltaPitch > MAX_ANGLE * DEG_TO_RAD) deltaPitch = MAX_ANGLE * DEG_TO_RAD;
        if (deltaPitch < -MAX_ANGLE * DEG_TO_RAD) deltaPitch = -MAX_ANGLE * DEG_TO_RAD;
        if (deltaRoll > MAX_ANGLE * DEG_TO_RAD) deltaRoll = MAX_ANGLE * DEG_TO_RAD;
        if (deltaRoll < -MAX_ANGLE * DEG_TO_RAD) deltaRoll = -MAX_ANGLE * DEG_TO_RAD;
        
        _data.roll = deltaRoll * RAD_TO_DEG;
        _data.pitch = deltaPitch * RAD_TO_DEG;
        _data.yaw = 0;
        _data.heading = 0;
        

        
        _data.status = IMU_STATUS_READY;
        _newData = true;
        
        return true;
    }

    ImuData getData() override {
        _newData = false;
        return _data;
    }

    ImuStatus getStatus() override {
        return _data.status;
    }

    void enable() override {
        _enabled = true;
        _data.status = IMU_STATUS_READY;
    }

    void disable() override {
        _enabled = false;
        _data.status = IMU_STATUS_IDLE;
    }

    bool hasNewData() override {
        return _newData;
    }

    bool calibrate() override {
        Serial.println("[IMU] Setting reference orientation...");
        _hasReferenceOrientation = true;
        _refRoll = _roll;
        _refPitch = _pitch;
        Serial.printf("[IMU] Reference set: Pitch=%.1f deg, Roll=%.1f deg\n",
                     _refPitch * RAD_TO_DEG, _refRoll * RAD_TO_DEG);
        _data.status = IMU_STATUS_READY;
        return true;
    }
    
    void setConfig(const ImuConfig& config) override {
        _config = config;
    }
    
    ImuConfig getConfig() const override {
        return _config;
    }
    
    void setFilterMode(ImuFilterMode mode) override {
        _config.filterMode = mode;
    }
    
    void setAlpha(float alpha) override {
        _config.alpha = constrain(alpha, 0.0, 1.0);
    }
    
    void setDeadZone(float deadZone) override {
        _config.deadZone = deadZone;
    }
    
    void resetOrientation() override {
        _roll = 0.0;
        _pitch = 0.0;
        _hasReferenceOrientation = false;
        _refRoll = 0;
        _refPitch = 0;
    }
    
    void setReferenceOrientation(float roll, float pitch, float yaw) override {
        _hasReferenceOrientation = true;
        _refRoll = roll * DEG_TO_RAD;
        _refPitch = pitch * DEG_TO_RAD;
    }
    
    void clearReferenceOrientation() override {
        _hasReferenceOrientation = false;
        _refRoll = 0;
        _refPitch = 0;
    }
    
    bool isMoving() const override {
        return _isMoving;
    }
    
    float getTemperature() override {
        return _temperature;
    }
    
    void enableVirtualHeading(bool enable) override {
    }
    
    float getVirtualHeading() const override {
        return 0;
    }
    
    void setVirtualHeading(float heading) override {
    }
    
    void calibrateVirtualHeading() override {
        calibrate();
    }
};

HalImu* imu = new M5Imu();
