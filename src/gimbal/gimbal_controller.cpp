#include "gimbal_controller.h"

GimbalController::GimbalController() : 
    _state(GIMBAL_STATE_STANDBY), 
    _isOnline(false), 
    _currentmA(0.0f), 
    _fwVersion(0),
    _lastStatusTick(0),
    _curAzAngle(90.0f), _curInclineAngle(90.0f), _curProgressAngle(90.0f),
    _tarAzAngle(90.0f), _tarInclineAngle(90.0f), _tarProgressAngle(90.0f),
    _velAz(0.0f), _velIncline(0.0f), _velProgress(0.0f),
    _lastTick(0),
    _initStartTime(0),
    _isPrepointing(false),
    _maxDegPerSec(12.0f),
    _smoothTime(0.35f),
    _lastLogTick(0),
    _motionTaskHandle(NULL) {}

void GimbalController::motionTaskEntry(void *param) {
    GimbalController *controller = static_cast<GimbalController*>(param);
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(20); // 严格定频 50Hz (20ms)

    while (true) {
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
        if (controller != nullptr) {
            controller->motionTick(0.020f);
        }
    }
}

float GimbalController::smoothDamp(float current, float target, float &currentVelocity, float smoothTime, float maxSpeed, float dt) {
    smoothTime = max(0.0001f, smoothTime);
    float omega = 2.0f / smoothTime;

    float x = omega * dt;
    float expFactor = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    
    float change = current - target;
    float originalTo = target;

    // 限制最大速度
    float maxChange = maxSpeed * smoothTime;
    change = constrain(change, -maxChange, maxChange);
    target = current - change;

    float temp = (currentVelocity + omega * change) * dt;
    currentVelocity = (currentVelocity - omega * temp) * expFactor;
    float output = target + (change + temp) * expFactor;

    // 防止在跨过目标点时出现过冲微小抖动
    if ((originalTo - current > 0.0f) == (output > originalTo)) {
        output = originalTo;
        currentVelocity = (output - originalTo) / dt;
    }

    return output;
}

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
        _velAz = 0.0f;
        _velIncline = 0.0f;
        _velProgress = 0.0f;
        
        // 立即向硬件所有 8 个通道下发 1500us (90 度中位) 控制信号锁定零位
        for (uint8_t ch = 0; ch < 8; ch++) {
            _servo.setServoPulse(ch, 1500);
            _servo.setServoAngle(ch, 90);
        }
        
        _state = GIMBAL_STATE_INITIALIZING;
        _initStartTime = millis();
        setLEDsByState();
        
        log_i("*******************************************************");
        log_i("[Gimbal] >>> INITIALIZING: 3 Servos Locked at 90° for Alignment <<<");
        log_i("*******************************************************");

        // 启动 50Hz (20ms) 独立定频运动插补后台任务
        if (_motionTaskHandle == NULL) {
            xTaskCreatePinnedToCore(
                motionTaskEntry,
                "GimbalMotionTask",
                3072,
                this,
                2, // 优先级 2 (低于 IMU 采样 3，高于/等于空闲任务)
                &_motionTaskHandle,
                0  // 绑定至 Core 0，与 Core 1 的 UI/3D 重度渲染彻底解耦
            );
            log_i("[Gimbal] Smooth Motion Task (50Hz / 20ms) spawned on Core 0");
        }
    } else {
        log_i("[Gimbal] Servo Driver Board NOT DETECTED on Grove port (Offline)");
    }
    _lastTick = millis();
    return _isOnline;
}

void GimbalController::updateStatus() {
    // 仅在硬件在线时维护状态，离线时不进行任何I2C侵入式重连探测
}

void GimbalController::calculateArchAngles(float trackHeading, float maxEl, float progressDeg, float satAz, float &outAz, float &outIncline, float &outProgress) {
    // 规范化 trackHeading 到 [0, 360)
    while (trackHeading < 0) trackHeading += 360.0f;
    while (trackHeading >= 360.0f) trackHeading -= 360.0f;
    
    // 规范化 satAz 到 [0, 360)
    while (satAz < 0) satAz += 360.0f;
    while (satAz >= 360.0f) satAz -= 360.0f;

    // 1. CH1 空间侧向倾角计算 (Ideal Arch Incline)
    float satAzRad = satAz * 0.0174532925f; // DEG_TO_RAD
    bool isLeaningNorth = (cosf(satAzRad) >= 0.0f);
    
    float clampedEl = constrain(maxEl, 0.0f, 90.0f);
    float idealIncline;
    if (isLeaningNorth) {
        // 偏北倒：仰角越低越接近 0°，防卡大梁限制最低 30°
        idealIncline = max(clampedEl, 30.0f);
    } else {
        // 偏南倒：仰角越低越接近 180°，无干涉允许达到 180°
        idealIncline = 180.0f - clampedEl;
    }

    // 2. CH0 (长梁走向) 与航向 TrackHeading 严格对齐
    bool isOppositeHemisphere = (trackHeading > 180.0f);
    if (!isOppositeHemisphere) {
        outAz = 180.0f - trackHeading;
        outIncline = idealIncline;
        outProgress = progressDeg; // 滑块顺向推进 (0° -> 180°)
    } else {
        outAz = 360.0f - trackHeading;
        outIncline = 180.0f - idealIncline;
        outProgress = 180.0f - progressDeg; // 滑块从远端滑回 (180° -> 0°)
    }

    // 硬件限位保护（CH1 单侧防卡大梁、CH2 机械结构防卡保护，有效范围均为 30° ~ 180°）
    outAz = constrain(outAz, 0.0f, 180.0f);
    outIncline = constrain(outIncline, 30.0f, 180.0f);
    outProgress = constrain(outProgress, 30.0f, 180.0f);
}

void GimbalController::setTargetArch(float trackHeading, float maxElevation, float progressDeg, float satAz, const char* targetName) {
    // 开机 90° 对齐自检期间受保护，严禁被打断
    if (_state == GIMBAL_STATE_INITIALIZING || _state == GIMBAL_STATE_TEST) return;

    if (targetName) _activeTargetName = targetName;

    float tarAz, tarIncline, tarProgress;
    calculateArchAngles(trackHeading, maxElevation, progressDeg, satAz, tarAz, tarIncline, tarProgress);
    
    _tarAzAngle = tarAz;
    _tarInclineAngle = tarIncline;
    _tarProgressAngle = tarProgress;
    
    if (_state != GIMBAL_STATE_TRACKING) {
        log_i("[Gimbal] State changed from %d to TRACKING [%s] (TrackHeading: %.1f, MaxEl: %.1f, SatAz: %.1f)", 
              _state, _activeTargetName.c_str(), trackHeading, maxElevation, satAz);
        _state = GIMBAL_STATE_TRACKING;
        _maxDegPerSec = 14.0f; // 动态跟随卫星，速度平稳适中
        _smoothTime = 0.30f;   // 0.30秒阻尼平滑，消灭微跳
        setLEDsByState();
    }
}

void GimbalController::setTargetPrePointArch(float trackHeading, float maxElevation, float satAz, const char* targetName) {
    // 开机 90° 对齐自检期间受保护，严禁被打断
    if (_state == GIMBAL_STATE_INITIALIZING || _state == GIMBAL_STATE_TEST) return;

    if (targetName) _activeTargetName = targetName;

    float tarAz, tarIncline, tarProgress;
    calculateArchAngles(trackHeading, maxElevation, 0.0f, satAz, tarAz, tarIncline, tarProgress);
    
    _tarAzAngle = tarAz;
    _tarInclineAngle = tarIncline;
    _tarProgressAngle = tarProgress; // 静止停在拱门起跑线 (0° 或 180°)
    
    if (_state != GIMBAL_STATE_PREPOINT) {
        log_i("[Gimbal] State changed from %d to PREPOINT [%s] (TrackHeading: %.1f, MaxEl: %.1f, SatAz: %.1f)", 
              _state, _activeTargetName.c_str(), trackHeading, maxElevation, satAz);
        _state = GIMBAL_STATE_PREPOINT;
        _maxDegPerSec = 16.0f; // 调整为 16°/s 优雅从容转速，告别粗暴冲击
        _smoothTime = 0.45f;   // 0.45秒缓启缓停缓冲，极致丝滑
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
    _tarInclineAngle = 90.0f;   // 黑色拱门竖直立起归位 (90°)
    _tarProgressAngle = 90.0f; // 星位指针直指拱顶归位 (90°)
    
    if (_state != GIMBAL_STATE_STANDBY) {
        log_i("[Gimbal] State changed from %d to STANDBY (All 3-Axis Locked at 90°)", _state);
        _state = GIMBAL_STATE_STANDBY;
        _maxDegPerSec = 8.0f;
        _smoothTime = 0.60f;   // 归中动作极其柔和舒缓
        setLEDsByState();
    }
}

void GimbalController::setHold() {
    // 开机 90° 对齐自检期间受保护，严禁被打断
    if (_state == GIMBAL_STATE_INITIALIZING) return;
    if (_state == GIMBAL_STATE_HOLD) return;

    _tarAzAngle = _curAzAngle;
    _tarInclineAngle = _curInclineAngle;
    _tarProgressAngle = _curProgressAngle;
    _velAz = 0.0f;
    _velIncline = 0.0f;
    _velProgress = 0.0f;
    
    if (_state != GIMBAL_STATE_TEST) {
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
    _velAz = 0.0f;
    _velIncline = 0.0f;
    _velProgress = 0.0f;
    _maxDegPerSec = 45.0f;
    _smoothTime = 0.10f;
    setLEDsByState();
    log_i("[Gimbal] >>> ENTER SERVO TEST MODE (Current: Az=%.1f, Inc=%.1f, Prog=%.1f) <<<", 
          _curAzAngle, _curInclineAngle, _curProgressAngle);
}

void GimbalController::exitManualTest() {
    _state = GIMBAL_STATE_HOLD;
    _tarAzAngle = _curAzAngle;
    _tarInclineAngle = _curInclineAngle;
    _tarProgressAngle = _curProgressAngle;
    _velAz = 0.0f;
    _velIncline = 0.0f;
    _velProgress = 0.0f;
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
        clamped = constrain(angleDeg, 30.0f, 180.0f);
        _tarInclineAngle = clamped;
        if (immediate) _curInclineAngle = clamped;
    } else if (ch == GIMBAL_CH_PROGRESS) {
        // CH2 机械结构限制：过低角度会卡到硬件，有效范围 30° ~ 180°
        clamped = constrain(angleDeg, 30.0f, 180.0f);
        _tarProgressAngle = clamped;
        if (immediate) _curProgressAngle = clamped;
    }
    
    if (immediate) {
        _velAz = 0.0f;
        _velIncline = 0.0f;
        _velProgress = 0.0f;
        if (!_isOnline) {
            log_w("[Gimbal] Manual Test Warning: Driver board is OFFLINE!");
        } else {
            uint16_t p = (uint16_t)(500.0f + (clamped / 180.0f) * 2000.0f + 0.5f);
            uint8_t a = (uint8_t)clamped;
            bool okP = _servo.setServoPulse(ch, p);
            bool okA = _servo.setServoAngle(ch, a);
            log_i("[Gimbal] Direct Hardware Write -> CH%d Pulse:%dus Angle:%d (I2C Res: Pulse=%d, Angle=%d)", 
                  ch, p, a, okP, okA);
        }
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

void GimbalController::processSmoothDamp(float dt) {
    _curAzAngle = smoothDamp(_curAzAngle, _tarAzAngle, _velAz, _smoothTime, _maxDegPerSec, dt);
    _curInclineAngle = smoothDamp(_curInclineAngle, _tarInclineAngle, _velIncline, _smoothTime, _maxDegPerSec, dt);
    _curProgressAngle = smoothDamp(_curProgressAngle, _tarProgressAngle, _velProgress, _smoothTime, _maxDegPerSec, dt);
}

void GimbalController::updateHardwareServos() {
    if (!_isOnline) return;

    auto angleToPulse = [](float deg) -> uint16_t {
        float clamped = constrain(deg, 0.0f, 180.0f);
        return (uint16_t)(500.0f + (clamped / 180.0f) * 2000.0f + 0.5f);
    };

    uint16_t p0 = angleToPulse(_curAzAngle);
    uint16_t p1 = angleToPulse(_curInclineAngle);
    uint16_t p2 = angleToPulse(_curProgressAngle);

    static uint16_t s_lastP0 = 0, s_lastP1 = 0, s_lastP2 = 0;
    // 采用 1us 细腻死区（约 0.09°），配合 50Hz 平滑阻尼插补，消灭阶梯顿挫，静止时 0 写入
    if (abs((int)p0 - (int)s_lastP0) >= 1) {
        _servo.setServoPulse(GIMBAL_CH_AZ, p0);
        s_lastP0 = p0;
    }
    if (abs((int)p1 - (int)s_lastP1) >= 1) {
        _servo.setServoPulse(GIMBAL_CH_INCLINE, p1);
        s_lastP1 = p1;
    }
    if (abs((int)p2 - (int)s_lastP2) >= 1) {
        _servo.setServoPulse(GIMBAL_CH_PROGRESS, p2);
        s_lastP2 = p2;
    }
}

void GimbalController::setLEDsByState() {
    // 浑仪专用于舵机控制，不向 Signal 脚发送单总线灯珠信号
}

void GimbalController::motionTick(float dt) {
    if (!_isOnline) return;

    if (_state == GIMBAL_STATE_INITIALIZING) {
        _curAzAngle = 90.0f;
        _curInclineAngle = 90.0f;
        _curProgressAngle = 90.0f;
        _tarAzAngle = 90.0f;
        _tarInclineAngle = 90.0f;
        _tarProgressAngle = 90.0f;
        _velAz = 0.0f;
        _velIncline = 0.0f;
        _velProgress = 0.0f;
        updateHardwareServos();
    } else if (_state == GIMBAL_STATE_TEST) {
        updateHardwareServos();
    } else {
        processSmoothDamp(dt);
        updateHardwareServos();
    }
}

void GimbalController::tick() {
    if (!_isOnline) {
        return;
    }
    
    updateStatus();
    
    unsigned long now = millis();
    
    // 开机自检对齐倒计时管理
    if (_state == GIMBAL_STATE_INITIALIZING) {
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
            _velAz = 0.0f;
            _velIncline = 0.0f;
            _velProgress = 0.0f;
            setLEDsByState();
        }
    } else {
        // 如果后台任务未正常运行（防御性兜底），则由主循环推进运动插补
        if (_motionTaskHandle == NULL) {
            float dt = (now - _lastTick) / 1000.0f;
            if (dt <= 0.0f) dt = 0.001f;
            if (dt > 0.1f) dt = 0.1f;
            motionTick(dt);
        }
    }
    _lastTick = now;
    
    // 周期性状态日志 (仅在有动态追踪/预指向任务时每 1500 毫秒输出，静止 HOLD 保持静默)
    if (now - _lastLogTick > 1500) {
        _lastLogTick = now;
        if (_state == GIMBAL_STATE_TRACKING) {
            log_i("[Gimbal] TRACKING [%s] | Target Arch -> BaseAz:%.1f, Incline:%.1f, Prog:%.1f | Out -> Az:%.1f, Inc:%.1f, Prog:%.1f",
                _activeTargetName.c_str(), _tarAzAngle, _tarInclineAngle, _tarProgressAngle, _curAzAngle, _curInclineAngle, _curProgressAngle);
        } else if (_state == GIMBAL_STATE_PREPOINT) {
            log_i("[Gimbal] PREPOINT [%s] | Target -> BaseAz:%.1f, Incline:%.1f | Out -> Az:%.1f, Inc:%.1f, Prog:%.1f",
                _activeTargetName.c_str(), _tarAzAngle, _tarInclineAngle, _curAzAngle, _curInclineAngle, _curProgressAngle);
        }
    }
}
