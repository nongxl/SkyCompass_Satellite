#ifndef SPRING_ANIMATOR_H
#define SPRING_ANIMATOR_H

#include <Arduino.h>
#include <vector>
#include <cmath>

/**
 * @brief 弹簧物理配置 (基于 SwiftUI / lilt 物理模型)
 */
struct SpringConfig {
    float response; // 固有无阻尼振动周期 (秒), 控制响应快慢
    float damping;  // 阻尼比 (1.0 为临界阻尼无回弹, <1.0 产生物理回弹)
    bool ease;      // 若为 true 则使用单向指数衰减, 忽略 damping

    constexpr SpringConfig(float r = 0.29f, float d = 0.75f, bool e = false)
        : response(r), damping(d), ease(e) {}

    // 常用预设 (苹果调教手感)
    static const SpringConfig Default;     // 列表光标、通用布局, 轻微回弹 (0.29s, 0.75)
    static const SpringConfig Smooth;      // 整屏转场、大视图滑动 (0.39s, 0.85)
    static const SpringConfig Snappy;      // 进度条、快速吸附 (0.31s, 0.80)
    static const SpringConfig Bouncy;      // 弹跳图标、Pop确认 (0.25s, 0.45)
    static const SpringConfig Interactive; // 物理按键跟手、无超调 (0.14s, 1.00)
};

class SpringScheduler;

/**
 * @brief 弹簧物理数值驱动器
 */
class SpringValue {
    friend class SpringScheduler;

private:
    float _current;
    float _target;
    float _velocity;
    float _precision;
    SpringConfig _config;
    SpringValue* _parent;
    bool _active;

    void wake();
    void step(float dt);

public:
    SpringValue(float initial = 0.0f, const SpringConfig& config = SpringConfig::Default);
    ~SpringValue();

    // 禁止拷贝构造和赋值拷贝, 避免调度器指针错乱
    SpringValue(const SpringValue&) = delete;
    SpringValue& operator=(const SpringValue&) = delete;

    // 获取当前显示值 (含父级变换叠加)
    float get() const;

    // 获取自身当前值 (不含父级)
    float current() const { return _current; }

    // 获取目标值
    float target() const { return _target; }

    // 获取当前速度 (单位/秒)
    float velocity() const { return _velocity; }

    // 是否已停稳静止
    bool isSettled() const { return !_active; }

    // 设置新目标并开始动画
    void set(float target);

    // 立即瞬移至数值并停止运动
    void jump(float value);

    // 相对位移: 当前值偏移 delta 但目标不变, 随后平滑弹回目标 (用于布局震荡反馈)
    void shift(float delta);

    // 施加冲量速度 (用于列表碰底振动等)
    void kick(float velocity);

    // 施加指定像素量级的回弹冲量
    void bump(float px);

    // 属性配置
    void setConfig(const SpringConfig& config) { _config = config; }
    void setParent(SpringValue* parent) { _parent = parent; }
    void setPrecision(float precision) { _precision = precision; }

    // 橡皮筋阻尼计算 (类似 iOS UIScrollView 阻尼回弹)
    static float rubberBand(float x, float dim, float coeff = 0.55f);
};

/**
 * @brief 弹簧动画全局调度器 (按需激活, 零空闲开销)
 */
class SpringScheduler {
private:
    static std::vector<SpringValue*> s_activeValues;
    static float s_timeScale;

public:
    // 推进一帧 (传入时间增量 dt 秒, 返回是否有数值正在运动)
    static bool update(float dt);

    // 检查是否有数值正在运动
    static bool isBusy();

    // 动画全局速度调节 (1.0 为正常速度, 可用于调试慢放)
    static void setTimeScale(float scale) { s_timeScale = scale; }
    static float getTimeScale() { return s_timeScale; }

    // 内部注册/注销
    static void registerActive(SpringValue* val);
    static void unregisterActive(SpringValue* val);
};

#endif // SPRING_ANIMATOR_H
