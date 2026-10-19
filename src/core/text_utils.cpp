#include "text_utils.h"

String TextUtils::truncateUtf8(const String& str, size_t maxChars) {
    size_t charCount = 0;
    size_t byteIdx = 0;
    size_t len = str.length();
    
    while (byteIdx < len && charCount < maxChars) {
        unsigned char c = (unsigned char)str[byteIdx];
        size_t charBytes = 1;
        if ((c & 0x80) == 0) charBytes = 1;
        else if ((c & 0xE0) == 0xC0) charBytes = 2;
        else if ((c & 0xF0) == 0xE0) charBytes = 3;
        else if ((c & 0xF8) == 0xF0) charBytes = 4;
        
        if (byteIdx + charBytes > len) break;
        byteIdx += charBytes;
        charCount++;
    }
    
    if (byteIdx < len) {
        return str.substring(0, byteIdx) + "..";
    }
    return str;
}

void TextUtils::wrapTextIntoLines(LGFX_Sprite* canvas, const String& text, int maxW, std::vector<String>& outLines) {
    if (!canvas || text.length() == 0) return;
    
    int start = 0;
    while (start < text.length()) {
        int len = 0;
        bool foundNewline = false;
        
        while (start + len < text.length()) {
            if (text[start + len] == '\n') {
                foundNewline = true;
                break;
            }
            
            int charLen = 1;
            unsigned char head = (unsigned char)text[start + len];
            if (head >= 0xF0) charLen = 4;
            else if (head >= 0xE0) charLen = 3;
            else if (head >= 0xC0) charLen = 2;
            
            if (start + len + charLen > text.length()) {
                charLen = text.length() - (start + len);
            }
            
            String sub = text.substring(start, start + len + charLen);
            int subW = canvas->textWidth(sub.c_str());
            if (subW > maxW) {
                break;
            }
            len += charLen;
        }
        
        if (len == 0 && !foundNewline) {
            int charLen = 1;
            unsigned char head = (unsigned char)text[start];
            if (head >= 0xF0) charLen = 4;
            else if (head >= 0xE0) charLen = 3;
            else if (head >= 0xC0) charLen = 2;
            if (start + charLen > text.length()) charLen = text.length() - start;
            len = charLen;
        }
        
        int end = start + len;
        if (end < text.length() && !foundNewline) {
            auto isAlphaNum = [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
            };
            if (isAlphaNum(text[end]) && isAlphaNum(text[end - 1])) {
                int lastSpace = text.substring(start, end).lastIndexOf(' ');
                if (lastSpace > start && lastSpace > start + len / 2) {
                    end = lastSpace;
                    len = end - start;
                }
            }
        }
        
        outLines.push_back(text.substring(start, end));
        
        start = end;
        if (foundNewline) {
            start++; // skip '\n'
        } else {
            if (start < text.length() && text[start] == ' ') {
                start++;
            }
        }
    }
}

int TextUtils::drawWrappedText(LGFX_Sprite* canvas, String text, int x, int y, int maxW, int lineH, bool draw) {
    if (!canvas) return 0;
    int lines = 0;
    int start = 0;
    
    while (start < text.length()) {
        lines++;
        int len = 0;
        bool foundNewline = false;
        
        while (start + len < text.length()) {
            if (text[start + len] == '\n') {
                foundNewline = true;
                break;
            }
            
            int charLen = 1;
            unsigned char head = (unsigned char)text[start + len];
            if (head >= 0xF0) charLen = 4;
            else if (head >= 0xE0) charLen = 3;
            else if (head >= 0xC0) charLen = 2;
            
            if (start + len + charLen > text.length()) {
                charLen = text.length() - (start + len);
            }
            
            String sub = text.substring(start, start + len + charLen);
            int subW = canvas->textWidth(sub.c_str());
            if (subW > maxW) {
                break;
            }
            len += charLen;
        }
        
        if (len == 0 && !foundNewline) {
            int charLen = 1;
            unsigned char head = (unsigned char)text[start];
            if (head >= 0xF0) charLen = 4;
            else if (head >= 0xE0) charLen = 3;
            else if (head >= 0xC0) charLen = 2;
            if (start + charLen > text.length()) charLen = text.length() - start;
            len = charLen;
        }
        
        int end = start + len;
        if (end < text.length() && !foundNewline) {
            auto isAlphaNum = [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
            };
            if (isAlphaNum(text[end]) && isAlphaNum(text[end - 1])) {
                int lastSpace = text.substring(start, end).lastIndexOf(' ');
                if (lastSpace > start && lastSpace > start + len / 2) {
                    end = lastSpace;
                    len = end - start;
                }
            }
        }
        
        if (draw) {
            canvas->drawString(text.substring(start, end).c_str(), x, y);
        }
        
        start = end;
        if (foundNewline) {
            start++; // skip '\n'
        } else {
            if (start < text.length() && text[start] == ' ') {
                start++;
            }
        }
        y += lineH;
    }
    return lines;
}

void TextUtils::drawScrollingText(LGFX_Sprite* canvas, const char* text, int x, int y, int maxWidth, uint16_t color) {
    if (!canvas || !text) return;
    canvas->setTextColor(color);
    int textWidth = canvas->textWidth(text);
    if (textWidth <= maxWidth) {
        canvas->drawString(text, x, y);
        return;
    }
    
    // 选中文本变化检测：切换条目时立即复位动画，确保总是从原位（居左）开始
    static String s_lastText = "";
    static unsigned long s_lastChangeMs = 0;
    static int s_lastY = -1;
    unsigned long now = millis();
    if (s_lastY != y || s_lastText != text) {
        s_lastText = text;
        s_lastY = y;
        s_lastChangeMs = now;
    }
    
    int maxScroll = textWidth - maxWidth;
    int speed = 24; // 像素/秒，舒适阅读速度
    int scrollDurationMs = (maxScroll * 1000) / speed;
    int holdStartMs = 1200;     // 起始停留时间 (在原本左对齐位置静止，让用户先看清开头)
    int holdEndMs = 1200;       // 末尾停留时间 (读完末尾)
    int returnDurationMs = 500; // 平滑返回起点的动画时长
    
    int totalCycleMs = holdStartMs + scrollDurationMs + holdEndMs + returnDurationMs;
    unsigned long elapsed = now - s_lastChangeMs;
    int phase = elapsed % totalCycleMs;
    
    float offset = 0.0f;
    if (phase < holdStartMs) {
        // 阶段 1: 起始停留，完全在原位居左对齐显示
        offset = 0.0f;
    } else if (phase < holdStartMs + scrollDurationMs) {
        // 阶段 2: 从原位平滑向左滑动，直至末尾完整露出
        offset = (float)(phase - holdStartMs) * ((float)maxScroll / (float)scrollDurationMs);
    } else if (phase < holdStartMs + scrollDurationMs + holdEndMs) {
        // 阶段 3: 停留在末尾
        offset = (float)maxScroll;
    } else {
        // 阶段 4: 平滑滑回起点
        float t = (float)(phase - (holdStartMs + scrollDurationMs + holdEndMs)) / (float)returnDurationMs;
        float ease = 0.5f * (1.0f + cosf(t * 3.14159265f));
        offset = (float)maxScroll * ease;
    }
    
    int fh = canvas->fontHeight();
    canvas->setClipRect(x, y, maxWidth, fh);
    canvas->drawString(text, x - (int)roundf(offset), y);
    canvas->clearClipRect();
}
