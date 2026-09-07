#include "gimbal_controller.h"

GimbalController::GimbalController() : 
    _state(GIMBAL_STATE_STANDBY), 
    _isOnline(false), 
    _currentmA(0.0f), 
    _fwVersion(0),
    _lastStatusTick(0),
    _curAzAngle(90.0f), _curInclineAngle(90.0f), _curProgressAngle(90.0f),
    _tarAzAngle(90.0f), _tarInclineAngle(90.0f), _tarProgressAngle(90.0f),
    _lastTick(0),
    _initStartTime(0),
    _lerpFactor(0.08f),
    _maxDegPerSec(5.0f),
    _lastLogTick(0) {}

bool GimbalController::begin(TwoWire *wire, uint8_t sda, uint8_t scl, uint32_t freq) {
    // 启用 I2C 超时检测机制，防线缆松动死锁
    wire->setTimeout(25); // 25ms timeout
    
    _isOnline = _servo.begin(wire, sda, scl, freq);
    if (_isOnline) {
        _fwVersion = _servo.getFirmwareVersion();
        _servo.setAllPinMode(SERVO_CTL_MODE);
        delay(20);
        
        // 软启动初始值（三轴全 90 度，便于硬件归中对齐）
        _curAzAngle = 90.0f;
        _curInclineAngle = 90.0f;
        _curProgressAngle = 90.0f;
        _tarAzAngle = 90.0f;
        _tarInclineAngle = 90.0f;
        _tarProgressAngle = 90.0f;
        
        // 立即向硬件下发 90 度控制信号锁定零位
        _servo.setServoAngle(GIMBAL_CH_AZ, 90);
        _servo.setServoAngle(GIMBAL_CH_INCLINE, 90);
        _servo.setServoAngle(GIMBAL_CH_PROGRESS, 90);
        
        _state = GIMBAL_STATE_INITIALIZING;
        _initStartTime = millis();
        setLEDsByState();
        
        log_i("*******************************************************");
        log_i("[Gimbal] >>> INITIALIZING: 3 Servos Locked at 90° for Alignment <<<");
        log_i("*******************************************************");
    } else {
        log_i("[Gimbal] Servo Driver Board NOT DETECTED on Grove port (Offline)");
    }
    _lastTick = millis();
    return _isOnline;
}

void GimbalController::updateStatus() {
    if (millis() - _lastStatusTick > 3000) {
        _lastStatusTick = millis();
        // 在线状态下保持总线纯净，不进行高频侵入式探测；仅在掉线时尝试探测重连
        if (!_isOnline) {
            bool conn = _servo.isConnected();
            if (conn) {
                _isOnline = true;
                _servo.setAllPinMode(SERVO_CTL_MODE);
                _fwVersion = _servo.getFirmwareVersion();
                // 恢复位置
                _servo.setServoAngle(GIMBAL_CH_AZ, (uint8_t)constrain(_curAzAngle, 0, 180));
                _servo.setServoAngle(GIMBAL_CH_INCLINE, (uint8_t)constrain(_curInclineAngle, 0, 180));
                _servo.setServoAngle(GIMBAL_CH_PROGRESS, (uint8_t)constrain(_curProgressAngle, 0, 180));
                setLEDsByState();
                log_i("[Gimbal] Hardware re-connected!");
            }
        }
    }
}

void GimbalController::calculateArchAngles(float baseAz, float maxEl, float progressDeg, float &outAz, float &outIncline, float &outProgress) {
    // 规范化 baseAz 到 [0, 360)
    while (baseAz < 0) baseAz += 360.0f;
    while (baseAz >= 360.0f) baseAz -= 360.0f;

    // 白色横梁是两端对称的长条直梁，旋转范围 0-180 度即可覆盖 360 度所有过境走向
    if (baseAz <= 180.0f) {
        outAz = baseAz;
        outProgress = progressDeg; // 正向划过拱门 (0° -> 180°)
    } else {
        outAz = baseAz - 180.0f;
        outProgress = 180.0f - progressDeg; // 对侧反向划过拱门 (180° -> 0°)
    }

    // 拱门倾角：最大仰角直接映射，限制在 0° - 90° 之间
    outIncline = constrain(maxEl, 0.0f, 90.0f);

    // 硬件限位
    outAz = constrain(outAz, 0.0f, 180.0f);
    outProgress = constrain(outProgress, 0.0f, 180.0f);
}

void GimbalController::setTargetArch(float baseAz, float maxElevation, float progressDeg) {
    // 开机 90° 对齐自检期间受保护，严禁被打断
    if (_state == GIMBAL_STATE_INITIALIZING) return;

    float tarAz, tarIncline, tarProgress;
    calculateArchAngles(baseAz, maxElevation, progressDeg, tarAz, tarIncline, tarProgress);
    
    _tarAzAngle = tarAz;
    _tarInclineAngle = tarIncline;
    _tarProgressAngle = tarProgress;
    
    if (_state != GIMBAL_STATE_TRACKING) {
        log_i("[Gimbal] State changed from %d to TRACKING (Arch BaseAz: %.1f, MaxEl: %.1f)", 
              _state, baseAz, maxElevation);
        _state = GIMBAL_STATE_TRACKING;
        _maxDegPerSec = 15.0f; // 跟踪模式下允许响应稍快
        setLEDsByState();
    }
}

void GimbalController::setTargetPrePointArch(float aosAz, float maxElevation) {
    // 开机 90° 对齐自检期间受保护，严禁被打断
    if (_state == GIMBAL_STATE_INITIALIZING) return;

    float tarAz, tarIncline, tarProgress;
    calculateArchAngles(aosAz, maxElevation, 0.0f, tarAz, tarIncline, tarProgress);
    
    _tarAzAngle = tarAz;
    _tarInclineAngle = tarIncline;
    _tarProgressAngle = tarProgress; // 静止停在拱门起跑线 (0° 或 180°)
    
    if (_state != GIMBAL_STATE_PREPOINT) {
        log_i("[Gimbal] State changed from %d to PREPOINT (AOS Az: %.1f, MaxEl: %.1f)", 
              _state, aosAz, maxElevation);
        _state = GIMBAL_STATE_PREPOINT;
        _maxDegPerSec = 3.0f; // 极慢角速度，静默预定目标
        setLEDsByState();
    }
}

void GimbalController::setTargetTrack(float realAz, float realEl, float realAltKm) {
    float maxEl = max(realEl, 45.0f);
    float progress = (realEl > 0.0f ? (realEl / maxEl) * 90.0f : 0.0f);
    setTargetArch(realAz, maxEl, progress);
}

void GimbalController::setTargetPrePoint(float aosAz) {
    setTargetPrePointArch(aosAz, 45.0f);
}

void GimbalController::setStandby() {
    // 开机 90° 对齐自检期间受保护，严禁被打断
    if (_state == GIMBAL_STATE_INITIALIZING) return;

    _tarAzAngle = 90.0f;       // 白色横梁居中归位 (90°)
    _tarInclineAngle = 90.0f;   // 黑色拱门竖直立起归位 (90°，绝不擅自向0°放平)
    _tarProgressAngle = 90.0f; // 星位指针直指拱顶归位 (90°)
    
    if (_state != GIMBAL_STATE_STANDBY) {
        log_i("[Gimbal] State changed from %d to STANDBY (All 3-Axis Locked at 90°)", _state);
        _state = GIMBAL_STATE_STANDBY;
        _maxDegPerSec = 3.0f;
        setLEDsByState();
    }
}

void GimbalController::setHold() {
    // 开机 90° 对齐自检期间受保护，严禁被打断
    if (_state == GIMBAL_STATE_INITIALIZING) return;

    // 冻结目标角度为当前实际角度，完全保持静止不动
    _tarAzAngle = _curAzAngle;
    _tarInclineAngle = _curInclineAngle;
    _tarProgressAngle = _curProgressAngle;
    
    if (_state != GIMBAL_STATE_HOLD) {
        log_i("[Gimbal] State changed from %d to HOLD (Stationary at Az:%.1f, Inc:%.1f, Prog:%.1f)", 
              _state, _curAzAngle, _curInclineAngle, _curProgressAngle);
        _state = GIMBAL_STATE_HOLD;
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
    lerpStep(_curInclineAngle, _tarInclineAngle);
    lerpStep(_curProgressAngle, _tarProgressAngle);
}

void GimbalController::updateHardwareServos() {
    if (!_isOnline) return;
    uint8_t a0 = (uint8_t)constrain(_curAzAngle, 0, 180);
    uint8_t a1 = (uint8_t)constrain(_curInclineAngle, 0, 180);
    uint8_t a2 = (uint8_t)constrain(_curProgressAngle, 0, 180);

    static uint8_t s_last0 = 255, s_last1 = 255, s_last2 = 255;
    if (a0 != s_last0) { _servo.setServoAngle(GIMBAL_CH_AZ, a0); s_last0 = a0; }
    if (a1 != s_last1) { _servo.setServoAngle(GIMBAL_CH_INCLINE, a1); s_last1 = a1; }
    if (a2 != s_last2) { _servo.setServoAngle(GIMBAL_CH_PROGRESS, a2); s_last2 = a2; }
}

void GimbalController::setLEDsByState() {
    if (!_isOnline) return;
    switch (_state) {
        case GIMBAL_STATE_INITIALIZING:
            // 自检：金黄色
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
    if (dt > 0.5f) dt = 0.5f; // 防止大卡顿时跳变
    
    if (_isOnline) {
        // 开机自检对齐：开机前 8 秒死死锁定在 90 度，不响应任何其他指令
        if (_state == GIMBAL_STATE_INITIALIZING) {
            _curAzAngle = 90.0f;
            _curInclineAngle = 90.0f;
            _curProgressAngle = 90.0f;
            _tarAzAngle = 90.0f;
            _tarInclineAngle = 90.0f;
            _tarProgressAngle = 90.0f;
            updateHardwareServos();
            
            unsigned long elapsed = now - _initStartTime;
            if (elapsed < 8000) {
                if (now - _lastLogTick > 1000) {
                    _lastLogTick = now;
                    int rem = (8000 - elapsed) / 1000;
                    log_i("[Gimbal] >>> INITIALIZING: 3-Axis Locked at 90° for Alignment (Remaining: %d s) <<<", rem);
                }
            } else {
                log_i("[Gimbal] Initialization complete! System is in HOLD (All 3-Axis Locked at 90°).");
                _state = GIMBAL_STATE_HOLD;
                _tarAzAngle = 90.0f;
                _tarInclineAngle = 90.0f;
                _tarProgressAngle = 90.0f;
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
    
    // 周期性状态日志 (每1500毫秒)
    if (now - _lastLogTick > 1500) {
        _lastLogTick = now;
        if (_isOnline) {
            if (_state == GIMBAL_STATE_TRACKING) {
                log_i("[Gimbal] TRACKING | Target Arch -> BaseAz:%.1f, Incline:%.1f, Prog:%.1f | Out -> Az:%.1f, Inc:%.1f, Prog:%.1f",
                    _tarAzAngle, _tarInclineAngle, _tarProgressAngle, _curAzAngle, _curInclineAngle, _curProgressAngle);
            } else if (_state == GIMBAL_STATE_PREPOINT) {
                log_i("[Gimbal] PREPOINT | Target -> BaseAz:%.1f, Incline:%.1f | Out -> Az:%.1f, Inc:%.1f, Prog:%.1f",
                    _tarAzAngle, _tarInclineAngle, _curAzAngle, _curInclineAngle, _curProgressAngle);
            } else if (_state == GIMBAL_STATE_STANDBY || _state == GIMBAL_STATE_HOLD) {
                log_i("[Gimbal] HOLD | Stationary at -> Az:%.1f, Inc:%.1f, Prog:%.1f",
                    _curAzAngle, _curInclineAngle, _curProgressAngle);
            }
        } else {
            static unsigned long lastOfflineLog = 0;
            if (now - lastOfflineLog > 5000) {
                lastOfflineLog = now;
                log_i("[Gimbal] OFFLINE (Check Grove port wiring)");
            }
        }
    }
}


