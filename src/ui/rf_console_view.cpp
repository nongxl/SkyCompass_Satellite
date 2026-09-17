#include "rf_console_view.h"
#include "../core/radio_manager.h"
#include "../core/i18n.h"

RfConsoleView::RfConsoleView() {
}

void RfConsoleView::handleKeys(bool justSemi, bool justDot, bool justEnter, bool justD, bool justEsc) {
    if (justEsc) {
        if (_showDetailModal) {
            _showDetailModal = false;
        } else {
            _isActive = false; // 退出终端
        }
        return;
    }

    const auto& packets = RadioManager::getInstance().getRecentPackets();
    int count = (int)packets.size();

    if (_showDetailModal) {
        if (justEnter) {
            _showDetailModal = false;
        }
        return;
    }

    if (justSemi) { // 上翻
        if (_selectedPacketIndex > 0) {
            _selectedPacketIndex--;
        } else if (count > 0) {
            _selectedPacketIndex = count - 1;
        }
    } else if (justDot) { // 下翻
        if (_selectedPacketIndex < count - 1) {
            _selectedPacketIndex++;
        } else {
            _selectedPacketIndex = 0;
        }
    } else if (justEnter && count > 0) {
        _showDetailModal = true;
    } else if (justD && count > 0) {
        _showDetailModal = !_showDetailModal;
    }
}

void RfConsoleView::draw(LGFX_Sprite* canvas, int width, int height) {
    if (!canvas) return;

    // 全黑底色
    canvas->fillScreen(0x0000);
    canvas->setFont(I18N::getFont());
    canvas->setTextSize(1);

    // 1. 顶部 Header 栏 (高度 18)
    canvas->fillRect(0, 0, width, 18, 0x10A2);

    // 绘制内存使用率进度条充当分割线 (与卫星百科页面风格完全一致)
    {
        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t totalHeap = ESP.getHeapSize();
        float memRatio = 0.0f;
        if (totalHeap > 0) {
            memRatio = (float)(totalHeap - freeHeap) / (float)totalHeap;
        }
        int fillWidth = (int)(width * memRatio);
        if (fillWidth > width) fillWidth = width;
        if (fillWidth < 0) fillWidth = 0;

        uint16_t memColor;
        if (memRatio < 0.65f) {
            memColor = canvas->color565(0, 220, 255); // 青色 (正常 <65%)
        } else if (memRatio < 0.82f) {
            memColor = TFT_YELLOW;                   // 黄色 (预警 65%-82%)
        } else {
            memColor = TFT_RED;                      // 红色 (高占用 >82%)
        }

        canvas->fillRect(0, 18, width, 2, canvas->color565(35, 45, 55)); // 轨道背景
        if (fillWidth > 0) {
            canvas->fillRect(0, 18, fillWidth, 2, memColor);             // 填充内存使用率
        }
    }

    canvas->setTextDatum(TL_DATUM);
    canvas->setTextColor(0x07FF, 0x10A2); // 青色标题
    canvas->drawString(I18N::get(TXT_RF_TITLE), 4, 3);

    RadioManager& rm = RadioManager::getInstance();
    if (!rm.isHardwareReady()) {
        canvas->setTextColor(0xF800, 0x10A2); // 红色
        canvas->drawString(I18N::get(TXT_RF_HW_NOT_DETECTED), 74, 3);
    } else if (rm.isListening()) {
        canvas->setTextColor(0x07E0, 0x10A2); // 绿色
        String stat = String(I18N::get(TXT_RF_RX_ACTIVE)) + " " + rm.getActiveSatName() + " " + String(rm.getActiveFreq(), 3) + "M";
        canvas->drawString(stat.c_str(), 74, 3);
    } else {
        canvas->setTextColor(0xFFE0, 0x10A2); // 黄色待机
        canvas->drawString(I18N::get(TXT_RF_IDLE_STANDBY), 74, 3);
    }

    // 右上角包数统计
    canvas->setTextDatum(TR_DATUM);
    canvas->setTextColor(0xFFFF, 0x10A2);
    canvas->drawString(String((int)rm.getTotalPacketsCount()) + " " + I18N::get(TXT_RF_PKTS), width - 4, 3);

    // 2. 主区域内容 (移除底部提示栏后，高度延伸至屏幕底部)
    const auto& packets = rm.getRecentPackets();
    int listY = 20;
    int bottomLimit = height - 2;

    if (packets.empty()) {
        canvas->setTextDatum(MC_DATUM);
        canvas->setTextColor(0x7BEF);
        if (!rm.isHardwareReady()) {
            canvas->drawString(I18N::get(TXT_RF_REQ_MODULE), width / 2, height / 2 - 8);
            canvas->setTextColor(0x4208);
            canvas->drawString(I18N::get(TXT_RF_ENABLE_IN_WIZARD), width / 2, height / 2 + 8);
        } else if (rm.isListening()) {
            canvas->drawString(I18N::get(TXT_RF_LISTENING_PASS), width / 2, height / 2 - 8);
            canvas->setTextColor(0x4208);
            canvas->drawString(I18N::get(TXT_RF_WAITING_PACKETS), width / 2, height / 2 + 8);
        } else {
            canvas->drawString(I18N::get(TXT_RF_NO_PASS_IDLE), width / 2, height / 2 - 8);
            canvas->setTextColor(0x4208);
            canvas->drawString(I18N::get(TXT_RF_AUTO_TRIGGER_TIP), width / 2, height / 2 + 8);
        }
    } else {
        // 修正越界
        if (_selectedPacketIndex >= (int)packets.size()) {
            _selectedPacketIndex = (int)packets.size() - 1;
        }
        if (_selectedPacketIndex < 0) _selectedPacketIndex = 0;

        // 显示数据包列表 (每条高度 16)
        int visibleRows = (bottomLimit - listY) / 16;
        int startIdx = 0;
        if (_selectedPacketIndex >= visibleRows) {
            startIdx = _selectedPacketIndex - visibleRows + 1;
        }

        for (int i = 0; i < visibleRows && (startIdx + i) < (int)packets.size(); i++) {
            int rowIdx = startIdx + i;
            int rowY = listY + i * 16;
            bool isSelected = (rowIdx == _selectedPacketIndex);

            if (isSelected) {
                canvas->fillRect(2, rowY, width - 4, 15, 0x2145);
                canvas->drawRect(2, rowY, width - 4, 15, 0x07FF);
            }

            const auto& p = packets[rowIdx];
            canvas->setTextDatum(TL_DATUM);

            // 序号与包类型
            canvas->setTextColor(isSelected ? 0xFFFF : 0x07E0);
            char prefix[16];
            sprintf(prefix, "#%02d", rowIdx + 1);
            canvas->drawString(prefix, 6, rowY + 3);

            // 卫星名与模式
            canvas->setTextColor(isSelected ? 0x07FF : 0xC618);
            String satInfo = p.decoded.satName;
            if (satInfo.length() > 10) satInfo = satInfo.substring(0, 10);
            canvas->drawString(satInfo, 32, rowY + 3);

            // 长度与指标
            canvas->setTextColor(0xCE79);
            char metrics[32];
            sprintf(metrics, "%dB | %ddBm | %.1fdB", (int)p.raw.length, (int)p.raw.rssi, p.raw.snr);
            canvas->drawString(metrics, 105, rowY + 3);

            // 简要字段预览 (如第一项物理量)
            if (!p.decoded.fields.empty()) {
                canvas->setTextColor(0xFFE0);
                String fPreview = p.decoded.fields[0].key + ":" + p.decoded.fields[0].value;
                canvas->drawString(fPreview.c_str(), 188, rowY + 3);
            }
        }
    }

    // 3. 详情弹窗 (Detail Modal)
    if (_showDetailModal && !packets.empty() && _selectedPacketIndex < (int)packets.size()) {
        const auto& sel = packets[_selectedPacketIndex];
        int modalW = width - 20;
        int modalH = height - 26;
        int modalX = 10;
        int modalY = 13;

        // 阴影与弹窗背景
        canvas->fillRect(modalX, modalY, modalW, modalH, 0x0841);
        canvas->drawRect(modalX, modalY, modalW, modalH, 0x07FF);
        canvas->drawFastHLine(modalX, modalY + 16, modalW, 0x2965);

        canvas->setTextDatum(TL_DATUM);
        canvas->setTextColor(0x07FF, 0x0841);
        canvas->drawString(I18N::get(TXT_RF_MODAL_TITLE), modalX + 6, modalY + 4);

        int curY = modalY + 20;

        // 基础指标
        canvas->setTextColor(0xFFFF, 0x0841);
        String headerLine = sel.decoded.satName + " [" + sel.decoded.frameType + "]";
        canvas->drawString(headerLine.c_str(), modalX + 6, curY);
        curY += 12;

        char rfLine[48];
        sprintf(rfLine, "Freq: %.3fM | RSSI: %.1f | SNR: %.1f",
                sel.raw.freqMHz, sel.raw.rssi, sel.raw.snr);
        canvas->setTextColor(0x07E0, 0x0841);
        canvas->drawString(rfLine, modalX + 6, curY);
        curY += 13;

        // 解码物理量
        canvas->setTextColor(0xFFE0, 0x0841);
        String fLine = "";
        for (size_t f = 0; f < sel.decoded.fields.size() && f < 4; f++) {
            fLine += sel.decoded.fields[f].key + ":" + sel.decoded.fields[f].value + sel.decoded.fields[f].unit + " ";
        }
        if (fLine.length() > 0) {
            canvas->drawString(fLine.c_str(), modalX + 6, curY);
            curY += 13;
        }

        // Hex Dump 预览 (最多显示前 32 字节)
        canvas->setTextColor(0xC618, 0x0841);
        canvas->drawString(I18N::get(TXT_RF_RAW_HEX), modalX + 6, curY);
        curY += 10;

        String hexLine1 = sel.decoded.rawHex.substring(0, 36);
        String hexLine2 = sel.decoded.rawHex.length() > 36 ? sel.decoded.rawHex.substring(36, 72) : "";
        canvas->setTextColor(0x7BEF, 0x0841);
        canvas->drawString(hexLine1.c_str(), modalX + 6, curY);
        curY += 10;
        if (hexLine2.length() > 0) {
            canvas->drawString(hexLine2.c_str(), modalX + 6, curY);
        }
    }
}
