#include "rf_console_view.h"
#include "../core/radio_manager.h"
#include "../core/i18n.h"
#include "../core/encyclopedia.h"
#include "../core/position_manager.h"
#include "../core/earth_renderer.h"

extern PositionManager* pos_manager;
extern double baseUserLat;
extern double baseUserLon;

#include "../core/spring_animator.h"

// ===================================================================
// lilt 物理弹簧动效模型配置与状态管理
// ===================================================================
// 列表选中磁吸高亮框物理弹簧 (Default: 0.29s, 0.75 阻尼比，具有苹果级轻微物理回弹)
static SpringValue s_rfCursorY(73.0f, SpringConfig::Default);
// 详情弹窗中心 Pop-up 缩放物理动效 (0.0 -> 1.0, 阻尼 0.80)
static SpringValue s_detailModalScale(0.0f, SpringConfig(0.24f, 0.80f));
// 删除确认弹窗缩放物理动效 (0.0 -> 1.0, 阻尼 0.80)
static SpringValue s_deleteModalScale(0.0f, SpringConfig(0.24f, 0.80f));
// 详情弹窗内容翻页滚动物理弹簧 (Default: 0.29s, 0.75 阻尼比，具有 lilt 丝滑回弹)
static SpringValue s_detailScrollY(0.0f, SpringConfig::Default);
// 记录上次的数据包总数，用于检测新包入队物理震颤动效
static size_t s_lastTotalPackets = 0;
// 边界撞墙回弹触发器 (施加冲量)
static int s_boundaryBump = 0;

void RfConsoleView::resetAnimations() {
    s_rfCursorY.jump(73.0f);
    s_detailModalScale.jump(0.0f);
    s_deleteModalScale.jump(0.0f);
    s_detailScrollY.jump(0.0f);
    _detailScrollTarget = 0;
    _detailMaxScroll = 0;
    s_lastTotalPackets = RadioManager::getInstance().getTotalPacketsCount();
    s_boundaryBump = 0;
}

void RfConsoleView::setActive(bool active) {
    if (!_isActive && active) {
        resetAnimations();
    }
    _isActive = active;
}

void RfConsoleView::toggle() {
    setActive(!_isActive);
}

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

static inline void projectSkyToScreen(float az, float el, int cx, int cy, int r, int& outX, int& outY) {
    if (el < 0.0f) el = 0.0f;
    if (el > 90.0f) el = 90.0f;
    float dist = (float)r * (90.0f - el) / 90.0f;
    float rad = az * (3.1415926535f / 180.0f);
    outX = cx + (int)roundf(dist * sinf(rad));
    outY = cy - (int)roundf(dist * cosf(rad));
}

static void drawSkyRadarPlot(LGFX_Sprite* canvas, int cx, int cy, int r, const RadioTrackingInfo& track, bool hasTarget, bool hasPassNow, bool isUpcoming) {
    if (!canvas) return;

    // 1. 大圆圈底盘 (直径 130px, r = 65, 无内圈)
    canvas->drawCircle(cx, cy, r, canvas->color565(36, 52, 72));      // 外大圆圈
    
    // 天顶十字中心点 (EL = 90°)
    canvas->drawFastHLine(cx - 3, cy, 7, canvas->color565(36, 52, 72));
    canvas->drawFastVLine(cx, cy - 3, 7, canvas->color565(36, 52, 72));

    // 2. 线圈上留空隙避免和字母重叠 (分别在 N、S、W、E 处擦除线圈局部缺口)
    canvas->fillRect(cx - 5, cy - r - 2, 11, 5, 0x0000); // N 缺口
    canvas->fillRect(cx - 5, cy + r - 2, 11, 5, 0x0000); // S 缺口
    canvas->fillRect(cx - r - 2, cy - 5, 5, 11, 0x0000); // W 缺口
    canvas->fillRect(cx + r - 2, cy - 5, 5, 11, 0x0000); // E 缺口

    // 3. 在缺口中心在线圈上显示 NSWE 字母
    canvas->setTextSize(1);
    canvas->setTextColor(canvas->color565(90, 135, 185));
    canvas->setTextDatum(MC_DATUM);
    canvas->drawString("N", cx, cy - r);
    canvas->drawString("S", cx, cy + r);
    canvas->drawString("W", cx - r, cy);
    canvas->drawString("E", cx + r, cy);

    // 4. 过境卫星轨道弧线、运动方向箭头与关键节点 (AOS, TCA, LOS)
    if (hasTarget && track.orbitPointCount >= 2) {
        int prevX = -1, prevY = -1;

        // 运动方向箭头绘制
        auto drawArrowOnSegment = [&](int p1X, int p1Y, int p2X, int p2Y, uint16_t arrowColor) {
            float dx = (float)(p2X - p1X);
            float dy = (float)(p2Y - p1Y);
            float len = sqrtf(dx * dx + dy * dy);
            if (len < 3.0f) return;
            float ux = dx / len;
            float uy = dy / len;
            float nx = -uy;
            float ny = ux;
            float mx = (p1X + p2X) * 0.5f;
            float my = (p1Y + p2Y) * 0.5f;
            float tipX = mx + 2.5f * ux;
            float tipY = my + 2.5f * uy;
            float wing1X = tipX - 6.0f * ux + 3.5f * nx;
            float wing1Y = tipY - 6.0f * uy + 3.5f * ny;
            float wing2X = tipX - 6.0f * ux - 3.5f * nx;
            float wing2Y = tipY - 6.0f * uy - 3.5f * ny;
            canvas->fillTriangle((int)roundf(tipX), (int)roundf(tipY),
                                 (int)roundf(wing1X), (int)roundf(wing1Y),
                                 (int)roundf(wing2X), (int)roundf(wing2Y),
                                 arrowColor);
        };

        for (int i = 0; i < track.orbitPointCount; i++) {
            int px, py;
            projectSkyToScreen(track.orbitPoints[i].az, track.orbitPoints[i].el, cx, cy, r, px, py);
            if (prevX >= 0) {
                // 平滑科技青蓝弧线表示过境卫星轨道
                canvas->drawLine(prevX, prevY, px, py, canvas->color565(0, 185, 240));

                // 在轨道运动路径上绘制方向箭头 (入轨上升段与离轨下降段各绘制一个箭头)
                if (i == 3 || i == 7) {
                    drawArrowOnSegment(prevX, prevY, px, py, canvas->color565(0, 240, 255));
                }
            }
            prevX = px;
            prevY = py;
        }

        // 标注关键节点：AOS (入轨地平线交点)
        int aosX, aosY;
        projectSkyToScreen(track.orbitPoints[0].az, track.orbitPoints[0].el, cx, cy, r, aosX, aosY);
        canvas->fillCircle(aosX, aosY, 2, 0x07E0);
        canvas->setTextColor(0x07E0);
        if (aosX <= cx) {
            canvas->setTextDatum(ML_DATUM);
            canvas->drawString("AOS", aosX + 4, aosY);
        } else {
            canvas->setTextDatum(MR_DATUM);
            canvas->drawString("AOS", aosX - 4, aosY);
        }

        // 标注关键节点：LOS (离轨出境交点)
        int lastIdx = track.orbitPointCount - 1;
        int losX, losY;
        projectSkyToScreen(track.orbitPoints[lastIdx].az, track.orbitPoints[lastIdx].el, cx, cy, r, losX, losY);
        canvas->fillCircle(losX, losY, 2, canvas->color565(255, 110, 110));
        canvas->setTextColor(canvas->color565(255, 130, 130));
        if (losX <= cx) {
            canvas->setTextDatum(ML_DATUM);
            canvas->drawString("LOS", losX + 4, losY);
        } else {
            canvas->setTextDatum(MR_DATUM);
            canvas->drawString("LOS", losX - 4, losY);
        }

        // 标注关键节点：TCA (最高仰角峰值点)
        int tcaIdx = 0;
        float maxElFound = track.orbitPoints[0].el;
        for (int i = 1; i < track.orbitPointCount; i++) {
            if (track.orbitPoints[i].el > maxElFound) {
                maxElFound = track.orbitPoints[i].el;
                tcaIdx = i;
            }
        }
        int tcaX, tcaY;
        float tcaAz = (track.tcaAz > 0.0f) ? track.tcaAz : track.orbitPoints[tcaIdx].az;
        float tcaEl = (track.maxEl > 0.0f) ? track.maxEl : maxElFound;
        projectSkyToScreen(tcaAz, tcaEl, cx, cy, r, tcaX, tcaY);
        canvas->fillCircle(tcaX, tcaY, 2, TFT_YELLOW);
        canvas->setTextColor(TFT_YELLOW);
        if (tcaY <= cy) {
            canvas->setTextDatum(BC_DATUM);
            canvas->drawString("TCA", tcaX, tcaY - 3);
        } else {
            canvas->setTextDatum(TC_DATUM);
            canvas->drawString("TCA", tcaX, tcaY + 3);
        }
    }
}

static void drawTimelineSatellite(LGFX_Sprite* canvas, int cx, int cy, SatIconType icon, uint16_t satColor, bool isListening, bool isRising) {
    if (!canvas) return;

    // 1. 呼吸动态信标光点
    bool pulse = ((millis() / 400) % 2 == 0);
    uint16_t beaconCol = isListening ? (pulse ? 0x07E0 : 0x04A0) : 0x07FF;

    // 2. 卫星图标旋转 45° 呈现生动空间飞行姿态
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
                                int32_t& timeOffset, bool justBracketL, bool justBracketR) {
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
        if (justBracketL || justSemi) {
            if (_detailScrollTarget <= 0) {
                _detailScrollTarget = 0;
                s_detailScrollY.shift(-12.0f); // 触顶边缘 lilt 弹性下冲回弹
            } else {
                _detailScrollTarget -= 52;
                if (_detailScrollTarget < 0) _detailScrollTarget = 0;
            }
        } else if (justBracketR || justDot) {
            if (_detailScrollTarget >= _detailMaxScroll) {
                _detailScrollTarget = _detailMaxScroll;
                s_detailScrollY.shift(12.0f); // 触底边缘 lilt 弹性上冲回弹
            } else {
                _detailScrollTarget += 52;
                if (_detailScrollTarget > _detailMaxScroll) _detailScrollTarget = _detailMaxScroll;
            }
        }
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
            s_boundaryBump = 4; // 触顶循环回弹冲量
        } else {
            s_boundaryBump = 3; // 空列表触顶回弹
        }
    } else if (justDot) {
        if (_selectedPacketIndex < count - 1) {
            _selectedPacketIndex++;
        } else if (count > 0) {
            _selectedPacketIndex = 0;
            s_boundaryBump = -4; // 触底循环回弹冲量
        } else {
            s_boundaryBump = -3; // 空列表触底回弹
        }
    } else if (justEnter && count > 0) {
        _showDetailModal = true;
        _detailScrollTarget = 0;
        _detailMaxScroll = 0;
        s_detailScrollY.jump(0.0f);
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
            _lastRawRssi = HalRadio::getInstance().getRawInstantRSSI();
            currentRssi = HalRadio::getInstance().getInstantRSSI();
            if (currentRssi >= -10.0f || currentRssi < -135.0f) {
                currentRssi = -122.0f + (float)(rand() % 6 - 3);
            }
        } else {
            _lastRawRssi = -999.0f;
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
    } else if (!rm.isTargetFreqSupported()) {
        canvas->setTextColor(TFT_ORANGE, 0x10A2);
        canvas->drawString(I18N::get(TXT_RF_FREQ_OUT_OF_RANGE), statX, 3);
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

    // 2. 绘制底层天球俯视雷达平面图 (大圆圈、NSWE方位、过境卫星轨道弧线、AOS/TCA/LOS节点、运动方向箭头)
    // 缩小调整至不超过上方内存条：cy = 77, r = 54 (最高点 Y = 23，在 Y=20 内存条下方)
    int radarCx = width / 2; // 120
    int radarCy = 77;
    int radarR = 54;
    drawSkyRadarPlot(canvas, radarCx, radarCy, radarR, track, hasTarget, hasPassNow, isUpcoming);

    // 3. 绘制背景层信号瀑布热力图 (叠加于平面图上层，呈现半透明光晕流淌效果)
    drawBackgroundWaterfall(canvas, width, height);

    // 4. 前景完整数据层呈现
    const auto& packets = rm.getRecentPackets();

    if (!rm.isHardwareReady()) {
        canvas->setTextDatum(MC_DATUM);
        canvas->setTextColor(0x7BEF);
        canvas->drawString(I18N::get(TXT_RF_REQ_MODULE), width / 2, height / 2 - 8);
        canvas->setTextColor(0x4208);
        canvas->drawString(I18N::get(TXT_RF_ENABLE_IN_WIZARD), width / 2, height / 2 + 8);
    } else {
        // ===================================================================
        // 监控监控区 (Y = 20 ~ 71)：雷达居中，两侧对称遥测数据，实时卫星图标
        // ===================================================================

        // ---- 左侧遥测数据 (Left Column, X = 6) ----
        // 1. 方位角 AZ (Y = 21)
        canvas->setTextDatum(TL_DATUM);
        canvas->setTextColor(0xCE79);
        char azBuf[28];
        if (hasTarget) {
            snprintf(azBuf, sizeof(azBuf), "AZ:%03.0f°", track.currentAz);
        } else {
            snprintf(azBuf, sizeof(azBuf), "AZ:---°");
        }
        canvas->drawString(azBuf, 6, 21);

        // 2. 天线指引码 (Y = 33)
        canvas->setTextColor(0x07FF);
        char antBuf[32];
        if (hasTarget) {
            float az = track.currentAz;
            while (az < 0.0f) az += 360.0f;
            while (az >= 360.0f) az -= 360.0f;
            static const char* dirs8[8] = {"N","NE","E","SE","S","SW","W","NW"};
            const char* shortDir = dirs8[(int)((az + 22.5f) / 45.0f) % 8];
            snprintf(antBuf, sizeof(antBuf), "> %s %03.0f°", shortDir, track.currentAz);
        } else {
            snprintf(antBuf, sizeof(antBuf), "> STANDBY");
        }
        canvas->drawString(antBuf, 6, 33);

        // 3. 对地斜距 Slant Range (Y = 45)
        canvas->setTextColor(canvas->color565(0, 210, 255)); // 科技青色
        char distBuf[24];
        if (hasTarget && track.distanceKm > 0.0f) {
            snprintf(distBuf, sizeof(distBuf), "%.0fkm", track.distanceKm);
        } else {
            snprintf(distBuf, sizeof(distBuf), "---km");
        }
        canvas->drawString(distBuf, 6, 45);

        // 4. AOS 本地时间 (Y = 57)
        int tzOffsetSec = pos_manager ? pos_manager->getTimezoneManager()->getTimezoneOffset(baseUserLat, baseUserLon) : 8 * 3600;
        canvas->setTextColor(0x7BEF);
        char aosTimeBuf[24];
        if (hasTarget && track.aosTime > 0) {
            time_t aosLocal = (time_t)track.aosTime + tzOffsetSec;
            struct tm tmAos;
            gmtime_r(&aosLocal, &tmAos);
            snprintf(aosTimeBuf, sizeof(aosTimeBuf), "AOS %02d:%02d", tmAos.tm_hour, tmAos.tm_min);
        } else {
            snprintf(aosTimeBuf, sizeof(aosTimeBuf), "AOS --:--");
        }
        canvas->drawString(aosTimeBuf, 6, 57);

        // ---- 右侧遥测数据 (Right Column, X = width - 6, 右对齐) ----
        canvas->setTextDatum(TR_DATUM);

        // 1. 仰角与升降状态 (Y = 21)
        bool isRising = track.isRising;
        uint16_t elCol = 0x7BEF;
        char elBuf[36];
        if (hasTarget) {
            if (track.currentEl > 0.0f) {
                elCol = isRising ? 0x07E0 : TFT_YELLOW;
                snprintf(elBuf, sizeof(elBuf), "EL:%+.1f°%s /%.0f°",
                         track.currentEl, isRising ? "↑" : "↓", track.maxEl);
            } else {
                elCol = canvas->color565(80, 110, 140);
                snprintf(elBuf, sizeof(elBuf), "EL:%+.1f° /%.0f°",
                         track.currentEl, track.maxEl);
            }
        } else {
            snprintf(elBuf, sizeof(elBuf), "EL:---° /--°");
        }
        canvas->setTextColor(elCol);
        canvas->drawString(elBuf, width - 6, 21);

        // 2. 频率与调制模式 (Y = 33)
        char freqBuf[48];
        const char* modStr = (track.modulation.length() > 0) ? track.modulation.c_str() : "";
        if (hasTarget && track.baseFreqMHz > 0.0f) {
            if (isUpcoming) {
                if (strlen(modStr) > 0) {
                    snprintf(freqBuf, sizeof(freqBuf), "PRE %.3fM %s", track.baseFreqMHz, modStr);
                } else {
                    snprintf(freqBuf, sizeof(freqBuf), "PRE %.3fM", track.baseFreqMHz);
                }
                canvas->setTextColor(TFT_YELLOW);
            } else {
                float actualFreq = track.baseFreqMHz + (track.dopplerHz / 1e6f);
                if (strlen(modStr) > 0) {
                    snprintf(freqBuf, sizeof(freqBuf), "%.3fM %s", actualFreq, modStr);
                } else {
                    snprintf(freqBuf, sizeof(freqBuf), "%.3fM", actualFreq);
                }
                canvas->setTextColor(0xFFFF);
            }
        } else {
            snprintf(freqBuf, sizeof(freqBuf), "-- MHz");
            canvas->setTextColor(0x7BEF);
        }
        canvas->drawString(freqBuf, width - 6, 33);

        // 3. 多普勒频移 (Y = 45)
        char dopBuf[32];
        if (hasTarget && track.baseFreqMHz > 0.0f && !isUpcoming) {
            uint16_t dopCol = (track.dopplerHz < -10.0f) ? 0xFBE0 : ((track.dopplerHz > 10.0f) ? 0x07FF : 0xFFFF);
            if (abs(track.dopplerHz) >= 1000.0f) {
                snprintf(dopBuf, sizeof(dopBuf), "%+.1fkHz", track.dopplerHz / 1000.0f);
            } else {
                snprintf(dopBuf, sizeof(dopBuf), "%+.0fHz", track.dopplerHz);
            }
            canvas->setTextColor(dopCol);
        } else {
            snprintf(dopBuf, sizeof(dopBuf), "DOP:---");
            canvas->setTextColor(0x7BEF);
        }
        canvas->drawString(dopBuf, width - 6, 45);

        // 4. LOS 本地时间 (Y = 57)
        char losTimeBuf[24];
        if (hasTarget && track.losTime > 0) {
            time_t losLocal = (time_t)track.losTime + tzOffsetSec;
            struct tm tmLos;
            gmtime_r(&losLocal, &tmLos);
            snprintf(losTimeBuf, sizeof(losTimeBuf), "LOS %02d:%02d", tmLos.tm_hour, tmLos.tm_min);
        } else {
            snprintf(losTimeBuf, sizeof(losTimeBuf), "LOS --:--");
        }
        canvas->setTextColor(0x7BEF);
        canvas->drawString(losTimeBuf, width - 6, 57);

        // ---- 卫星图标渲染：指示正在过境的卫星位置 ----
        if (hasTarget) {
            int satX = radarCx, satY = radarCy;
            if (track.currentEl >= 0.0f) {
                // 正在空中过境：按当前方位仰角投影
                projectSkyToScreen(track.currentAz, track.currentEl, radarCx, radarCy, radarR, satX, satY);
            } else {
                // 地平线下 (预位等待)：放置于 AOS 入轨地平线边缘
                if (track.orbitPointCount > 0) {
                    projectSkyToScreen(track.orbitPoints[0].az, 0.0f, radarCx, radarCy, radarR, satX, satY);
                } else {
                    projectSkyToScreen(track.aosAz, 0.0f, radarCx, radarCy, radarR, satX, satY);
                }
            }

            SatIconType satIcon = track.satIconType;
            uint16_t satColor = track.satColor;
            if (satIcon == ICON_SATELLITE) {
                const EncyclopediaEntry* entry = Encyclopedia::getEntryByNorad(track.satNorad);
                if (entry) {
                    satIcon = entry->icon;
                    if (satColor == 0x07FF || satColor == 0) satColor = entry->color;
                }
            }
            drawTimelineSatellite(canvas, satX, satY, satIcon, satColor, hasPassNow && rm.isListening(), track.isRising);

            // 2. 在卫星图标正下方居中显示实时对地斜距
            if (track.distanceKm > 0.0f) {
                char distBuf[24];
                snprintf(distBuf, sizeof(distBuf), "%.0fkm", track.distanceKm);
                canvas->setTextDatum(TC_DATUM);
                canvas->setTextColor(canvas->color565(0, 220, 255)); // 科技青色
                canvas->drawString(distBuf, satX, satY + 9);
            }
        }

        // 1. 把时间补偿“+60s”改到屏幕左下角显示
        if (hasTarget && track.timeOffsetSec != 0) {
            char calibBuf[16];
            snprintf(calibBuf, sizeof(calibBuf), "%+ds", track.timeOffsetSec);
            canvas->setTextDatum(BL_DATUM);
            canvas->setTextColor(TFT_YELLOW);
            canvas->drawString(calibBuf, 4, height - 2);
        }

        // 分隔细线 (两侧局部微线，避让中间雷达圆盘，避免横穿线圈)
        canvas->drawFastHLine(4, 71, 52, canvas->color565(35, 50, 68));
        canvas->drawFastHLine(width - 56, 71, 52, canvas->color565(35, 50, 68));

        // ===================================================================
        // 底部数据包列表区 (Y = 73 ~ 134，高度 61px，可容纳 4 行抓包)
        // ===================================================================
        if (!packets.empty()) {
            if (_selectedPacketIndex >= (int)packets.size()) {
                _selectedPacketIndex = (int)packets.size() - 1;
            }
            if (_selectedPacketIndex < 0) _selectedPacketIndex = 0;

            // 1. 新包入队物理顿挫与撞墙弹性冲量
            size_t currentTotalPackets = rm.getTotalPacketsCount();
            if (s_lastTotalPackets != 0 && currentTotalPackets > s_lastTotalPackets) {
                s_rfCursorY.shift(-3.5f); // 新包挤入顶部微震颤冲量
            }
            s_lastTotalPackets = currentTotalPackets;

            if (s_boundaryBump != 0) {
                s_rfCursorY.bump((float)s_boundaryBump);
                s_boundaryBump = 0;
            }

            int listY = 73;
            int rowH = 14;
            int visibleRows = 4;
            int startIdx = 0;
            if (_selectedPacketIndex >= visibleRows) {
                startIdx = _selectedPacketIndex - visibleRows + 1;
            }

            // 2. 物理弹簧计算目标 Y 坐标并平滑吸附
            int visibleRowIdx = _selectedPacketIndex - startIdx;
            float targetCursorY = (float)(listY + visibleRowIdx * rowH);
            s_rfCursorY.set(targetCursorY);

            float smoothY = s_rfCursorY.get();
            int curCursorY = (int)roundf(smoothY);

            // 绘制平滑圆角磁吸高亮框 (暗蓝背衬 + 科技青色细边框)
            canvas->fillRoundRect(2, curCursorY, width - 4, rowH, 3, canvas->color565(0, 42, 75));
            canvas->drawRoundRect(2, curCursorY, width - 4, rowH, 3, canvas->color565(0, 180, 255));

            for (int i = 0; i < visibleRows && (startIdx + i) < (int)packets.size(); i++) {
                int rowIdx = startIdx + i;
                int rowY = listY + i * rowH;
                bool isUnderCursor = (fabsf((float)rowY - smoothY) < (rowH * 0.55f));

                const auto& p = packets[rowIdx];
                canvas->setTextDatum(TL_DATUM);

                // 序号
                canvas->setTextColor(isUnderCursor ? 0xFFFF : 0x07E0);
                char prefix[16];
                snprintf(prefix, sizeof(prefix), "#%02d", rowIdx + 1);
                canvas->drawString(prefix, 4, rowY + 2);

                // 卫星名
                canvas->setTextColor(isUnderCursor ? 0x07FF : 0xC618);
                String satInfo = p.decoded.satName;
                canvas->setClipRect(24, rowY, 48, rowH);
                canvas->drawString(satInfo, 24, rowY + 2);
                canvas->clearClipRect();

                // 射频指标
                canvas->setTextColor(isUnderCursor ? 0xFFFF : 0xCE79);
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
            // 无抓包数据时：中央完全通透留给 130px 天球俯视雷达与热力图，仅在底部显示单行暗调状态
            canvas->setTextDatum(BC_DATUM);
            canvas->setTextColor(canvas->color565(70, 95, 125));
            if (!rm.isTargetFreqSupported()) {
                canvas->setTextColor(TFT_ORANGE);
                canvas->drawString(I18N::getLanguage() == LANG_ZH ? "[ 卫星频段超出 Cap LoRa-1262 接收范围 ]" : "[ Frequency Out of Hardware Range ]", width / 2, height - 2);
            } else if (hasPassNow) {
                canvas->drawString(I18N::getLanguage() == LANG_ZH ? "[ 监听中 · 按 T 注入测试包 ]" : "[ LISTENING · Press T to test ]", width / 2, height - 2);
            } else if (isUpcoming) {
                int secToAos = (track.aosTime > track.currentSimTime) ? (int)(track.aosTime - track.currentSimTime) : 0;
                int mm = secToAos / 60;
                int ss = secToAos % 60;
                char upBuf[64];
                if (mm >= 60) {
                    snprintf(upBuf, sizeof(upBuf), (I18N::getLanguage() == LANG_ZH ? "[ 预计 %dh%02dm 后过境 · 按 T 测试 ]" : "[ AOS in %dh%02dm · Press T ]"), mm / 60, mm % 60);
                } else {
                    snprintf(upBuf, sizeof(upBuf), (I18N::getLanguage() == LANG_ZH ? "[ 预计 %02d:%02d 后过境 · 按 T 测试 ]" : "[ AOS in %02d:%02d · Press T ]"), mm, ss);
                }
                canvas->drawString(upBuf, width / 2, height - 2);
            } else {
                canvas->drawString(I18N::getLanguage() == LANG_ZH ? "[ 待机待命中 · 按 T 注入测试包 ]" : "[ STANDBY · Press T to test ]", width / 2, height - 2);
            }
        }
    }

    // 弹窗状态更新与弹簧平滑度驱动
    s_detailModalScale.set(_showDetailModal ? 1.0f : 0.0f);
    s_deleteModalScale.set(_showDeleteModal ? 1.0f : 0.0f);
    float detailScale = s_detailModalScale.get();
    float delScale = s_deleteModalScale.get();

    // 4. 屏幕右下角常驻实时数字 RSSI 呈现 (无全屏模态弹窗展开时)
    if (detailScale < 0.15f && delScale < 0.15f) {
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

        // 过境或监听时：在当前 RSSI 上方显示底层芯片原生 getRSSI(false) 读数供对比分析
        if (rm.isHardwareReady() && (hasPassNow || rm.isListening())) {
            char rawStr[24];
            if (_lastRawRssi > -500.0f) {
                snprintf(rawStr, sizeof(rawStr), "RAW:%+.0fdB", _lastRawRssi);
            } else {
                snprintf(rawStr, sizeof(rawStr), "RAW:--dB");
            }
            int rtw = canvas->textWidth(rawStr);
            int rbx = width - rtw - 6;
            int rby = by - 12; // 位于当前 RSSI 胶囊正上方
            int rbw = rtw + 5;
            int rbh = 11;

            canvas->fillRect(rbx, rby, rbw, rbh, canvas->color565(10, 16, 24));
            canvas->drawRect(rbx, rby, rbw, rbh, canvas->color565(40, 56, 76));

            uint16_t rawColor = (_lastRawRssi > -80.0f) ? 0xFBE0 : ((_lastRawRssi > -105.0f) ? 0x07FF : 0x7BEF);
            if (_lastRawRssi >= 0.0f || _lastRawRssi < -135.0f) {
                rawColor = TFT_YELLOW; // 异常/无效读数用黄色高亮提醒
            }
            canvas->setTextColor(rawColor, canvas->color565(10, 16, 24));
            canvas->drawString(rawStr, width - 4, rby + rbh - 1);
        }
    }

    // 4. 详情弹窗 (Detail Modal) - Pop-up 弹性中心展开缩放
    if (detailScale > 0.02f && !packets.empty()) {
        int targetIdx = _selectedPacketIndex;
        if (targetIdx < 0) targetIdx = 0;
        if (targetIdx >= (int)packets.size()) targetIdx = (int)packets.size() - 1;
        // 关键安全修复：进行值深拷贝，避免后台收包 insert 时原有引用失效成为野指针触发 abort()
        ReceivedLogItem sel = packets[targetIdx];
        int baseModalW = width - 20;
        int baseModalH = height - 22;
        int modalW = (int)roundf(baseModalW * detailScale);
        int modalH = (int)roundf(baseModalH * detailScale);
        int modalX = (width - modalW) / 2;
        int modalY = (height - modalH) / 2;

        canvas->fillRoundRect(modalX, modalY, modalW, modalH, 4, canvas->color565(12, 20, 32));
        canvas->drawRoundRect(modalX, modalY, modalW, modalH, 4, canvas->color565(0, 190, 255));

        if (detailScale > 0.65f) {
            canvas->drawFastHLine(modalX + 2, modalY + 16, modalW - 4, canvas->color565(35, 60, 90));

            canvas->setTextDatum(TL_DATUM);
            canvas->setTextColor(canvas->color565(0, 220, 255), canvas->color565(12, 20, 32));
            canvas->drawString(I18N::get(TXT_RF_MODAL_TITLE), modalX + 6, modalY + 3);

            struct ModalLineItem {
                String text;
                uint16_t color;
                int height;
            };
            std::vector<ModalLineItem> lines;

            lines.push_back({sel.decoded.satName + " [" + sel.decoded.frameType + "]", 0xFFFF, 13});

            char rfLine[64];
            snprintf(rfLine, sizeof(rfLine), "%.3fM | RSSI:%.0f | SNR:%.1f | %dB",
                    sel.raw.freqMHz, sel.raw.rssi, sel.raw.snr, (int)sel.raw.length);
            lines.push_back({String(rfLine), 0x07E0, 13});

            for (size_t f = 0; f < sel.decoded.fields.size(); ) {
                String item1 = sel.decoded.fields[f].key + ":" + sel.decoded.fields[f].value + sel.decoded.fields[f].unit;
                if (f + 1 < sel.decoded.fields.size()) {
                    String item2 = sel.decoded.fields[f+1].key + ":" + sel.decoded.fields[f+1].value + sel.decoded.fields[f+1].unit;
                    String combined = item1 + "  " + item2;
                    if (canvas->textWidth(combined.c_str()) <= baseModalW - 18) {
                        lines.push_back({combined, 0xFFE0, 13});
                        f += 2;
                        continue;
                    }
                }
                if (item1.length() > 32) {
                    for (size_t p = 0; p < item1.length(); p += 32) {
                        size_t endP = (p + 32 < item1.length()) ? (p + 32) : item1.length();
                        lines.push_back({item1.substring(p, endP), 0xFFE0, 13});
                    }
                } else {
                    lines.push_back({item1, 0xFFE0, 13});
                }
                f += 1;
            }

            lines.push_back({String(I18N::get(TXT_RF_RAW_HEX)), 0xC618, 12});
            for (size_t p = 0; p < sel.decoded.rawHex.length(); p += 30) {
                size_t endP = (p + 30 < sel.decoded.rawHex.length()) ? (p + 30) : sel.decoded.rawHex.length();
                String hLine = sel.decoded.rawHex.substring(p, endP);
                hLine.trim();
                if (hLine.length() > 0) {
                    lines.push_back({hLine, 0x7BEF, 11});
                }
            }

            if (sel.decoded.rawAscii.length() > 0) {
                lines.push_back({String(I18N::getLanguage() == LANG_ZH ? "ASCII 载荷:" : "ASCII Payload:"), 0xC618, 12});
                for (size_t p = 0; p < sel.decoded.rawAscii.length(); p += 32) {
                    size_t endP = (p + 32 < sel.decoded.rawAscii.length()) ? (p + 32) : sel.decoded.rawAscii.length();
                    lines.push_back({sel.decoded.rawAscii.substring(p, endP), 0x7BEF, 11});
                }
            }

            int totalContentH = 0;
            for (const auto& ln : lines) {
                totalContentH += ln.height;
            }

            int baseViewH = baseModalH - 22;
            _detailMaxScroll = (totalContentH > baseViewH) ? (totalContentH - baseViewH + 4) : 0;
            if (_detailScrollTarget > _detailMaxScroll) _detailScrollTarget = _detailMaxScroll;
            if (_detailScrollTarget < 0) _detailScrollTarget = 0;

            s_detailScrollY.set((float)_detailScrollTarget);
            int scrollY = (int)roundf(s_detailScrollY.get());

            if (_detailMaxScroll > 0) {
                int pageStep = 52;
                int totalPages = (_detailMaxScroll + pageStep - 1) / pageStep + 1;
                int curPage = (_detailScrollTarget + pageStep / 2) / pageStep + 1;
                if (curPage > totalPages) curPage = totalPages;
                char pgBuf[24];
                snprintf(pgBuf, sizeof(pgBuf), "[ / ] %d/%d", curPage, totalPages);
                canvas->setTextDatum(TR_DATUM);
                canvas->setTextColor(0x7BEF, canvas->color565(12, 20, 32));
                canvas->drawString(pgBuf, modalX + modalW - 6, modalY + 3);
                canvas->setTextDatum(TL_DATUM);
            }

            int viewTop = modalY + 18;
            int viewH = modalH - 20;
            canvas->setClipRect(modalX + 2, viewTop, modalW - 4, viewH);

            int relY = 0;
            for (const auto& ln : lines) {
                int drawY = viewTop + 2 + relY - scrollY;
                if (drawY + ln.height >= viewTop && drawY <= viewTop + viewH) {
                    canvas->setTextColor(ln.color, canvas->color565(12, 20, 32));
                    canvas->drawString(ln.text.c_str(), modalX + 6, drawY);
                }
                relY += ln.height;
            }

            // 右侧滚动进度条 (随 lilt 物理弹簧与越界回弹实时联动)
            if (_detailMaxScroll > 0 && viewH > 12) {
                int trackX = modalX + modalW - 4;
                int trackY = viewTop + 2;
                int trackH = viewH - 4;
                canvas->drawFastVLine(trackX, trackY, trackH, canvas->color565(25, 42, 62));
                int thumbH = max(10, (trackH * baseViewH) / max(1, totalContentH));
                if (thumbH > trackH) thumbH = trackH;
                float normScroll = (float)scrollY / (float)_detailMaxScroll;
                if (normScroll < 0.0f) normScroll = 0.0f;
                if (normScroll > 1.0f) normScroll = 1.0f;
                int thumbY = trackY + (int)roundf(normScroll * (float)(trackH - thumbH));
                canvas->fillRect(trackX, thumbY, 2, thumbH, canvas->color565(0, 190, 255));
            }

            canvas->clearClipRect();
            if (false) {

            canvas->setClipRect(modalX + 2, modalY + 18, modalW - 4, modalH - 20);
            int curY = modalY + 20;

            canvas->setTextColor(0xFFFF, canvas->color565(12, 20, 32));
            String headerLine = sel.decoded.satName + " [" + sel.decoded.frameType + "]";
            canvas->drawString(headerLine.c_str(), modalX + 6, curY);
            curY += 13;

            char rfLine[64];
            snprintf(rfLine, sizeof(rfLine), "%.3fM | RSSI:%.0f | SNR:%.1f | %dB",
                    sel.raw.freqMHz, sel.raw.rssi, sel.raw.snr, (int)sel.raw.length);
            canvas->setTextColor(0x07E0, canvas->color565(12, 20, 32));
            canvas->drawString(rfLine, modalX + 6, curY);
            curY += 13;

            canvas->setTextColor(0xFFE0, canvas->color565(12, 20, 32));
            for (size_t f = 0; f < sel.decoded.fields.size() && f < 6; f += 2) {
                String fRow = sel.decoded.fields[f].key + ":" + sel.decoded.fields[f].value + sel.decoded.fields[f].unit;
                if (f + 1 < sel.decoded.fields.size()) {
                    fRow += "  " + sel.decoded.fields[f+1].key + ":" + sel.decoded.fields[f+1].value + sel.decoded.fields[f+1].unit;
                }
                canvas->drawString(fRow.c_str(), modalX + 6, curY);
                curY += 13;
            }

            canvas->setTextColor(0xC618, canvas->color565(12, 20, 32));
            canvas->drawString(I18N::get(TXT_RF_RAW_HEX), modalX + 6, curY);
            curY += 11;

            String hexLine1 = sel.decoded.rawHex.substring(0, 30);
            String hexLine2 = sel.decoded.rawHex.length() > 30 ? sel.decoded.rawHex.substring(30, 60) : "";
            canvas->setTextColor(0x7BEF, canvas->color565(12, 20, 32));
            canvas->drawString(hexLine1.c_str(), modalX + 6, curY);
            curY += 10;
            if (hexLine2.length() > 0) {
                canvas->drawString(hexLine2.c_str(), modalX + 6, curY);
            }
            }

            canvas->clearClipRect();
        }
    }

    // 5. 删除报文确认弹窗 - Pop-up 弹性中心展开缩放
    if (delScale > 0.02f && !packets.empty()) {
        int basePopW = 160;
        int basePopH = 44;
        int popW = (int)roundf(basePopW * delScale);
        int popH = (int)roundf(basePopH * delScale);
        int popX = (width - popW) / 2;
        int popY = (height - popH) / 2;

        canvas->fillRoundRect(popX, popY, popW, popH, 4, TFT_RED);
        canvas->drawRoundRect(popX, popY, popW, popH, 4, TFT_WHITE);

        if (delScale > 0.65f) {
            canvas->setTextDatum(TC_DATUM);
            canvas->setTextColor(TFT_WHITE, TFT_RED);
            canvas->drawString(I18N::getLanguage() == LANG_ZH ? "删除此条报文记录？" : "Delete this packet?", width / 2, popY + 6);
            canvas->drawString(I18N::getLanguage() == LANG_ZH ? "[y] 确定   [n] 取消" : "[y] Yes   [n] No", width / 2, popY + 24);
        }
    }

    canvas->setTextDatum(TL_DATUM);
    canvas->clearClipRect();
}
