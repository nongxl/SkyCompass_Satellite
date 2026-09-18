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
    canvas->drawFastVLine(cx, cy - 8, 3, pointerCol);
    canvas->drawFastVLine(cx, cy + 6, 3, pointerCol);

    // 呼吸动态信标光点
    bool pulse = ((millis() / 400) % 2 == 0);
    uint16_t beaconCol = isListening ? (pulse ? 0x07E0 : 0x04A0) : 0x07FF;

    static LGFX_Sprite* s_satSprite = nullptr;
    if (!s_satSprite) {
        s_satSprite = new LGFX_Sprite(canvas);
        s_satSprite->setColorDepth(16);
        s_satSprite->createSprite(26, 26);
        s_satSprite->setPivot(13, 13);
    }

    const uint16_t CHROMA = 0x0001;
    s_satSprite->fillScreen(CHROMA);

    int scx = 13;
    int scy = 13;

    if (icon == ICON_STATION) {
        s_satSprite->fillRect(scx - 3, scy - 1, 7, 3, 0xD6BA);
        s_satSprite->fillRect(scx - 1, scy - 2, 3, 5, 0xE71C);
        s_satSprite->drawFastHLine(scx - 9, scy, 19, 0x7BEF);
        s_satSprite->fillRect(scx - 9, scy - 4, 4, 9, 0x1B3F);
        s_satSprite->drawRect(scx - 9, scy - 4, 4, 9, 0xFDA0);
        s_satSprite->drawFastHLine(scx - 9, scy, 4, TFT_BLACK);
        s_satSprite->fillRect(scx + 6, scy - 4, 4, 9, 0x1B3F);
        s_satSprite->drawRect(scx + 6, scy - 4, 4, 9, 0xFDA0);
        s_satSprite->drawFastHLine(scx + 6, scy, 4, TFT_BLACK);
        s_satSprite->drawPixel(scx, scy, beaconCol);
    } else if (icon == ICON_DFH1) {
        s_satSprite->fillCircle(scx, scy, 3, 0xD6BA);
        s_satSprite->drawCircle(scx, scy, 3, 0x7BEF);
        s_satSprite->drawPixel(scx - 1, scy - 1, 0xFFFF);
        s_satSprite->drawLine(scx - 2, scy - 2, scx - 6, scy - 5, TFT_WHITE);
        s_satSprite->drawLine(scx + 2, scy - 2, scx + 6, scy - 5, TFT_WHITE);
        s_satSprite->drawLine(scx - 2, scy + 2, scx - 6, scy + 5, TFT_WHITE);
        s_satSprite->drawLine(scx + 2, scy + 2, scx + 6, scy + 5, TFT_WHITE);
        s_satSprite->drawPixel(scx, scy, beaconCol);
    } else if (icon == ICON_WEATHER) {
        s_satSprite->fillRect(scx - 2, scy - 2, 4, 5, 0xCE79);
        s_satSprite->fillRect(scx - 8, scy - 4, 5, 9, 0x0AD5);
        s_satSprite->drawRect(scx - 8, scy - 4, 5, 9, 0x345F);
        s_satSprite->drawFastHLine(scx - 8, scy, 5, TFT_BLACK);
        s_satSprite->fillRect(scx + 2, scy - 1, 3, 3, 0x4208);
        s_satSprite->drawPixel(scx + 5, scy, beaconCol);
    } else if (icon == ICON_NAVIGATION) {
        s_satSprite->fillRect(scx - 2, scy - 3, 5, 7, 0xD6BA);
        s_satSprite->fillRect(scx - 8, scy - 2, 5, 5, 0x1B3F);
        s_satSprite->drawRect(scx - 8, scy - 2, 5, 5, 0xFDA0);
        s_satSprite->fillRect(scx + 4, scy - 2, 5, 5, 0x1B3F);
        s_satSprite->drawRect(scx + 4, scy - 2, 5, 5, 0xFDA0);
        s_satSprite->drawFastHLine(scx - 2, scy + 4, 5, TFT_WHITE);
        s_satSprite->drawPixel(scx, scy, beaconCol);
    } else {
        s_satSprite->fillRect(scx - 2, scy - 2, 5, 5, 0xD6BA);
        s_satSprite->drawRect(scx - 2, scy - 2, 5, 5, 0x07FF);
        s_satSprite->drawFastHLine(scx - 7, scy, 5, 0x1B3F);
        s_satSprite->drawFastHLine(scx + 3, scy, 4, 0x01EF);
        s_satSprite->drawFastHLine(scx - 3, scy, 7, 0xBDF7);
        s_satSprite->fillRect(scx - 2, scy - 2, 5, 5, 0xFEA0);
        s_satSprite->fillRect(scx - 1, scy - 1, 3, 3, 0xFFFF);
        s_satSprite->drawFastVLine(scx, scy + 3, 2, TFT_WHITE);
        s_satSprite->drawPixel(scx, scy + 4, beaconCol);
    }

    // 旋转 45° 呈现沿轨滑行姿态
    s_satSprite->pushRotateZoom(canvas, (float)cx, (float)cy, 45.0f, 1.0f, 1.0f, CHROMA);
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
        addRssiSample(-92.0f);
        addRssiSample(-68.0f);
        addRssiSample(-88.0f);
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

// 热力图色彩映射：基准为鲜明橙色，信号越强越向纯红与炽红偏移
static uint16_t getWaterfallHeatColor(float norm, bool isGlowLine) {
    uint8_t r, g, b;
    if (norm < 0.20f) {
        // 微弱底噪：暗琥珀橙
        float t = norm / 0.20f;
        r = (uint8_t)(80 + 100 * t);  // 80 -> 180
        g = (uint8_t)(35 + 45 * t);   // 35 -> 80
        b = 0;
    } else if (norm < 0.55f) {
        // 中低信号：标准热力图鲜明橙色
        float t = (norm - 0.20f) / 0.35f;
        r = (uint8_t)(180 + 75 * t);  // 180 -> 255
        g = (uint8_t)(80 + 40 * t);   // 80 -> 120
        b = 0;
    } else if (norm < 0.80f) {
        // 中高信号：鲜橙向橙红与炽红过渡
        float t = (norm - 0.55f) / 0.25f;
        r = 255;
        g = (uint8_t)(120 * (1.0f - t) + 20 * t); // 120 -> 20 (绿光衰减，快速转红)
        b = (uint8_t)(5 * t);
    } else {
        // 极强信号：峰值纯红与炽烈高光
        float t = (norm - 0.80f) / 0.20f;
        r = 255;
        g = (uint8_t)(20 * (1.0f - t)); // 20 -> 0 (完全纯红)
        b = (uint8_t)(10 + 35 * t);     // 10 -> 45
    }

    if (!isGlowLine) {
        // 背景柱体调低亮度（约 35% 浓度），呈半透明深邃热力图光晕，保证前景数据完全清晰可辨
        r = (uint8_t)(r * 0.35f);
        g = (uint8_t)(g * 0.35f);
        b = (uint8_t)(b * 0.35f);
    }

    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void RfConsoleView::drawBackgroundWaterfall(LGFX_Sprite* canvas, int width, int height) {
    int bgY = 22;
    int bgH = height - bgY; // 113px

    // 将采样数据渲染为热力图色彩的深邃背景频谱波形
    int prevX = -1;
    int prevY = -1;

    for (int i = 0; i < WATERFALL_POINTS; i++) {
        int readIdx = (_historyHead + i) % WATERFALL_POINTS;
        float rVal = _rssiHistory[readIdx];

        // 归一化到背景高度 ( -125dBm 底噪为底, -55dBm 强信号为顶 )
        float norm = (rVal - (-125.0f)) / 70.0f;
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;

        int px = i * 2;
        int py = height - 1 - (int)(norm * (bgH - 6));

        // 1. 背景能量柱 (热力图：橙色至红色半透明光晕柱)
        if (norm > 0.04f) {
            uint16_t col = getWaterfallHeatColor(norm, false);
            int colH = height - 1 - py;
            if (colH > 0) {
                canvas->drawFastVLine(px, py, colH, col);
                canvas->drawFastVLine(px + 1, py, colH, col);
            }
        }

        // 2. 轮廓辉光线 (热力图：鲜亮橙色，信号越高向红色偏移)
        uint16_t lineCol = getWaterfallHeatColor(norm, true);
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
        float currentRssi = -122.0f;
        if (rm.isHardwareReady() && rm.isListening()) {
            currentRssi = HalRadio::getInstance().getInstantRSSI();
            if (currentRssi >= -10.0f || currentRssi < -135.0f) {
                currentRssi = -122.0f + (float)(rand() % 6 - 3);
            }
        } else {
            currentRssi = -124.0f + (float)(rand() % 4 - 2);
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

    if (!rm.isHardwareReady()) {
        canvas->setTextDatum(MC_DATUM);
        canvas->setTextColor(0x7BEF);
        canvas->drawString(I18N::get(TXT_RF_REQ_MODULE), width / 2, height / 2 - 8);
        canvas->setTextColor(0x4208);
        canvas->drawString(I18N::get(TXT_RF_ENABLE_IN_WIZARD), width / 2, height / 2 + 8);
    } else {
        // ===================================================================
        // 顶部精简雷达与时间轴监控区 (Y = 20 ~ 71)
        // ===================================================================

        // 1. 方位、仰角与峰值仰角 (Y = 20)
        canvas->setTextDatum(TL_DATUM);
        canvas->setTextColor(0xCE79);
        char azBuf[36];
        snprintf(azBuf, sizeof(azBuf), "%s:%03.0f° %s", I18N::get(TXT_RF_AZ), track.currentAz, getCompass8Dir(track.currentAz));
        canvas->drawString(azBuf, 6, 20);

        char elBuf[32];
        uint16_t elCol = 0x7BEF;
        bool isRising = track.isRising;
        if (track.currentEl > 0.0f) {
            elCol = isRising ? 0x07E0 : TFT_YELLOW;
            snprintf(elBuf, sizeof(elBuf), "%s:%+.1f°%s", I18N::get(TXT_RF_EL), track.currentEl, isRising ? "^" : "v");
        } else {
            snprintf(elBuf, sizeof(elBuf), "%s:%+.1f°", I18N::get(TXT_RF_EL), track.currentEl);
        }
        canvas->setTextDatum(TC_DATUM);
        canvas->setTextColor(elCol);
        canvas->drawString(elBuf, 118, 20);

        canvas->setTextDatum(TR_DATUM);
        canvas->setTextColor(0xFFE0);
        char maxBuf[32];
        snprintf(maxBuf, sizeof(maxBuf), "%s:%.1f°", I18N::get(TXT_RF_MAX_EL), track.maxEl);
        canvas->drawString(maxBuf, width - 6, 20);

        // 2. 天线朝向推荐与多普勒频移 (Y = 31)
        canvas->setTextDatum(TL_DATUM);
        canvas->setTextColor(0x07FF);
        char antBuf[48];
        int elTarget = (int)track.currentEl;
        if (elTarget < 0) elTarget = 0;
        snprintf(antBuf, sizeof(antBuf), "%s: %s %03.0f°/%02d°", I18N::get(TXT_RF_ANTENNA_DIR), getCompass8Dir(track.currentAz), track.currentAz, elTarget);
        canvas->drawString(antBuf, 6, 31);

        canvas->setTextDatum(TR_DATUM);
        char dopBuf[40];
        uint16_t dopCol = (track.dopplerHz < -10.0f) ? 0xFBE0 : ((track.dopplerHz > 10.0f) ? 0x07FF : 0xFFFF);
        float actualFreq = track.baseFreqMHz + (track.dopplerHz / 1e6f);
        if (abs(track.dopplerHz) >= 1000.0f) {
            snprintf(dopBuf, sizeof(dopBuf), "%s:%+.1fk (%.3fM)", I18N::get(TXT_RF_DOPPLER), track.dopplerHz / 1000.0f, actualFreq);
        } else {
            snprintf(dopBuf, sizeof(dopBuf), "%s:%+.0fHz (%.3fM)", I18N::get(TXT_RF_DOPPLER), track.dopplerHz, actualFreq);
        }
        canvas->setTextColor(dopCol);
        canvas->drawString(dopBuf, width - 6, 31);

        // 3. 紧凑时间轴仪表 (Y = 42 ~ 69)
        int trackX = 12;
        int trackW = width - 24;
        int trackY = 53;

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

        canvas->fillRect(trackX, trackY - 1, trackW, 2, canvas->color565(30, 45, 60));
        int fillTrackW = (int)(trackW * progressRatio);
        if (fillTrackW > 0) {
            canvas->fillRect(trackX, trackY - 1, fillTrackW, 2, canvas->color565(0, 180, 220));
        }

        int tcaX = trackX + trackW / 2;
        if (track.tcaTime > track.aosTime && track.losTime > track.aosTime) {
            float tcaRatio = (float)(track.tcaTime - track.aosTime) / (float)totalDur;
            if (tcaRatio >= 0.1f && tcaRatio <= 0.9f) {
                tcaX = trackX + (int)(trackW * tcaRatio);
            }
        }
        canvas->drawCircle(tcaX, trackY, 2, TFT_YELLOW);

        int curX = trackX + fillTrackW;
        SatIconType satIcon = ICON_SATELLITE;
        const EncyclopediaEntry* entry = Encyclopedia::getEntryByNorad(track.satNorad);
        if (entry) satIcon = entry->icon;
        drawTimelineSatellite(canvas, curX, trackY, satIcon, rm.isListening(), track.isRising);

        canvas->setTextDatum(TL_DATUM);
        canvas->setTextColor(0x7BEF);
        canvas->drawString("AOS", trackX, trackY - 10);
        canvas->setTextDatum(TC_DATUM);
        canvas->setTextColor(TFT_YELLOW);
        canvas->drawString("TCA", tcaX, trackY - 10);
        canvas->setTextDatum(TR_DATUM);
        canvas->setTextColor(0x7BEF);
        canvas->drawString("LOS", trackX + trackW, trackY - 10);

        canvas->setTextDatum(TL_DATUM);
        char passedBuf[32];
        snprintf(passedBuf, sizeof(passedBuf), "%02d:%02d (%d%%)", elapsedSec / 60, elapsedSec % 60, (int)(progressRatio * 100));
        canvas->setTextColor(0x07E0);
        canvas->drawString(passedBuf, trackX, trackY + 7);

        canvas->setTextDatum(TR_DATUM);
        char remBuf[32];
        snprintf(remBuf, sizeof(remBuf), "%02d:%02d", remSec / 60, remSec % 60);
        canvas->setTextColor(0xFFE0);
        canvas->drawString(remBuf, trackX + trackW, trackY + 7);

        // 在时间轴卫星图标正下方显示时间校准偏差（如 +20s 或 -15s），不占用额外底框空间
        if (track.timeOffsetSec != 0) {
            char calibBuf[16];
            snprintf(calibBuf, sizeof(calibBuf), "%+ds", track.timeOffsetSec);
            int tw = canvas->textWidth(calibBuf);
            int offX = curX;
            if (offX - tw / 2 < 68) offX = 68 + tw / 2;
            if (offX + tw / 2 > width - 60) offX = width - 60 - tw / 2;
            canvas->setTextDatum(TC_DATUM);
            canvas->setTextColor(TFT_YELLOW);
            canvas->drawString(calibBuf, offX, trackY + 7);
        }

        // 4. 分隔细线 (Y = 71)
        canvas->drawFastHLine(4, 71, width - 8, canvas->color565(35, 50, 68));

        // ===================================================================
        // 底部数据包列表区 (Y = 73 ~ 134，高度 61px，可容纳 4 行抓包)
        // ===================================================================
        if (!packets.empty()) {
            if (_selectedPacketIndex >= (int)packets.size()) {
                _selectedPacketIndex = (int)packets.size() - 1;
            }
            if (_selectedPacketIndex < 0) _selectedPacketIndex = 0;

            int listY = 73;
            int rowH = 14;
            int visibleRows = 4;
            int startIdx = 0;
            if (_selectedPacketIndex >= visibleRows) {
                startIdx = _selectedPacketIndex - visibleRows + 1;
            }

            for (int i = 0; i < visibleRows && (startIdx + i) < (int)packets.size(); i++) {
                int rowIdx = startIdx + i;
                int rowY = listY + i * rowH;
                bool isSelected = (rowIdx == _selectedPacketIndex);

                if (isSelected) {
                    canvas->fillRect(2, rowY, width - 4, rowH, 0x2145);
                    canvas->drawRect(2, rowY, width - 4, rowH, 0x07FF);
                }

                const auto& p = packets[rowIdx];
                canvas->setTextDatum(TL_DATUM);

                // 序号
                canvas->setTextColor(isSelected ? 0xFFFF : 0x07E0);
                char prefix[16];
                snprintf(prefix, sizeof(prefix), "#%02d", rowIdx + 1);
                canvas->drawString(prefix, 4, rowY + 2);

                // 卫星名
                canvas->setTextColor(isSelected ? 0x07FF : 0xC618);
                String satInfo = p.decoded.satName;
                canvas->setClipRect(24, rowY, 48, rowH);
                canvas->drawString(satInfo, 24, rowY + 2);
                canvas->clearClipRect();

                // 射频指标
                canvas->setTextColor(0xCE79);
                char metrics[32];
                snprintf(metrics, sizeof(metrics), "%ddB/%.0fdB %dB", (int)p.raw.rssi, p.raw.snr, (int)p.raw.length);
                canvas->setClipRect(74, rowY, 68, rowH);
                canvas->drawString(metrics, 74, rowY + 2);
                canvas->clearClipRect();

                // 字段预览
                if (!p.decoded.fields.empty()) {
                    canvas->setTextColor(0xFFE0);
                    String fPreview = p.decoded.fields[0].key + ":" + p.decoded.fields[0].value + p.decoded.fields[0].unit;
                    canvas->setClipRect(144, rowY, width - 148, rowH);
                    canvas->drawString(fPreview.c_str(), 144, rowY + 2);
                    canvas->clearClipRect();
                }
            }
        } else {
            // 空闲等待提示卡片 (纯净优雅)
            canvas->setTextDatum(MC_DATUM);
            canvas->setTextColor(0x7BEF);
            canvas->drawString(I18N::get(TXT_RF_WAITING_PACKETS), width / 2, 93);
            canvas->setTextColor(0x4208);
            canvas->drawString(I18N::getLanguage() == LANG_ZH ? "[ 按 T 键注入测试遥测包 ]" : "[ Press T to inject test packet ]", width / 2, 109);
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
