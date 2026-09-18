#include "servo_test_view.h"

ServoTestView::ServoTestView(GimbalController& gimbal)
    : _gimbal(gimbal),
      _activeChannel(0),
      _lastKey(0),
      _keyHoldStartTime(0),
      _lastKeyRepeat(0) {
}

void ServoTestView::reset() {
    _lastKey = 0;
    _keyHoldStartTime = 0;
    _lastKeyRepeat = 0;
}

void ServoTestView::draw(LGFX_Sprite* canvas) {
    if (!canvas) return;

    uint16_t width = canvas->width();
    uint16_t height = canvas->height();
    
    // 全局重置对齐基准与裁剪区域，杜绝状态污染
    canvas->setTextDatum(top_left);
    canvas->clearClipRect();
    
    // 背景深空灰蓝（卫星百科与硬件向导同款）
    canvas->fillRect(0, 0, width, height, canvas->color565(20, 30, 40));
    
    bool isZh = (I18N::getLanguage() == LANG_ZH);
    canvas->setFont(I18N::getFont());
    canvas->setTextSize(1);
    
    // 顶部标题栏（百科同款深蓝灰底色与边框）
    canvas->fillRect(0, 0, width, 20, canvas->color565(30, 40, 50));
    canvas->drawFastHLine(0, 20, width, canvas->color565(50, 65, 80));
    
    // 标题：科技青
    canvas->setTextColor(canvas->color565(0, 220, 255));
    canvas->drawString(isZh ? "浑仪舵机校准标定" : "Gimbal Servo Calibration", 6, 4);
    
    // 右侧状态与按键提示：在线电流 + [Aa/Esc]退出
    int rightX = width - 4;
    canvas->setTextDatum(top_right);
    canvas->setTextColor(canvas->color565(170, 190, 210)); // 浅灰银色
    canvas->drawString(isZh ? "[Aa/Esc]退出" : "[Aa/Esc]Exit", rightX, 4);
    rightX -= (canvas->textWidth(isZh ? "[Aa/Esc]退出" : "[Aa/Esc]Exit") + 8);
    
    if (_gimbal.isOnline()) {
        char statBuf[32];
        snprintf(statBuf, sizeof(statBuf), "ON %.0fmA", _gimbal.getCurrentmA());
        canvas->setTextColor(TFT_GREEN);
        canvas->drawString(statBuf, rightX, 4);
    } else {
        canvas->setTextColor(TFT_YELLOW);
        canvas->drawString("OFFLINE", rightX, 4);
    }
    canvas->setTextDatum(top_left);
    
    // 三个通道名称定义
    const char* chNamesZh[3] = {"CH0 走向", "CH1 倾角", "CH2 星位"};
    const char* chNamesEn[3] = {"CH0 Base", "CH1 Inc ", "CH2 Prog"};
    
    // 绘制三个通道卡片 (y = 23, 49, 75，每个高度 24)
    int cardY[3] = {23, 49, 75};
    int cardH = 24;
    
    for (int i = 0; i < 3; i++) {
        int y = cardY[i];
        bool isSelected = (_activeChannel == i);
        
        // 背景与边框
        if (isSelected) {
            canvas->fillRect(4, y, width - 8, cardH, canvas->color565(25, 55, 90));
            canvas->drawRect(4, y, width - 8, cardH, canvas->color565(0, 220, 255));
            canvas->setTextColor(canvas->color565(0, 255, 200));
            canvas->drawString(">", 8, y + 4);
            canvas->setTextColor(TFT_WHITE);
        } else {
            canvas->fillRect(4, y, width - 8, cardH, canvas->color565(26, 38, 52));
            canvas->drawRect(4, y, width - 8, cardH, canvas->color565(42, 58, 76));
            canvas->setTextColor(canvas->color565(140, 180, 210));
        }
        
        // 列 1: 通道名称 (x = 18 ~ 70)
        canvas->drawString(isZh ? chNamesZh[i] : chNamesEn[i], 18, y + 4);
        
        float curAngle = _gimbal.getChannelAngle(i);
        uint16_t curPulse = _gimbal.getChannelPulse(i);
        
        // 列 2: 角度数值 (x = 75 ~ 118)
        char angleBuf[16];
        snprintf(angleBuf, sizeof(angleBuf), "%5.1f\xC2\xB0", curAngle);
        canvas->setTextColor(isSelected ? TFT_YELLOW : canvas->color565(0, 200, 230));
        canvas->drawString(angleBuf, 75, y + 4);
        
        // 列 3: 微秒脉宽 (x = 124 ~ 168)
        char pulseBuf[16];
        snprintf(pulseBuf, sizeof(pulseBuf), "%4dus", curPulse);
        canvas->setTextColor(isSelected ? canvas->color565(220, 230, 240) : canvas->color565(140, 155, 170));
        canvas->drawString(pulseBuf, 124, y + 4);
        
        // 列 4: 进度条 (x = 172 ~ 230, 宽 58, 高 6)
        int barX = 172;
        int barY = y + 9;
        int barW = 58;
        int barH = 6;
        canvas->fillRect(barX, barY, barW, barH, canvas->color565(35, 48, 62));
        int fillW = constrain((int)((curAngle / 180.0f) * barW), 0, barW);
        if (fillW > 0) {
            canvas->fillRect(barX, barY, fillW, barH, isSelected ? canvas->color565(0, 220, 255) : canvas->color565(60, 100, 150));
        }
        // 标尺中点 90° 刻度小竖线
        canvas->drawFastVLine(barX + barW / 2, barY - 1, barH + 2, canvas->color565(180, 200, 220));
    }
    
    // 底部按键提示栏 (y = 101 ~ 135)
    canvas->drawFastHLine(0, 101, width, canvas->color565(50, 65, 80));
    canvas->fillRect(0, 102, width, 33, canvas->color565(16, 24, 34));
    
    canvas->setTextColor(canvas->color565(170, 190, 210));
    if (isZh) {
        canvas->drawString("[; .]选通道   [, /]微调-+5\xC2\xB0", 6, 104);
        canvas->drawString("预设: [Z]0\xC2\xB0  [A]45\xC2\xB0  [X]90\xC2\xB0  [S]135\xC2\xB0  [C]180\xC2\xB0", 6, 118);
    } else {
        canvas->drawString("[; .]Channel   [, /]Step -/+5\xC2\xB0", 6, 104);
        canvas->drawString("Preset: [Z]0\xC2\xB0  [A]45\xC2\xB0  [X]90\xC2\xB0  [S]135\xC2\xB0  [C]180\xC2\xB0", 6, 118);
    }
    
    canvas->setTextDatum(top_left);
    canvas->clearClipRect();
}

void ServoTestView::handleContinuousInput() {
    char currentKey = 0;
    if (M5Cardputer.Keyboard.isKeyPressed(',')) currentKey = ',';
    else if (M5Cardputer.Keyboard.isKeyPressed('/')) currentKey = '/';

    if (currentKey != 0) {
        if (_lastKey != currentKey) {
            _lastKey = currentKey;
            _keyHoldStartTime = millis();
            _lastKeyRepeat = millis();
            float cur = _gimbal.getChannelAngle(_activeChannel);
            if (currentKey == ',') cur -= 5.0f;
            else if (currentKey == '/') cur += 5.0f;
            _gimbal.setManualTestAngle(_activeChannel, cur, true);
            log_i("[ServoTest] CH%d -> %.1f deg (%d us)", _activeChannel, 
                  _gimbal.getChannelAngle(_activeChannel), 
                  _gimbal.getChannelPulse(_activeChannel));
        } else {
            unsigned long heldTime = millis() - _keyHoldStartTime;
            if (heldTime > 250) {
                if (millis() - _lastKeyRepeat >= 50) {
                    _lastKeyRepeat = millis();
                    float cur = _gimbal.getChannelAngle(_activeChannel);
                    if (currentKey == ',') cur -= 2.0f;
                    else if (currentKey == '/') cur += 2.0f;
                    _gimbal.setManualTestAngle(_activeChannel, cur, true);
                }
            }
        }
    } else {
        _lastKey = 0;
    }
}

bool ServoTestView::handleDiscreteInput(bool justShift, bool justEsc, bool justTick, bool justBack,
                                       bool justSemi, bool justDot, bool justC, bool justS) {
    if (justShift || justEsc || justTick || justBack) {
        _gimbal.exitManualTest();
        return true; // 触发退出
    } else if (M5Cardputer.Keyboard.isKeyPressed('0')) {
        _activeChannel = 0;
    } else if (M5Cardputer.Keyboard.isKeyPressed('1')) {
        _activeChannel = 1;
    } else if (M5Cardputer.Keyboard.isKeyPressed('2')) {
        _activeChannel = 2;
    } else if (justSemi) { // 轮换上一个通道
        _activeChannel = (_activeChannel - 1 + 3) % 3;
    } else if (justDot) { // 轮换下一个通道
        _activeChannel = (_activeChannel + 1) % 3;
    } else if (justC) { // 快捷置 180°
        _gimbal.setManualTestAngle(_activeChannel, 180.0f, true);
        log_i("[ServoTest] CH%d -> 180.0 deg (2500 us)", _activeChannel);
    } else if (M5Cardputer.Keyboard.isKeyPressed('x') || M5Cardputer.Keyboard.isKeyPressed('X')) { // 快捷置 90° (中点)
        _gimbal.setManualTestAngle(_activeChannel, 90.0f, true);
        log_i("[ServoTest] CH%d -> 90.0 deg (1500 us)", _activeChannel);
    } else if (M5Cardputer.Keyboard.isKeyPressed('z') || M5Cardputer.Keyboard.isKeyPressed('Z')) { // 快捷置 0°
        _gimbal.setManualTestAngle(_activeChannel, 0.0f, true);
        log_i("[ServoTest] CH%d -> 0.0 deg (500 us)", _activeChannel);
    } else if (M5Cardputer.Keyboard.isKeyPressed('a') || M5Cardputer.Keyboard.isKeyPressed('A')) { // 快捷置 45°
        _gimbal.setManualTestAngle(_activeChannel, 45.0f, true);
        log_i("[ServoTest] CH%d -> 45.0 deg (1000 us)", _activeChannel);
    } else if (justS) { // 快捷置 135°
        _gimbal.setManualTestAngle(_activeChannel, 135.0f, true);
        log_i("[ServoTest] CH%d -> 135.0 deg (2000 us)", _activeChannel);
    }
    return false;
}
