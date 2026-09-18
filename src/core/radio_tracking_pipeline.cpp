#include "core/radio_tracking_pipeline.h"
#include <math.h>
#include <vector>
#include "core/coord_transform.h"
#include "core/radio_manager.h"
#include "ui/rf_console_view.h"
#include "core/recent_launch_item.h"
#include "core/observation_predictor.h"

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

        trackInfo.hasPass = true;
        trackInfo.satNorad = rNorad;
        trackInfo.satName = rName;
        trackInfo.currentEl = rEl;
        trackInfo.currentAz = rAz;
        trackInfo.baseFreqMHz = rFreq;

        // 1. 多普勒频移计算 (基于 1 秒微分离散差分)
        if (rFreq > 0.0f) {
            double x0 = 0, y0 = 0, z0 = 0;
            double x1 = 0, y1 = 0, z1 = 0;
            if (g_satellites[chosenRadioSat].calc.getTEME(currentSimTime, x0, y0, z0) &&
                g_satellites[chosenRadioSat].calc.getTEME(currentSimTime + 1, x1, y1, z1)) {
                double g0 = CoordTransform::getGMST(CoordTransform::unixToJulian(currentSimTime));
                double g1 = CoordTransform::getGMST(CoordTransform::unixToJulian(currentSimTime + 1));
                ECEFCoord satEcef0 = CoordTransform::temeToECEF(x0, y0, z0, g0);
                ECEFCoord satEcef1 = CoordTransform::temeToECEF(x1, y1, z1, g1);
                ECEFCoord obsEcef = CoordTransform::geodeticToECEF(obsRadio);

                double d0 = sqrt(sq(satEcef0.x - obsEcef.x) + sq(satEcef0.y - obsEcef.y) + sq(satEcef0.z - obsEcef.z));
                double d1 = sqrt(sq(satEcef1.x - obsEcef.x) + sq(satEcef1.y - obsEcef.y) + sq(satEcef1.z - obsEcef.z));
                double vr = d1 - d0; // km/s (负为接近/蓝移, 正为远离/红移)
                double c_kms = 299792.458;
                trackInfo.dopplerHz = -(float)(rFreq * 1e6 * (vr / c_kms));
            }
        }

        // 2. 匹配或快速估算本次过境的 AOS, TCA, LOS, MaxEl
        bool passFound = false;
        lockPassMutex();
        for (const auto& p : recommendedPasses) {
            if ((p.satIndex == chosenRadioSat || p.satName == rName) && 
                (int64_t)currentSimTime >= (int64_t)p.aosTime - 300 && 
                (int64_t)currentSimTime <= (int64_t)p.losTime + 60) {
                trackInfo.aosTime = p.aosTime;
                trackInfo.tcaTime = p.maxElevTime;
                trackInfo.losTime = p.losTime;
                trackInfo.maxEl = p.maxElevation;
                passFound = true;
                break;
            }
        }
        unlockPassMutex();

        if (!passFound) {
            // 若无缓存，在 currentSimTime 前后快速步进探测过境区间
            uint32_t stepAos = currentSimTime;
            for (int s = 0; s < 12; s++) {
                uint32_t tTest = currentSimTime - (s + 1) * 60;
                double xt, yt, zt;
                if (g_satellites[chosenRadioSat].calc.getTEME(tTest, xt, yt, zt)) {
                    double gt = CoordTransform::getGMST(CoordTransform::unixToJulian(tTest));
                    ECEFCoord sect = CoordTransform::temeToECEF(xt, yt, zt, gt);
                    TopocentricCoord tpt = CoordTransform::ecefToTopocentric(obsRadio, sect);
                    if (tpt.el <= 0.0f) {
                        stepAos = tTest;
                        break;
                    }
                }
            }
            uint32_t stepLos = currentSimTime + 600;
            float peakEl = rEl;
            uint32_t peakTime = currentSimTime;
            for (int s = 0; s < 12; s++) {
                uint32_t tTest = currentSimTime + (s + 1) * 60;
                double xt, yt, zt;
                if (g_satellites[chosenRadioSat].calc.getTEME(tTest, xt, yt, zt)) {
                    double gt = CoordTransform::getGMST(CoordTransform::unixToJulian(tTest));
                    ECEFCoord sect = CoordTransform::temeToECEF(xt, yt, zt, gt);
                    TopocentricCoord tpt = CoordTransform::ecefToTopocentric(obsRadio, sect);
                    if (tpt.el > peakEl) {
                        peakEl = tpt.el;
                        peakTime = tTest;
                    }
                    if (tpt.el <= 0.0f) {
                        stepLos = tTest;
                        break;
                    }
                }
            }
            trackInfo.aosTime = stepAos;
            trackInfo.losTime = stepLos;
            trackInfo.tcaTime = peakTime;
            trackInfo.maxEl = peakEl > rEl ? peakEl : rEl;
        }

        trackInfo.isRising = (currentSimTime < trackInfo.tcaTime);
    }

    RadioManager::getInstance().updateTracking(trackInfo);
    RadioManager::getInstance().update(rNorad, rName, rEl, rHasRadio, rFreq, rMode);
}
