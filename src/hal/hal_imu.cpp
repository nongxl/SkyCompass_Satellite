#include "hal_imu.h"
#include "core/log_manager.h"
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
        LOG_I("IMU", "Starting IMU initialization...");
        
        bool imuOk = M5.Imu.begin();
        
        if (imuOk) {
            LOG_I("IMU", "IMU initialized successfully");
            _enabled = true;
            _data.status = IMU_STATUS_READY;
            _lastUpdate = millis();
            return true;
        } else {
            LOG_I("IMU", "IMU initialization FAILED!");
            _data.status = IMU_STATUS_ERROR;
            return false;
        }
    }

    bool update() override {
        if (!_enabled) {
            return false;
        }

        // 由于主循环中 M5Cardputer.update() 已经读取了 IMU 数据
        // 这里如果检查 M5.Imu.update() 的返回值，几乎永远为 false，导致严重的丢帧卡顿
        // 我们直接按时间流逝(dt)获取最新数据进行积分即可，这是连续物理系统的标准做法
        M5.Imu.update();

        unsigned long currentTime = millis();
        _dt = (currentTime - _lastUpdate) / 1000.0;
        if (_dt <= 0) _dt = 0.01;
        _lastUpdate = currentTime;
        
        auto imu_data = M5.Imu.getImuData();
        
        _data.accelX = imu_data.accel.x;
        _data.accelY = imu_data.accel.y;
        _data.accelZ = imu_data.accel.z;
        _data.gyroX = imu_data.gyro.x;
        _data.gyroY = imu_data.gyro.y;
        _data.gyroZ = imu_data.gyro.z;
        
        float currentPitch = atan2(_data.accelY, _data.accelZ);
        float currentRoll = atan2(-_data.accelX, sqrt(_data.accelY * _data.accelY + _data.accelZ * _data.accelZ));
        
        // 完美解决“卡顿”和“不跟手”：引入陀螺仪(Gyro)做互补滤波(Complementary Filter)
        // 陀螺仪反应极快且平滑（解决不跟手和卡顿），加速计提供绝对重力参考纠正漂移
        float gx = _data.gyroX * DEG_TO_RAD;
        float gy = _data.gyroY * DEG_TO_RAD;
        
        // 积分陀螺仪角速度 (M5Cardputer的坐标系中：X=右, Y=前, Z=下)
        // 经过严格的右手定则分析：
        // 抬头 (Pitch UP)：绕X轴负向旋转 (gx < 0)，重力向后倒 (accelY < 0)，atan2(Y,Z) < 0。符号相同，相加！
        // 右滚 (Roll RIGHT)：绕Y轴正向旋转 (gy > 0)，重力向左倒 (accelX < 0)，atan2(-X,Z) > 0。符号相同，相加！
        _pitch += gx * _dt;
        _roll += gy * _dt;
        
        // 互补滤波系数：时间常数越长，越信任陀螺仪；越短越信任加速计
        // 加大 tau 至 1.0f，让它更信任陀螺仪
        float tau = 1.0f; 
        float alpha = _dt / (tau + _dt);
        if (alpha > 1.0f) alpha = 1.0f;
        
        // 处理角度翻转（例如从179度跳到-179度）
        float PI_F = 3.14159265f;
        float TWO_PI_F = PI_F * 2.0f;
        
        // 动态阈值防抖：只有在总加速度接近 1G 时（0.8G ~ 1.2G），说明此时没有剧烈的线性加减速（例如挥动手臂或急停）
        // 这时才用加速计去纠正角度。完美解决“急停时地球往回转一点”的问题！
        float accelMag = sqrt(_data.accelX*_data.accelX + _data.accelY*_data.accelY + _data.accelZ*_data.accelZ);
        if (accelMag > 0.8f && accelMag < 1.2f) {
            float diffPitch = currentPitch - _pitch;
            while (diffPitch > PI_F) diffPitch -= TWO_PI_F;
            while (diffPitch < -PI_F) diffPitch += TWO_PI_F;
            _pitch += diffPitch * alpha;
            
            float diffRoll = currentRoll - _roll;
            while (diffRoll > PI_F) diffRoll -= TWO_PI_F;
            while (diffRoll < -PI_F) diffRoll += TWO_PI_F;
            _roll += diffRoll * alpha;
        }
        
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
        LOG_I("IMU", "Setting reference orientation...");
        _hasReferenceOrientation = true;
        _refRoll = _roll;
        _refPitch = _pitch;
        LOG_I("IMU", "Reference set: Pitch=%.1f deg, Roll=%.1f deg", _refPitch * RAD_TO_DEG, _refRoll * RAD_TO_DEG);
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
