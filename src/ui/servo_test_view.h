#ifndef SERVO_TEST_VIEW_H
#define SERVO_TEST_VIEW_H

#include <Arduino.h>
#include <M5Cardputer.h>
#include <M5GFX.h>
#include "gimbal/gimbal_controller.h"
#include "core/i18n.h"

class ServoTestView {
public:
    explicit ServoTestView(GimbalController& gimbal);

    // 重置状态
    void reset();

    // 绘制舵机测试页面到指定的 Canvas 上
    void draw(LGFX_Sprite* canvas);

    // 持续按键处理（长按连续微调：, 和 /）
    void handleContinuousInput();

    // 离散按键处理（通道切换与角度快捷设置）
    // 返回 true 表示用户退出舵机测试页面
    bool handleDiscreteInput(bool justShift, bool justEsc, bool justTick, bool justBack,
                             bool justSemi, bool justDot, bool justC, bool justS);

    int getActiveChannel() const { return _activeChannel; }
    void setActiveChannel(int ch) { if (ch >= 0 && ch < 3) _activeChannel = ch; }

private:
    GimbalController& _gimbal;
    int _activeChannel; // 0: CH0(基座长梁), 1: CH1(拱门倾角), 2: CH2(星位滑块)

    // 连续按键状态记录
    char _lastKey;
    unsigned long _keyHoldStartTime;
    unsigned long _lastKeyRepeat;
};

#endif // SERVO_TEST_VIEW_H
