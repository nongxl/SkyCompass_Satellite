#include "rf_console_view.h"
#include "../core/radio_manager.h"
#include "../core/i18n.h"
#include "../core/encyclopedia.h"
#include "../core/position_manager.h"
#include "../core/earth_renderer.h"

extern PositionManager* pos_manager;
extern double baseUserLat;
extern double baseUserLon;

RfConsoleView::RfConsoleView() {
    for (int i = 0; i < WATERFALL_POINTS; i++) {
        _rssiHistory[i] = -120.0f + (float)(rand() % 6 - 3);
    }
}

void RfConsoleView::addRssiSample(float rssi) {
    if (rssi < -135.0f) rssi = -135.0f;
    if (rssi > -40.0f) rssi = -40.0f;
    _lastInstantRssi = rssi;
    _rssiHistory[_historyHead] = rssi;
    _historyHead = (_historyHead + 1) % WATERFALL_POINTS;
}

static void drawTimelineSatellite(LGFX_Sprite* canvas, int cx, int cy, SatIconType icon, uint16_t satColor, bool isListening, bool isRising) {
    if (!canvas) return;

    // 1. 垂直对准微刻度线（上下两条指引微刻度针）
    uint16_t pointerCol = isListening ? 0x07E0 : 0x07FF;
    canvas->drawFastVLine(cx, cy - 8, 3, pointerCol);
    canvas->drawFastVLine(cx, cy + 6, 3, pointerCol);

    // 2. 呼吸动态信标光点
    bool pulse = ((millis() / 400) % 2 == 0);
    uint16_t beaconCol = isListening ? (pulse ? 0x07E0 : 0x04A0) : 0x07FF;

    // 3. 卫星图标旋转 45° 呈现生动空间飞行姿态
    uint16_t drawColor = (satColor != 0) ? satColor : 0x07FF;

    static LGFX_Sprite s_satRotSprite;
    static bool s_rotInit = false;
    if (!s_rotInit) {
        s_satRotSprite.setColorDepth(16);
        s_satRotSprite.createSprite(24, 24);
        s_satRotSprite.setPivot(12, 12);
        s_rotInit = true;
    }

    const uint16_t TRANSP_KEY = 0x0001; // 透明键值
    s_satRotSprite.fillScreen(TRANSP_KEY);
    EarthRenderer::renderSatelliteIcon(&s_satRotSprite, 12, 12, icon, drawColor, false, 1.0f);
    s_satRotSprite.pushRotateZoom(canvas, cx, cy, 45.0f, 1.0f, 1.0f, TRANSP_KEY);

    // 4. 在卫星核心打上动态信标光点
    canvas->drawPixel(cx, cy, beaconCol);
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

    // 绘制极简暗态参考线与微型刻度标尺 (-80dBm 强信号门限 与 -105dBm 弱信号/卫星门限)
    auto drawDottedRefLine = [&](float dbm, const char* label) {
        float norm = (dbm - (-125.0f)) / 70.0f;
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;
        int ly = height - 1 - (int)(norm * (bgH - 6));
        uint16_t lineColor = canvas->color565(22, 32, 44); // 极暗灰蓝，完全不干扰前景
        for (int x = 2; x < width - 26; x += 4) {
            canvas->drawPixel(x, ly, lineColor);
        }
        canvas->setTextDatum(MR_DATUM);
        canvas->setTextColor(canvas->color565(55, 75, 95));
        canvas->drawString(label, width - 4, ly);
    };

    drawDottedRefLine(-80.0f, "-80");
    drawDottedRefLine(-105.0f, "-105");

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

    RadioManager& rm = RadioManager::getInstance();
    const auto& track = rm.getTrackingInfo();

    bool hasPassNow = track.hasPass && (track.satName.length() > 0);
    bool isUpcoming = track.isUpcoming && (track.satName.length() > 0);
    bool hasTarget = hasPassNow || isUpcoming;
    String activeName = hasTarget ? track.satName : "";

    // 1. 左上角标题呈现：有卫星目标（正在过境或下一次过境预位）时显示卫星名字；无过境时回退为“射频遥测终端”
    canvas->setTextDatum(TL_DATUM);
    canvas->setTextColor(0x07FF, 0x10A2);

    String leftTitleStr;
    if (hasTarget) {
        leftTitleStr = activeName;
    } else {
        leftTitleStr = I18N::get(TXT_RF_TITLE); // "射频遥测终端"
    }

    canvas->drawString(leftTitleStr.c_str(), 4, 3);
    int titleW = canvas->textWidth(leftTitleStr.c_str());
    int statX = 4 + titleW + 6;

    int maxStatW = width - statX - 90;
    if (maxStatW > 0) {
        canvas->setClipRect(statX, 0, maxStatW, 18);
    }

    if (!rm.isHardwareReady()) {
        canvas->setTextColor(0xF800, 0x10A2);
        canvas->drawString(I18N::get(TXT_RF_HW_NOT_DETECTED), statX, 3);
    } else if (hasPassNow && (rm.isListening() || track.hasPass)) {
        canvas->setTextColor(0x07E0, 0x10A2);
        canvas->drawString(I18N::get(TXT_RF_RX_ACTIVE), statX, 3);
    } else if (isUpcoming) {
        // 下一次过境预位状态：仅显示精简倒计时，不再重复频率，彻底释放顶栏空间
        canvas->setTextColor(0xFFE0, 0x10A2);
        int secToAos = (track.aosTime > track.currentSimTime) ? (int)(track.aosTime - track.currentSimTime) : 0;
        char waitBuf[32];
        int mm = secToAos / 60;
        int ss = secToAos % 60;
        if (mm >= 60) {
            int hh = mm / 60;
            mm = mm % 60;
            snprintf(waitBuf, sizeof(waitBuf), "[-%02d:%02d:%02d]", hh, mm, ss);
        } else {
            snprintf(waitBuf, sizeof(waitBuf), "[-%02d:%02d]", mm, ss);
        }
        canvas->drawString(waitBuf, statX, 3);
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

        // 1. 方位（左）+ 仰角/峰值仰角（右）合并一行 (Y = 20)
        canvas->setTextDatum(TL_DATUM);
        canvas->setTextColor(0xCE79);
        char azBuf[28];
        if (hasTarget) {
            snprintf(azBuf, sizeof(azBuf), "AZ:%03.0f°", track.currentAz);
        } else {
            snprintf(azBuf, sizeof(azBuf), "AZ:---°");
        }
        canvas->drawString(azBuf, 6, 20);

        // 内存使用率条下方中间 (X=width/2, Y=20)：显示实时对地斜距 (Slant Range)
        if (hasTarget && track.distanceKm > 0.0f) {
            char distBuf[24];
            snprintf(distBuf, sizeof(distBuf), "%.0fkm", track.distanceKm);
            canvas->setTextDatum(TC_DATUM);
            canvas->setTextColor(canvas->color565(0, 210, 255)); // 科技青色
            canvas->drawString(distBuf, width / 2, 20);
        }

        // 仰角 + 最大仰角合并右对齐，避免三列重叠
        bool isRising = track.isRising;
        uint16_t elCol = 0x7BEF;
        char elBuf[36];
        if (hasTarget) {
            if (track.currentEl > 0.0f) {
                elCol = isRising ? 0x07E0 : TFT_YELLOW;
                snprintf(elBuf, sizeof(elBuf), "EL:%+.1f°%s /%.0f°",
                         track.currentEl, isRising ? "↑" : "↓", track.maxEl);
            } else {
                // 地平线下（等待过境预位）：暗灰蓝色提示负仰角
                elCol = canvas->color565(80, 110, 140);
                snprintf(elBuf, sizeof(elBuf), "EL:%+.1f° /%.0f°",
                         track.currentEl, track.maxEl);
            }
        } else {
            snprintf(elBuf, sizeof(elBuf), "EL:---° /--°");
        }
        canvas->setTextDatum(TR_DATUM);
        canvas->setTextColor(elCol);
        canvas->drawString(elBuf, width - 6, 20);

        // 2. 天线指引（极简）+ 多普勒频移（无标签）+ 调制模式 (Y = 31)
        // 天线指引：去掉冗长前缀标签，只保留方向码+角度
        canvas->setTextDatum(TL_DATUM);
        canvas->setTextColor(0x07FF);
        char antBuf[32];
        if (hasTarget) {
            int elTarget = (int)track.currentEl;
            if (elTarget < 0) elTarget = 0;
            float az = track.currentAz;
            while (az < 0.0f) az += 360.0f;
            while (az >= 360.0f) az -= 360.0f;
            static const char* dirs8[8] = {"N","NE","E","SE","S","SW","W","NW"};
            const char* shortDir = dirs8[(int)((az + 22.5f) / 45.0f) % 8];
            if (isUpcoming) {
                snprintf(antBuf, sizeof(antBuf), "> PRE %s %03.0f°/%02d°", shortDir, track.currentAz, elTarget);
            } else {
                snprintf(antBuf, sizeof(antBuf), "> %s %03.0f°/%02d°", shortDir, track.currentAz, elTarget);
            }
        } else {
            snprintf(antBuf, sizeof(antBuf), "> STANDBY");
        }
        canvas->drawString(antBuf, 6, 31);

        // 多普勒与频率：显示频移值+实际频率+调制模式，颜色区分正负
        canvas->setTextDatum(TR_DATUM);
        char dopBuf[64];
        const char* modStr = (track.modulation.length() > 0) ? track.modulation.c_str() : "";
        if (hasTarget && track.baseFreqMHz > 0.0f) {
            if (isUpcoming) {
                if (strlen(modStr) > 0) {
                    snprintf(dopBuf, sizeof(dopBuf), "PRESET %.3fM %s", track.baseFreqMHz, modStr);
                } else {
                    snprintf(dopBuf, sizeof(dopBuf), "PRESET %.3fM", track.baseFreqMHz);
                }
                canvas->setTextColor(TFT_YELLOW);
            } else {
                uint16_t dopCol = (track.dopplerHz < -10.0f) ? 0xFBE0 : ((track.dopplerHz > 10.0f) ? 0x07FF : 0xFFFF);
                float actualFreq = track.baseFreqMHz + (track.dopplerHz / 1e6f);
                if (strlen(modStr) > 0) {
                    if (abs(track.dopplerHz) >= 1000.0f) {
                        snprintf(dopBuf, sizeof(dopBuf), "%+.1fkHz %.3fM %s", track.dopplerHz / 1000.0f, actualFreq, modStr);
                    } else {
                        snprintf(dopBuf, sizeof(dopBuf), "%+.0fHz %.3fM %s", track.dopplerHz, actualFreq, modStr);
                    }
                } else {
                    if (abs(track.dopplerHz) >= 1000.0f) {
                        snprintf(dopBuf, sizeof(dopBuf), "%+.1fkHz %.3fM", track.dopplerHz / 1000.0f, actualFreq);
                    } else {
                        snprintf(dopBuf, sizeof(dopBuf), "%+.0fHz %.3fM", track.dopplerHz, actualFreq);
                    }
                }
                canvas->setTextColor(dopCol);
            }
        } else {
            if (strlen(modStr) > 0) {
                snprintf(dopBuf, sizeof(dopBuf), "-- MHz %s", modStr);
            } else {
                snprintf(dopBuf, sizeof(dopBuf), "-- MHz");
            }
            canvas->setTextColor(0x7BEF);
        }
        canvas->drawString(dopBuf, width - 6, 31);

        // 3. 紧凑时间轴仪表 (Y = 41 ~ 69)
        int trackX = 6;
        int trackW = width - 12;
        int trackY = 53;

        if (hasTarget) {
            uint32_t totalDur = (track.losTime > track.aosTime) ? (track.losTime - track.aosTime) : 600;
            if (totalDur == 0) totalDur = 600;

            // TCA 位置计算 (绝对物理位置，不随时间补偿而漂移)
            int tcaX = trackX + trackW / 2;
            if (track.tcaTime > track.aosTime && track.losTime > track.aosTime) {
                float tcaRatio = (float)(track.tcaTime - track.aosTime) / (float)totalDur;
                if (tcaRatio >= 0.05f && tcaRatio <= 0.95f) {
                    tcaX = trackX + (int)(trackW * tcaRatio);
                }
            }

            // 标签上移到 Y = 41 (AOS 右侧增加进境方位角，LOS 左侧增加出境方位角)
            char aosLabel[32];
            if (track.aosAz > 0.0f || track.losAz > 0.0f) {
                snprintf(aosLabel, sizeof(aosLabel), "AOS %03.0f°", track.aosAz);
            } else {
                snprintf(aosLabel, sizeof(aosLabel), "AOS");
            }
            canvas->setTextDatum(TL_DATUM);
            canvas->setTextColor(0x7BEF);
            canvas->drawString(aosLabel, trackX, 41);

            canvas->setTextDatum(TC_DATUM);
            canvas->setTextColor(TFT_YELLOW);
            canvas->drawString("TCA", tcaX, 41);

            char losLabel[32];
            if (track.aosAz > 0.0f || track.losAz > 0.0f) {
                snprintf(losLabel, sizeof(losLabel), "%03.0f° LOS", track.losAz);
            } else {
                snprintf(losLabel, sizeof(losLabel), "LOS");
            }
            canvas->setTextDatum(TR_DATUM);
            canvas->setTextColor(0x7BEF);
            canvas->drawString(losLabel, trackX + trackW, 41);

            // 真实时间线进度：由当前仿真时间严格线性驱动，按 , / 增减补偿时图标毫秒级实时响应
            float progressRatio = 0.0f;
            if (track.losTime > track.aosTime && track.currentSimTime >= track.aosTime) {
                progressRatio = (float)((int64_t)track.currentSimTime - (int64_t)track.aosTime) / (float)totalDur;
            }
            if (progressRatio < 0.0f) progressRatio = 0.0f;
            if (progressRatio > 1.0f) progressRatio = 1.0f;

            // 时间轴底槽与已通过进度条
            canvas->fillRect(trackX, trackY - 1, trackW, 2, canvas->color565(30, 45, 60));
            int fillTrackW = (int)(trackW * progressRatio);
            if (fillTrackW > 0) {
                canvas->fillRect(trackX, trackY - 1, fillTrackW, 2, canvas->color565(0, 180, 220));
            }

            // TCA 黄色标记圆点
            canvas->drawCircle(tcaX, trackY, 2, TFT_YELLOW);

            // 卫星图标实体
            int curX = trackX + fillTrackW;
            SatIconType satIcon = track.satIconType;
            uint16_t satColor = track.satColor;
            if (satIcon == ICON_SATELLITE) {
                const EncyclopediaEntry* entry = Encyclopedia::getEntryByNorad(track.satNorad);
                if (entry) {
                    satIcon = entry->icon;
                    if (satColor == 0x07FF || satColor == 0) satColor = entry->color;
                }
            }
            drawTimelineSatellite(canvas, curX, trackY, satIcon, satColor, hasPassNow && rm.isListening(), track.isRising);

            // 时间轴起终点下方显示本地起止时刻 (Y = 58)
            int tzOffsetSec = pos_manager ? pos_manager->getTimezoneManager()->getTimezoneOffset(baseUserLat, baseUserLon) : 8 * 3600;
            char timeBuf[16];

            if (track.aosTime > 0) {
                time_t aosLocal = (time_t)track.aosTime + tzOffsetSec;
                struct tm tmAos;
                gmtime_r(&aosLocal, &tmAos);
                snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", tmAos.tm_hour, tmAos.tm_min, tmAos.tm_sec);
                canvas->setTextDatum(TL_DATUM);
                canvas->setTextColor(0x7BEF);
                canvas->drawString(timeBuf, trackX, 58);
            }

            if (track.losTime > 0) {
                time_t losLocal = (time_t)track.losTime + tzOffsetSec;
                struct tm tmLos;
                gmtime_r(&losLocal, &tmLos);
                snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", tmLos.tm_hour, tmLos.tm_min, tmLos.tm_sec);
                canvas->setTextDatum(TR_DATUM);
                canvas->setTextColor(0x7BEF);
                canvas->drawString(timeBuf, trackX + trackW, 58);
            }

            // 在时间轴卫星图标正下方居中跟随显示时间校准偏差（如 +20s 或 -15s）
            if (track.timeOffsetSec != 0) {
                char calibBuf[16];
                snprintf(calibBuf, sizeof(calibBuf), "%+ds", track.timeOffsetSec);
                int tw = canvas->textWidth(calibBuf);
                int offX = curX;
                // 安全 clamp，避免与两端的起止时间重叠挤压
                if (offX - tw / 2 < trackX + 54) offX = trackX + 54 + tw / 2;
                if (offX + tw / 2 > trackX + trackW - 54) offX = trackX + trackW - 54 - tw / 2;
                canvas->setTextDatum(TC_DATUM);
                canvas->setTextColor(TFT_YELLOW);
                canvas->drawString(calibBuf, offX, 58);
            }
        } else {
            // 无过境卫星目标时：呈现优雅的静息等待时间槽与待机状态
            canvas->fillRect(trackX, trackY - 1, trackW, 2, canvas->color565(25, 35, 48));
            canvas->setTextDatum(MC_DATUM);
            canvas->setTextColor(0x4208);
            canvas->drawString(I18N::get(TXT_RF_IDLE_STANDBY), width / 2, trackY);
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
                    if (p.decoded.fields.size() > 1) {
                        fPreview += " " + p.decoded.fields[1].key + ":" + p.decoded.fields[1].value + p.decoded.fields[1].unit;
                    }
                    int maxClipW = width - 148;
                    if (i == visibleRows - 1) {
                        maxClipW = width - 148 - 66; // 为右下角常驻 RSSI 胶囊留出避让安全宽度
                    }
                    if (maxClipW > 10) {
                        canvas->setClipRect(144, rowY, maxClipW, rowH);
                        canvas->drawString(fPreview.c_str(), 144, rowY + 2);
                        canvas->clearClipRect();
                    }
                }
            }
        } else {
            // 空闲等待提示卡片 (纯净优雅，主次分明)
            canvas->setTextDatum(MC_DATUM);
            if (hasPassNow) {
                canvas->setTextColor(0x07FF);
                canvas->drawString(I18N::get(TXT_RF_TITLE), width / 2, 87);
                canvas->setTextColor(0x7BEF);
                canvas->drawString(I18N::get(TXT_RF_WAITING_PACKETS), width / 2, 101);
                canvas->setTextColor(0x4208);
                canvas->drawString(I18N::getLanguage() == LANG_ZH ? "[ 按 T 键注入测试遥测包 ]" : "[ Press T to inject test packet ]", width / 2, 115);
            } else if (isUpcoming) {
                int secToAos = (track.aosTime > track.currentSimTime) ? (int)(track.aosTime - track.currentSimTime) : 0;
                int mm = secToAos / 60;
                int ss = secToAos % 60;
                char upTitle[64];
                char upSub[64];
                snprintf(upTitle, sizeof(upTitle), "%s: %s", 
                         (I18N::getLanguage() == LANG_ZH ? "下一次过境目标" : "NEXT PASS TARGET"),
                         track.satName.c_str());
                if (mm >= 60) {
                    int hh = mm / 60;
                    mm = mm % 60;
                    snprintf(upSub, sizeof(upSub), (I18N::getLanguage() == LANG_ZH ? "预计 %d小时%02d分 后升空 (峰值仰角 %.0f°)" : "AOS in %dh%02dm (Peak El %.0f deg)"),
                             hh, mm, track.maxEl);
                } else {
                    snprintf(upSub, sizeof(upSub), (I18N::getLanguage() == LANG_ZH ? "预计 %02d分%02d秒 后升空 (峰值仰角 %.0f°)" : "AOS in %02dm%02ds (Peak El %.0f deg)"),
                             mm, ss, track.maxEl);
                }
                canvas->setTextColor(TFT_YELLOW);
                canvas->drawString(upTitle, width / 2, 87);
                canvas->setTextColor(0x7BEF);
                canvas->drawString(upSub, width / 2, 101);
                canvas->setTextColor(0xCE79);
                canvas->drawString(I18N::getLanguage() == LANG_ZH ? "[ 按 , / 键调整时间补偿 | 按 T 注入测试包 ]" : "[ Press , / to calibrate time | T to test ]", width / 2, 115);
            } else {
                canvas->setTextColor(TFT_YELLOW);
                canvas->drawString(I18N::get(TXT_RF_NO_PASS_IDLE), width / 2, 87);
                canvas->setTextColor(0x7BEF);
                canvas->drawString(I18N::get(TXT_RF_AUTO_TRIGGER_TIP), width / 2, 101);
                canvas->setTextColor(0x4208);
                canvas->drawString(I18N::getLanguage() == LANG_ZH ? "[ 按 T 键注入测试遥测包 ]" : "[ Press T to inject test packet ]", width / 2, 115);
            }
        }
    }

    // 4. 屏幕右下角常驻实时数字 RSSI 呈现 (无全屏模态弹窗时)
    if (!_showDetailModal && !_showDeleteModal) {
        char rssiStr[24];
        if (rm.isHardwareReady()) {
            snprintf(rssiStr, sizeof(rssiStr), "RSSI:%+.0fdBm", _lastInstantRssi);
        } else {
            snprintf(rssiStr, sizeof(rssiStr), "RSSI:--dBm");
        }
        canvas->setTextDatum(BR_DATUM);
        int tw = canvas->textWidth(rssiStr);
        int bx = width - tw - 6;
        int by = height - 12;
        int bw = tw + 5;
        int bh = 11;

        // 半透明微深底衬与精致发光细外框
        canvas->fillRect(bx, by, bw, bh, canvas->color565(10, 16, 24));
        canvas->drawRect(bx, by, bw, bh, canvas->color565(30, 48, 68));

        // 动态状态色彩：强信号亮红/橙，有效信号青绿，弱底噪柔和灰蓝
        uint16_t valColor = (_lastInstantRssi > -80.0f) ? 0xFBE0 : ((_lastInstantRssi > -100.0f) ? 0x07FF : 0x7BEF);
        if (!rm.isHardwareReady()) valColor = 0x52AA;
        canvas->setTextColor(valColor, canvas->color565(10, 16, 24));
        canvas->drawString(rssiStr, width - 4, height - 2);
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
        for (size_t f = 0; f < sel.decoded.fields.size() && f < 6; f += 2) {
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
