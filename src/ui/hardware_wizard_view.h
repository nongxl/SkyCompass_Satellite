#ifndef HARDWARE_WIZARD_VIEW_H
#define HARDWARE_WIZARD_VIEW_H

#include <Arduino.h>
#include <M5Cardputer.h>
#include <M5GFX.h>
#include "core/hardware_config.h"
#include "core/i18n.h"

class HardwareWizardView {
public:
    HardwareWizardView();

    // 绘制向导完整界面到指定的 Sprite/Canvas 上
    void draw(LGFX_Sprite* canvas);

    // 键盘事件处理，返回 true 表示用户退出了向导（需切回主界面）
    bool handleKey(const Keyboard_Class::KeysState& keys, char keyChar);

    // 重置向导状态（打开界面时调用）
    void reset();

    // 检查是否正在执行保存重启倒计时
    bool isRebooting() const { return _isRebooting; }

private:
    void drawBitmapScaled(LGFX_Sprite* canvas, int dstX, int dstY, int dstW, int dstH, const uint16_t* srcData, int srcW = 120, int srcH = 120);
    static void wrapText(LGFX_Sprite* canvas, const String& text, int maxW, std::vector<String>& outLines);

    int _cursorIndex;            // 0~3: 4个模块项
    uint8_t _previewIndex;       // 当前左侧显示的模块索引
    uint8_t _lastPreviewIndex;   // 用于检测预览模块切换并重置介绍滚屏
    unsigned long _lastRotateTick;
    bool _isRebooting;
    unsigned long _rebootStartTime;
    String _conflictMsg;
    bool _hasConflict;
    bool _showExitConfirm;       // 是否显示按 Esc 后的退出确认弹窗
    unsigned long _confirmOpenTime; // 记录弹窗打开时间用于防抖

    // 自动滚屏计时控制
    unsigned long _descScrollStartTime;   // 模块介绍滚屏起始时刻
    unsigned long _guideScrollStartTime;  // 接线方案滚屏起始时刻
    uint8_t _lastConfigMask;              // 记录勾选掩码，变化时重置接线方案滚屏

    // 中括号手动翻页控制 (和卫星百科界面保持一致)
    bool _guideManualScrolled;
    int _guideManualYOffset;
    int _guideMaxScroll;

    bool _descManualScrolled;
    int _descManualYOffset;
    int _descMaxScroll;
};

#endif // HARDWARE_WIZARD_VIEW_H
