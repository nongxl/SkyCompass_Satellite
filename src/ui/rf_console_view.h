#pragma once
#include <Arduino.h>
#include <M5GFX.h>

enum RfConsoleMode {
    RF_VIEW_WATERFALL = 0, // 模式 1：过境追踪仪表 + 实时信号瀑布能量图
    RF_VIEW_PACKETS   = 1  // 模式 2：收包历史列表 + HEX 物理量详情
};

class RfConsoleView {
public:
    static RfConsoleView& getInstance() {
        static RfConsoleView instance;
        return instance;
    }

    // 绘制全屏 RF Console 视图
    void draw(LGFX_Sprite* canvas, int width, int height);

    // 键盘交互处理 (支持 Tab 切换视图，T 键测试注入，, / 键微调时间轴校准，0/R 键复位，d/y/n 键删除数据)
    void handleKeys(bool justSemi, bool justDot, bool justEnter, bool justD, bool justEsc, 
                    bool justT, bool justComma, bool justSlash, bool justZero, bool justY, bool justN,
                    bool justTab, int32_t& timeOffset);

    // 查询当前控制台是否处于激活显示状态
    bool isActive() const { return _isActive; }
    void setActive(bool active) { _isActive = active; }
    void toggle() { _isActive = !_isActive; }

    // 瀑布能量图采样管理
    void addRssiSample(float rssi);

private:
    RfConsoleView();
    bool _isActive = false;
    RfConsoleMode _currentMode = RF_VIEW_WATERFALL;
    int _selectedPacketIndex = 0;
    bool _showDetailModal = false;
    bool _showDeleteModal = false;
    int _scrollOffset = 0;

    // 瀑布能量图历史采样点 (宽 224px，每 2px 一个采样点，共 112 个采样)
    static const int WATERFALL_POINTS = 112;
    float _rssiHistory[WATERFALL_POINTS];
    int _historyHead = 0;
    uint32_t _lastSampleTime = 0;
};
