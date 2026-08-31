#ifndef _GIMBAL_CONTROLLER_H_
#define _GIMBAL_CONTROLLER_H_

#include <Arduino.h>
#include <Wire.h>
#include "Unit8Servo.h"

// 舵机通道定义
#define GIMBAL_CH_AZ      0 // 方位轴 (Yaw)
#define GIMBAL_CH_EL      1 // 高度轴 (Pitch)
#define GIMBAL_CH_ALT     2 // 卫星高度轴 (Roll/Height)

// 云台状态机
enum GimbalState {
    GIMBAL_STATE_INITIALIZING, // 自检初始化
    GIMBAL_STATE_PREPOINT,     // 预瞄准下一次过境AOS
    GIMBAL_STATE_TRACKING,     // 实时过境跟踪
    GIMBAL_STATE_STANDBY       // 闲时待命
};

class GimbalController {
private:
    Unit8Servo _servo;
    GimbalState _state;
    bool _isOnline;
    float _currentmA;
    uint8_t _fwVersion;
    unsigned long _lastStatusTick;
    
    // 当前物理输出角度
    float _curAzAngle;
    float _curElAngle;
    float _curAltAngle;
    
    // 目标物理角度
    float _tarAzAngle;
    float _tarElAngle;
    float _tarAltAngle;
    
    // 预瞄准和跟踪参数
    unsigned long _lastTick;
    bool _isPrepointing;
    
    // 平滑滤波器参数
    float _lerpFactor;      // 跟踪模式的Lerp因子 (0.0f - 1.0f)
    float _maxDegPerSec;    // 最大度数/秒，防止焦点切换和自检时的骤动
    unsigned long _lastLogTick;

    void updateHardwareServos();
    void processLerp(float dt);
    void apply180DegreeLimit(float az, float el, float alt, float &outAz, float &outEl, float &outAlt);
    void updateStatus();
    void setLEDsByState();

public:
    GimbalController();
    
    bool begin(TwoWire *wire = &Wire, uint8_t sda = 2, uint8_t scl = 1, uint32_t freq = 400000);
    void tick();
    
    // 输入接口
    void setTargetTrack(float realAz, float realEl, float realAltKm);
    void setTargetPrePoint(float aosAz);
    void setStandby();
    
    // 状态查询
    bool isOnline() const { return _isOnline; }
    float getCurrentmA() const { return _currentmA; }
    GimbalState getState() const { return _state; }
};

#endif
