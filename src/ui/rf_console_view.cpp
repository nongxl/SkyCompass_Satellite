#include "rf_console_view.h"
#include "../core/radio_manager.h"
#include "../core/i18n.h"
#include "../core/encyclopedia.h"

RfConsoleView::RfConsoleView() {
    // 初始化瀑布能量图噪底采样
    for (int i = 0; i < WATERFALL_POINTS; i++) {
        _rssiHistory[i] = -120.0f + (float)(rand() % 6 - 3);
    }
}

void RfConsoleView::addRssiSample(float rssi) {
    if (rssi < -135.0f) rssi = -135.0f;
    if (rssi > -40.0f) rssi = -40.0f;
    _rssiHistory[_historyHead] = rssi;
    _historyHead = (_historyHead + 1) % WATERFALL_POINTS;
}

static void drawTimelineSatellite(LGFX_Sprite* canvas, int cx, int cy, SatIconType icon, bool isListening, bool isRising) {
    if (!canvas) return;

    // 绘制垂直对准微刻度线（高亮青绿，精确标定当前时间点位置）
    uint16_t pointerCol = isListening ? 0x07E0 : 0x07FF;
    canvas->drawFastVLine(cx, cy - 5, 2, pointerCol);
    canvas->drawFastVLine(cx, cy + 4, 2, pointerCol);

    // 呼吸动态信标光点颜色
    bool pulse = ((millis() / 400) % 2 == 0);
    uint16_t beaconCol = isListening ? (pulse ? 0x07E0 : 0x04A0) : 0x07FF;

    if (icon == ICON_STATION) {
        // === 空间站 (天宫/ISS 等大型桁架与双翼大面积光伏阵列) ===
        canvas->fillRect(cx - 3, cy - 1, 7, 3, 0xD6BA);
        canvas->fillRect(cx - 1, cy - 2, 3, 5, 0xE71C);
        canvas->drawFastHLine(cx - 8, cy, 17, 0x7BEF);
        canvas->fillRect(cx - 8, cy - 3, 4, 7, 0x1B3F);
        canvas->drawRect(cx - 8, cy - 3, 4, 7, 0xFDA0);
        canvas->fillRect(cx + 5, cy - 3, 4, 7, 0x1B3F);
        canvas->drawRect(cx + 5, cy - 3, 4, 7, 0xFDA0);
        canvas->drawPixel(cx, cy, beaconCol);
    } else if (icon == ICON_DFH1) {
        // === 东方红一号 (球体核心 + 斜向鞭状天线) ===
        canvas->fillCircle(cx, cy, 3, 0xD6BA);
        canvas->drawCircle(cx, cy, 3, 0x7BEF);
        canvas->drawPixel(cx - 1, cy - 1, 0xFFFF);
        canvas->drawLine(cx - 2, cy - 2, cx - 5, cy - 4, TFT_WHITE);
        canvas->drawLine(cx + 2, cy - 2, cx + 5, cy - 4, TFT_WHITE);
        canvas->drawPixel(cx, cy, beaconCol);
    } else {
        // === 通用立方星 / 卫星 ===
        canvas->fillRect(cx - 2, cy - 2, 5, 5, 0xD6BA);
        canvas->drawRect(cx - 2, cy - 2, 5, 5, 0x07FF);
        canvas->drawFastHLine(cx - 6, cy, 4, 0x1B3F);
        canvas->drawFastHLine(cx + 3, cy, 4, 0x1B3F);
        canvas->drawPixel(cx, cy, beaconCol);
    }
}

static const char* getCompass8Dir(float az) {
    while (az < 0.0f) az += 360.0f;
    while (az >= 360.0f) az -= 360.0f;
    int idx = (int)((az + 22.5f) / 45.0f) % 8;
    Language lang = I18N::getLanguage();
    if (lang == LANG_ZH) {
        static const char* dirsZh[8] = {"北(N)", "东北(NE)", "东(E)", "东南(SE)", "南(S)", "西南(SW)", "西(W)", "西北(NW)"};
        return dirsZh[idx];
    }
    static const char* dirsEn[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return dirsEn[idx];
}

void RfConsoleView::handleKeys(bool justSemi, bool justDot, bool justEnter, bool justD, bool justEsc, 
                                bool justT, bool justComma, bool justSlash, bool justZero, bool justY, bool justN,
                                bool justTab, int32_t& timeOffset) {
    if (justEsc) {
        if (_showDeleteModal) {
            _showDeleteModal = false;
        } else if (_showDetailModal) {
            _showDetailModal = false;
        } else {
            _isActive = false; // 退出终端
        }
        return;
    }

    // 1. 删除确认模态弹窗按键响应
    if (_showDeleteModal) {
        if (justY) {
            RadioManager::getInstance().deletePacket(_selectedPacketIndex);
            _showDeleteModal = false;
            const auto& packets = RadioManager::getInstance().getRecentPackets();
            if (_selectedPacketIndex >= (int)packets.size() && !packets.empty()) {
                _selectedPacketIndex = (int)packets.size() - 1;
            }
        } else if (justN || justEsc) {
            _showDeleteModal = false;
        }
        return;
    }

    // 2. Tab 键：在【瀑布图过境仪表模式】与【报文详情列表模式】之间切换
    if (justTab) {
        _currentMode = (_currentMode == RF_VIEW_WATERFALL) ? RF_VIEW_PACKETS : RF_VIEW_WATERFALL;
        return;
    }

    // 3. T 键：模拟测试遥测包注入
    if (justT) {
        RadioManager::getInstance().injectTestPacket();
        // 瞬间在瀑布图注入高能量尖峰
        addRssiSample(-78.0f);
        return;
    }

    // 4. 时间轴校准控制：按 , 减 5 秒，按 / 加 5 秒，按 0/R 复位
    if (justComma) {
        timeOffset -= 5;
    } else if (justSlash) {
        timeOffset += 5;
    } else if (justZero) {
        timeOffset = 0;
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
        } else if (count > 0) {
            _selectedPacketIndex = 0;
        }
    } else if (justEnter && count > 0) {
        _showDetailModal = true;
    } else if (justD && count > 0) {
        _showDeleteModal = true;
    }
}

void RfConsoleView::draw(LGFX_Sprite* canvas, int width, int height) {
    if (!canvas) return;

    // 定期采样 RSSI 注入瀑布历史
    uint32_t now = millis();
    if (now - _lastSampleTime >= 90) {
        _lastSampleTime = now;
        RadioManager& rm = RadioManager::getInstance();
        float currentRssi = -120.0f;
        if (rm.isHardwareReady() && rm.isListening()) {
            currentRssi = HalRadio::getInstance().getInstantRSSI();
            if (currentRssi < -130.0f || currentRssi > 0.0f) {
                // 若底噪未锁定，加入真实的轻微起伏
                currentRssi = -118.0f + (float)(rand() % 8 - 4);
            }
        } else {
            currentRssi = -122.0f + (float)(rand() % 4 - 2);
        }
        addRssiSample(currentRssi);
    }

    // 全黑科技底色
    canvas->fillScreen(0x0000);
    canvas->setFont(I18N::getFont());
    canvas->setTextSize(1);
    canvas->setTextDatum(TL_DATUM);

    // 1. 顶部 Header 栏 (高度 18)
    canvas->fillRect(0, 0, width, 18, 0x10A2);

    // 绘制内存使用率进度条
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

        uint16_t memColor = (memRatio < 0.65f) ? canvas->color565(0, 220, 255) : ((memRatio < 0.82f) ? TFT_YELLOW : TFT_RED);
        canvas->fillRect(0, 18, width, 2, canvas->color565(35, 45, 55));
        if (fillWidth > 0) {
            canvas->fillRect(0, 18, fillWidth, 2, memColor);
        }
    }

    canvas->setTextDatum(TL_DATUM);
    canvas->setTextColor(0x07FF, 0x10A2);
    String titleStr = I18N::get(TXT_RF_TITLE);
    canvas->drawString(titleStr.c_str(), 4, 3);
    int titleW = canvas->textWidth(titleStr.c_str());
    int statX = 4 + titleW + 8;

    RadioManager& rm = RadioManager::getInstance();
    const auto& track = rm.getTrackingInfo();

    int maxStatW = width - statX - 70;
    if (maxStatW > 0) {
        canvas->setClipRect(statX, 0, maxStatW, 18);
    }

    if (!rm.isHardwareReady()) {
        canvas->setTextColor(0xF800, 0x10A2);
        canvas->drawString(I18N::get(TXT_RF_HW_NOT_DETECTED), statX, 3);
    } else if (rm.isListening() || track.hasPass) {
        canvas->setTextColor(0x07E0, 0x10A2);
        String stat = String(I18N::get(TXT_RF_RX_ACTIVE)) + " " + (track.satName.length() > 0 ? track.satName : rm.getActiveSatName());
        if (track.baseFreqMHz > 0.0f) {
            stat += " " + String(track.baseFreqMHz, 3) + "M";
        }
        canvas->drawString(stat.c_str(), statX, 3);
    } else {
        canvas->setTextColor(0xFFE0, 0x10A2);
        canvas->drawString(I18N::get(TXT_RF_IDLE_STANDBY), statX, 3);
    }
    if (maxStatW > 0) {
        canvas->clearClipRect();
    }

    // 右上角包数与 CRC 质量统计
    canvas->setTextDatum(TR_DATUM);
    canvas->setTextColor(0xFFFF, 0x10A2);
    char statBuf[32];
    snprintf(statBuf, sizeof(statBuf), "OK:%d E:%d", (int)rm.getValidPacketsCount(), (int)rm.getCrcErrorCount());
    canvas->drawString(statBuf, width - 4, 3);

    // 2. 主区域内容
    const auto& packets = rm.getRecentPackets();

    // 如果强制选择列表视图，或者当前处于报文模式
    if (_currentMode == RF_VIEW_PACKETS && !packets.empty()) {
        // ==========================================
        // 模式 2：收包历史列表 + 结构化字段浏览
        // ==========================================
        if (_selectedPacketIndex >= (int)packets.size()) {
            _selectedPacketIndex = (int)packets.size() - 1;
        }
        if (_selectedPacketIndex < 0) _selectedPacketIndex = 0;

        int listY = 22;
        int maxListHeight = 84;
        int visibleRows = maxListHeight / 16;
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

            // 第 1 列：序号
            canvas->setTextColor(isSelected ? 0xFFFF : 0x07E0);
            char prefix[16];
            snprintf(prefix, sizeof(prefix), "#%02d", rowIdx + 1);
            canvas->drawString(prefix, 5, rowY + 3);

            // 第 2 列：卫星名
            canvas->setTextColor(isSelected ? 0x07FF : 0xC618);
            String satInfo = p.decoded.satName;
            canvas->setClipRect(26, rowY, 46, 15);
            canvas->drawString(satInfo, 26, rowY + 3);
            canvas->clearClipRect();

            // 第 3 列：射频指标
            canvas->setTextColor(0xCE79);
            char metrics[32];
            snprintf(metrics, sizeof(metrics), "%ddB/%.0fdB %dB", (int)p.raw.rssi, p.raw.snr, (int)p.raw.length);
            canvas->setClipRect(74, rowY, 64, 15);
            canvas->drawString(metrics, 74, rowY + 3);
            canvas->clearClipRect();

            // 第 4 列：字段简要预览
            if (!p.decoded.fields.empty()) {
                canvas->setTextColor(0xFFE0);
                String fPreview = p.decoded.fields[0].key + ":" + p.decoded.fields[0].value;
                canvas->setClipRect(140, rowY, width - 144, 15);
                canvas->drawString(fPreview.c_str(), 140, rowY + 3);
                canvas->clearClipRect();
            }
        }

        // 底部提示行
        canvas->fillRect(0, 108, width, 27, canvas->color565(12, 18, 25));
        canvas->drawFastHLine(0, 108, width, canvas->color565(35, 50, 68));
        canvas->setTextDatum(TL_DATUM);
        canvas->setTextColor(0x07FF);
        canvas->drawString("[Enter] 查看详情  [d] 删除  [Tab] 瀑布图", 8, 114);
    } else {
        // ==========================================
        // 模式 1：过境雷达 + 实时信号瀑布能量图 (Waterfall)
        // ==========================================
        if (!rm.isHardwareReady()) {
            canvas->setTextDatum(MC_DATUM);
            canvas->setTextColor(0x7BEF);
            canvas->drawString(I18N::get(TXT_RF_REQ_MODULE), width / 2, height / 2 - 8);
            canvas->setTextColor(0x4208);
            canvas->drawString(I18N::get(TXT_RF_ENABLE_IN_WIZARD), width / 2, height / 2 + 8);
        } else {
            // A1. 单行紧凑参数栏 (Y = 22)
            canvas->setTextDatum(TL_DATUM);
            canvas->setTextColor(0xCE79);
            char azBuf[32];
            snprintf(azBuf, sizeof(azBuf), "Az:%03.0f° %s", track.currentAz, getCompass8Dir(track.currentAz));
            canvas->drawString(azBuf, 8, 22);

            // 中间仰角
            canvas->setTextDatum(TC_DATUM);
            char elBuf[32];
            uint16_t elCol = track.currentEl > 0.0f ? (track.isRising ? 0x07E0 : TFT_YELLOW) : 0x7BEF;
            canvas->setTextColor(elCol);
            snprintf(elBuf, sizeof(elBuf), "El:%+.0f° (%s:%.0f°)", track.currentEl, I18N::get(TXT_RF_MAX_EL), track.maxEl);
            canvas->drawString(elBuf, width / 2, 22);

            // 右侧多普勒
            canvas->setTextDatum(TR_DATUM);
            char dopBuf[32];
            float actualFreq = track.baseFreqMHz + (track.dopplerHz / 1e6f);
            if (abs(track.dopplerHz) >= 1000.0f) {
                snprintf(dopBuf, sizeof(dopBuf), "Dop:%+.1fk", track.dopplerHz / 1000.0f);
            } else {
                snprintf(dopBuf, sizeof(dopBuf), "Dop:%+.0fHz", track.dopplerHz);
            }
            canvas->setTextColor(0x07FF);
            canvas->drawString(dopBuf, width - 8, 22);

            // A2. 紧凑过境时间进度轴 (Y = 37 ~ 50)
            int trackX = 24;
            int trackW = width - 48; // 192px
            int trackY = 44;

            uint32_t totalDur = (track.losTime > track.aosTime) ? (track.losTime - track.aosTime) : 600;
            if (totalDur == 0) totalDur = 600;
            int32_t elapsedSec = (int32_t)totalDur / 2;
            if (track.aosTime > 0 && track.losTime > track.aosTime) {
                if (track.isRising) {
                    elapsedSec = (int32_t)((float)totalDur * 0.5f * (track.currentEl > 0 ? (track.currentEl / (track.maxEl > 1.0f ? track.maxEl : 1.0f)) : 0.05f));
                } else {
                    elapsedSec = (int32_t)(totalDur * 0.5f + (float)totalDur * 0.5f * (1.0f - (track.currentEl > 0 ? (track.currentEl / (track.maxEl > 1.0f ? track.maxEl : 1.0f)) : 0.95f)));
                }
            }
            if (elapsedSec < 0) elapsedSec = 0;
            if (elapsedSec > (int32_t)totalDur) elapsedSec = (int32_t)totalDur;

            float progressRatio = (float)elapsedSec / (float)totalDur;
            if (progressRatio < 0.0f) progressRatio = 0.0f;
            if (progressRatio > 1.0f) progressRatio = 1.0f;

            canvas->fillRect(trackX, trackY - 1, trackW, 2, canvas->color565(30, 45, 60));
            int fillTrackW = (int)(trackW * progressRatio);
            if (fillTrackW > 0) {
                canvas->fillRect(trackX, trackY - 1, fillTrackW, 2, canvas->color565(0, 180, 220));
            }

            canvas->setTextDatum(TL_DATUM);
            canvas->setTextColor(0x7BEF);
            canvas->drawString("AOS", 4, trackY - 4);
            canvas->setTextDatum(TR_DATUM);
            canvas->drawString("LOS", width - 4, trackY - 4);

            int curSatX = trackX + fillTrackW;
            SatIconType satIcon = ICON_SATELLITE;
            const EncyclopediaEntry* entry = Encyclopedia::getEntryByNorad(track.satNorad);
            if (entry) satIcon = entry->icon;
            drawTimelineSatellite(canvas, curSatX, trackY, satIcon, rm.isListening(), track.isRising);

            // A3. 重点：信号瀑布能量图 (Mini Waterfall, Y = 54 ~ 104, 高度 50px)
            int wfX = 8;
            int wfY = 54;
            int wfW = width - 16; // 224px
            int wfH = 48;

            canvas->fillRect(wfX, wfY, wfW, wfH, canvas->color565(8, 14, 20));
            canvas->drawRect(wfX, wfY, wfW, wfH, canvas->color565(30, 50, 70));

            // 网格刻度线与分贝标注 (-120dBm, -100dBm, -80dBm, -60dBm)
            // 映射范围：-130 dBm (底部 wfY + wfH - 2) ~ -50 dBm (顶部 wfY + 2)
            auto rssiToY = [wfY, wfH](float rssi) -> int {
                float norm = (rssi - (-130.0f)) / 80.0f; // 0.0 ~ 1.0
                if (norm < 0.0f) norm = 0.0f;
                if (norm > 1.0f) norm = 1.0f;
                return wfY + wfH - 2 - (int)(norm * (wfH - 4));
            };

            int yNoise = rssiToY(-115.0f);
            int yMid = rssiToY(-90.0f);
            int yStrong = rssiToY(-65.0f);

            // 绘制虚线刻度
            for (int gx = wfX + 2; gx < wfX + wfW - 2; gx += 6) {
                canvas->drawPixel(gx, yNoise, canvas->color565(40, 55, 65));
                canvas->drawPixel(gx, yMid, canvas->color565(35, 60, 50));
            }

            // 刻度文字
            canvas->setTextDatum(TL_DATUM);
            canvas->setTextColor(canvas->color565(70, 90, 110));
            canvas->drawString("-115", wfX + 3, yNoise - 7);
            canvas->drawString("-90", wfX + 3, yMid - 7);

            // 遍历 112 个采样，绘制瀑布能量柱与折线
            float peakRssi = -140.0f;
            int prevX = -1;
            int prevY = -1;

            for (int i = 0; i < WATERFALL_POINTS; i++) {
                int readIdx = (_historyHead + i) % WATERFALL_POINTS;
                float rVal = _rssiHistory[readIdx];
                if (rVal > peakRssi) peakRssi = rVal;

                int px = wfX + i * 2;
                int py = rssiToY(rVal);

                // 能量柱渐变填充
                if (rVal > -115.0f) {
                    uint16_t col = (rVal > -85.0f) ? 0x07E0 : ((rVal > -100.0f) ? 0x07FF : canvas->color565(0, 120, 150));
                    canvas->drawFastVLine(px, py, (wfY + wfH - 2) - py, col);
                    canvas->drawFastVLine(px + 1, py, (wfY + wfH - 2) - py, col);
                }

                // 顶端折线
                if (prevX >= 0) {
                    canvas->drawLine(prevX, prevY, px, py, 0x07FF);
                }
                prevX = px;
                prevY = py;
            }

            // 瀑布图右上角指示峰值与瞬时值
            canvas->setTextDatum(TR_DATUM);
            char wfStatBuf[36];
            snprintf(wfStatBuf, sizeof(wfStatBuf), "Peak:%.0fdB  Now:%.0fdB", peakRssi, _rssiHistory[(_historyHead - 1 + WATERFALL_POINTS) % WATERFALL_POINTS]);
            canvas->setTextColor(peakRssi > -95.0f ? 0x07E0 : 0x07FF);
            canvas->drawString(wfStatBuf, wfX + wfW - 4, wfY + 3);

            // A4. 底部紧凑双列状态栏 (Y = 106 ~ 134)
            canvas->fillRect(0, 106, width, 29, canvas->color565(12, 18, 25));
            canvas->drawFastHLine(0, 106, width, canvas->color565(35, 50, 68));

            // 左列：天线朝向引导
            canvas->setTextDatum(TL_DATUM);
            canvas->setTextColor(0x07FF);
            char antBuf[48];
            int elTarget = (int)track.currentEl;
            if (elTarget < 0) elTarget = 0;
            snprintf(antBuf, sizeof(antBuf), "%s:%03.0f°/%02d° %s", I18N::get(TXT_RF_ANTENNA_DIR), track.currentAz, elTarget, getCompass8Dir(track.currentAz));
            canvas->drawString(antBuf, 8, 112);

            // 右列：收包质量、CRC 错误与切换提示
            canvas->setTextDatum(TR_DATUM);
            char qualBuf[48];
            float lossRate = rm.getPacketLossRate();
            snprintf(qualBuf, sizeof(qualBuf), "CRC:%d ERR:%d(%.0f%%)", (int)rm.getValidPacketsCount(), (int)rm.getCrcErrorCount(), lossRate);
            canvas->setTextColor(rm.getCrcErrorCount() > 0 ? TFT_YELLOW : 0x07E0);
            canvas->drawString(qualBuf, width - 8, 112);

            // 极底部浅灰按键提示
            canvas->setTextDatum(TC_DATUM);
            canvas->setTextColor(canvas->color565(80, 100, 120));
            canvas->drawString("[T] 注入测试  [Tab] 报文列表  [Esc] 退出", width / 2, 123);
        }
    }

    // 3. 详情弹窗 (Detail Modal)
    if (_showDetailModal && !packets.empty() && _selectedPacketIndex < (int)packets.size()) {
        const auto& sel = packets[_selectedPacketIndex];
        int modalW = width - 20;
        int modalH = height - 22;
        int modalX = 10;
        int modalY = 11;

        canvas->fillRect(modalX, modalY, modalW, modalH, 0x0841);
        canvas->drawRect(modalX, modalY, modalW, modalH, 0x07FF);
        canvas->drawFastHLine(modalX, modalY + 16, modalW, 0x2965);

        canvas->setTextDatum(TL_DATUM);
        canvas->setTextColor(0x07FF, 0x0841);
        canvas->drawString(I18N::get(TXT_RF_MODAL_TITLE), modalX + 6, modalY + 3);

        canvas->setClipRect(modalX + 2, modalY + 18, modalW - 4, modalH - 20);
        int curY = modalY + 20;

        canvas->setTextColor(0xFFFF, 0x0841);
        String headerLine = sel.decoded.satName + " [" + sel.decoded.frameType + "]";
        canvas->drawString(headerLine.c_str(), modalX + 6, curY);
        curY += 13;

        char rfLine[64];
        snprintf(rfLine, sizeof(rfLine), "%.3fM | RSSI:%.0f | SNR:%.1f | %dB",
                sel.raw.freqMHz, sel.raw.rssi, sel.raw.snr, (int)sel.raw.length);
        canvas->setTextColor(0x07E0, 0x0841);
        canvas->drawString(rfLine, modalX + 6, curY);
        curY += 13;

        canvas->setTextColor(0xFFE0, 0x0841);
        for (size_t f = 0; f < sel.decoded.fields.size() && f < 4; f += 2) {
            String fRow = sel.decoded.fields[f].key + ":" + sel.decoded.fields[f].value + sel.decoded.fields[f].unit;
            if (f + 1 < sel.decoded.fields.size()) {
                fRow += "  " + sel.decoded.fields[f+1].key + ":" + sel.decoded.fields[f+1].value + sel.decoded.fields[f+1].unit;
            }
            canvas->drawString(fRow.c_str(), modalX + 6, curY);
            curY += 13;
        }

        canvas->setTextColor(0xC618, 0x0841);
        canvas->drawString(I18N::get(TXT_RF_RAW_HEX), modalX + 6, curY);
        curY += 11;

        String hexLine1 = sel.decoded.rawHex.substring(0, 30);
        String hexLine2 = sel.decoded.rawHex.length() > 30 ? sel.decoded.rawHex.substring(30, 60) : "";
        canvas->setTextColor(0x7BEF, 0x0841);
        canvas->drawString(hexLine1.c_str(), modalX + 6, curY);
        curY += 10;
        if (hexLine2.length() > 0) {
            canvas->drawString(hexLine2.c_str(), modalX + 6, curY);
        }

        canvas->clearClipRect();
    }

    // 4. 删除报文确认弹窗
    if (_showDeleteModal && !packets.empty()) {
        int popW = 160;
        int popH = 44;
        int popX = (width - popW) / 2;
        int popY = (height - popH) / 2;

        canvas->fillRect(popX, popY, popW, popH, TFT_RED);
        canvas->drawRect(popX, popY, popW, popH, TFT_WHITE);
        canvas->setTextDatum(TC_DATUM);
        canvas->setTextColor(TFT_WHITE);
        canvas->drawString(I18N::getLanguage() == LANG_ZH ? "删除此条报文记录？" : "Delete this packet?", width / 2, popY + 6);
        canvas->drawString(I18N::getLanguage() == LANG_ZH ? "[y] 确定   [n] 取消" : "[y] Yes   [n] No", width / 2, popY + 24);
    }

    // 退出绘制时复位全局状态
    canvas->setTextDatum(TL_DATUM);
    canvas->clearClipRect();
}
