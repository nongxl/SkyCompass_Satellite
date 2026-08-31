#include "gimbal_controller.h"

GimbalController::GimbalController() : 
    _state(GIMBAL_STATE_STANDBY), 
    _isOnline(false), 
    _currentmA(0.0f), 
    _fwVersion(0),
    _lastStatusTick(0),
    _curAzAngle(90.0f), _curElAngle(90.0f), _curAltAngle(90.0f),
    _tarAzAngle(90.0f), _tarElAngle(90.0f), _tarAltAngle(90.0f),
    _lastTick(0),
    _lerpFactor(0.08f),
    _maxDegPerSec(5.0f) {}

bool GimbalController::begin(TwoWire *wire, uint8_t sda, uint8_t scl, uint32_t freq) {
    // 启用 I2C 超时检测机制，防线缆松动死锁
    wire->setTimeout(25); // 25ms timeout
    
    _isOnline = _servo.begin(wire, sda, scl, freq);
    if (_isOnline) {
        _fwVersion = _servo.getFirmwareVersion();
        _servo.setAllPinMode(SERVO_CTL_MODE);
        delay(20);
        
        // 软启动初始值
        _curAzAngle = 90.0f;
        _curElAngle = 90.0f;
        _curAltAngle = 90.0f;
        _tarAzAngle = 90.0f;
        _tarElAngle = 90.0f;
        _tarAltAngle = 90.0f;
        
        _servo.setServoAngle(GIMBAL_CH_AZ, 90);
        _servo.setServoAngle(GIMBAL_CH_EL, 90);
        _servo.setServoAngle(GIMBAL_CH_ALT, 90);
        
        _state = GIMBAL_STATE_INITIALIZING;
        setLEDsByState();
    }
    _lastTick = millis();
    return _isOnline;
}

void GimbalController::updateStatus() {
    if (millis() - _lastStatusTick > 2000) {
        _lastStatusTick = millis();
        bool conn = _servo.isConnected();
        if (conn != _isOnline) {
            _isOnline = conn;
            if (_isOnline) {
                _servo.setAllPinMode(SERVO_CTL_MODE);
                _fwVersion = _servo.getFirmwareVersion();
                // 恢复位置
                _servo.setServoAngle(GIMBAL_CH_AZ, (uint8_t)constrain(_curAzAngle, 0, 180));
                _servo.setServoAngle(GIMBAL_CH_EL, (uint8_t)constrain(_curElAngle, 0, 180));
                _servo.setServoAngle(GIMBAL_CH_ALT, (uint8_t)constrain(_curAltAngle, 0, 180));
                setLEDsByState();
            }
        }
        if (_isOnline) {
            _currentmA = _servo.getCurrent() * 1000.0f; // 转换为 mA
        } else {
            _currentmA = 0.0f;
        }
    }
}

void GimbalController::apply180DegreeLimit(float az, float el, float alt, float &outAz, float &outEl, float &outAlt) {
    // 规范化方位角到 [0, 360)
    while (az < 0) az += 360.0f;
    while (az >= 360.0f) az -= 360.0f;

    // 方位轴舵机限位及头顶穿越镜像转换
    if (az <= 180.0f) {
        outAz = az;
        outEl = el;
    } else {
        outAz = az - 180.0f;
        outEl = 180.0f - el; // 越过头顶对侧指向
    }

    // 卫星高度转换：取 [350km - 1500km] 对比映射到 [30° - 150°] 舵机角，如果过低归为30度，过高归为150度
    if (alt <= 0.0f) {
        outAlt = 90.0f; // 默认
    } else {
        outAlt = 30.0f + ((alt - 350.0f) / (1500.0f - 350.0f)) * (150.0f - 30.0f);
        outAlt = constrain(outAlt, 30.0f, 150.0f);
    }

    // 仰角及方位做硬件硬限制限制
    outAz = constrain(outAz, 0.0f, 180.0f);
    outEl = constrain(outEl, 0.0f, 180.0f);
}

void GimbalController::setTargetTrack(float realAz, float realEl, float realAltKm) {
    _tarAltAngle = 90.0f;
    apply180DegreeLimit(realAz, realEl, realAltKm, _tarAzAngle, _tarElAngle, _tarAltAngle);
    
    if (_state != GIMBAL_STATE_TRACKING) {
        _state = GIMBAL_STATE_TRACKING;
        _maxDegPerSec = 10.0f; // 跟踪模式下允许响应稍快，但仍限制暴冲
        setLEDsByState();
    }
}

void GimbalController::setTargetPrePoint(float aosAz) {
    // 预瞄准状态下仰角平放为0，高度轴居中90
    _tarAltAngle = 90.0f;
    apply180DegreeLimit(aosAz, 0.0f, 0.0f, _tarAzAngle, _tarElAngle, _tarAltAngle);
    
    if (_state != GIMBAL_STATE_PREPOINT) {
        _state = GIMBAL_STATE_PREPOINT;
        _maxDegPerSec = 2.0f; // 极慢角速度，静默预定目标
        setLEDsByState();
    }
}

void GimbalController::setStandby() {
    _tarAzAngle = 90.0f;
    _tarElAngle = 90.0f;
    _tarAltAngle = 90.0f;
    
    if (_state != GIMBAL_STATE_STANDBY) {
        _state = GIMBAL_STATE_STANDBY;
        _maxDegPerSec = 3.0f;
        setLEDsByState();
    }
}

void GimbalController::processLerp(float dt) {
    float maxStep = _maxDegPerSec * dt;
    
    auto lerpStep = [&](float &cur, float tar) {
        float diff = tar - cur;
        if (fabs(diff) > 0.05f) {
            // 平滑滤波
            float step = diff * _lerpFactor;
            // 速度饱和限制
            if (step > maxStep) step = maxStep;
            else if (step < -maxStep) step = -maxStep;
            cur += step;
        } else {
            cur = tar;
        }
    };

    lerpStep(_curAzAngle, _tarAzAngle);
    lerpStep(_curElAngle, _tarElAngle);
    lerpStep(_curAltAngle, _tarAltAngle);
}

void GimbalController::updateHardwareServos() {
    if (!_isOnline) return;
    _servo.setServoAngle(GIMBAL_CH_AZ, (uint8_t)constrain(_curAzAngle, 0, 180));
    _servo.setServoAngle(GIMBAL_CH_EL, (uint8_t)constrain(_curElAngle, 0, 180));
    _servo.setServoAngle(GIMBAL_CH_ALT, (uint8_t)constrain(_curAltAngle, 0, 180));
}

void GimbalController::setLEDsByState() {
    if (!_isOnline) return;
    switch (_state) {
        case GIMBAL_STATE_INITIALIZING:
            // 自检：金黄色循环流水
            for (int i = 0; i < 8; i++) {
                _servo.setLEDColor(i, 0xFF7A00);
            }
            break;
        case GIMBAL_STATE_PREPOINT:
            // 预瞄准：橙色
            for (int i = 0; i < 8; i++) {
                _servo.setLEDColor(i, 0xFF4500);
            }
            break;
        case GIMBAL_STATE_TRACKING:
            // 跟踪：前三轴对应亮绿色，其他灭
            for (int i = 0; i < 8; i++) {
                if (i < 3) {
                    _servo.setLEDColor(i, 0x00FF00); // 绿色高亮
                } else {
                    _servo.setLEDColor(i, 0x000000);
                }
            }
            break;
        case GIMBAL_STATE_STANDBY:
        default:
            // 待命/离线：低亮度暗蓝自锁
            for (int i = 0; i < 8; i++) {
                _servo.setLEDColor(i, 0x001133);
            }
            break;
    }
}

void GimbalController::tick() {
    updateStatus();
    
    unsigned long now = millis();
    float dt = (now - _lastTick) / 1000.0f;
    _lastTick = now;
    if (dt <= 0.0f) dt = 0.001f;
    if (dt > 0.5f) dt = 0.5f; // 防止系统在重计算等大卡顿时角度跳变
    
    if (_isOnline) {
        // 自检动画结束后才能转为普通状态
        if (_state == GIMBAL_STATE_INITIALIZING) {
            // 开机慢速滑行归中
            _tarAzAngle = 90.0f;
            _tarElAngle = 90.0f;
            _tarAltAngle = 90.0f;
            _maxDegPerSec = 1.5f; // 极慢
            processLerp(dt);
            updateHardwareServos();
            
            if (fabs(_curAzAngle - 90.0f) < 1.0f && fabs(_curElAngle - 90.0f) < 1.0f) {
                _state = GIMBAL_STATE_STANDBY;
                setLEDsByState();
            }
        } else {
            processLerp(dt);
            updateHardwareServos();
            
            // 针对预瞄准等状态进行周期LED状态呼吸
            static unsigned long lastBreathe = 0;
            static float breatheDir = 1.0f;
            static float breatheVal = 0.5f;
            if (now - lastBreathe > 40) {
                lastBreathe = now;
                breatheVal += breatheDir * 0.03f;
                if (breatheVal >= 1.0f) { breatheVal = 1.0f; breatheDir = -1.0f; }
                else if (breatheVal <= 0.2f) { breatheVal = 0.2f; breatheDir = 1.0f; }
                
                if (_state == GIMBAL_STATE_PREPOINT) {
                    // 呼吸橙色
                    uint8_t r = (uint8_t)(255 * breatheVal);
                    uint8_t g = (uint8_t)(69 * breatheVal);
                    uint32_t color = ((uint32_t)r << 16) | ((uint32_t)g << 8);
                    for (int i = 0; i < 8; i++) {
                        _servo.setLEDColor(i, color);
                    }
                }
            }
        }
    }
}
