#include "hardware_wizard_view.h"

HardwareWizardView::HardwareWizardView() :
    _cursorIndex(0),
    _previewIndex(0),
    _lastPreviewIndex(255),
    _lastRotateTick(0),
    _isRebooting(false),
    _rebootStartTime(0),
    _hasConflict(false),
    _showExitConfirm(false),
    _confirmOpenTime(0),
    _descScrollStartTime(0),
    _guideScrollStartTime(0),
    _lastConfigMask(255),
    _guideManualScrolled(false),
    _guideManualYOffset(0),
    _guideMaxScroll(0),
    _descManualScrolled(false),
    _descManualYOffset(0),
    _descMaxScroll(0) {
}

void HardwareWizardView::reset() {
    _cursorIndex = 0;
    _previewIndex = 0;
    _lastPreviewIndex = 255;
    _lastRotateTick = millis();
    _isRebooting = false;
    _rebootStartTime = 0;
    _showExitConfirm = false;
    _confirmOpenTime = 0;
    _descScrollStartTime = millis();
    _guideScrollStartTime = millis();
    _lastConfigMask = 255;
    _guideManualScrolled = false;
    _guideManualYOffset = 0;
    _guideMaxScroll = 0;
    _descManualScrolled = false;
    _descManualYOffset = 0;
    _descMaxScroll = 0;
    
    // 载入当前已保存的硬件配置
    HardwareConfig::getInstance().load();
}

void HardwareWizardView::drawBitmapScaled(LGFX_Sprite* canvas, int dstX, int dstY, int dstW, int dstH, const uint16_t* srcData, int srcW, int srcH) {
    if (!srcData) return;
    for (int dy = 0; dy < dstH; dy++) {
        int sy = (dy * srcH) / dstH;
        const uint16_t* row = srcData + sy * srcW;
        for (int dx = 0; dx < dstW; dx++) {
            int sx = (dx * srcW) / dstW;
            canvas->drawPixel(dstX + dx, dstY + dy, row[sx]);
        }
    }
}

void HardwareWizardView::wrapText(LGFX_Sprite* canvas, const String& text, int maxW, std::vector<String>& outLines) {
    if (text.length() == 0) return;
    int start = 0;
    while (start < (int)text.length()) {
        int len = 0;
        bool foundNewline = false;

        while (start + len < (int)text.length()) {
            if (text[start + len] == '\n') {
                foundNewline = true;
                break;
            }

            int charLen = 1;
            unsigned char head = (unsigned char)text[start + len];
            if (head >= 0xF0) charLen = 4;
            else if (head >= 0xE0) charLen = 3;
            else if (head >= 0xC0) charLen = 2;

            if (start + len + charLen > (int)text.length()) {
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
            if (start + charLen > (int)text.length()) charLen = text.length() - start;
            len = charLen;
        }

        outLines.push_back(text.substring(start, start + len));

        if (foundNewline) {
            start += len + 1; // 跳过 '\n'
        } else {
            start += len;
        }
    }
}

void HardwareWizardView::draw(LGFX_Sprite* canvas) {
    Language lang = I18N::getLanguage();
    bool isZh = (lang == LANG_ZH);

    // ==========================================
    // 卫星百科界面同款深空优雅配色系统 (告别纯黑死寂)
    // ==========================================
    const uint16_t bgColor       = canvas->color565(20, 30, 40);  // 百科主背景深灰蓝
    const uint16_t headerBg      = canvas->color565(30, 40, 50);  // 顶部状态条背景
    const uint16_t borderColor   = canvas->color565(50, 65, 80);  // 分割线与边框蓝灰色
    const uint16_t activeItemBg  = canvas->color565(0, 100, 220); // 列表高亮选中项纯正蓝
    const uint16_t hintTextColor = canvas->color565(140, 180, 210);// 提示文字淡青灰

    // 1. 处理重启倒计时卡片
    if (_isRebooting) {
        canvas->fillRect(0, 0, 240, 135, bgColor);
        canvas->fillRoundRect(15, 25, 210, 85, 6, canvas->color565(25, 40, 55));
        canvas->drawRoundRect(15, 25, 210, 85, 6, TFT_GREEN);

        canvas->setFont(I18N::getFont());
        canvas->setTextDatum(top_center);
        canvas->setTextColor(TFT_GREEN, canvas->color565(25, 40, 55));
        canvas->drawString(isZh ? "配置已保存至 NVS!" : "Settings Saved to NVS!", 120, 36);

        canvas->setTextColor(TFT_WHITE, canvas->color565(25, 40, 55));
        canvas->drawString(isZh ? "请按照接线指引连接硬件" : "Connect modules per guide", 120, 56);

        canvas->setTextColor(TFT_YELLOW, canvas->color565(25, 40, 55));
        canvas->drawString(isZh ? "系统即将重启生效..." : "System rebooting...", 120, 76);

        // 彻底复位文字对齐与裁剪，防污染
        canvas->setTextDatum(top_left);
        canvas->clearClipRect();

        if (millis() - _rebootStartTime >= 1200) {
            ESP.restart();
        }
        return;
    }

    // 校验冲突
    _hasConflict = !HardwareConfig::getInstance().validate(_conflictMsg, lang);

    // 锁定左侧预览模块与光标一致
    if (_cursorIndex >= 0 && _cursorIndex < HW_MOD_COUNT) {
        _previewIndex = _cursorIndex;
    }

    // 检测预览模块切换，重置左栏介绍滚屏起始时刻
    if (_previewIndex != _lastPreviewIndex) {
        _lastPreviewIndex = _previewIndex;
        _descScrollStartTime = millis();
        _descManualScrolled = false;
        _descManualYOffset = 0;
    }

    // 获取当前方案名称与接线指引
    std::vector<String> rawGuides;
    HardwareConfig::getInstance().getWiringGuide(rawGuides, lang);

    // 2. 全局背景与顶部 Header（方案名称置顶展示）
    canvas->fillRect(0, 0, 240, 135, bgColor);
    canvas->fillRect(0, 0, 240, 14, headerBg);
    canvas->drawFastHLine(0, 14, 240, borderColor);

    canvas->setFont(I18N::getFont());
    canvas->setTextDatum(top_left);

    // 动态方案名称直接显示在顶部标题位置
    String planTitle = "";
    if (_hasConflict) {
        planTitle = isZh ? "【冲突】外设接口引脚互斥" : "[Conflict] Hardware Pins";
        canvas->setTextColor(TFT_RED, headerBg);
    } else {
        if (!rawGuides.empty()) {
            planTitle = rawGuides[0];
        } else {
            planTitle = isZh ? "【方案】纯单机模拟演示" : "[Plan] Standalone Simulator";
        }
        canvas->setTextColor(TFT_YELLOW, headerBg);
    }

    // 顶部右侧按键提示：统一为“按键在前，说明在后”格式：[[/] ]翻页
    const char* pageHint = isZh ? "[[/] ]翻页" : "[[/] ]Page";
    int pageHintW = canvas->textWidth(pageHint);
    const int rightHintX = 236;

    // 方案名称区域宽度与自动横向平滑跑马灯滚动
    const int headerTitleX = 4;
    const int headerTitleMaxW = rightHintX - pageHintW - 6 - headerTitleX;

    int titleW = canvas->textWidth(planTitle.c_str());
    if (titleW <= headerTitleMaxW) {
        canvas->drawString(planTitle, headerTitleX, 1);
    } else {
        int gap = 24;
        int cycleW = titleW + gap;
        int speedPxPerSec = 22; // 22 像素/秒，平缓舒适易读
        int offset = (int)((millis() * speedPxPerSec) / 1000) % cycleW;

        canvas->setClipRect(headerTitleX, 0, headerTitleMaxW, 14);
        canvas->drawString(planTitle, headerTitleX - offset, 1);
        canvas->drawString(planTitle, headerTitleX - offset + cycleW, 1);
        canvas->clearClipRect();
    }

    // 绘制顶部右侧按键提示
    canvas->setTextDatum(top_right);
    canvas->setTextColor(hintTextColor, headerBg);
    canvas->drawString(pageHint, rightHintX, 1);

    // 左右两栏中间垂直分割线
    canvas->drawFastVLine(93, 14, 121, borderColor);

    // ==========================================
    // 3. 左栏：特大号模块位图 (92x92) 铺满左右不留黑区 + 左上角灰色[Esc]提示 + 下方详细介绍
    // ==========================================
    const int imgBoxX = 1;
    const int imgBoxY = 16;
    const int imgBoxW = 92;
    const int imgBoxH = 92;

    // 紧贴边框
    canvas->drawRect(0, 15, 93, 94, borderColor);
    const uint16_t* previewImg = HardwareConfig::getInstance().getModuleImage((HardwareModule)_previewIndex);
    if (previewImg) {
        drawBitmapScaled(canvas, imgBoxX, imgBoxY, imgBoxW, imgBoxH, previewImg, HW_IMG_WIDTH, HW_IMG_HEIGHT);
    } else {
        canvas->fillRect(imgBoxX, imgBoxY, imgBoxW, imgBoxH, canvas->color565(15, 22, 30));
    }

    // 图片区域左上角：使用灰色字体显示 [Esc]退出保存 按键提示（无背景色，透明叠加）
    const char* escHint = isZh ? "[Esc]退出保存" : "[Esc]Exit";
    canvas->setTextDatum(top_left);
    canvas->setTextColor(canvas->color565(160, 180, 195)); // 优雅浅灰银色
    canvas->drawString(escHint, 3, 17);

    // 图片下方显示功能介绍视口：X=2, Y=110, W=89, H=24 (显示两行，超出支持滚动)
    const int descVx = 2;
    const int descVy = 110;
    const int descVw = 89;
    const int descVh = 24;
    const int lineH = 12;

    String descStr = HardwareConfig::getInstance().getModuleDescription((HardwareModule)_previewIndex, lang);
    std::vector<String> descLines;
    wrapText(canvas, descStr, descVw, descLines);

    int totalDescLines = (int)descLines.size();
    _descMaxScroll = (totalDescLines * lineH > descVh) ? (totalDescLines * lineH - descVh + 2) : 0;
    int descYOffset = 0;

    if (_descMaxScroll > 0) {
        if (_descManualScrolled) {
            descYOffset = _descManualYOffset;
        } else {
            int scrollRange = _descMaxScroll;
            int holdTop = 1800;
            int holdBottom = 2000;
            int speedMs = 55;
            int cycle = holdTop + scrollRange * speedMs + holdBottom;
            int t = (millis() - _descScrollStartTime) % cycle;
            if (t < holdTop) {
                descYOffset = 0;
            } else if (t < holdTop + scrollRange * speedMs) {
                descYOffset = (t - holdTop) / speedMs;
            } else {
                descYOffset = scrollRange;
            }
        }
    }

    canvas->setClipRect(descVx, descVy, descVw, descVh);
    canvas->setTextDatum(top_left);
    canvas->setTextColor(canvas->color565(190, 215, 235), bgColor); // 柔和浅青白字
    for (int i = 0; i < totalDescLines; i++) {
        int curLineY = descVy + i * lineH - descYOffset;
        if (curLineY >= descVy - lineH && curLineY <= descVy + descVh) {
            canvas->drawString(descLines[i], descVx, curLineY);
        }
    }
    canvas->clearClipRect();

    // ==========================================
    // 4. 右栏：模块清单（带硬件总线胶囊标签）+ 接线方案视口
    // ==========================================
    const int listX = 95;
    const int listW = 143;
    int curY = 17;

    const char* busBadges[HW_MOD_COUNT] = { "Cap-14P", "Grove", "UART", "I2C" };
    uint16_t busBgColors[HW_MOD_COUNT]  = { 0x19E8,    0x4200,  0x0280, 0x28B0 };
    uint16_t busTxtColors[HW_MOD_COUNT] = { TFT_CYAN,  TFT_YELLOW, TFT_GREEN, 0xCE79 };

    for (int i = 0; i < HW_MOD_COUNT; i++) {
        HardwareModule mod = (HardwareModule)i;
        bool isHover = (_cursorIndex == i);
        bool isChecked = HardwareConfig::getInstance().isEnabled(mod);

        // 高亮选中条背景与边框
        if (isHover) {
            canvas->fillRect(listX, curY, listW, 12, activeItemBg);
            canvas->drawRect(listX, curY, listW, 12, TFT_CYAN);
        }

        // 勾选框
        canvas->setTextDatum(top_left);
        canvas->setTextColor(isChecked ? TFT_GREEN : canvas->color565(120, 140, 160), isHover ? activeItemBg : bgColor);
        canvas->drawString(isChecked ? "[x]" : "[ ]", listX + 2, curY + 1);

        // 模块名称（使用紧凑无空格名称如 CapLoRa-1262，不溢出不截断）
        canvas->setTextColor(isChecked ? TFT_WHITE : TFT_LIGHTGRAY, isHover ? activeItemBg : bgColor);
        canvas->drawString(HardwareConfig::getInstance().getModuleName(mod, lang), listX + 20, curY + 1);

        // 右侧硬件总线胶囊徽章（Badge）
        const char* badgeText = busBadges[i];
        int badgeW = canvas->textWidth(badgeText) + 6;
        int badgeX = listX + listW - badgeW - 2;
        int badgeY = curY + 1;
        canvas->fillRoundRect(badgeX, badgeY, badgeW, 10, 2, busBgColors[i]);
        canvas->setTextColor(busTxtColors[i]);
        canvas->drawString(badgeText, badgeX + 3, badgeY);

        curY += 14;
    }

    // 水平分割线
    int guideDividerY = 74;
    canvas->drawFastHLine(listX, guideDividerY, listW, borderColor);

    // 接线方案与冲突展示视口
    const int guideVx = 95;
    const int guideVy = 76;
    const int guideVw = 143;
    const int guideVh = 58;

    // 检查配置是否改变，若改变重置接线滚屏计时器与手动翻页状态
    uint8_t curMask = 0;
    for (int i = 0; i < HW_MOD_COUNT; i++) {
        if (HardwareConfig::getInstance().isEnabled((HardwareModule)i)) curMask |= (1 << i);
    }
    if (curMask != _lastConfigMask) {
        _lastConfigMask = curMask;
        _guideScrollStartTime = millis();
        _guideManualScrolled = false;
        _guideManualYOffset = 0;
    }

    struct GuideLineItem {
        String text;
        uint16_t color;
        bool isIndent;
    };
    std::vector<GuideLineItem> displayGuideLines;

    if (_hasConflict) {
        std::vector<String> errWrapped;
        wrapText(canvas, _conflictMsg, guideVw - 4, errWrapped);
        for (size_t l = 0; l < errWrapped.size(); l++) {
            displayGuideLines.push_back({errWrapped[l], TFT_RED, false});
        }
    } else {
        // rawGuides[0] 方案名称已直接在顶部标题栏醒目显示，下方接线视口直接从 g = 1 开始显示纯粹接线指示
        for (size_t g = 1; g < rawGuides.size(); g++) {
            std::vector<String> subWrapped;
            wrapText(canvas, rawGuides[g], guideVw - 6, subWrapped);
            for (size_t s = 0; s < subWrapped.size(); s++) {
                uint16_t color = 0xCE79; // 接线指示全部统一为清爽青白绿
                bool isIndent = (s > 0);
                displayGuideLines.push_back({subWrapped[s], color, isIndent});
            }
        }
    }

    int totalGuideLines = (int)displayGuideLines.size();
    _guideMaxScroll = (totalGuideLines * lineH > guideVh) ? (totalGuideLines * lineH - guideVh + 2) : 0;
    int guideYOffset = 0;

    if (_guideMaxScroll > 0) {
        if (_guideManualScrolled) {
            guideYOffset = _guideManualYOffset;
        } else {
            int scrollRange = _guideMaxScroll;
            int holdTop = 2200;
            int holdBottom = 2500;
            int speedMs = 50;
            int cycle = holdTop + scrollRange * speedMs + holdBottom;
            int t = (millis() - _guideScrollStartTime) % cycle;
            if (t < holdTop) {
                guideYOffset = 0;
            } else if (t < holdTop + scrollRange * speedMs) {
                guideYOffset = (t - holdTop) / speedMs;
            } else {
                guideYOffset = scrollRange;
            }
        }
    }

    // 视口区域裁剪渲染
    canvas->setClipRect(guideVx, guideVy, guideVw, guideVh);
    canvas->setTextDatum(top_left);
    for (int i = 0; i < totalGuideLines; i++) {
        int curLineY = guideVy + i * lineH - guideYOffset;
        if (curLineY >= guideVy - lineH && curLineY <= guideVy + guideVh) {
            canvas->setTextColor(displayGuideLines[i].color, bgColor);
            int drawX = guideVx + (displayGuideLines[i].isIndent ? 8 : 1);
            canvas->drawString(displayGuideLines[i].text, drawX, curLineY);
        }
    }
    canvas->clearClipRect();

    // 若接线行数较多，在最右侧画微型滚动条
    if (_guideMaxScroll > 0) {
        int trackH = guideVh - 4;
        int thumbH = (guideVh * trackH) / (totalGuideLines * lineH);
        if (thumbH < 6) thumbH = 6;
        int thumbY = guideVy + 2 + (guideYOffset * (trackH - thumbH)) / _guideMaxScroll;
        canvas->drawFastVLine(237, guideVy + 2, trackH, borderColor);
        canvas->fillRect(236, thumbY, 2, thumbH, TFT_CYAN);
    }

    // ==========================================
    // 5. 按 Esc 弹出的退出与保存确认弹窗 (Modal Dialog)
    // ==========================================
    if (_showExitConfirm) {
        const int cardX = 16;
        const int cardY = 24;
        const int cardW = 208;
        const int cardH = 88;
        const uint16_t cardBg = canvas->color565(25, 38, 52);

        canvas->fillRoundRect(cardX, cardY, cardW, cardH, 6, cardBg);
        canvas->drawRoundRect(cardX, cardY, cardW, cardH, 6, TFT_CYAN);
        canvas->drawRoundRect(cardX + 1, cardY + 1, cardW - 2, cardH - 2, 5, canvas->color565(50, 75, 100));

        canvas->setTextDatum(top_center);
        canvas->setTextColor(TFT_YELLOW, cardBg);
        canvas->drawString(isZh ? "【退出硬件向导】" : "[Exit Hardware Setup]", 120, cardY + 8);

        if (_hasConflict) {
            canvas->setTextColor(TFT_RED, cardBg);
            canvas->drawString(isZh ? "当前勾选存在冲突，无法生效!" : "Conflicts detected, cannot save!", 120, cardY + 30);

            canvas->setTextColor(TFT_LIGHTGRAY, cardBg);
            canvas->drawString(isZh ? "[Esc] 不保存直接退出" : "[Esc] Discard & Exit", 120, cardY + 50);

            canvas->setTextColor(hintTextColor, cardBg);
            canvas->drawString(isZh ? "[任意键] 返回修改配置" : "[Any Key] Back to edit", 120, cardY + 68);
        } else {
            canvas->setTextColor(TFT_WHITE, cardBg);
            canvas->drawString(isZh ? "是否保存配置并重启系统生效？" : "Save changes & reboot system?", 120, cardY + 30);

            canvas->setTextColor(TFT_GREEN, cardBg);
            canvas->drawString(isZh ? "[Enter] 保存并重启生效" : "[Enter] Save & Reboot", 120, cardY + 50);

            canvas->setTextColor(0xCE79, cardBg);
            canvas->drawString(isZh ? "[Esc] 不保存退出  [任意键] 继续配置" : "[Esc] Discard  [Any] Continue", 120, cardY + 68);
        }
    }

    // ==========================================
    // 6. 退出绘制时无条件复位全局状态，彻底防止污染主程序排版
    // ==========================================
    canvas->setTextDatum(top_left);
    canvas->clearClipRect();
}

bool HardwareWizardView::handleKey(const Keyboard_Class::KeysState& keys, char keyChar) {
    if (_isRebooting) return false;

    Language lang = I18N::getLanguage();

    bool isEscKey = (keyChar == 27 || keyChar == '`' || keys.del);
    bool isEnterKey = (keys.enter || keyChar == '\r' || keyChar == '\n');

    // 1. 若当前处于退出确认弹窗中
    if (_showExitConfirm) {
        // 防止打开弹窗的当次按键长按连击直接关闭弹窗（防抖 250ms）
        if (millis() - _confirmOpenTime < 250) {
            return false;
        }

        // 按 Esc 或 Backspace/Del：放弃修改直接退出，返回 true 切回主界面
        if (isEscKey) {
            _showExitConfirm = false;
            return true;
        }

        // 按 Enter：如果合法则保存并重启
        if (isEnterKey) {
            String err;
            if (HardwareConfig::getInstance().validate(err, lang)) {
                HardwareConfig::getInstance().save();
                _isRebooting = true;
                _rebootStartTime = millis();
                _showExitConfirm = false;
            }
            return false;
        }

        // 按任意其他键（如空格、方向键、字母键等）：取消弹窗继续留在向导
        if (keys.word.size() > 0 || (keyChar != 0 && keyChar != 27 && keyChar != '`') || keys.fn) {
            _showExitConfirm = false;
            return false;
        }
        return false;
    }

    // 2. 手动中括号翻页键处理（和卫星百科右侧界面保持完全一致）
    // '[' 向上翻页
    if (keyChar == '[' || M5Cardputer.Keyboard.isKeyPressed('[')) {
        if (_guideMaxScroll > 0) {
            _guideManualScrolled = true;
            _guideManualYOffset -= 26; // 向上翻动 2 行
            if (_guideManualYOffset < 0) _guideManualYOffset = 0;
        }
        if (_descMaxScroll > 0) {
            _descManualScrolled = true;
            _descManualYOffset -= 24;
            if (_descManualYOffset < 0) _descManualYOffset = 0;
        }
        return false;
    }

    // ']' 向下翻页
    if (keyChar == ']' || M5Cardputer.Keyboard.isKeyPressed(']')) {
        if (_guideMaxScroll > 0) {
            _guideManualScrolled = true;
            _guideManualYOffset += 26; // 向下翻动 2 行
            if (_guideManualYOffset > _guideMaxScroll) _guideManualYOffset = _guideMaxScroll;
        }
        if (_descMaxScroll > 0) {
            _descManualScrolled = true;
            _descManualYOffset += 24;
            if (_descManualYOffset > _descMaxScroll) _descManualYOffset = _descMaxScroll;
        }
        return false;
    }

    // 3. 正常向导界面下的按键处理
    // 按 Esc / Tick / Del：弹出退出确认对话框
    if (isEscKey) {
        _showExitConfirm = true;
        _confirmOpenTime = millis();
        return false;
    }

    // 方向键移动光标（在 0~3 四个模块项间循环）
    if (keys.word.size() > 0) {
        for (auto c : keys.word) {
            if (c == ';') { // Up (Cardputer 上键物理映射为 ';')
                _cursorIndex = (_cursorIndex - 1 + HW_MOD_COUNT) % HW_MOD_COUNT;
                _descScrollStartTime = millis();
                _descManualScrolled = false;
                _descManualYOffset = 0;
            } else if (c == '.') { // Down (Cardputer 下键物理映射为 '.')
                _cursorIndex = (_cursorIndex + 1) % HW_MOD_COUNT;
                _descScrollStartTime = millis();
                _descManualScrolled = false;
                _descManualYOffset = 0;
            } else if (c == ',' || c == '/') { // Left / Right: 翻转当前勾选
                HardwareConfig::getInstance().toggle((HardwareModule)_cursorIndex);
                _guideScrollStartTime = millis();
                _guideManualScrolled = false;
                _guideManualYOffset = 0;
            }
        }
    }

    // 回车键或空格键：翻转当前选中项勾选
    if (isEnterKey || keyChar == ' ') {
        HardwareConfig::getInstance().toggle((HardwareModule)_cursorIndex);
        _guideScrollStartTime = millis();
        _guideManualScrolled = false;
        _guideManualYOffset = 0;
    }

    return false;
}
