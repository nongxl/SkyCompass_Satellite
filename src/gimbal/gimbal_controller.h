#ifndef _GIMBAL_CONTROLLER_H_
#define _GIMBAL_CONTROLLER_H_

#include <Arduino.h>
#include <Wire.h>
#include "Unit8Servo.h"

// 舵机通道定义 - 适配“轨道拱门浑天仪” (Orbital Arch Gimbal)
#define GIMBAL_CH_AZ          0 // 地平面走向轴 (Yaw / Ground Baseline)
#define GIMBAL_CH_INCLINE     1 // 轨道平面拱高倾角轴 (Incline / Max Elevation)
#define GIMBAL_CH_PROGRESS    2 // 卫星在天空中的运动位置轴 (Satellite Sky Progress)

// 兼容别名定义
#define GIMBAL_CH_EL          GIMBAL_CH_INCLINE
#define GIMBAL_CH_ALT         GIMBAL_CH_PROGRESS

// 云台状态机
enum GimbalState {
    GIMBAL_STATE_INITIALIZING, // 自检初始化
    GIMBAL_STATE_PREPOINT,     // 预瞄准下一次过境AOS与拱高
    GIMBAL_STATE_TRACKING,     // 实时过境跟踪 (CH2移动，CH0/CH1锁定)
    GIMBAL_STATE_STANDBY,      // 闲时待命
    GIMBAL_STATE_HOLD,         // 非sat view模式原地静止锁定
    GIMBAL_STATE_TEST          // 手动舵机测试与标定模式
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
    float _curInclineAngle;
    float _curProgressAngle;
    
    // 目标物理角度
    float _tarAzAngle;
    float _tarInclineAngle;
    float _tarProgressAngle;
    
    // 预瞄准和跟踪参数
    unsigned long _lastTick;
    unsigned long _initStartTime;
    bool _isPrepointing;
    
    // 平滑滤波器参数
    float _lerpFactor;      // 跟踪模式的Lerp因子 (0.0f - 1.0f)
    float _maxDegPerSec;    // 最大度数/秒，防止焦点切换和自检时的骤动
    unsigned long _lastLogTick;

    void updateHardwareServos();
    void processLerp(float dt);
    void calculateArchAngles(float trackHeading, float maxEl, float progressDeg, float satAz, float &outAz, float &outIncline, float &outProgress);
    void updateStatus();
    void setLEDsByState();

public:
    GimbalController();
    
    bool begin(TwoWire *wire = &Wire, uint8_t sda = 2, uint8_t scl = 1, uint32_t freq = 400000);
    void tick();
    
    // 轨道拱门专用输入接口 (trackHeading 为轨道天面飞行航向角，satAz 为卫星侧向方位角)
    void setTargetArch(float trackHeading, float maxElevation, float progressDeg, float satAz = 90.0f);
    void setTargetPrePointArch(float trackHeading, float maxElevation, float satAz = 90.0f);
    
    // 兼容传统输入接口
    void setTargetTrack(float realAz, float realEl, float realAltKm);
    void setTargetPrePoint(float aosAz);
    void setStandby();
    void setHold();
    
    // 舵机手动标定与测试接口
    void enterManualTest();
    void exitManualTest();
    void setManualTestAngle(uint8_t ch, float angleDeg, bool immediate = true);
    float getChannelAngle(uint8_t ch) const;
    uint16_t getChannelPulse(uint8_t ch) const;
    
    // 状态查询
    bool isOnline() const { return _isOnline; }
    float getCurrentmA() const { return _currentmA; }
    GimbalState getState() const { return _state; }
};

#endif

