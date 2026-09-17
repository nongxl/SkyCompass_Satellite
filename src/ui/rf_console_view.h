#pragma once
#include <Arduino.h>
#include <M5GFX.h>

class RfConsoleView {
public:
    static RfConsoleView& getInstance() {
        static RfConsoleView instance;
        return instance;
    }

    // 绘制全屏 RF Console 视图
    void draw(LGFX_Sprite* canvas, int width, int height);

    // 键盘交互处理
    void handleKeys(bool justSemi, bool justDot, bool justEnter, bool justD, bool justEsc);

    // 查询当前控制台是否处于激活显示状态
    bool isActive() const { return _isActive; }
    void setActive(bool active) { _isActive = active; }
    void toggle() { _isActive = !_isActive; }

private:
    RfConsoleView();
    bool _isActive = false;
    int _selectedPacketIndex = 0;
    bool _showDetailModal = false;
    int _scrollOffset = 0;
};
