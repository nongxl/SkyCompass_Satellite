#include "ui/dialog_views.h"
#include "core/i18n.h"
#include <ctype.h>
#include <string.h>

void DialogViews::drawHelpDialog(LGFX_Sprite* canvas) {
    if (!canvas) return;

    uint16_t w = 216, h = 127;
    int x = (canvas->width() - w) / 2;
    int y = (canvas->height() - h) / 2;
    
    canvas->fillRect(x, y, w, h, canvas->color565(20, 30, 40));
    canvas->drawRect(x, y, w, h, TFT_LIGHTGRAY);
    
    canvas->setTextColor(TFT_WHITE);
    canvas->setTextSize(1);
    canvas->drawString(I18N::get(TXT_HELP_TITLE), x + 35, y + 4);
    
    auto drawHotKey = [&](const char* word, char keyChar, int dx, int dy) {
        int cx = dx;
        bool highlighted = false;
        int i = 0;
        int openBracketCount = 0;
        bool inBracket = false;
        while (word[i] != '\0') {
            int charLen = 1;
            unsigned char head = (unsigned char)word[i];
            if (head >= 0xF0) charLen = 4;
            else if (head >= 0xE0) charLen = 3;
            else if (head >= 0xC0) charLen = 2;
            
            char cstr[5] = {0};
            for (int j = 0; j < charLen && word[i + j] != '\0'; j++) {
                cstr[j] = word[i + j];
            }

            if (cstr[0] == '[') inBracket = true;
            
            bool isYellow = false;
            if (keyChar == '[') {
                if (cstr[0] == '[') {
                    openBracketCount++;
                    if (openBracketCount > 1) isYellow = true;
                } else if (cstr[0] == ']') {
                    if (word[i + 1] != '\0' && strchr(word + i + 1, ']') != nullptr) {
                        isYellow = true;
                    }
                }
            } else if (keyChar == ' ') {
                if (!highlighted && (strcmp(cstr, "Spc") == 0 || strcmp(cstr, " ") == 0)) {
                    isYellow = true;
                    highlighted = true;
                }
            } else if (keyChar == '^') {
                if (inBracket && cstr[0] != '[' && cstr[0] != ']') {
                    isYellow = true;
                }
            } else if (keyChar != '\0') {
                if (charLen == 1 && !highlighted && tolower((unsigned char)cstr[0]) == tolower((unsigned char)keyChar)) {
                    isYellow = true;
                    highlighted = true;
                }
            }

            if (cstr[0] == ']') inBracket = false;
            
            if (isYellow) {
                canvas->setTextColor(TFT_YELLOW);
            } else {
                canvas->setTextColor(TFT_LIGHTGRAY);
            }
            
            canvas->drawString(cstr, cx, dy);
            cx += canvas->textWidth(cstr);
            i += charLen;
        }
    };

    int ty = y + 17;
    drawHotKey(I18N::get(TXT_HELP_BRIGHT), '[', x + 8, ty);
    drawHotKey(I18N::get(TXT_HELP_GNSS), 'g', x + 112, ty); ty += 13;
    
    drawHotKey(I18N::get(TXT_HELP_HELP), 'h', x + 8, ty);
    drawHotKey(I18N::get(TXT_HELP_HUD), '\0', x + 112, ty); ty += 13;
    
    drawHotKey(I18N::get(TXT_HELP_LOCK), ' ', x + 8, ty);
    drawHotKey(I18N::get(TXT_HELP_PASSLIST), 'e', x + 112, ty); ty += 13;
    
    drawHotKey(I18N::get(TXT_HELP_SATS), 's', x + 8, ty);
    drawHotKey(I18N::get(TXT_HELP_TIME), ',', x + 112, ty); ty += 13;
    
    drawHotKey(I18N::get(TXT_HELP_VIEW), 'v', x + 8, ty);
    drawHotKey(I18N::get(TXT_HELP_WIFI), 'w', x + 112, ty); ty += 13;
    
    drawHotKey(I18N::get(TXT_HELP_CONFIG), 'c', x + 8, ty);
    drawHotKey(I18N::get(TXT_HELP_REALTIME), 'r', x + 112, ty); ty += 13;
    
    drawHotKey(I18N::get(TXT_HELP_TAB), 't', x + 8, ty);
    drawHotKey(I18N::get(TXT_HELP_MODULE), 'm', x + 112, ty); ty += 13;
    
    drawHotKey(I18N::get(TXT_HELP_SERVO), '\0', x + 8, ty);
    drawHotKey(I18N::get(TXT_HELP_RF_CONSOLE), '^', x + 112, ty); ty += 13;
}

void DialogViews::drawLangSelectDialog(LGFX_Sprite* canvas, int selectedLangIndex) {
    if (!canvas) return;
    
    int w = 164, h = 96;
    int x = (canvas->width() - w) / 2;
    int y = (canvas->height() - h) / 2;
    
    canvas->fillRect(x, y, w, h, canvas->color565(25, 35, 45));
    canvas->drawRect(x, y, w, h, TFT_YELLOW);
    
    canvas->setTextColor(TFT_WHITE);
    canvas->setTextSize(1);
    canvas->setFont(I18N::getFont());
    canvas->drawString(I18N::get(TXT_LANGUAGE_MENU), x + (w - canvas->textWidth(I18N::get(TXT_LANGUAGE_MENU))) / 2, y + 4);
    
    const char* options[4] = {"1. English", "2. 简体中文", "3. 日本語", "4. Español"};
    const lgfx::IFont* optFonts[4] = {
        &fonts::efontCN_12,
        &fonts::efontCN_12,
        &fonts::efontJA_12,
        &fonts::efontCN_12
    };
    
    for (int i = 0; i < 4; i++) {
        canvas->setFont(optFonts[i]);
        if (selectedLangIndex == i) {
            canvas->fillRect(x + 6, y + 18 + i * 15, w - 12, 14, canvas->color565(0, 100, 200));
            canvas->setTextColor(TFT_WHITE);
        } else {
            canvas->setTextColor(TFT_LIGHTGRAY);
        }
        canvas->drawString(options[i], x + 12, y + 19 + i * 15);
    }
    canvas->setFont(&fonts::Font0);
    canvas->setTextColor(TFT_YELLOW);
    canvas->drawString("[1-4] or [;/.] Move  [Enter] OK", x + 6, y + h - 12);
    canvas->setFont(I18N::getFont());
}
