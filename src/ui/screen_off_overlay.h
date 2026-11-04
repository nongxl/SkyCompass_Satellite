#pragma once
#include <Arduino.h>
#include <M5Cardputer.h>
#include "core/i18n.h"

class ScreenOffOverlay {
public:
    static void draw(LGFX_Sprite* canvas, int remainingSeconds, float progress01) {
        if (!canvas) return;
        
        int w = 156;
        int h = 76;
        int x = (canvas->width() - w) / 2;
        int y = (canvas->height() - h) / 2;
        
        // 绘制阴影与深蓝黑背景框
        canvas->fillRoundRect(x + 2, y + 2, w, h, 8, 0x0000);
        canvas->fillRoundRect(x, y, w, h, 8, canvas->color565(16, 24, 38));
        canvas->drawRoundRect(x, y, w, h, 8, canvas->color565(52, 152, 219));
        canvas->drawRoundRect(x + 1, y + 1, w - 2, h - 2, 7, canvas->color565(26, 82, 118));
        
        // 顶部标题
        canvas->setTextDatum(top_center);
        canvas->setFont(I18N::getFont());
        canvas->setTextColor(canvas->color565(200, 225, 255));
        canvas->drawString((I18N::getLanguage() == LANG_ZH) ? "准备息屏待机" : "Screen Sleep", x + w / 2, y + 6);
        
        // 中间倒计时大号数字（3、2、1）
        char numStr[4];
        snprintf(numStr, sizeof(numStr), "%d", remainingSeconds);
        canvas->setTextDatum(middle_center);
        canvas->setFont(&fonts::Font4);
        canvas->setTextColor(canvas->color565(255, 204, 0));
        canvas->drawString(numStr, x + w / 2, y + 36);
        
        // 底部提示
        canvas->setTextDatum(bottom_center);
        canvas->setFont(I18N::getFont());
        canvas->setTextColor(canvas->color565(160, 175, 195));
        canvas->drawString((I18N::getLanguage() == LANG_ZH) ? "按任意键取消" : "Press any key to cancel", x + w / 2, y + h - 5);
        
        // 恢复默认字体对齐与颜色
        canvas->setTextDatum(top_left);
    }
};

