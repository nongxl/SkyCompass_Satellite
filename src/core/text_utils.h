#ifndef TEXT_UTILS_H
#define TEXT_UTILS_H

#include <Arduino.h>
#include <vector>
#include <M5GFX.h>

class TextUtils {
public:
    // 截断 UTF-8 字符串至指定字符数（中英文字符安全截断，溢出追加 ..）
    static String truncateUtf8(const String& str, size_t maxChars);

    // 将长文本折行拆分为多行文本
    static void wrapTextIntoLines(LGFX_Sprite* canvas, const String& text, int maxW, std::vector<String>& outLines);

    // 在 Sprite 上绘制自动换行文本，返回总行数
    static int drawWrappedText(LGFX_Sprite* canvas, String text, int x, int y, int maxW, int lineH, bool draw = true);

    // 在 Sprite 剪裁区域内绘制单行跑马灯滚动文本
    static void drawScrollingText(LGFX_Sprite* canvas, const char* text, int x, int y, int maxWidth, uint16_t color);
};

#endif // TEXT_UTILS_H
