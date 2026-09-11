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
        
        // 立即向硬件下发 1500us (90 度中位) 控制信号锁定零位
        _servo.setServoPulse(GIMBAL_CH_AZ, 1500);
        _servo.setServoPulse(GIMBAL_CH_INCLINE, 1500);
        _servo.setServoPulse(GIMBAL_CH_PROGRESS, 1500);
        
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
    // 仅在硬件在线时维护状态，离线时不进行任何I2C侵入式重连探测
}

void GimbalController::calculateArchAngles(float baseAz, float maxEl, float progressDeg, float maxAz, float &outAz, float &outIncline, float &outProgress) {
    // 规范化 baseAz 到 [0, 360)
    while (baseAz < 0) baseAz += 360.0f;
    while (baseAz >= 360.0f) baseAz -= 360.0f;
    
    // 规范化 maxAz 到 [0, 360)
    while (maxAz < 0) maxAz += 360.0f;
    while (maxAz >= 360.0f) maxAz -= 360.0f;

    // 1. CH1 理想空间侧向倾角计算 (Ideal Arch Incline)：
    // 实测物理规律：
    // CH1 = 90° 为天顶垂直立起；
    // CH1 < 90° 为向正北倾倒；
    // CH1 > 90° 为向正南倾倒。
    // 最高点方位 maxAz: 若 cos(maxAz) >= 0 (即 maxAz 在 270°~90°，偏北天区)，则拱门倒向正北；
    // 若 cos(maxAz) < 0 (maxAz 在 90°~270°，偏南天区)，则拱门倒向正南。
    float maxAzRad = maxAz * 0.0174532925f; // DEG_TO_RAD
    bool isLeaningNorth = (cosf(maxAzRad) >= 0.0f);
    
    float clampedEl = constrain(maxEl, 0.0f, 90.0f);
    float idealIncline;
    if (isLeaningNorth) {
        // 偏北倒：仰角越低越接近 0°
        idealIncline = clampedEl;
    } else {
        // 偏南倒：仰角越低越接近 180°
        idealIncline = 180.0f - clampedEl;
    }

    // 2. CH0 (走向) 与长梁 180° 对向折叠刚体联动：
    // 实测物理规律：
    // CH0 = 180° 指向正北 (0°)；
    // CH0 = 90°  指向正东 (90°)；
    // CH0 = 0°   指向正南 (180°)。
    // 线性方程：CH0 = 180° - Az
    bool isOppositeHemisphere = (baseAz > 180.0f);
    if (!isOppositeHemisphere) {
        // 正向东半球 (0° ~ 180°)：长梁指示端直对升起点
        outAz = 180.0f - baseAz;
        outIncline = idealIncline;
        outProgress = progressDeg; // 滑块从指示端(0°)顺向滑向(180°)
    } else {
        // 对向西半球 (180° ~ 360°)：长梁掉头 180° 对齐升起点
        outAz = 360.0f - baseAz; // 即 180° - (baseAz - 180°)
        // 长梁掉头导致安装在其上的舵机底座左右/起止翻转，必须联动补角反相补偿：
        outIncline = 180.0f - idealIncline;
        outProgress = 180.0f - progressDeg; // 滑块从远端(180°)滑向(0°)
    }

    // 硬件限位保护
    outAz = constrain(outAz, 0.0f, 180.0f);
    outIncline = constrain(outIncline, 0.0f, 180.0f);
    outProgress = constrain(outProgress, 0.0f, 180.0f);
}

void GimbalController::setTargetArch(float baseAz, float maxElevation, float progressDeg, float maxAz) {
    // 开机 90° 对齐自检期间受保护，严禁被打断
    if (_state == GIMBAL_STATE_INITIALIZING || _state == GIMBAL_STATE_TEST) return;

    float tarAz, tarIncline, tarProgress;
    calculateArchAngles(baseAz, maxElevation, progressDeg, maxAz, tarAz, tarIncline, tarProgress);
    
    _tarAzAngle = tarAz;
    _tarInclineAngle = tarIncline;
    _tarProgressAngle = tarProgress;
    
    if (_state != GIMBAL_STATE_TRACKING) {
        log_i("[Gimbal] State changed from %d to TRACKING (Arch BaseAz: %.1f, MaxEl: %.1f, MaxAz: %.1f)", 
              _state, baseAz, maxElevation, maxAz);
        _state = GIMBAL_STATE_TRACKING;
        _maxDegPerSec = 15.0f; // 跟踪模式下允许响应稍快
        setLEDsByState();
    }
}

void GimbalController::setTargetPrePointArch(float aosAz, float maxElevation, float maxAz) {
    // 开机 90° 对齐自检期间受保护，严禁被打断
    if (_state == GIMBAL_STATE_INITIALIZING || _state == GIMBAL_STATE_TEST) return;

    float tarAz, tarIncline, tarProgress;
    calculateArchAngles(aosAz, maxElevation, 0.0f, maxAz, tarAz, tarIncline, tarProgress);
    
    _tarAzAngle = tarAz;
    _tarInclineAngle = tarIncline;
    _tarProgressAngle = tarProgress; // 静止停在拱门起跑线 (0° 或 180°)
    
    if (_state != GIMBAL_STATE_PREPOINT) {
        log_i("[Gimbal] State changed from %d to PREPOINT (AOS Az: %.1f, MaxEl: %.1f, MaxAz: %.1f)", 
              _state, aosAz, maxElevation, maxAz);
        _state = GIMBAL_STATE_PREPOINT;
        _maxDegPerSec = 12.0f; // 提升预瞄准转动速度，顺滑且快速就位
        setLEDsByState();
    }
}

void GimbalController::setTargetTrack(float realAz, float realEl, float realAltKm) {
    float maxEl = max(realEl, 45.0f);
    float progress = (realEl > 0.0f ? (realEl / maxEl) * 90.0f : 0.0f);
    setTargetArch(realAz, maxEl, progress, realAz);
}

void GimbalController::setTargetPrePoint(float aosAz) {
    setTargetPrePointArch(aosAz, 45.0f, aosAz);
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
    
    if (_state != GIMBAL_STATE_HOLD && _state != GIMBAL_STATE_TEST) {
        log_i("[Gimbal] State changed from %d to HOLD (Stationary at Az:%.1f, Inc:%.1f, Prog:%.1f)", 
              _state, _curAzAngle, _curInclineAngle, _curProgressAngle);
        _state = GIMBAL_STATE_HOLD;
        setLEDsByState();
    }
}

void GimbalController::enterManualTest() {
    _state = GIMBAL_STATE_TEST;
    _tarAzAngle = _curAzAngle;
    _tarInclineAngle = _curInclineAngle;
    _tarProgressAngle = _curProgressAngle;
    _maxDegPerSec = 45.0f; // 测试模式下插补响应更迅速
    setLEDsByState();
    log_i("[Gimbal] >>> ENTER SERVO TEST MODE (Current: Az=%.1f, Inc=%.1f, Prog=%.1f) <<<", 
          _curAzAngle, _curInclineAngle, _curProgressAngle);
}

void GimbalController::exitManualTest() {
    _state = GIMBAL_STATE_HOLD;
    _tarAzAngle = _curAzAngle;
    _tarInclineAngle = _curInclineAngle;
    _tarProgressAngle = _curProgressAngle;
    setLEDsByState();
    log_i("[Gimbal] <<< EXIT SERVO TEST MODE -> Back to HOLD (Az=%.1f, Inc=%.1f, Prog=%.1f) >>>",
          _curAzAngle, _curInclineAngle, _curProgressAngle);
}

void GimbalController::setManualTestAngle(uint8_t ch, float angleDeg, bool immediate) {
    float clamped = constrain(angleDeg, 0.0f, 180.0f);
    if (ch == GIMBAL_CH_AZ) {
        _tarAzAngle = clamped;
        if (immediate) _curAzAngle = clamped;
    } else if (ch == GIMBAL_CH_INCLINE) {
        _tarInclineAngle = clamped;
        if (immediate) _curInclineAngle = clamped;
    } else if (ch == GIMBAL_CH_PROGRESS) {
        _tarProgressAngle = clamped;
        if (immediate) _curProgressAngle = clamped;
    }
    
    if (immediate) {
        updateHardwareServos();
    }
}

float GimbalController::getChannelAngle(uint8_t ch) const {
    if (ch == GIMBAL_CH_AZ) return _curAzAngle;
    if (ch == GIMBAL_CH_INCLINE) return _curInclineAngle;
    if (ch == GIMBAL_CH_PROGRESS) return _curProgressAngle;
    return 90.0f;
}

uint16_t GimbalController::getChannelPulse(uint8_t ch) const {
    float deg = getChannelAngle(ch);
    float clamped = constrain(deg, 0.0f, 180.0f);
    return (uint16_t)(500.0f + (clamped / 180.0f) * 2000.0f + 0.5f);
}

void GimbalController::processLerp(float dt) {
    float maxStep = _maxDegPerSec * dt;
    
    // 真线性恒速插补：以恒定线速度向目标移动，无非线性指数衰减，转动平滑且匀速
    auto linearStep = [&](float &cur, float tar) {
        float diff = tar - cur;
        if (fabs(diff) > 0.02f) {
            if (diff > maxStep) {
                cur += maxStep;
            } else if (diff < -maxStep) {
                cur -= maxStep;
            } else {
                cur = tar;
            }
        } else {
            cur = tar;
        }
    };

    linearStep(_curAzAngle, _tarAzAngle);
    linearStep(_curInclineAngle, _tarInclineAngle);
    linearStep(_curProgressAngle, _tarProgressAngle);
}

void GimbalController::updateHardwareServos() {
    if (!_isOnline) return;

    // 将浮点角度 [0.0°, 180.0°] 映射为高精度微秒级脉冲 [500us, 2500us]
    // 2000 个细分台阶，相比 8-bit 整数角度 (180 阶) 分辨率提升 11 倍
    auto angleToPulse = [](float deg) -> uint16_t {
        float clamped = constrain(deg, 0.0f, 180.0f);
        return (uint16_t)(500.0f + (clamped / 180.0f) * 2000.0f + 0.5f);
    };

    uint16_t p0 = angleToPulse(_curAzAngle);
    uint16_t p1 = angleToPulse(_curInclineAngle);
    uint16_t p2 = angleToPulse(_curProgressAngle);

    static uint16_t s_lastP0 = 0, s_lastP1 = 0, s_lastP2 = 0;
    // 采用 2us 动态死区（约 0.18° 变动阈值），既保证极致细腻丝滑，又避免无意义的 I2C 总线频繁刷写
    if (abs((int)p0 - (int)s_lastP0) >= 2) {
        _servo.setServoPulse(GIMBAL_CH_AZ, p0);
        s_lastP0 = p0;
    }
    if (abs((int)p1 - (int)s_lastP1) >= 2) {
        _servo.setServoPulse(GIMBAL_CH_INCLINE, p1);
        s_lastP1 = p1;
    }
    if (abs((int)p2 - (int)s_lastP2) >= 2) {
        _servo.setServoPulse(GIMBAL_CH_PROGRESS, p2);
        s_lastP2 = p2;
    }
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
        case GIMBAL_STATE_TEST:
            // 测试模式：前三轴高亮青蓝色 (Cyan: 0x00FFFF)，其余灭
            for (int i = 0; i < 8; i++) {
                if (i < 3) {
                    _servo.setLEDColor(i, 0x00FFFF);
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
    // 若开机未检测到舵机硬件或硬件离线，完全退出，不进行任何I2C总线探测和CPU运算
    if (!_isOnline) {
        return;
    }
    
    updateStatus();
    
    unsigned long now = millis();
    float dt = (now - _lastTick) / 1000.0f;
    _lastTick = now;
    if (dt <= 0.0f) dt = 0.001f;
    if (dt > 0.5f) dt = 0.5f; // 防止大卡顿时跳变
    
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
    }
    
    // 周期性状态日志 (每1500毫秒)
    if (now - _lastLogTick > 1500) {
        _lastLogTick = now;
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
    }
}


