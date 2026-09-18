#include "gimbal/gimbal_tracking_pipeline.h"
#include <math.h>
#include <vector>
#include "gimbal/gimbal_controller.h"
#include "core/coord_transform.h"
#include "core/recent_launch_item.h"
#include "core/observation_predictor.h"

extern GimbalController gimbal;
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

void GimbalTrackingPipeline::update(uint32_t currentSimTime) {
    if (!gimbal.isOnline()) return;
    GeodeticCoord observerPos = {baseUserLat, baseUserLon, baseUserAlt / 1000.0};
            // currentSimTime passed in
            // observerPos initialized above
            
            static int s_trackingSatIndex = -1;
            static bool s_inPassSession = false;
            static PassEvent s_lockedPass;
            static float s_lockedHeading = 90.0f;
            
            // 辅助 Lambda：解算指定卫星在特定时刻在天平面的真实飞行航向角 Track Heading
            auto getSatTrackHeading = [&](int satIdx, uint32_t t) -> float {
                if (satIdx < 0 || satIdx >= NUM_SATELLITES) return 90.0f;
                double x0 = 0, y0 = 0, z0 = 0;
                double x1 = 0, y1 = 0, z1 = 0;
                if (g_satellites[satIdx].calc.getTEME(t, x0, y0, z0) &&
                    g_satellites[satIdx].calc.getTEME(t + 15, x1, y1, z1)) {
                    double g0 = CoordTransform::getGMST(CoordTransform::unixToJulian(t));
                    double g1 = CoordTransform::getGMST(CoordTransform::unixToJulian(t + 15));
                    ECEFCoord ec0 = CoordTransform::temeToECEF(x0, y0, z0, g0);
                    ECEFCoord ec1 = CoordTransform::temeToECEF(x1, y1, z1, g1);
                    TopocentricCoord tp0 = CoordTransform::ecefToTopocentric(observerPos, ec0);
                    TopocentricCoord tp1 = CoordTransform::ecefToTopocentric(observerPos, ec1);
                    
                    float r0 = cosf(tp0.el * DEG_TO_RAD);
                    float e0 = r0 * sinf(tp0.az * DEG_TO_RAD);
                    float n0 = r0 * cosf(tp0.az * DEG_TO_RAD);
                    
                    float r1 = cosf(tp1.el * DEG_TO_RAD);
                    float e1 = r1 * sinf(tp1.az * DEG_TO_RAD);
                    float n1 = r1 * cosf(tp1.az * DEG_TO_RAD);
                    
                    float de = e1 - e0;
                    float dn = n1 - n0;
                    if (fabsf(de) > 1e-5f || fabsf(dn) > 1e-5f) {
                        float hdg = atan2f(de, dn) * RAD_TO_DEG;
                        if (hdg < 0.0f) hdg += 360.0f;
                        return hdg;
                    }
                }
                return 90.0f;
            };

            int targetTrackSat = -1;
            bool isTargetPassing = false;

            // =======================================================================
            // 模式分支 1：用户进入 Sat View 单星特写视口（用户意图最高优先 Focus Override）
            // =======================================================================
            if (isSatViewMode && focusSatIndex >= 0 && focusSatIndex < NUM_SATELLITES && g_satellites[focusSatIndex].selected) {
                targetTrackSat = focusSatIndex;
                if (g_satCaches[targetTrackSat].lastGeoValid) {
                    ECEFCoord ec = CoordTransform::geodeticToECEF(g_satCaches[targetTrackSat].lastGeo);
                    TopocentricCoord tp = CoordTransform::ecefToTopocentric(observerPos, ec);
                    if (tp.el >= 3.0f) isTargetPassing = true;
                }
            } 
            // =======================================================================
            // 模式分支 2：主地球仪大盘全局视图（全天自主巡天 Autonomous Sky Patrol）
            // =======================================================================
            else {
                // 1. 扫描当前所有已选卫星在天穹的实时状态，执行多星评分仲裁
                int bestPassingSat = -1;
                float highestScore = -999.0f;

                for (int i = 0; i < NUM_SATELLITES; i++) {
                    if (!g_satellites[i].selected || !g_satCaches[i].lastGeoValid) continue;

                    ECEFCoord ec = CoordTransform::geodeticToECEF(g_satCaches[i].lastGeo);
                    TopocentricCoord tp = CoordTransform::ecefToTopocentric(observerPos, ec);

                    // 门槛：仰角需高于地平线 3.0°（过滤遮挡与地平线边缘杂音）
                    if (tp.el >= 3.0f) {
                        float score = tp.el * 1.5f; // 仰角越高得分越高

                        // 特权空间站额外加分（优先追踪引人注目的人类空间站）
                        String satNameUpper = g_satellites[i].name;
                        satNameUpper.toUpperCase();
                        if (satNameUpper.indexOf("ISS") >= 0 || satNameUpper.indexOf("CSS") >= 0 || 
                            satNameUpper.indexOf("TIANGONG") >= 0 || satNameUpper.indexOf("SPACE STATION") >= 0) {
                            score += 80.0f;
                        }

                        // 会话黏性加分：当前正在跟踪的卫星赋予 +40 分防抽搐加权
                        // 保证浑仪顺畅追踪该星至落山，除非有高特权空间站升空才允许中途换星
                        if (i == s_trackingSatIndex && s_inPassSession) {
                            score += 40.0f;
                        }

                        if (score > highestScore) {
                            highestScore = score;
                            bestPassingSat = i;
                        }
                    }
                }

                if (bestPassingSat >= 0) {
                    targetTrackSat = bestPassingSat;
                    isTargetPassing = true;
                }
            }

            // =======================================================================
            // 执行调度跟踪或全局预瞄
            // =======================================================================
            if (isTargetPassing && targetTrackSat >= 0) {
                // -------------------------------------------------------------
                // A. 目标卫星正在过境中（平滑推行轨道拱门）
                // -------------------------------------------------------------
                if (s_trackingSatIndex != targetTrackSat) {
                    s_trackingSatIndex = targetTrackSat;
                    s_inPassSession = false; // 换星转场，重新初始化会话
                }

                ECEFCoord satEcef = CoordTransform::geodeticToECEF(g_satCaches[targetTrackSat].lastGeo);
                TopocentricCoord topo = CoordTransform::ecefToTopocentric(observerPos, satEcef);

                if (!s_inPassSession) {
                    bool foundPass = false;
                    PassEvent activePass;
                    
                    lockPassMutex();
                    for (const auto& pass : recommendedPasses) {
                        if (pass.satName == g_satellites[targetTrackSat].name) {
                            if (currentSimTime >= (pass.aosTime > 60 ? pass.aosTime - 60 : 0) && currentSimTime <= pass.losTime + 60) {
                                activePass = pass;
                                foundPass = true;
                                break;
                            }
                        }
                    }
                    unlockPassMutex();
                    
                    if (foundPass && activePass.losTime > activePass.aosTime) {
                        s_lockedPass = activePass;
                    } else {
                        // 就地推算精准过境
                        s_lockedPass.satName = g_satellites[targetTrackSat].name;
                        s_lockedPass.maxElevation = max((float)topo.el, 15.0f);
                        s_lockedPass.maxAz = topo.az;
                        
                        uint32_t tAos = currentSimTime;
                        for (int k = 1; k <= 45; k++) {
                            uint32_t tb = currentSimTime - k * 20;
                            double bx=0, by=0, bz=0;
                            if (g_satellites[targetTrackSat].calc.getTEME(tb, bx, by, bz)) {
                                double gmst = CoordTransform::getGMST(CoordTransform::unixToJulian(tb));
                                ECEFCoord bec = CoordTransform::temeToECEF(bx, by, bz, gmst);
                                TopocentricCoord btp = CoordTransform::ecefToTopocentric(observerPos, bec);
                                if (btp.el < 0.0f) { tAos = tb; break; }
                                if (btp.el > s_lockedPass.maxElevation) { s_lockedPass.maxElevation = (float)btp.el; s_lockedPass.maxAz = btp.az; }
                            }
                        }
                        uint32_t tLos = currentSimTime + 600;
                        for (int k = 1; k <= 45; k++) {
                            uint32_t tf = currentSimTime + k * 20;
                            double fx=0, fy=0, fz=0;
                            if (g_satellites[targetTrackSat].calc.getTEME(tf, fx, fy, fz)) {
                                double gmst = CoordTransform::getGMST(CoordTransform::unixToJulian(tf));
                                ECEFCoord fec = CoordTransform::temeToECEF(fx, fy, fz, gmst);
                                TopocentricCoord ftp = CoordTransform::ecefToTopocentric(observerPos, fec);
                                if (ftp.el < 0.0f) { tLos = tf; break; }
                                if (ftp.el > s_lockedPass.maxElevation) { s_lockedPass.maxElevation = (float)ftp.el; s_lockedPass.maxAz = ftp.az; }
                            }
                        }
                        s_lockedPass.aosTime = tAos;
                        s_lockedPass.losTime = tLos;
                    }
                    uint32_t tMid = (s_lockedPass.aosTime + s_lockedPass.losTime) / 2;
                    s_lockedHeading = getSatTrackHeading(targetTrackSat, tMid);
                    s_inPassSession = true;
                }

                // 计算过境平滑进度 (0.0 -> 1.0 -> 0° -> 180°)
                float totalDur = (float)(s_lockedPass.losTime - s_lockedPass.aosTime);
                if (totalDur < 30.0f) totalDur = 600.0f;
                float ratio = (float)(currentSimTime - s_lockedPass.aosTime) / totalDur;
                ratio = constrain(ratio, 0.0f, 1.0f);
                float progressDeg = ratio * 180.0f;
                
                // 下发刚性锁定的轨道航向走向、拱高与平滑进度
                gimbal.setTargetArch(s_lockedHeading, s_lockedPass.maxElevation, progressDeg, s_lockedPass.maxAz, g_satellites[targetTrackSat].name.c_str());
            } else {
                // -------------------------------------------------------------
                // B. 空中无正在过境卫星：结束当前会话，执行全局最早下一次过境预瞄
                // -------------------------------------------------------------
                s_inPassSession = false;
                s_trackingSatIndex = -1;

                bool foundNext = false;
                PassEvent nextPass;
                uint32_t earliestAos = 0xFFFFFFFF;
                int nextSatIdx = -1;

                // 1. 在推荐过境列表中检索未来过境
                lockPassMutex();
                for (const auto& pass : recommendedPasses) {
                    if (pass.aosTime > currentSimTime && pass.aosTime < earliestAos) {
                        // 特写模式下仅搜寻焦点卫星；自主巡天模式下搜寻全部已选卫星
                        if (isSatViewMode && focusSatIndex >= 0) {
                            if (pass.satName != g_satellites[focusSatIndex].name) continue;
                        }
                        // 确认该卫星当前处于已选勾选状态
                        for (int k = 0; k < NUM_SATELLITES; k++) {
                            if (g_satellites[k].selected && pass.satName == g_satellites[k].name) {
                                earliestAos = pass.aosTime;
                                nextPass = pass;
                                nextSatIdx = k;
                                foundNext = true;
                                break;
                            }
                        }
                    }
                }
                unlockPassMutex();

                if (foundNext && nextSatIdx >= 0) {
                    uint32_t waitSec = (earliestAos > currentSimTime) ? (earliestAos - currentSimTime) : 0;
                    // 若下次过境在 45 分钟（2700秒）内，积极预瞄升起点静候；若超过 45 分钟，待机归中节能
                    if (waitSec <= 2700) {
                        float nextHeading = getSatTrackHeading(nextSatIdx, earliestAos);
                        gimbal.setTargetPrePointArch(nextHeading, nextPass.maxElevation, nextPass.maxAz, nextPass.satName.c_str());
                    } else if (gimbal.getState() != GIMBAL_STATE_STANDBY) {
                        gimbal.setStandby();
                    }
                } else if (gimbal.getState() != GIMBAL_STATE_HOLD && gimbal.getState() != GIMBAL_STATE_STANDBY) {
                    gimbal.setStandby();
                }
        }
}
