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
    bool _firstUpdate;
    
    float _gyroBiasX;
    float _gyroBiasY;
    float _gyroBiasZ;
    int _stillCount;
    
    static constexpr float FILTER_ALPHA = 0.1;
    static constexpr float MAX_ANGLE = 90.0f;

public:
    M5Imu() : _enabled(false), _newData(false), _roll(0), _pitch(0), 
              _lastUpdate(0), _dt(0), _isMoving(false),
              _lastAccelX(0), _lastAccelY(0), _lastAccelZ(1.0),
              _hasReferenceOrientation(false), _refRoll(0), _refPitch(0),
              _temperature(25.0), _firstUpdate(true),
              _gyroBiasX(0.0f), _gyroBiasY(0.0f), _gyroBiasZ(0.0f), _stillCount(0) {
        
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
            LOG_I("IMU", "IMU detected on I2C bus. Performing sanity check to filter out phantom devices...");
            
            // 进行 15 次快速采样，通过检验重力加速度范围与 ADC 噪声变化排除假阳性（如 Chain Mono UART 引起的虚假 ACK）
            int validCount = 0;
            float prevX = 0, prevY = 0, prevZ = 0;
            bool first = true;
            bool hasChange = false;
            
            for (int i = 0; i < 15; i++) {
                M5.Imu.update();
                auto imu_data = M5.Imu.getImuData();
                float x = imu_data.accel.x;
                float y = imu_data.accel.y;
                float z = imu_data.accel.z;
                
                // 排除无效数值
                if (isnan(x) || isnan(y) || isnan(z) || isinf(x) || isinf(y) || isinf(z)) {
                    delay(10);
                    continue;
                }
                
                // 校验加速度模长：对于静止/微动设备，重力加速度绝对值应在合理区间 [0.6G, 1.8G]
                float mag = sqrt(x*x + y*y + z*z);
                if (mag > 0.6f && mag < 1.8f) {
                    validCount++;
                }
                
                // 校验数据波动：真实 IMU 因热噪声，连续浮点读数绝无可能 100% 完全相同。若全部相同，说明是死数据/无响应总线
                if (!first) {
                    if (x != prevX || y != prevY || z != prevZ) {
                        hasChange = true;
                    }
                }
                prevX = x; prevY = y; prevZ = z;
                first = false;
                
                delay(10);
            }
            
            // 必须有足够数量的正常模长数据，且数据必须存在微小的随机噪声波动
            if (validCount >= 8 && hasChange) {
                LOG_I("IMU", "IMU verified successfully (validCount=%d, hasChange=%d)", validCount, (int)hasChange);
                _enabled = true;
                _data.status = IMU_STATUS_READY;
                _lastUpdate = millis();
                return true;
            } else {
                LOG_I("IMU", "IMU verification failed (validCount=%d, hasChange=%d). Treating as phantom/fake device and disabling.", validCount, (int)hasChange);
                _data.status = IMU_STATUS_ERROR;
                _enabled = false;
                return false;
            }
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
        
        // 校验 I2C 读数合法性，防止因 I2C 总线冲突/干扰导致 NaN, Inf 或物理飞尖峰数据引发地球视角乱跳
        if (isnan(imu_data.accel.x) || isnan(imu_data.accel.y) || isnan(imu_data.accel.z) ||
            isnan(imu_data.gyro.x)  || isnan(imu_data.gyro.y)  || isnan(imu_data.gyro.z)  ||
            isinf(imu_data.accel.x) || isinf(imu_data.accel.y) || isinf(imu_data.accel.z) ||
            isinf(imu_data.gyro.x)  || isinf(imu_data.gyro.y)  || isinf(imu_data.gyro.z)) {
            return false;
        }

        // 陀螺仪角速度硬限幅：人体手持设备不可能超过 1000 deg/s 的极高旋转速度，过滤串口波特率脉冲干扰
        if (fabs(imu_data.gyro.x) > 1000.0f || fabs(imu_data.gyro.y) > 1000.0f || fabs(imu_data.gyro.z) > 1000.0f) {
            return false;
        }

        // 加速度模长校验：全零说明 I2C 读数失败，>5G 说明出现强烈冲击或野值，丢弃异常帧
        float rawAccelMag = sqrt(imu_data.accel.x*imu_data.accel.x + imu_data.accel.y*imu_data.accel.y + imu_data.accel.z*imu_data.accel.z);
        if (rawAccelMag < 0.2f || rawAccelMag > 5.0f) {
            return false;
        }

        // 获取原始陀螺仪读数
        float rawGyroX = imu_data.gyro.x;
        float rawGyroY = imu_data.gyro.y;
        float rawGyroZ = imu_data.gyro.z;

        // 计算加速度变化差值与原始陀螺仪模长
        float accelDiff = sqrt(pow(imu_data.accel.x - _lastAccelX, 2) + 
                               pow(imu_data.accel.y - _lastAccelY, 2) + 
                               pow(imu_data.accel.z - _lastAccelZ, 2));
        float rawGyroMag = sqrt(rawGyroX * rawGyroX + rawGyroY * rawGyroY + rawGyroZ * rawGyroZ);

        // 在线静止检测：
        // 1. 加速度模长在 1G 附近 (0.85G ~ 1.15G)
        // 2. 加速度两帧变动极小 (< 0.08G)
        // 3. 陀螺仪原始模长较小 (< 15.0 deg/s)
        bool isStatic = (rawAccelMag > 0.85f && rawAccelMag < 1.15f) && (accelDiff < 0.08f) && (rawGyroMag < 15.0f);

        if (isStatic) {
            _stillCount++;
            // 当静止持续超过 8 帧 (约 80ms~100ms) 时，自动在线学习并校准陀螺仪零偏
            if (_stillCount > 8) {
                float biasAlpha = 0.05f;
                _gyroBiasX = _gyroBiasX * (1.0f - biasAlpha) + rawGyroX * biasAlpha;
                _gyroBiasY = _gyroBiasY * (1.0f - biasAlpha) + rawGyroY * biasAlpha;
                _gyroBiasZ = _gyroBiasZ * (1.0f - biasAlpha) + rawGyroZ * biasAlpha;
            }
        } else {
            _stillCount = 0;
        }

        _data.accelX = imu_data.accel.x;
        _data.accelY = imu_data.accel.y;
        _data.accelZ = imu_data.accel.z;
        // 扣除自动推算出的零偏
        _data.gyroX = rawGyroX - _gyroBiasX;
        _data.gyroY = rawGyroY - _gyroBiasY;
        _data.gyroZ = rawGyroZ - _gyroBiasZ;
        
        float currentPitch = atan2(_data.accelY, _data.accelZ);
        float currentRoll = atan2(-_data.accelX, sqrt(_data.accelY * _data.accelY + _data.accelZ * _data.accelZ));
        
        if (_firstUpdate) {
            _pitch = currentPitch;
            _roll = currentRoll;
            _firstUpdate = false;
        }
        
        // 引入扣除零偏后的陀螺仪(Gyro)角速度做互补滤波
        float gx = _data.gyroX * DEG_TO_RAD;
        float gy = _data.gyroY * DEG_TO_RAD;
        
        // 处理角度规整常量定义
        float PI_F = 3.14159265f;
        float TWO_PI_F = PI_F * 2.0f;

        // 积分陀螺仪角速度
        _pitch += gx * _dt;
        _roll += gy * _dt;
        
        // 规整角度到 [-PI, PI] 之间
        while (_pitch > PI_F) _pitch -= TWO_PI_F;
        while (_pitch < -PI_F) _pitch += TWO_PI_F;
        while (_roll > PI_F) _roll -= TWO_PI_F;
        while (_roll < -PI_F) _roll += TWO_PI_F;
        
        // 动态互补滤波系数：
        // 静止状态下加速度计绝对重力 100% 可信，使用快速修正系数 (0.15f) 在 0.2 秒内迅速把地球拉回静止姿态并消除任何残余漂移晃动
        // 运动状态下使用平滑系数 (0.005f)，避免运动中的加速度抖动引发回弹
        float alpha = isStatic ? 0.15f : 0.005f;
        
        if (rawAccelMag > 0.8f && rawAccelMag < 1.2f) {
            float diffPitch = currentPitch - _pitch;
            while (diffPitch > PI_F) diffPitch -= TWO_PI_F;
            while (diffPitch < -PI_F) diffPitch += TWO_PI_F;
            _pitch += diffPitch * alpha;
            
            float diffRoll = currentRoll - _roll;
            while (diffRoll > PI_F) diffRoll -= TWO_PI_F;
            while (diffRoll < -PI_F) diffRoll += TWO_PI_F;
            _roll += diffRoll * alpha;

            // 再次规整以确保值始终处于合理范围
            while (_pitch > PI_F) _pitch -= TWO_PI_F;
            while (_pitch < -PI_F) _pitch += TWO_PI_F;
            while (_roll > PI_F) _roll -= TWO_PI_F;
            while (_roll < -PI_F) _roll += TWO_PI_F;
        }
        
        accelDiff = sqrt(pow(_data.accelX - _lastAccelX, 2) + 
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
        
        // 规整相对偏角到 [-PI, PI]，避免由于跨越正负180度边界计算出巨大偏角被错误截断
        while (deltaPitch > PI_F) deltaPitch -= TWO_PI_F;
        while (deltaPitch < -PI_F) deltaPitch += TWO_PI_F;
        while (deltaRoll > PI_F) deltaRoll -= TWO_PI_F;
        while (deltaRoll < -PI_F) deltaRoll += TWO_PI_F;
        
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
