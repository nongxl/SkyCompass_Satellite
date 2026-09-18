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

    // 键盘交互处理 (支持 T 键测试注入，, / 键微调时间轴校准，0/R 键复位，d/y/n 键删除数据)
    void handleKeys(bool justSemi, bool justDot, bool justEnter, bool justD, bool justEsc, 
                    bool justT, bool justComma, bool justSlash, bool justZero, bool justY, bool justN,
                    int32_t& timeOffset);

    // 查询当前控制台是否处于激活显示状态
    bool isActive() const { return _isActive; }
    void setActive(bool active) { _isActive = active; }
    void toggle() { _isActive = !_isActive; }

private:
    RfConsoleView();
    bool _isActive = false;
    int _selectedPacketIndex = 0;
    bool _showDetailModal = false;
    bool _showDeleteModal = false;
    int _scrollOffset = 0;
};
