#pragma once

#include <Arduino.h>
#include <M5GFX.h>
#include "core/earth_renderer.h"

class SatFocusDrawer {
public:
    static SatFocusDrawer& getInstance();

    // 当用户在 Sat View 下按 ; 或 . 切换焦点时触发：激活显示并重置 5 秒倒计时
    void trigger();

    // 当用户按下其它按键或离开 Sat View 时调用：立即隐藏
    void hideImmediately();

    // 每帧在主循环中渲染抽屉
    void draw(LGFX_Sprite* canvas);

    // 抽屉是否处于激活或物理滑动展示中
    bool isVisible() const;

private:
    SatFocusDrawer();
    ~SatFocusDrawer() = default;
    SatFocusDrawer(const SatFocusDrawer&) = delete;
    SatFocusDrawer& operator=(const SatFocusDrawer&) = delete;

    bool _active = false;
    uint32_t _hideMs = 0;
    uint32_t _lastTriggerMs = 0;
};

