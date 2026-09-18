#include "rf_console_view.h"
#include "../core/radio_manager.h"
#include "../core/i18n.h"
#include "../core/encyclopedia.h"

RfConsoleView::RfConsoleView() {
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

    // 垂直对准微刻度线
    uint16_t pointerCol = isListening ? 0x07E0 : 0x07FF;
    canvas->drawFastVLine(cx, cy - 6, 2, pointerCol);
    canvas->drawFastVLine(cx, cy + 5, 2, pointerCol);

    // 呼吸动态信标光点
    bool pulse = ((millis() / 400) % 2 == 0);
    uint16_t beaconCol = isListening ? (pulse ? 0x07E0 : 0x04A0) : 0x07FF;

    if (icon == ICON_STATION) {
        canvas->fillRect(cx - 3, cy - 1, 7, 3, 0xD6BA);
        canvas->fillRect(cx - 1, cy - 2, 3, 5, 0xE71C);
        canvas->drawFastHLine(cx - 9, cy, 19, 0x7BEF);
        canvas->fillRect(cx - 9, cy - 4, 4, 9, 0x1B3F);
        canvas->drawRect(cx - 9, cy - 4, 4, 9, 0xFDA0);
        canvas->drawFastHLine(cx - 9, cy, 4, TFT_BLACK);
        canvas->fillRect(cx + 6, cy - 4, 4, 9, 0x1B3F);
        canvas->drawRect(cx + 6, cy - 4, 4, 9, 0xFDA0);
        canvas->drawFastHLine(cx + 6, cy, 4, TFT_BLACK);
        canvas->drawPixel(cx, cy, beaconCol);
    } else if (icon == ICON_DFH1) {
        canvas->fillCircle(cx, cy, 3, 0xD6BA);
        canvas->drawCircle(cx, cy, 3, 0x7BEF);
        canvas->drawPixel(cx - 1, cy - 1, 0xFFFF);
        canvas->drawLine(cx - 2, cy - 2, cx - 6, cy - 5, TFT_WHITE);
        canvas->drawLine(cx + 2, cy - 2, cx + 6, cy - 5, TFT_WHITE);
        canvas->drawLine(cx - 2, cy + 2, cx - 6, cy + 5, TFT_WHITE);
        canvas->drawLine(cx + 2, cy + 2, cx + 6, cy + 5, TFT_WHITE);
        canvas->drawPixel(cx, cy, beaconCol);
    } else if (icon == ICON_WEATHER) {
        canvas->fillRect(cx - 2, cy - 2, 4, 5, 0xCE79);
        canvas->fillRect(cx - 8, cy - 4, 5, 9, 0x0AD5);
        canvas->drawRect(cx - 8, cy - 4, 5, 9, 0x345F);
        canvas->drawFastHLine(cx - 8, cy, 5, TFT_BLACK);
        canvas->fillRect(cx + 2, cy - 1, 3, 3, 0x4208);
        canvas->drawPixel(cx + 5, cy, beaconCol);
    } else if (icon == ICON_NAVIGATION) {
        canvas->fillRect(cx - 2, cy - 3, 5, 7, 0xD6BA);
        canvas->fillRect(cx - 8, cy - 2, 5, 5, 0x1B3F);
        canvas->drawRect(cx - 8, cy - 2, 5, 5, 0xFDA0);
        canvas->fillRect(cx + 4, cy - 2, 5, 5, 0x1B3F);
        canvas->drawRect(cx + 4, cy - 2, 5, 5, 0xFDA0);
        canvas->drawFastHLine(cx - 2, cy + 4, 5, TFT_WHITE);
        canvas->drawPixel(cx, cy, beaconCol);
    } else {
        canvas->fillRect(cx - 2, cy - 2, 5, 5, 0xD6BA);
        canvas->drawRect(cx - 2, cy - 2, 5, 5, 0x07FF);
        canvas->drawFastHLine(cx - 7, cy, 5, 0x1B3F);
        canvas->drawFastHLine(cx + 3, cy, 4, 0x01EF);
        canvas->drawFastHLine(cx - 3, cy, 7, 0xBDF7);
        canvas->fillRect(cx - 2, cy - 2, 5, 5, 0xFEA0);
        canvas->fillRect(cx - 1, cy - 1, 3, 3, 0xFFFF);
        canvas->drawFastVLine(cx, cy + 3, 2, TFT_WHITE);
        canvas->drawPixel(cx, cy + 4, beaconCol);
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
                                int32_t& timeOffset) {
    if (justEsc) {
        if (_showDeleteModal) {
            _showDeleteModal = false;
        } else if (_showDetailModal) {
            _showDetailModal = false;
        } else {
            _isActive = false;
        }
        return;
    }

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

    if (justT) {
        RadioManager::getInstance().injectTestPacket();
        addRssiSample(-75.0f);
        return;
    }

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

    if (justSemi) {
        if (_selectedPacketIndex > 0) {
            _selectedPacketIndex--;
        } else if (count > 0) {
            _selectedPacketIndex = count - 1;
        }
    } else if (justDot) {
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

void RfConsoleView::drawBackgroundWaterfall(LGFX_Sprite* canvas, int width, int height) {
    int bgY = 22;
    int bgH = height - bgY; // 113px
    int bgW = width;

    // 将采样数据渲染为深邃的背景频谱波形
    int prevX = -1;
    int prevY = -1;

    for (int i = 0; i < WATERFALL_POINTS; i++) {
        int readIdx = (_historyHead + i) % WATERFALL_POINTS;
        float rVal = _rssiHistory[readIdx];

        // 归一化到背景高度 ( -130dBm 为底, -50dBm 为顶 )
        float norm = (rVal - (-130.0f)) / 80.0f;
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;

        int px = i * 2;
        int py = height - 1 - (int)(norm * (bgH - 6));

        // 背景幽深能量柱 (不抢前景视野)
        if (rVal > -116.0f) {
            uint16_t col = (rVal > -85.0f) ? canvas->color565(0, 45, 60) : canvas->color565(8, 22, 34);
            canvas->drawFastVLine(px, py, height - 1 - py, col);
            canvas->drawFastVLine(px + 1, py, height - 1 - py, col);
        }

        // 背景轮廓辉光线
        uint16_t lineCol = (rVal > -85.0f) ? canvas->color565(0, 150, 180) : canvas->color565(18, 48, 65);
        if (prevX >= 0) {
            canvas->drawLine(prevX, prevY, px, py, lineCol);
        }
        prevX = px;
        prevY = py;
    }
}

void RfConsoleView::draw(LGFX_Sprite* canvas, int width, int height) {
    if (!canvas) return;

    // 采样 RSSI
    uint32_t now = millis();
    if (now - _lastSampleTime >= 100) {
        _lastSampleTime = now;
        RadioManager& rm = RadioManager::getInstance();
        float currentRssi = -120.0f;
        if (rm.isHardwareReady() && rm.isListening()) {
            currentRssi = HalRadio::getInstance().getInstantRSSI();
            if (currentRssi < -130.0f || currentRssi > 0.0f) {
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

    // 内存使用率进度条
    {
        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t totalHeap = ESP.getHeapSize();
        float memRatio = (totalHeap > 0) ? ((float)(totalHeap - freeHeap) / (float)totalHeap) : 0.0f;
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

    int maxStatW = width - statX - 90;
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

    // 右上角包数与 CRC 质量统计 (例如: 1 PKT OK:1 E:0)
    canvas->setTextDatum(TR_DATUM);
    canvas->setTextColor(0xFFFF, 0x10A2);
    char statBuf[40];
    snprintf(statBuf, sizeof(statBuf), "%d %s (OK:%d E:%d)",
             (int)rm.getTotalPacketsCount(), I18N::get(TXT_RF_PKTS),
             (int)rm.getValidPacketsCount(), (int)rm.getCrcErrorCount());
    canvas->drawString(statBuf, width - 4, 3);

    // 2. 绘制背景层信号瀑布波形
    drawBackgroundWaterfall(canvas, width, height);

    // 3. 前景完整数据层呈现
    const auto& packets = rm.getRecentPackets();

    if (packets.empty()) {
        // ==========================================
        // 状态 A：等待过境 / 地面站雷达仪表盘模式 (全套数据完整还原)
        // ==========================================
        if (!rm.isHardwareReady()) {
            canvas->setTextDatum(MC_DATUM);
            canvas->setTextColor(0x7BEF);
            canvas->drawString(I18N::get(TXT_RF_REQ_MODULE), width / 2, height / 2 - 8);
            canvas->setTextColor(0x4208);
            canvas->drawString(I18N::get(TXT_RF_ENABLE_IN_WIZARD), width / 2, height / 2 + 8);
        } else if (!track.hasPass || (track.currentEl < -10.0f && !rm.isListening())) {
            canvas->setTextDatum(MC_DATUM);
            canvas->setTextColor(0x7BEF);
            canvas->drawString(I18N::get(TXT_RF_NO_PASS_IDLE), width / 2, height / 2 - 8);
            canvas->setTextColor(0x4208);
            canvas->drawString(I18N::get(TXT_RF_AUTO_TRIGGER_TIP), width / 2, height / 2 + 8);
        } else {
            // A1. 方位与仰角 (Y = 24)
            canvas->setTextDatum(TL_DATUM);
            canvas->setTextColor(0xCE79);
            char azBuf[36];
            snprintf(azBuf, sizeof(azBuf), "%s: %03.0f° %s", I18N::get(TXT_RF_AZ), track.currentAz, getCompass8Dir(track.currentAz));
            canvas->drawString(azBuf, 8, 24);

            canvas->setTextDatum(TR_DATUM);
            char elBuf[36];
            uint16_t elCol = 0x7BEF;
            bool showArrow = false;
            bool isRising = track.isRising;

            if (track.currentEl > 0.0f) {
                elCol = isRising ? 0x07E0 : TFT_YELLOW;
                showArrow = true;
                snprintf(elBuf, sizeof(elBuf), "%s: %+.1f°", I18N::get(TXT_RF_EL), track.currentEl);
            } else {
                snprintf(elBuf, sizeof(elBuf), "%s: %+.1f° (待出圈)", I18N::get(TXT_RF_EL), track.currentEl);
            }

            canvas->setTextColor(elCol);
            if (showArrow) {
                int textRightX = width - 18;
                canvas->drawString(elBuf, textRightX, 24);
                int triX = width - 11;
                int triY = 24 + 5;
                if (isRising) {
                    canvas->fillTriangle(triX - 3, triY + 2, triX + 3, triY + 2, triX, triY - 3, 0x07E0);
                } else {
                    canvas->fillTriangle(triX - 3, triY - 3, triX + 3, triY - 3, triX, triY + 2, TFT_YELLOW);
                }
            } else {
                canvas->drawString(elBuf, width - 8, 24);
            }

            // A2. 峰值仰角与多普勒频移 (Y = 38)
            canvas->setTextDatum(TL_DATUM);
            canvas->setTextColor(0xFFE0);
            char maxBuf[32];
            snprintf(maxBuf, sizeof(maxBuf), "%s: %.1f°", I18N::get(TXT_RF_MAX_EL), track.maxEl);
            canvas->drawString(maxBuf, 8, 38);

            canvas->setTextDatum(TR_DATUM);
            char dopBuf[40];
            uint16_t dopCol = (track.dopplerHz < -10.0f) ? 0xFBE0 : ((track.dopplerHz > 10.0f) ? 0x07FF : 0xFFFF);
            float actualFreq = track.baseFreqMHz + (track.dopplerHz / 1e6f);
            if (abs(track.dopplerHz) >= 1000.0f) {
                snprintf(dopBuf, sizeof(dopBuf), "%s: %+.1fk (%.3fM)", I18N::get(TXT_RF_DOPPLER), track.dopplerHz / 1000.0f, actualFreq);
            } else {
                snprintf(dopBuf, sizeof(dopBuf), "%s: %+.0fHz (%.3fM)", I18N::get(TXT_RF_DOPPLER), track.dopplerHz, actualFreq);
            }
            canvas->setTextColor(dopCol);
            canvas->drawString(dopBuf, width - 8, 38);

            // A3. 宽幅时间轴仪表 (Y = 53 ~ 79)
            int trackX = 14;
            int trackW = width - 28;
            int trackY = 64;

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
            int32_t remSec = (int32_t)totalDur - elapsedSec;

            float progressRatio = (float)elapsedSec / (float)totalDur;
            if (progressRatio < 0.0f) progressRatio = 0.0f;
            if (progressRatio > 1.0f) progressRatio = 1.0f;

            canvas->fillRect(trackX, trackY - 1, trackW, 3, canvas->color565(30, 45, 60));
            int fillTrackW = (int)(trackW * progressRatio);
            if (fillTrackW > 0) {
                canvas->fillRect(trackX, trackY - 1, fillTrackW, 3, canvas->color565(0, 180, 220));
            }

            int tcaX = trackX + trackW / 2;
            if (track.tcaTime > track.aosTime && track.losTime > track.aosTime) {
                float tcaRatio = (float)(track.tcaTime - track.aosTime) / (float)totalDur;
                if (tcaRatio >= 0.1f && tcaRatio <= 0.9f) {
                    tcaX = trackX + (int)(trackW * tcaRatio);
                }
            }
            canvas->drawCircle(tcaX, trackY, 3, TFT_YELLOW);

            int curX = trackX + fillTrackW;
            SatIconType satIcon = ICON_SATELLITE;
            const EncyclopediaEntry* entry = Encyclopedia::getEntryByNorad(track.satNorad);
            if (entry) satIcon = entry->icon;
            drawTimelineSatellite(canvas, curX, trackY, satIcon, rm.isListening(), track.isRising);

            canvas->setTextDatum(TL_DATUM);
            canvas->setTextColor(0x7BEF);
            canvas->drawString("AOS", trackX, trackY - 12);
            canvas->setTextDatum(TC_DATUM);
            canvas->setTextColor(TFT_YELLOW);
            canvas->drawString("TCA", tcaX, trackY - 12);
            canvas->setTextDatum(TR_DATUM);
            canvas->setTextColor(0x7BEF);
            canvas->drawString("LOS", trackX + trackW, trackY - 12);

            canvas->setTextDatum(TL_DATUM);
            char passedBuf[32];
            snprintf(passedBuf, sizeof(passedBuf), "%s %02d:%02d (%d%%)", I18N::get(TXT_RF_PASSED), elapsedSec / 60, elapsedSec % 60, (int)(progressRatio * 100));
            canvas->setTextColor(0x07E0);
            canvas->drawString(passedBuf, trackX, trackY + 7);

            canvas->setTextDatum(TR_DATUM);
            char remBuf[32];
            snprintf(remBuf, sizeof(remBuf), "%s %02d:%02d", I18N::get(TXT_RF_REMAIN), remSec / 60, remSec % 60);
            canvas->setTextColor(0xFFE0);
            canvas->drawString(remBuf, trackX + trackW, trackY + 7);

            // A4. 天线朝向指南卡片 (Y = 87)
            canvas->drawFastHLine(10, 86, width - 20, canvas->color565(35, 45, 55));
            canvas->setTextDatum(TL_DATUM);
            canvas->setTextColor(0x07FF);
            char antBuf[64];
            int elTarget = (int)track.currentEl;
            if (elTarget < 0) elTarget = 0;
            snprintf(antBuf, sizeof(antBuf), "%s: %s %03.0f° / %s %02d°", I18N::get(TXT_RF_ANTENNA_DIR), getCompass8Dir(track.currentAz), track.currentAz, I18N::get(TXT_RF_EL), elTarget);
            canvas->drawString(antBuf, 10, 90);

            // A5. 时间校准状态底框 (Y = 106 ~ 128)
            canvas->fillRoundRect(8, 106, width - 16, 22, 4, canvas->color565(18, 26, 36));
            canvas->drawRoundRect(8, 106, width - 16, 22, 4, canvas->color565(40, 55, 75));

            canvas->setTextDatum(MC_DATUM);
            char calibBuf[48];
            if (track.timeOffsetSec != 0) {
                canvas->setTextColor(TFT_YELLOW);
                snprintf(calibBuf, sizeof(calibBuf), "%s: %+ds", I18N::get(TXT_RF_TIME_CALIB), track.timeOffsetSec);
            } else {
                canvas->setTextColor(0x07E0);
                snprintf(calibBuf, sizeof(calibBuf), "%s: 0s [%s]", I18N::get(TXT_RF_TIME_CALIB), 
                         I18N::getLanguage() == LANG_ZH ? "实时" : "Realtime");
            }
            canvas->drawString(calibBuf, width / 2, 117);
        }
    } else {
        // ==========================================
        // 状态 B：已捕获报文列表 + 底部紧凑过境监视栏
        // ==========================================
        if (_selectedPacketIndex >= (int)packets.size()) {
            _selectedPacketIndex = (int)packets.size() - 1;
        }
        if (_selectedPacketIndex < 0) _selectedPacketIndex = 0;

        int listY = 21;
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

            // 序号
            canvas->setTextColor(isSelected ? 0xFFFF : 0x07E0);
            char prefix[16];
            snprintf(prefix, sizeof(prefix), "#%02d", rowIdx + 1);
            canvas->drawString(prefix, 5, rowY + 3);

            // 卫星名
            canvas->setTextColor(isSelected ? 0x07FF : 0xC618);
            String satInfo = p.decoded.satName;
            canvas->setClipRect(25, rowY, 46, 15);
            canvas->drawString(satInfo, 25, rowY + 3);
            canvas->clearClipRect();

            // 射频指标
            canvas->setTextColor(0xCE79);
            char metrics[32];
            snprintf(metrics, sizeof(metrics), "%ddB/%.0fdB %dB", (int)p.raw.rssi, p.raw.snr, (int)p.raw.length);
            canvas->setClipRect(74, rowY, 64, 15);
            canvas->drawString(metrics, 74, rowY + 3);
            canvas->clearClipRect();

            // 字段预览
            if (!p.decoded.fields.empty()) {
                canvas->setTextColor(0xFFE0);
                String fPreview = p.decoded.fields[0].key + ":" + p.decoded.fields[0].value + p.decoded.fields[0].unit;
                canvas->setClipRect(140, rowY, width - 144, 15);
                canvas->drawString(fPreview.c_str(), 140, rowY + 3);
                canvas->clearClipRect();
            }
        }

        // B2. 底部紧凑过境状态监视栏 (Y = 106 ~ 134)
        canvas->fillRect(0, 106, width, 29, canvas->color565(12, 18, 25));
        canvas->drawFastHLine(0, 106, width, canvas->color565(35, 50, 68));

        int barX = 34;
        int barW = 150;
        int barY = 111;
        canvas->fillRect(barX, barY - 1, barW, 2, canvas->color565(35, 45, 55));

        canvas->setTextDatum(TL_DATUM);
        canvas->setTextColor(0x7BEF);
        canvas->drawString("AOS", 8, barY - 4);
        canvas->setTextDatum(TR_DATUM);
        canvas->drawString("LOS", width - 8, barY - 4);

        float pRatio = 0.5f;
        if (track.maxEl > 1.0f) {
            float elFrac = track.currentEl > 0 ? (track.currentEl / track.maxEl) : 0.05f;
            pRatio = track.isRising ? (elFrac * 0.5f) : (0.5f + (1.0f - elFrac) * 0.5f);
        }
        if (pRatio < 0.0f) pRatio = 0.0f;
        if (pRatio > 1.0f) pRatio = 1.0f;

        canvas->fillRect(barX, barY - 1, (int)(barW * pRatio), 2, 0x07E0);
        canvas->fillCircle(barX + (int)(barW * pRatio), barY, 3, 0x07E0);
        canvas->drawCircle(barX + barW / 2, barY, 2, TFT_YELLOW);

        // 下层关键数值
        canvas->setTextDatum(TL_DATUM);
        char dynBuf[48];
        snprintf(dynBuf, sizeof(dynBuf), "El:%02d°/%02d° Az:%03.0f° Dop:%+.1fk",
                 (int)track.currentEl, (int)track.maxEl, track.currentAz, track.dopplerHz / 1000.0f);
        canvas->setTextColor(0x07FF);
        canvas->drawString(dynBuf, 8, 119);

        if (track.timeOffsetSec != 0) {
            canvas->setTextDatum(TR_DATUM);
            char keyBuf[20];
            snprintf(keyBuf, sizeof(keyBuf), "%+ds", track.timeOffsetSec);
            canvas->setTextColor(TFT_YELLOW);
            canvas->drawString(keyBuf, width - 8, 119);
        }
    }

    // 4. 详情弹窗 (Detail Modal)
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

    // 5. 删除报文确认弹窗
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

    canvas->setTextDatum(TL_DATUM);
    canvas->clearClipRect();
}
