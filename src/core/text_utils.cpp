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
    if (!canvas) return;
    canvas->setTextColor(color);
    int textWidth = canvas->textWidth(text);
    if (textWidth <= maxWidth) {
        canvas->drawString(text, x, y);
        return;
    }
    
    int gap = 30;
    int cycleWidth = textWidth + gap;
    int speed = 25; // pixels per second
    int offset = (int)(millis() * speed / 1000) % cycleWidth;
    
    int fh = canvas->fontHeight();
    
    canvas->setClipRect(x, y, maxWidth, fh);
    canvas->drawString(text, x - offset, y);
    canvas->drawString(text, x - offset + cycleWidth, y);
    canvas->clearClipRect();
}
