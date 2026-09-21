#include "core/radio_tracking_pipeline.h"
#include <math.h>
#include <vector>
#include "core/coord_transform.h"
#include "core/radio_manager.h"
#include "ui/rf_console_view.h"
#include "core/recent_launch_item.h"
#include "core/observation_predictor.h"
#include "core/encyclopedia.h"

extern double baseUserLat;
extern double baseUserLon;
extern double baseUserAlt;

extern bool isSatViewMode;
extern int focusSatIndex;
extern int NUM_SATELLITES;
extern SatProfile g_satellites[];
extern SatRealtimeCache g_satCaches[];

extern void lockPassMutex();
extern void unlockPassMutex();
extern std::vector<PassEvent> recommendedPasses;
extern unsigned long lastTimeAdjustMillis;

void RadioTrackingPipeline::update(uint32_t currentSimTime, int32_t tmOffset) {
    GeodeticCoord obsRadio = {baseUserLat, baseUserLon, baseUserAlt / 1000.0};

    int chosenRadioSat = -1;
    // 1. 优先选择：如果在 Sat View 视角，且选中的卫星有无线电下行频率
    if (isSatViewMode && focusSatIndex >= 0 && focusSatIndex < NUM_SATELLITES && g_satellites[focusSatIndex].selected) {
        if (g_satellites[focusSatIndex].downlinkFreq.length() > 0) {
            chosenRadioSat = focusSatIndex;
        }
    }
    
    // 2. 否则全局巡天：在所有已勾选的卫星中，寻找当前仰角 > -3° 且仰角最高的无线电卫星
    if (chosenRadioSat < 0) {
        float maxRadioEl = -90.0f;
        double gmst = CoordTransform::getGMST(CoordTransform::unixToJulian(currentSimTime));
        for (int i = 0; i < NUM_SATELLITES; i++) {
            if (!g_satellites[i].selected) continue;
            if (g_satellites[i].downlinkFreq.length() == 0) continue;
            
            float el = -90.0f;
            if (tmOffset == 0 && g_satCaches[i].lastGeoValid) {
                ECEFCoord ec = CoordTransform::geodeticToECEF(g_satCaches[i].lastGeo);
                TopocentricCoord tp = CoordTransform::ecefToTopocentric(obsRadio, ec);
                el = tp.el;
            } else {
                double x = 0, y = 0, z = 0;
                if (g_satellites[i].calc.getTEME(currentSimTime, x, y, z)) {
                    ECEFCoord ec = CoordTransform::temeToECEF(x, y, z, gmst);
                    TopocentricCoord tp = CoordTransform::ecefToTopocentric(obsRadio, ec);
                    el = tp.el;
                }
            }

            if (el > -3.0f && el > maxRadioEl) {
                maxRadioEl = el;
                chosenRadioSat = i;
            }
        }
    }

    bool isUpcomingPass = false;
    PassEvent upcomingEvent;

    // 3. 待命预位：若当前无正在过境卫星，从 recommendedPasses 中寻找下一次最早过境的已勾选无线电卫星
    if (chosenRadioSat < 0) {
        uint32_t earliestAos = 0xFFFFFFFF;
        lockPassMutex();
        for (const auto& p : recommendedPasses) {
            if (p.aosTime > currentSimTime) {
                int satIdx = -1;
                if (p.satIndex >= 0 && p.satIndex < NUM_SATELLITES) {
                    satIdx = p.satIndex;
                } else {
                    for (int i = 0; i < NUM_SATELLITES; i++) {
                        if (g_satellites[i].name == p.satName) {
                            satIdx = i;
                            break;
                        }
                    }
                }
                if (satIdx >= 0 && g_satellites[satIdx].selected && g_satellites[satIdx].downlinkFreq.length() > 0) {
                    if (p.aosTime < earliestAos) {
                        earliestAos = p.aosTime;
                        upcomingEvent = p;
                        chosenRadioSat = satIdx;
                        isUpcomingPass = true;
                    }
                }
            }
        }
        unlockPassMutex();
    }

    uint32_t rNorad = 0;
    String rName = "";
    float rEl = -90.0f;
    bool rHasRadio = false;
    float rFreq = 0.0f;
    String rMode = "";

    RadioTrackingInfo trackInfo;
    trackInfo.timeOffsetSec = tmOffset;

    if (chosenRadioSat >= 0 && chosenRadioSat < NUM_SATELLITES) {
        rNorad = g_satellites[chosenRadioSat].noradId;
        rName = g_satellites[chosenRadioSat].name;
        float rAz = 0.0f;
        if (tmOffset == 0 && g_satCaches[chosenRadioSat].lastGeoValid) {
            ECEFCoord ec = CoordTransform::geodeticToECEF(g_satCaches[chosenRadioSat].lastGeo);
            TopocentricCoord tp = CoordTransform::ecefToTopocentric(obsRadio, ec);
            rEl = tp.el;
            rAz = tp.az;
        } else {
            double curX = 0, curY = 0, curZ = 0;
            if (g_satellites[chosenRadioSat].calc.getTEME(currentSimTime, curX, curY, curZ)) {
                double curGmst = CoordTransform::getGMST(CoordTransform::unixToJulian(currentSimTime));
                ECEFCoord curEc = CoordTransform::temeToECEF(curX, curY, curZ, curGmst);
                TopocentricCoord curTp = CoordTransform::ecefToTopocentric(obsRadio, curEc);
                rEl = curTp.el;
                rAz = curTp.az;
            }
        }
        if (g_satellites[chosenRadioSat].downlinkFreq.length() > 0) {
            rHasRadio = true;
            rFreq = g_satellites[chosenRadioSat].downlinkFreq.toFloat();
            rMode = g_satellites[chosenRadioSat].radioMode;
        }

        trackInfo.hasPass = !isUpcomingPass;
        trackInfo.isUpcoming = isUpcomingPass;
        trackInfo.satNorad = rNorad;
        trackInfo.satName = rName;
        trackInfo.currentEl = rEl;
        trackInfo.currentAz = rAz;
        trackInfo.baseFreqMHz = rFreq;
        trackInfo.modulation = rMode;
        if (trackInfo.modulation.length() == 0) {
            const EncyclopediaEntry* entry = Encyclopedia::getEntryByNorad(rNorad);
            if (entry && entry->radioMode && strlen(entry->radioMode) > 0) {
                trackInfo.modulation = entry->radioMode;
            }
        }
        trackInfo.currentSimTime = currentSimTime;
        trackInfo.satIconType = g_satellites[chosenRadioSat].iconType;
        trackInfo.satColor = g_satellites[chosenRadioSat].color;

        // 1. 实时对地斜距 (Slant Range) 与多普勒频移计算 (基于 1 秒微分离散差分)
        double curX0 = 0, curY0 = 0, curZ0 = 0;
        double curX1 = 0, curY1 = 0, curZ1 = 0;
        if (g_satellites[chosenRadioSat].calc.getTEME(currentSimTime, curX0, curY0, curZ0)) {
            double g0 = CoordTransform::getGMST(CoordTransform::unixToJulian(currentSimTime));
            ECEFCoord satEcef0 = CoordTransform::temeToECEF(curX0, curY0, curZ0, g0);
            ECEFCoord obsEcef = CoordTransform::geodeticToECEF(obsRadio);
            double d0 = sqrt(sq(satEcef0.x - obsEcef.x) + sq(satEcef0.y - obsEcef.y) + sq(satEcef0.z - obsEcef.z));
            trackInfo.distanceKm = (float)d0;

            if (rFreq > 0.0f && g_satellites[chosenRadioSat].calc.getTEME(currentSimTime + 1, curX1, curY1, curZ1)) {
                double g1 = CoordTransform::getGMST(CoordTransform::unixToJulian(currentSimTime + 1));
                ECEFCoord satEcef1 = CoordTransform::temeToECEF(curX1, curY1, curZ1, g1);
                double d1 = sqrt(sq(satEcef1.x - obsEcef.x) + sq(satEcef1.y - obsEcef.y) + sq(satEcef1.z - obsEcef.z));
                double vr = d1 - d0; // km/s (负为接近/蓝移, 正为远离/红移)
                double c_kms = 299792.458;
                trackInfo.dopplerHz = -(float)(rFreq * 1e6 * (vr / c_kms));
            }
        }

        // 2. 匹配或快速估算本次过境的 AOS, TCA, LOS, MaxEl 及进出境方位角
        bool passFound = false;
        if (isUpcomingPass) {
            trackInfo.aosTime = upcomingEvent.aosTime;
            trackInfo.tcaTime = upcomingEvent.maxElevTime;
            trackInfo.losTime = upcomingEvent.losTime;
            trackInfo.maxEl = upcomingEvent.maxElevation;
            trackInfo.aosAz = upcomingEvent.startAz;
            trackInfo.losAz = upcomingEvent.endAz;
            passFound = true;
        } else {
            lockPassMutex();
            for (const auto& p : recommendedPasses) {
                if ((p.satIndex == chosenRadioSat || p.satName == rName) && 
                    (int64_t)currentSimTime >= (int64_t)p.aosTime - 300 && 
                    (int64_t)currentSimTime <= (int64_t)p.losTime + 60) {
                    trackInfo.aosTime = p.aosTime;
                    trackInfo.tcaTime = p.maxElevTime;
                    trackInfo.losTime = p.losTime;
                    trackInfo.maxEl = p.maxElevation;
                    trackInfo.aosAz = p.startAz;
                    trackInfo.losAz = p.endAz;
                    passFound = true;
                    break;
                }
            }
            unlockPassMutex();
        }

        // 静态缓存：在同一颗卫星同一次过境事件的生命周期内，锁定 AOS/TCA/LOS/Az，彻底杜绝时间补偿时的漂移
        static uint32_t s_cachedSatNorad = 0;
        static uint32_t s_cachedAos = 0;
        static uint32_t s_cachedLos = 0;
        static uint32_t s_cachedTca = 0;
        static float s_cachedMaxEl = 0.0f;
        static float s_cachedAosAz = 0.0f;
        static float s_cachedLosAz = 0.0f;

        if (!passFound) {
            // 缓存有效性判定：同一颗卫星、且当前模拟时间尚未超过过境结束+60秒、且过境点在未来合理窗口内（4小时内）
            if (s_cachedSatNorad == rNorad && s_cachedAos > 0 && s_cachedLos > s_cachedAos &&
                currentSimTime <= s_cachedLos + 60 && currentSimTime + 14400 >= s_cachedAos) {
                trackInfo.aosTime = s_cachedAos;
                trackInfo.tcaTime = s_cachedTca;
                trackInfo.losTime = s_cachedLos;
                trackInfo.maxEl = s_cachedMaxEl;
                trackInfo.aosAz = s_cachedAosAz;
                trackInfo.losAz = s_cachedLosAz;
                passFound = true;
            }
        }

        // 若用户正在长按按键快进调节时间（时光机高速滚动），跳过昂贵的未来步进轨道搜索，保持极致流畅
        if (!passFound && lastTimeAdjustMillis != 0) {
            if (s_cachedSatNorad == rNorad && s_cachedAos > 0) {
                trackInfo.aosTime = s_cachedAos;
                trackInfo.tcaTime = s_cachedTca;
                trackInfo.losTime = s_cachedLos;
                trackInfo.maxEl = s_cachedMaxEl;
                trackInfo.aosAz = s_cachedAosAz;
                trackInfo.losAz = s_cachedLosAz;
            } else {
                trackInfo.aosTime = currentSimTime + 3600;
                trackInfo.tcaTime = currentSimTime + 3900;
                trackInfo.losTime = currentSimTime + 4200;
                trackInfo.maxEl = 45.0f;
            }
            passFound = true;
        }

        if (!passFound) {
            auto getEl = [&](uint32_t t) -> float {
                double x, y, z;
                if (g_satellites[chosenRadioSat].calc.getTEME(t, x, y, z)) {
                    double gmst = CoordTransform::getGMST(CoordTransform::unixToJulian(t));
                    ECEFCoord ec = CoordTransform::temeToECEF(x, y, z, gmst);
                    TopocentricCoord tp = CoordTransform::ecefToTopocentric(obsRadio, ec);
                    return tp.el;
                }
                return -90.0f;
            };

            auto getAz = [&](uint32_t t) -> float {
                double x, y, z;
                if (g_satellites[chosenRadioSat].calc.getTEME(t, x, y, z)) {
                    double gmst = CoordTransform::getGMST(CoordTransform::unixToJulian(t));
                    ECEFCoord ec = CoordTransform::temeToECEF(x, y, z, gmst);
                    TopocentricCoord tp = CoordTransform::ecefToTopocentric(obsRadio, ec);
                    return tp.az;
                }
                return 0.0f;
            };

            uint32_t stepAos = currentSimTime;
            uint32_t stepLos = currentSimTime + 600;

            if (rEl > 0.0f) {
                // 当前正在过境：向前搜索升交时刻 AOS
                for (int s = 0; s < 30; s++) {
                    uint32_t t = currentSimTime - (s + 1) * 30;
                    if (getEl(t) <= 0.0f) {
                        stepAos = t;
                        break;
                    }
                }
                // 向后搜索降交时刻 LOS
                for (int s = 0; s < 30; s++) {
                    uint32_t t = currentSimTime + (s + 1) * 30;
                    if (getEl(t) <= 0.0f) {
                        stepLos = t;
                        break;
                    }
                }
            } else {
                // 当前在地平线以下：向未来探测下一次升交时刻 AOS (以 120 秒步长快速跳步探测未来 2 小时)
                bool nextAosFound = false;
                for (int s = 0; s < 60; s++) {
                    uint32_t t = currentSimTime + s * 120;
                    if (getEl(t) > 0.0f) {
                        stepAos = (s > 0) ? (currentSimTime + (s - 1) * 120) : t;
                        nextAosFound = true;
                        break;
                    }
                }
                if (nextAosFound) {
                    stepLos = stepAos + 600;
                    for (int s = 0; s < 15; s++) {
                        uint32_t t = stepAos + (s + 1) * 60;
                        if (getEl(t) <= 0.0f) {
                            stepLos = t;
                            break;
                        }
                    }
                } else {
                    // 未来 2 小时未探测到过境，将缓存有效区延后 2 小时，避免后续帧重复暴力全量探测
                    stepAos = currentSimTime + 7200;
                    stepLos = currentSimTime + 7800;
                }
            }

            // 在 [stepAos, stepLos] 整个过境区间内快速采样最高仰角点 TCA
            float peakEl = -90.0f;
            uint32_t peakTime = (stepAos + stepLos) / 2;
            uint32_t searchSpan = (stepLos > stepAos) ? (stepLos - stepAos) : 600;
            int numSteps = 8;
            uint32_t stepSec = searchSpan / numSteps;
            if (stepSec < 10) stepSec = 10;

            for (uint32_t t = stepAos; t <= stepLos; t += stepSec) {
                float el = getEl(t);
                if (el > peakEl) {
                    peakEl = el;
                    peakTime = t;
                }
            }

            trackInfo.aosTime = stepAos;
            trackInfo.losTime = stepLos;
            trackInfo.tcaTime = peakTime;
            trackInfo.maxEl = (peakEl > 0.0f) ? peakEl : 0.0f;
            trackInfo.aosAz = getAz(stepAos);
            trackInfo.losAz = getAz(stepLos);

            // 写入静态缓存锁定状态
            s_cachedSatNorad = rNorad;
            s_cachedAos = stepAos;
            s_cachedLos = stepLos;
            s_cachedTca = peakTime;
            s_cachedMaxEl = trackInfo.maxEl;
            s_cachedAosAz = trackInfo.aosAz;
            s_cachedLosAz = trackInfo.losAz;
        }

        trackInfo.isRising = (currentSimTime < trackInfo.tcaTime);
    }

    RadioManager::getInstance().updateTracking(trackInfo);
    if (!isUpcomingPass) {
        RadioManager::getInstance().update(rNorad, rName, rEl, rHasRadio, rFreq, rMode);
    } else {
        RadioManager::getInstance().update(0, "", rEl, false, 0.0f, "");
    }
}
