#include "ui/recommendation_view.h"
#include <WiFi.h>
#include <math.h>
#include "core/i18n.h"
#include "core/earth_renderer.h"
#include "core/recent_launch_item.h"
#include "core/position_manager.h"
#include "core/hardware_config.h"
#include "core/mono_animator.h"
#include "hal/hal_gnss.h"
#include "hal/hal_wifi.h"
#include "gimbal/gimbal_controller.h"

static uint32_t parseTleEpoch(const String& line1) {
    if (line1.length() < 32) return 0;
    int offset = 0;
    if (line1.length() >= 9 && line1[8] == 'U') {
        offset = 1; // 容错非标 6 位目录号导致的 1 列后移
    }
    String yrStr = line1.substring(18 + offset, 20 + offset);
    String dayStr = line1.substring(20 + offset, 32 + offset);
    int yr = yrStr.toInt();
    double days = dayStr.toDouble();
    
    int year = (yr < 57) ? (2000 + yr) : (1900 + yr);
    
    auto isLeap = [](int y) {
        return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    };
    
    uint32_t seconds = 0;
    for (int y = 1970; y < year; ++y) {
        seconds += isLeap(y) ? 366 * 86400 : 365 * 86400;
    }
    seconds += (uint32_t)((days - 1.0) * 86400.0);
    return seconds;
}

extern EarthRenderer* earth_renderer;
extern bool predictionsReady;
extern int predictionProgress;
extern volatile bool g_timeSynced;
extern void lockPassMutex();
extern void unlockPassMutex();
extern std::vector<PassEvent> recommendedPasses;
extern std::vector<TreeItem> displayTree;
extern uint32_t current_unix;
extern int32_t timeMachineOffset;
extern int NUM_SATELLITES;
extern SatProfile g_satellites[];
extern bool g_recentLaunchFocusMode;
extern SGP4Calc g_repSatCalc;
extern double baseUserLat;
extern double baseUserLon;
extern double baseUserAlt;
extern PositionManager* pos_manager;
extern int selectedPassIndex;
extern int passScrollIndex;
extern bool catExpanded[4];
extern volatile bool g_orbitCalculating;
extern bool manualWifiToggle;
extern HalGnss* gnss;
extern bool gnssTimedOut;
extern bool isMonoInitialized;
extern GimbalController gimbal;


void rebuildTreeLocal(std::vector<TreeItem>& tree, const std::vector<PassEvent>& passes, uint32_t current_unix) {
    tree.clear();
    for (int c = 0; c < 4; c++) {
        tree.push_back({true, c, -1});
        if (catExpanded[c]) {
            for (size_t i = 0; i < passes.size(); i++) {
                const auto& p = passes[i];
                bool isPassValid = (p.isVisible || p.isRadioPass);
                bool match = false;
                if (c == 0 && isPassValid && p.losTime >= current_unix && p.aosTime < current_unix + 24 * 3600) match = true;
                else if (c == 1 && isPassValid && p.losTime >= current_unix && p.aosTime < current_unix + 7 * 24 * 3600) match = true;
                else if (c == 2 && isPassValid && p.score >= 4 && p.losTime >= current_unix) match = true;
                else if (c == 3 && p.losTime >= current_unix) match = true;
                
                if (match) {
                    tree.push_back({false, c, (int)i});
                }
            }
        }
    }
}

RecommendationView::RecommendationView() {
}


void RecommendationView::draw(LGFX_Sprite* canvas) {
    if (!canvas) return;
    HardwareConfig& hw = HardwareConfig::getInstance();
            // Draw semi-transparent dark overlay on the left side (width: 140)
            canvas->fillRect(0, 0, 140, 135, canvas->color565(15, 20, 25));
            canvas->drawFastVLine(140, 0, 135, TFT_DARKGREY); // separator line
            
            canvas->setTextColor(TFT_WHITE);
            canvas->setTextSize(1);
            canvas->drawString(I18N::get(TXT_RECOMMENDED_PASSES), 2, 5);
            
            bool localPredictionsReady = false;
            int localPredictionProgress = 0;
            bool localTimeSynced = false;
            static std::vector<PassEvent> localRecommendedPasses;
            static std::vector<TreeItem> localDisplayTree;
            static uint32_t lastCopiedTime = 0;
            static int lastCopiedCount = -1;

            lockPassMutex();
            localPredictionsReady = predictionsReady;
            localPredictionProgress = predictionProgress;
            localTimeSynced = g_timeSynced;
            if (localPredictionsReady) {
                if (lastCopiedCount != (int)recommendedPasses.size() || (millis() - lastCopiedTime > 1000)) {
                    localRecommendedPasses = recommendedPasses;
                    localDisplayTree = displayTree;
                    lastCopiedCount = (int)recommendedPasses.size();
                    lastCopiedTime = millis();
                }
            } else {
                if (!localRecommendedPasses.empty()) {
                    localRecommendedPasses.clear();
                    localDisplayTree.clear();
                }
                lastCopiedCount = -1;
            }
            unlockPassMutex();

            if (!localPredictionsReady) {
                canvas->setTextColor(TFT_YELLOW);
                if (!localTimeSynced) {
                    canvas->drawString(I18N::get(TXT_WAITING_TIME_SYNC), 5, 30);
                } else {
                    char buf[32];
                    sprintf(buf, "%s %d%%", I18N::get(TXT_PASS_CALCULATING), localPredictionProgress);
                    canvas->drawString(buf, 5, 30);
                }
            } else {
                if (localRecommendedPasses.empty()) {
                    canvas->setTextColor(TFT_LIGHTGRAY);
                    canvas->drawString(I18N::get(TXT_NO_PASSES_7D), 5, 30);
                    
                    // Detect if selected satellites have stale TLEs
                    bool hasStaleTle = false;
                    uint32_t currentSimTime = current_unix + timeMachineOffset;
                    for (int i = 0; i < NUM_SATELLITES; i++) {
                        if (g_satellites[i].selected && g_satellites[i].tle.line1.length() >= 32) {
                            uint32_t ep = parseTleEpoch(g_satellites[i].tle.line1);
                            if (ep > 0 && currentSimTime > ep && (currentSimTime - ep) > 30 * 86400) {
                                hasStaleTle = true;
                                break;
                            }
                        }
                    }
                    if (hasStaleTle) {
                        canvas->setTextColor(TFT_YELLOW);
                        Language currL = I18N::getLanguage();
                        const char* staleMsg = (currL == LANG_ZH) ? "TLE过期,请连WiFi更新" : ((currL == LANG_JA) ? "TLE期限切れ,WiFi更新" : ((currL == LANG_ES) ? "TLE vencido, sinc WiFi" : "Stale TLE, sync WiFi"));
                        canvas->drawString(staleMsg, 5, 48);
                    }
                } else if (selectedPassIndex >= 0 && selectedPassIndex < (int)localRecommendedPasses.size()) {
                    // Draw Detail View
                    const auto& p = localRecommendedPasses[selectedPassIndex];
                    canvas->setTextColor(TFT_CYAN);
                    canvas->drawString(I18N::get(TXT_PASS_NAME), 5, 20);
                    canvas->setTextColor(TFT_WHITE);
                    canvas->drawString(p.satName.c_str(), 40, 20);
                    
                    bool isCjk = (I18N::getLanguage() == LANG_ZH || I18N::getLanguage() == LANG_JA);

                    // Score: (y=32)
                    canvas->setTextColor(TFT_CYAN);
                    canvas->drawString(I18N::get(TXT_PASS_SCORE), 5, 32);
                    String stars = "";
                    for(int s=0;s<p.score;s++) stars += "*";
                    uint16_t starColor = (p.score==5) ? TFT_GOLD : (p.score>=3 ? TFT_GREEN : TFT_LIGHTGRAY);
                    canvas->setTextColor(starColor);
                    int scoreX = isCjk ? 60 : 45;
                    canvas->drawString(stars.c_str(), scoreX, 32);
                    
                    // Orbit: MM/DD (y=45)
                    canvas->setTextColor(TFT_CYAN);
                    canvas->drawString(I18N::get(TXT_RL_ORBIT), 5, 45);
                    
                    int tzOffsetSec = pos_manager ? pos_manager->getTimezoneManager()->getTimezoneOffset(baseUserLat, baseUserLon) : 8*3600;
                    time_t aos_t = (time_t)p.aosTime + tzOffsetSec;
                    time_t los_t = (time_t)p.losTime + tzOffsetSec;
                    struct tm aos_tm;
                    struct tm los_tm;
                    gmtime_r(&aos_t, &aos_tm);
                    gmtime_r(&los_t, &los_tm);
                    
                    char dateStr[32];
                    sprintf(dateStr, "%02d/%02d", aos_tm.tm_mon + 1, aos_tm.tm_mday);
                    canvas->setTextColor(TFT_WHITE);
                    canvas->drawString(dateStr, 45, 45);
                    
                    // Time: HH:MM:SS - HH:MM:SS (y=57)
                    char timeStr[64];
                    sprintf(timeStr, "%02d:%02d:%02d-%02d:%02d:%02d", 
                            aos_tm.tm_hour, aos_tm.tm_min, aos_tm.tm_sec, 
                            los_tm.tm_hour, los_tm.tm_min, los_tm.tm_sec);
                    canvas->setTextColor(TFT_LIGHTGRAY);
                    canvas->drawString(timeStr, 5, 57);
                    
                    // Mag & Peak (y=70)
                    canvas->setTextColor(TFT_CYAN);
                    canvas->drawString(I18N::get(TXT_PASS_MAG), 5, 70);
                    canvas->setTextColor(TFT_WHITE);
                    char magBuf[16];
                    if (p.maxBrightness < 98.0) {
                        sprintf(magBuf, "%.1f", p.maxBrightness);
                    } else {
                        sprintf(magBuf, "%s", I18N::get(TXT_VIS_NA));
                    }
                    int magX = isCjk ? 40 : 35;
                    canvas->drawString(magBuf, magX, 70);
                    
                    canvas->setTextColor(TFT_CYAN);
                    canvas->drawString(I18N::get(TXT_PASS_MAX_EL), 65, 70);
                    canvas->setTextColor(TFT_WHITE);
                    int maxElX = isCjk ? 120 : 115;
                    canvas->drawString((String((int)p.maxElevation) + "°").c_str(), maxElX, 70);
                    
                    int sIdx = -1;
                    for (int i = 0; i < NUM_SATELLITES; i++) {
                        if (g_satellites[i].name == p.satName) { sIdx = i; break; }
                    }
                    
                    SGP4Calc* satCalc = nullptr;
                    SatelliteType satType = SAT_TYPE_VISUAL;
                    int noradId = 0;
                    String downlinkFreq = "";
                    String uplinkFreq = "";
                    String tone = "";
                    
                    if (sIdx != -1) {
                        satCalc = &(g_satellites[sIdx].calc);
                        satType = g_satellites[sIdx].type;
                        noradId = g_satellites[sIdx].noradId;
                        downlinkFreq = g_satellites[sIdx].downlinkFreq;
                        uplinkFreq = g_satellites[sIdx].uplinkFreq;
                        tone = g_satellites[sIdx].tone;
                    } else if (g_recentLaunchFocusMode) {
                        satCalc = &g_repSatCalc;
                    }

                    // Reason: (y=82)
                    canvas->setTextColor(TFT_CYAN);
                    canvas->drawString(I18N::get(TXT_PASS_REASON), 5, 82);
                    String reason = "";
                    if (p.isRadioPass || satType == SAT_TYPE_HAM) {
                        reason = I18N::get(TXT_PASS_REASON_RADIO);
                        if (p.maxElevation >= 60) reason += I18N::get(TXT_PASS_REASON_HIGH_EL);
                        else if (p.maxElevation >= 30) reason += I18N::get(TXT_PASS_REASON_GOOD_EL);
                        if ((p.losTime - p.aosTime) >= 480) reason += I18N::get(TXT_PASS_REASON_LONG_WINDOW);
                    } else {
                        reason = I18N::get(TXT_PASS_REASON_DARK);
                        if (p.maxBrightness <= 2.0) reason += I18N::get(TXT_PASS_REASON_BRIGHT);
                        if (p.maxElevation > 60) reason += I18N::get(TXT_PASS_REASON_ZENITH);
                        if (p.visibleDuration > 300) reason += I18N::get(TXT_PASS_REASON_LONG);
                    }
                    canvas->setTextColor(TFT_LIGHTGRAY);
                    int reasonX = isCjk ? 40 : 50;
                    canvas->drawString(reason.c_str(), reasonX, 82);
                    
                    if (satCalc != nullptr) {
                        double tx, ty, tz;
                        if (satCalc->getTEME(current_unix + timeMachineOffset, tx, ty, tz)) {
                            double gmst = CoordTransform::getGMST(CoordTransform::unixToJulian(current_unix + timeMachineOffset));
                            ECEFCoord ecef = CoordTransform::temeToECEF(tx, ty, tz, gmst);
                            GeodeticCoord geo = CoordTransform::ecefToGeodetic(ecef);
                            GeodeticCoord obsGeo = {baseUserLat, baseUserLon, baseUserAlt / 1000.0};
                            TopocentricCoord topo = CoordTransform::ecefToTopocentric(obsGeo, ecef);
                            double az = topo.az;
                            double el = topo.el;
                            double dist = topo.range;
                            
                            double tx_prev, ty_prev, tz_prev;
                            double dist_prev = dist;
                            if (satCalc->getTEME(current_unix + timeMachineOffset - 1, tx_prev, ty_prev, tz_prev)) {
                                double gmst_prev = CoordTransform::getGMST(CoordTransform::unixToJulian(current_unix + timeMachineOffset - 1));
                                ECEFCoord ecef_prev = CoordTransform::temeToECEF(tx_prev, ty_prev, tz_prev, gmst_prev);
                                TopocentricCoord topo_prev = CoordTransform::ecefToTopocentric(obsGeo, ecef_prev);
                                dist_prev = topo_prev.range;
                            }
                            double range_rate = dist - dist_prev;
                            
                            canvas->setTextColor(TFT_GREEN);
                            bool isCjk = (I18N::getLanguage() == LANG_ZH || I18N::getLanguage() == LANG_JA);
                            char azaltBuf[32];
                            if (isCjk) {
                                sprintf(azaltBuf, "方位:%03d° 仰角:%02d°", (int)az, (int)el);
                            } else {
                                sprintf(azaltBuf, "Az:%03d° El:%02d°", (int)az, (int)el);
                            }
                            canvas->drawString(azaltBuf, 5, 95);
                        }
                    }
                } else {
                    
                    // Count passes per category for display in top-level items
                    int catCounts[4] = {0, 0, 0, 0};
                    uint32_t currentSimTime = current_unix + timeMachineOffset;
                    for (const auto& p : localRecommendedPasses) {
                        if (p.losTime >= currentSimTime) {
                            bool isPassValid = (p.isVisible || p.isRadioPass);
                            if (isPassValid && p.aosTime < currentSimTime + 24*3600) catCounts[0]++;
                            if (isPassValid && p.aosTime < currentSimTime + 7*24*3600) catCounts[1]++;
                            if (isPassValid && p.score >= 4) catCounts[2]++;
                            catCounts[3]++;
                        }
                    }

                    // Draw Tree View
                    bool isCjk = (I18N::getLanguage() == LANG_ZH || I18N::getLanguage() == LANG_JA);
                    const char* catNames[] = {
                        I18N::get(TXT_CAT_TONIGHT),
                        I18N::get(TXT_CAT_NEXT_7D),
                        I18N::get(TXT_CAT_HIGHLY_REC),
                        I18N::get(TXT_CAT_ALL_PASSES)
                    };
                    int lineH = isCjk ? 14 : 11;
                    int y = 20;
                    int itemsPerPage = isCjk ? 6 : 7;
                    int startIndex = (passScrollIndex / itemsPerPage) * itemsPerPage;
                    
                    for (int i = 0; i < itemsPerPage && (startIndex + i) < localDisplayTree.size(); i++) {
                        int idx = startIndex + i;
                        const auto& item = localDisplayTree[idx];
                        
                        if (idx == passScrollIndex) {
                            canvas->fillRect(2, y-1, 136, lineH, canvas->color565(0, 120, 255));
                        }
                        
                        if (item.isCategory) {
                            bool isCalculatingCat = g_orbitCalculating && (item.categoryIndex > 0);
                            if (isCalculatingCat) {
                                canvas->setTextColor(TFT_YELLOW);
                            } else {
                                canvas->setTextColor(idx == passScrollIndex ? TFT_WHITE : TFT_CYAN);
                            }
                            
                            String prefix = catExpanded[item.categoryIndex] ? "[-] " : "[+] ";
                            String label = prefix + catNames[item.categoryIndex] + "(" + String(catCounts[item.categoryIndex]) + ")";
                            if (isCalculatingCat) {
                                int dotState = (millis() / 400) % 3;
                                if (dotState == 0) label += ".";
                                else if (dotState == 1) label += "..";
                                else label += "...";
                            }
                            canvas->drawString(label.c_str(), 5, y);
                        } else {
                            const auto& p = localRecommendedPasses[item.passIndex];
                            canvas->setTextColor(idx == passScrollIndex ? TFT_WHITE : TFT_LIGHTGRAY);
                            String name = String(p.satName.c_str());
                            if (name.length() > 8) name = name.substring(0, 7) + ".";
                            canvas->drawString(name.c_str(), 15, y);
                            
                            // Draw stars
                            String stars = "";
                            for(int s=0;s<p.score;s++) stars += "*";
                            uint16_t starColor = (p.score==5) ? TFT_GOLD : (p.score>=3 ? TFT_GREEN : TFT_LIGHTGRAY);
                            if (idx == passScrollIndex) starColor = TFT_WHITE;
                            canvas->setTextColor(starColor);
                            canvas->drawString(stars.c_str(), 70, y);
                            
                            // Draw day if not tonight
                            if (item.categoryIndex != 0) {
                                int tzOffsetSec = pos_manager ? pos_manager->getTimezoneManager()->getTimezoneOffset(baseUserLat, baseUserLon) : 8*3600;
                                time_t aos_t = (time_t)p.aosTime + tzOffsetSec;
                                struct tm aos_tm;
                                gmtime_r(&aos_t, &aos_tm);
                                char dayStr[16];
                                sprintf(dayStr, "%02d/%02d", aos_tm.tm_mon + 1, aos_tm.tm_mday);
                                canvas->setTextColor(TFT_DARKGREY);
                                canvas->drawString(dayStr, 105, y);
                            }
                        }
                        y += lineH;
                    }
                    
                    if (localDisplayTree.size() > itemsPerPage) {
                        canvas->setTextColor(TFT_DARKGREY);
                        canvas->drawString("[^/v]", 110, 5);
                    }
                }
            }
            
            // Draw 2-Row Status Bar at the bottom of the panel
            // Divider line at y=100
            canvas->drawFastHLine(0, 100, 140, TFT_DARKGREY);
            
            // hw already declared above
            
            // Row 1 (y=105): GP Epoch (Left, x=3) & WiFi Status (Right, x=98)
            String tleEpoch = String(I18N::get(TXT_RL_EPOCH));
            if (g_satellites[0].tle.line1.length() >= 24) {
                int year = 2000 + g_satellites[0].tle.line1.substring(18, 20).toInt();
                int doy = g_satellites[0].tle.line1.substring(20, 23).toInt();
                int daysInMonth[] = {31, (year % 4 == 0 ? 29 : 28), 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
                int month = 0;
                while (month < 12 && doy > daysInMonth[month]) {
                    doy -= daysInMonth[month];
                    month++;
                }
                char buf[16];
                snprintf(buf, sizeof(buf), "%02d-%02d-%02d", year % 100, month + 1, doy);
                tleEpoch += buf;
            } else {
                tleEpoch += I18N::get(TXT_VIS_NA);
            }
            canvas->setTextColor(TFT_LIGHTGRAY);
            canvas->drawString(tleEpoch.c_str(), 3, 105);
            
            if (HalWifi::isConnected()) {
                canvas->setTextColor(TFT_GREEN);
                canvas->drawString("WF:ON", 98, 105);
            } else {
                canvas->setTextColor(TFT_LIGHTGRAY);
                canvas->drawString("WF:OFF", 98, 105);
            }
            
            // Row 2 (y=118): GP (x=4), MN (x=52), GB (x=98)
            // 1. GNSS Status (x=4)
            bool gnssConfigured = hw.isEnabled(HW_MOD_CAP_LORA1262) || hw.isEnabled(HW_MOD_UNIT_GPSV11);
            if (!gnssConfigured) {
                canvas->setTextColor(TFT_DARKGREY);
                canvas->drawString("GP:--", 4, 118);
            } else if (gnss && gnss->isModuleInitialized()) {
                if (gnss->isInStandbyMode()) {
                    if (gnssTimedOut) {
                        canvas->setTextColor(TFT_RED);
                        canvas->drawString("GP:TMO", 4, 118);
                    } else {
                        canvas->setTextColor(TFT_LIGHTGRAY);
                        canvas->drawString("GP:OFF", 4, 118);
                    }
                } else {
                    // 检查最近是否有 NMEA 串口数据（如果超过 3.5 秒完全没有数据字符，显示 ND）
                    unsigned long lastNmea = gnss->getLastNmeaTime();
                    bool hasUartTraffic = (lastNmea > 0 && (millis() - lastNmea < 3500));
                    
                    if (!hasUartTraffic && gnss->getGpsChars() < 10) {
                        canvas->setTextColor(TFT_YELLOW);
                        canvas->drawString("GP:ND", 4, 118);
                    } else {
                        int sats = gnss->getSatelliteCount();
                        char gpBuf[12];
                        uint16_t txtColor = TFT_YELLOW;

                        if (gnss->getStatus() == GNSS_STATUS_LOCKED) {
                            // 定位锁定: 绿字显示参与卫星数，如 GP:8D
                            if (sats > 0) {
                                snprintf(gpBuf, sizeof(gpBuf), "GP:%dD", sats);
                            } else {
                                snprintf(gpBuf, sizeof(gpBuf), "GP:3D");
                            }
                            txtColor = TFT_GREEN;
                        } else {
                            // 搜星中: 若抓到卫星则显示 GP:n* (青蓝)，若天空中 0 星则显示 GP:0 (黄色)
                            if (sats > 0) {
                                snprintf(gpBuf, sizeof(gpBuf), "GP:%d*", sats);
                                txtColor = TFT_CYAN;
                            } else {
                                snprintf(gpBuf, sizeof(gpBuf), "GP:0");
                                txtColor = TFT_YELLOW;
                            }
                        }

                        canvas->setTextColor(txtColor);
                        canvas->drawString(gpBuf, 4, 118);

                        // 绘制心跳呼吸点：放置在文本右下角 (文字起始x=4，计算字符串宽度并在其右侧 y=124 处绘制 4x4 呼吸点)
                        int textWidth = canvas->textWidth(gpBuf);
                        int dotX = 4 + textWidth + 2;
                        int dotY = 124;

                        unsigned long lastNmea = gnss->getLastNmeaTime();
                        uint32_t elapsed = (lastNmea > 0) ? (millis() - lastNmea) : 99999;

                        // 只要在最近 2.0 秒内接收到串口 NMEA 物理数据，就保持与模组节奏严格同步的 1Hz 呼吸心跳
                        if (elapsed < 2000) {
                            // 呼吸周期 1000ms：0~250ms 强劲起搏充盈，250~750ms 平滑呼出渐暗，750~1000ms 待机微光
                            uint32_t phase = elapsed % 1000;
                            float breath = 0.0f;
                            if (phase < 250) {
                                breath = sinf((phase / 250.0f) * 1.5707963f);
                            } else if (phase < 750) {
                                breath = cosf(((phase - 250) / 500.0f) * 1.5707963f);
                            } else {
                                breath = 0.0f;
                            }
                            if (breath < 0.0f) breath = 0.0f;
                            if (breath > 1.0f) breath = 1.0f;

                            // 呼吸色相：搜星阶段为亮青色(0, 255, 255)，定位锁定阶段为翡翠绿(0, 255, 120)
                            bool isLocked = (gnss->getStatus() == GNSS_STATUS_LOCKED);
                            uint8_t targetR = isLocked ? 20 : 0;
                            uint8_t targetG = 255;
                            uint8_t targetB = isLocked ? 120 : 255;

                            // 待机暗底色 (深太空青灰)
                            uint8_t baseR = 25, baseG = 40, baseB = 55;

                            uint8_t curR = (uint8_t)(baseR + breath * (targetR - baseR));
                            uint8_t curG = (uint8_t)(baseG + breath * (targetG - baseG));
                            uint8_t curB = (uint8_t)(baseB + breath * (targetB - baseB));
                            uint16_t coreColor = canvas->color565(curR, curG, curB);

                            // 绘制 4x4 平滑呼吸核心
                            canvas->fillRect(dotX, dotY, 4, 4, coreColor);

                            // 脉冲峰值区 (起搏充盈 breath > 0.55) 叠加亮白高光核，呈现强烈的心跳爆闪呼吸质感
                            if (breath > 0.55f) {
                                canvas->fillRect(dotX + 1, dotY + 1, 2, 2, TFT_WHITE);
                            }
                        } else {
                            // 硬件离线/未通信/待机：固定显示低调的暗灰色待机槽，无呼吸、无高光
                            canvas->fillRect(dotX, dotY, 4, 4, canvas->color565(40, 45, 50));
                        }
                    }
                }
            } else {
                canvas->setTextColor(TFT_YELLOW);
                canvas->drawString("GP:ND", 4, 118);
            }
            
            // 2. Chain Mono Sub-screen Status (x=52)
            bool monoConfigured = hw.isEnabled(HW_MOD_CHAIN_MONO);
            if (!monoConfigured) {
                canvas->setTextColor(TFT_DARKGREY);
                canvas->drawString("MN:--", 52, 118);
            } else if (isMonoInitialized) {
                canvas->setTextColor(TFT_GREEN);
                canvas->drawString("MN:OK", 52, 118);
            } else {
                canvas->setTextColor(TFT_YELLOW);
                canvas->drawString("MN:ND", 52, 118);
            }
            
            // 3. Gimbal Status (x=98)
            bool gimbalConfigured = hw.isEnabled(HW_MOD_UNIT_8SERVOS);
            if (!gimbalConfigured) {
                canvas->setTextColor(TFT_DARKGREY);
                canvas->drawString("GB:--", 98, 118);
            } else if (gimbal.isOnline()) {
                uint16_t gbColor = TFT_GREEN;
                const char* gbStatus = "GB:OK";
                switch (gimbal.getState()) {
                    case GIMBAL_STATE_INITIALIZING: gbColor = TFT_ORANGE; gbStatus = "GB:INI"; break;
                    case GIMBAL_STATE_PREPOINT:     gbColor = TFT_YELLOW; gbStatus = "GB:AIM"; break;
                    case GIMBAL_STATE_TRACKING:     gbColor = TFT_GREEN;  gbStatus = "GB:TRK"; break;
                    case GIMBAL_STATE_STANDBY:      
                    default:                        gbColor = TFT_GREEN;  gbStatus = "GB:OK";  break;
                }
                canvas->setTextColor(gbColor);
                canvas->drawString(gbStatus, 98, 118);
            } else {
                canvas->setTextColor(TFT_YELLOW);
                canvas->drawString("GB:ND", 98, 118);
            }

}
