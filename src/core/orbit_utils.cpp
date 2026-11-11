#pragma GCC optimize ("O3")

#include "core/orbit_utils.h"
#include <esp_task_wdt.h>
#include "core/json_parser.h"
#include <LittleFS.h>
#include <math.h>
#include <vector>
#include "core/coord_transform.h"
#include "core/sun_calculator.h"
#include "core/recent_launch_item.h"
#include "core/santa_tracker.h"

extern volatile bool recentLaunchDownloading;
extern bool recentLaunchCacheDirty;

extern bool isSatViewMode;
extern int focusSatIndex;
extern int NUM_SATELLITES;
extern SatProfile g_satellites[];
extern bool g_recentLaunchFocusMode;
extern bool g_santaFocusMode;
extern std::vector<RecentLaunchItem> g_recentLaunches;
extern String recentLaunchActiveBatchId;
extern bool g_repSatInitialized;
extern void initRecentLaunchCalcs(RecentLaunchItem& item);

extern SunCalculator* sun_calc;

void OrbitUtils::autoAssignIconAndColor(const String& name, SatIconType& icon, uint16_t& color) {
    String nameUpper = name;
    nameUpper.toUpperCase();
    
    // 1. Rocket body (R/B)
    if (nameUpper.indexOf("R/B") != -1 || nameUpper.indexOf("ROCKET BODY") != -1 || nameUpper.indexOf("ROCKET DEB") != -1) {
        icon = ICON_ROCKET;
        color = TFT_LIGHTGRAY;
    }
    // 2. Debris
    else if (nameUpper.indexOf("DEB") != -1 || nameUpper.indexOf("DEBRIS") != -1) {
        icon = ICON_DEBRIS;
        color = TFT_DARKGREY;
    }
    // 3. Space Station
    else if (nameUpper.indexOf("ISS") != -1 || nameUpper.indexOf("TIANGONG") != -1 || nameUpper.indexOf("CSS") != -1 || nameUpper.indexOf("SPACE STATION") != -1) {
        icon = ICON_STATION;
        color = TFT_YELLOW;
    }
    // 4. Weather Satellites
    else if (nameUpper.indexOf("NOAA") != -1 || nameUpper.indexOf("METEOR") != -1 || nameUpper.indexOf("FENGYUN") != -1 || nameUpper.indexOf("FY-") != -1) {
        icon = ICON_WEATHER;
        color = TFT_ORANGE;
    }
    // 5. Navigation Satellites
    else if (nameUpper.indexOf("BEIDOU") != -1 || nameUpper.indexOf("GPS") != -1 || nameUpper.indexOf("GLONASS") != -1 || nameUpper.indexOf("GALILEO") != -1) {
        icon = ICON_NAVIGATION;
        color = TFT_RED;
    }
    // 6. Telescope / Observatories
    else if (nameUpper.indexOf("HUBBLE") != -1 || nameUpper.indexOf("JWST") != -1 || nameUpper.indexOf("ROMAN") != -1 || nameUpper.indexOf("NGRST") != -1 || nameUpper.indexOf("HERSCHEL") != -1 || nameUpper.indexOf("TELESCOPE") != -1) {
        icon = ICON_TELESCOPE;
        if (nameUpper.indexOf("ROMAN") != -1 || nameUpper.indexOf("NGRST") != -1) {
            color = TFT_MAGENTA;
        } else if (nameUpper.indexOf("HERSCHEL") != -1) {
            color = TFT_YELLOW;
        } else {
            color = TFT_CYAN;
        }
    }
    // 7. Communication
    else if (nameUpper.indexOf("IRIDIUM") != -1 || nameUpper.indexOf("STARLINK") != -1 || nameUpper.indexOf("ONEWEB") != -1 || nameUpper.indexOf("SO-") != -1 || nameUpper.indexOf("AO-") != -1) {
        icon = ICON_COMMUNICATION;
        color = TFT_MAGENTA;
    }
    // 8. Default
    else {
        icon = ICON_SATELLITE;
        color = TFT_WHITE;
    }
}

double OrbitUtils::getGeoSlotLongitude(uint32_t noradId, const String& slotStr) {
    if (noradId == 49125) return 101.4; // ChinaSat 9B
    if (noradId == 52235) return 125.0; // ChinaSat 6D
    if (noradId == 45863) return 134.0; // APStar 6D
    if (noradId == 29037) return 19.2;  // Astra 1KR
    if (noradId == 33403) return -97.0; // Galaxy 19 (97.0°W)
    
    if (noradId == 33051) return 92.2;  // ChinaSat 9
    if (noradId == 42763) return 101.4; // ChinaSat 9A
    if (noradId == 31792) return 115.5; // ChinaSat 6B
    if (noradId == 58250) return 115.5; // ChinaSat 6E
    if (noradId == 32062) return 128.0; // JCSAT-3A
    if (noradId == 42951) return 110.0; // BSAT-4A
    if (noradId == 37258) return 116.0; // KoreaSat 6
    if (noradId == 39500) return 78.5;  // Thaicom 6
    if (noradId == 52899) return 91.5;  // Measat 3d
    if (noradId == 36832) return -7.0;  // Nilesat 201 (7.0°W)
    
    if (slotStr.length() > 0) {
        double val = slotStr.toDouble();
        if (slotStr.indexOf("W") != -1 || slotStr.indexOf("w") != -1) {
            val = -val;
        }
        return val;
    }
    return 0.0;
}

void OrbitUtils::calculateGeoSatPosition(double satLonDeg, double userLatDeg, double userLonDeg, double userAltMeters, GeodeticCoord& outGeo, ECEFCoord& outEcef, TopocentricCoord& outTopo, double& outSkewDeg) {
    outGeo.lat = 0.0;
    outGeo.lon = satLonDeg;
    outGeo.alt = 35785.863; // Standard GEO orbital height above Earth surface (km)
    
    outEcef = CoordTransform::geodeticToECEF(outGeo);
    
    GeodeticCoord obsGeo = {userLatDeg, userLonDeg, userAltMeters / 1000.0};
    outTopo = CoordTransform::ecefToTopocentric(obsGeo, outEcef);
    
    double dLonRad = (userLonDeg - satLonDeg) * DEG_TO_RAD;
    double uLatRad = userLatDeg * DEG_TO_RAD;
    if (fabs(uLatRad) < 1e-5) {
        outSkewDeg = 0.0;
    } else {
        outSkewDeg = atan2(sin(dLonRad), tan(uLatRad)) * RAD_TO_DEG;
    }
}


String OrbitUtils::getShortNameForDisplay(const String& fullName, uint32_t epoch) {
    // 1. Extract prefix by splitting special symbols to get clean constellation/group name
    // e.g., STARLINK-32128 -> STARLINK, QIANFAN-1-03 -> QIANFAN
    String baseName = fullName;
    int sepIdx = baseName.indexOf('-');
    if (sepIdx == -1) sepIdx = baseName.indexOf('_');
    if (sepIdx == -1) sepIdx = baseName.indexOf(' ');
    if (sepIdx != -1) {
        baseName = baseName.substring(0, sepIdx);
    }
    baseName.trim();
    
    String nameUpper = baseName;
    nameUpper.toUpperCase();
    
    char dateBuf[8] = "";
    if (epoch > 0) {
        time_t ep = (time_t)epoch;
        struct tm ep_tm;
        gmtime_r(&ep, &ep_tm);
        sprintf(dateBuf, " %02d/%02d", ep_tm.tm_mon + 1, ep_tm.tm_mday);
    }
    
    // 2. Pick abbreviation: Special case rules for common constellations, general fallbacks for future ones
    String abbr = "";
    if (nameUpper.indexOf("STARLINK") != -1) {
        abbr = "SL";
    } else if (nameUpper.indexOf("ONEWEB") != -1) {
        abbr = "OW";
    } else if (nameUpper.indexOf("KUIPER") != -1) {
        abbr = "KP";
    } else if (nameUpper.indexOf("OBJECT") != -1 || nameUpper.indexOf("DEBRIS") != -1) {
        abbr = "DEB";
    } else if (nameUpper.indexOf("GALAXY") != -1) {
        abbr = "GAL";
    } else if (nameUpper.indexOf("YAOGAN") != -1) {
        abbr = "YG";
    } else if (nameUpper.indexOf("SHIJIAN") != -1) {
        abbr = "SJ";
    } else {
        // High future compatibility general fallback:
        // Slice the first 3 letters of prefix as abbreviation (e.g. QIANFAN -> QIA)
        if (baseName.length() >= 3) {
            abbr = nameUpper.substring(0, 3);
        } else {
            abbr = nameUpper;
        }
    }
    
    return abbr + String(dateBuf);
}

void OrbitUtils::assignShortNameAndIcon(RecentLaunchItem& item) {
    item.shortName = getShortNameForDisplay(item.displayName, item.epoch);
    item.iconType = ICON_SATELLITE;
}


void OrbitUtils::calculateFormationsForItems(std::vector<RecentLaunchItem>& items, const std::vector<std::vector<float>>* providedPhases) {
    if (recentLaunchDownloading) return; // Prevent file read collision during background download
    if (items.empty()) return;
    
    std::vector<std::vector<float>>* allocatedPhases = nullptr;
    const std::vector<std::vector<float>>* rawPhases = providedPhases;
    
    // 如果外部没有直接提供单趟提取的相位，才回退去打开文件解析
    if (!rawPhases) {
        if (!LittleFS.exists("/json_recent_raw.jsonl")) {
            // Fallback: Default dummy values
            for (auto& item : items) {
                assignShortNameAndIcon(item);
                item.occupancy = 0.0f;
                item.proxyFormation.clear();
                FormationPoint fp = {0.0f, 1.0f};
                item.proxyFormation.push_back(fp);
            }
            return;
        }
        
        allocatedPhases = new std::vector<std::vector<float>>(items.size());
        if (!allocatedPhases) return;
        rawPhases = allocatedPhases;
        
        File f = LittleFS.open("/json_recent_raw.jsonl", "r");
        if (!f) {
            delete allocatedPhases;
            return;
        }
        
        JSONParser parser;
        int calcLineCount = 0;
        while (f.available()) {
            String singleLine = f.readStringUntil('\n');
            singleLine.trim();
            if (singleLine.length() == 0) continue;
            calcLineCount++;
            
            OrbitRecord record;
            if (parser.parse(singleLine, record)) {
                String batchId = record.getBatchId();
                if (batchId.length() == 0) continue;
                
                for (size_t i = 0; i < items.size(); i++) {
                    if (items[i].batchId == batchId) {
                        (*allocatedPhases)[i].push_back(record.meanAnomaly);
                        break;
                    }
                }
            }
            
            // 方案三：优化看门狗与调度节拍，从每 5 行改为每 50 行
            if (calcLineCount % 50 == 0) {
                esp_task_wdt_reset();
                taskYIELD();
            }
        }
        f.close();
    }
    
    for (size_t i = 0; i < items.size(); i++) {
        auto& item = items[i];
        const auto& phases = (*rawPhases)[i];
        
        // 方案三：优化聚类运算过程中的看门狗重置频率
        esp_task_wdt_reset();
        if (i % 10 == 0) taskYIELD();
        
        // 1. Assign shortName and icon
        assignShortNameAndIcon(item);
        
        if (phases.empty()) {
            item.occupancy = 0.0f;
            item.repAlongTrackPhase = 0.0f;
            item.proxyFormation.clear();
            FormationPoint fp = {0.0f, 1.0f};
            item.proxyFormation.push_back(fp);
            continue;
        }
        
        // Record repAlongTrackPhase (assuming first read one is representative)
        item.repAlongTrackPhase = phases[0];
        
        // 2. Calculate Occupancy and Start/End Phases using circular max gap
        std::vector<float> sortedPhases = phases;
        std::sort(sortedPhases.begin(), sortedPhases.end());
        
        float maxGap = 0.0f;
        float gapStart = sortedPhases.back();
        float gapEnd = sortedPhases.front();
        
        if (sortedPhases.size() == 1) {
            item.occupancy = 0.0f;
            item.occupancyStartPhase = sortedPhases[0];
            item.occupancyEndPhase = sortedPhases[0];
        } else {
            for (size_t j = 0; j < sortedPhases.size(); j++) {
                float p1 = sortedPhases[j];
                float p2 = sortedPhases[(j + 1) % sortedPhases.size()];
                float gap = p2 - p1;
                if (gap < 0.0f) gap += 360.0f;
                if (gap > maxGap) {
                    maxGap = gap;
                    gapStart = p1;
                    gapEnd = p2;
                }
            }
            item.occupancy = 360.0f - maxGap;
            item.occupancyStartPhase = gapEnd;
            item.occupancyEndPhase = gapStart;
        }
        
        // 3. Hierarchical Agglomerative Clustering to compress N phases into K proxies
        int N = sortedPhases.size();
        int K = 5;
        if (N <= 5) {
            K = N;
        } else if (N < 30) {
            K = 6;
        } else if (N < 80) {
            K = 7;
        } else {
            K = 8;
        }
        
        struct Cluster {
            float phase;
            int count;
        };
        std::vector<Cluster> clusters;
        clusters.reserve(N);
        for (float p : sortedPhases) {
            clusters.push_back({p, 1});
        }
        
        while ((int)clusters.size() > K) {
            float minDist = 360.0f;
            int bestA = -1;
            int bestB = -1;
            
            for (size_t a = 0; a < clusters.size(); a++) {
                for (size_t b = a + 1; b < clusters.size(); b++) {
                    float diff = abs(clusters[a].phase - clusters[b].phase);
                    float d = min(diff, 360.0f - diff);
                    if (d < minDist) {
                        minDist = d;
                        bestA = a;
                        bestB = b;
                    }
                }
            }
            
            if (bestA == -1 || bestB == -1) break;
            
            Cluster& cA = clusters[bestA];
            Cluster& cB = clusters[bestB];
            
            float pA = cA.phase;
            float pB = cB.phase;
            if (abs(pA - pB) > 180.0f) {
                if (pA < pB) pA += 360.0f;
                else pB += 360.0f;
            }
            
            float newPhase = (pA * cA.count + pB * cB.count) / (cA.count + cB.count);
            if (newPhase >= 360.0f) newPhase -= 360.0f;
            
            cA.phase = newPhase;
            cA.count = cA.count + cB.count;
            
            clusters.erase(clusters.begin() + bestB);
        }
        
        // Store into proxyFormation
        item.proxyFormation.clear();
        for (const auto& cl : clusters) {
            FormationPoint fp;
            fp.AlongTrackPhase = cl.phase;
            fp.brightness = 1.0f;
            item.proxyFormation.push_back(fp);
        }
    }
    if (allocatedPhases) {
        delete allocatedPhases;
    }
}


void OrbitUtils::validateSatViewFocusState() {
    if (!isSatViewMode) return;

    // 0. 若当前聚焦的是圣诞老人特别任务，校验时间是否在 12 月飞行期
    if (g_santaFocusMode) {
        if (SantaTracker::isDecember() && SantaTracker::isFlightPhase()) {
            focusSatIndex = -1;
            g_recentLaunchFocusMode = false;
            return;
        } else {
            g_santaFocusMode = false;
        }
    }

    // 1. 若当前聚焦的是常规百科卫星，校验该卫星是否仍然有效且处于勾选状态
    if (focusSatIndex >= 0) {
        if (focusSatIndex >= NUM_SATELLITES || !g_satellites[focusSatIndex].selected) {
            focusSatIndex = -1;
        } else {
            g_recentLaunchFocusMode = false;
            return;
        }
    }

    // 2. 若当前处于最近发射编队聚焦模式，校验当前活跃的 Batch 是否有效且仍被勾选
    if (g_recentLaunchFocusMode) {
        bool activeValid = false;
        for (const auto& item : g_recentLaunches) {
            if (item.selected && item.batchId == recentLaunchActiveBatchId) {
                activeValid = true;
                break;
            }
        }
        if (activeValid) {
            return;
        }
        
        bool foundOther = false;
        for (auto& item : g_recentLaunches) {
            if (item.selected) {
                recentLaunchActiveBatchId = item.batchId;
                initRecentLaunchCalcs(item);
                foundOther = true;
                break;
            }
        }
        if (!foundOther) {
            g_recentLaunchFocusMode = false;
            recentLaunchActiveBatchId = "";
            g_repSatInitialized = false;
        }
    }

    // 3. 兜底搜索：若当前无有效焦点，优先寻找首个被勾选的常规百科卫星
    if (focusSatIndex == -1 && !g_recentLaunchFocusMode) {
        for (int i = 0; i < NUM_SATELLITES; i++) {
            if (g_satellites[i].selected) {
                focusSatIndex = i;
                g_recentLaunchFocusMode = false;
                return;
            }
        }
    }

    // 4. 兜底搜索：若仍无焦点，寻找首个被勾选的最近发射编队
    if (focusSatIndex == -1 && !g_recentLaunchFocusMode) {
        for (auto& item : g_recentLaunches) {
            if (item.selected) {
                g_recentLaunchFocusMode = true;
                recentLaunchActiveBatchId = item.batchId;
                initRecentLaunchCalcs(item);
                return;
            }
        }
    }

    // 5. 若全系统无任何卫星/编队被勾选，自动退出 Sat View 模式
    if (focusSatIndex == -1 && !g_recentLaunchFocusMode) {
        isSatViewMode = false;
    }
}

void OrbitUtils::calculateOrbit(SGP4Calc& calc, uint32_t baseTime, OrbitCache& cache, int& calcCount, bool isTimeScrolling, bool forceUpdate) {
    static uint32_t lastGlobalCalcMs = 0;

    const bool emptyRing = (cache.lastCalcTime == 0 || cache.ring.empty());
    const bool needUpgradeToFocus = (forceUpdate && !cache.isFocusRes);
    const bool needDowngradeToBg = (!forceUpdate && cache.isFocusRes && !isTimeScrolling);

    if (!emptyRing && !needUpgradeToFocus) {
        if (isTimeScrolling) {
            // 闭合轨道由惯性环 + J2 升交点进动 + 实时 GMST 在渲染端 60FPS 精确投影，快进期间无需重算
            if (cache.isClosedLoop) {
                return;
            }
            // 非闭合深空/逃逸轨道：仅当快进滑出缓存窗口 40% 时按帧限流补算一次
            int64_t dt = (int64_t)baseTime - (int64_t)cache.lastCalcTime;
            if (dt < 0) dt = -dt;
            if (dt <= (int64_t)(cache.periodSec * 0.40f) || calcCount >= 1) {
                return;
            }
        } else if (!needDowngradeToBg) {
            if (!forceUpdate && millis() - lastGlobalCalcMs < 150) {
                return;
            }
            int64_t dt = (int64_t)baseTime - (int64_t)cache.lastCalcTime;
            if (dt < 0) dt = -dt;
            int64_t refreshThreshold = cache.isClosedLoop ? 3600 : (int64_t)(cache.periodSec * 0.25f);
            if (refreshThreshold < 600) refreshThreshold = 600;
            if (dt <= refreshThreshold) {
                return;
            }
        }
    }

    // 限流保护：背景卫星首次加载每帧最多 2 颗，常规后台刷新每帧最多 1 颗，彻底杜绝单帧尖峰
    int maxAllowed = emptyRing ? 2 : 1;
    if (!forceUpdate && calcCount >= maxAllowed) {
        return;
    }
    if (!forceUpdate) {
        lastGlobalCalcMs = millis();
        calcCount++;
    }

    double teme_x = 0.0, teme_y = 0.0, teme_z = 0.0;
    double vx = 0.0, vy = 0.0, vz = 0.0;

    double periodSec = 5400.0;
    double raanRate = 0.0;
    bool isClosedLoop = true;

    if (calc.getTEMEFast((double)baseTime, teme_x, teme_y, teme_z, vx, vy, vz)) {
        if (!std::isnan(teme_x) && !std::isnan(teme_y) && !std::isnan(teme_z) &&
            !std::isinf(teme_x) && !std::isinf(teme_y) && !std::isinf(teme_z) &&
            !std::isnan(vx) && !std::isnan(vy) && !std::isnan(vz) &&
            !std::isinf(vx) && !std::isinf(vy) && !std::isinf(vz)) {

            double r = sqrt(teme_x * teme_x + teme_y * teme_y + teme_z * teme_z);
            double vSq = vx * vx + vy * vy + vz * vz;
            const double mu = 398600.4418;

            if (r > 100.0) {
                double inv_a = 2.0 / r - vSq / mu;
                if (inv_a > 0.0) {
                    double a = 1.0 / inv_a;
                    double calculatedPeriod = 2.0 * M_PI * sqrt((a * a * a) / mu);
                    if (!std::isnan(calculatedPeriod) && !std::isinf(calculatedPeriod) && calculatedPeriod > 60.0) {
                        if (calculatedPeriod <= 26.0 * 3600.0) {
                            periodSec = calculatedPeriod;
                            isClosedLoop = true;

                            // 计算 J2 长期升交点赤经进动角速度 (rad/s)
                            double hx = teme_y * vz - teme_z * vy;
                            double hy = teme_z * vx - teme_x * vz;
                            double hz = teme_x * vy - teme_y * vx;
                            double hSq = hx * hx + hy * hy + hz * hz;
                            if (hSq > 1.0) {
                                double hMag = sqrt(hSq);
                                double p = hSq / mu;
                                double cosI = hz / hMag;
                                double n = (2.0 * M_PI) / periodSec;
                                const double J2 = 1.08262668e-3;
                                const double Re = 6378.137;
                                double rpRatio = Re / p;
                                raanRate = -1.5 * J2 * (rpRatio * rpRatio) * n * cosI;
                            }
                        } else {
                            // 超长周期深空轨道（如月球/L2 探测器）：采样前后共 24 小时弧段
                            periodSec = 24.0 * 3600.0;
                            isClosedLoop = false;
                        }
                    }
                } else {
                    // 双曲线/逃逸轨道：采样前后共 3 小时弧段
                    periodSec = 10800.0;
                    isClosedLoop = false;
                }
            }
        }
    }

    int steps = forceUpdate ? 40 : ((periodSec > 10800.0) ? 18 : 12);
    size_t targetCount = (size_t)(2 * steps + 1);

    // 焦点退回背景星时主动收缩归还堆内存，防止碎片累积
    if (cache.ring.capacity() > targetCount + 16) {
        std::vector<InertialOrbitNode>().swap(cache.ring);
    }
    cache.ring.clear();
    if (cache.ring.capacity() < targetCount) {
        cache.ring.reserve(targetCount);
    }

    double stepSizeSec = (periodSec * 0.5) / (double)steps;
    for (int k = -steps; k <= steps; k++) {
        double tau = (double)k * stepSizeSec;
        double tSample = (double)baseTime + tau;
        if (tSample < 0.0) tSample = 0.0;

        if (calc.getTEMEFast(tSample, teme_x, teme_y, teme_z)) {
            if (std::isnan(teme_x) || std::isnan(teme_y) || std::isnan(teme_z) ||
                std::isinf(teme_x) || std::isinf(teme_y) || std::isinf(teme_z)) {
                continue;
            }
            // 解缠单圈内的 J2 升交点漂移，使闭合环严格对齐至 baseTime 惯性参考架
            double unwindAng = -raanRate * tau;
            double c = cos(unwindAng);
            double s = sin(unwindAng);
            InertialOrbitNode node;
            node.tx = (float)(teme_x * c - teme_y * s);
            node.ty = (float)(teme_x * s + teme_y * c);
            node.tz = (float)teme_z;
            cache.ring.push_back(node);
        }
    }

    // 闭合轨道首尾 C0 无缝缝合，且保持中点 (k = 0) 零扰动
    if (isClosedLoop && cache.ring.size() == targetCount && targetCount >= 3) {
        const size_t L = targetCount - 1;
        float seamX = cache.ring[L].tx - cache.ring[0].tx;
        float seamY = cache.ring[L].ty - cache.ring[0].ty;
        float seamZ = cache.ring[L].tz - cache.ring[0].tz;
        float invL = 1.0f / (float)L;
        for (size_t j = 0; j <= L; j++) {
            float factor = ((float)j - (float)steps) * invL;
            cache.ring[j].tx -= seamX * factor;
            cache.ring[j].ty -= seamY * factor;
            cache.ring[j].tz -= seamZ * factor;
        }
        cache.ring[L] = cache.ring[0];
    }

    cache.periodSec = (float)periodSec;
    cache.raanRateRadPerSec = (float)raanRate;
    cache.isClosedLoop = isClosedLoop;
    cache.isFocusRes = forceUpdate;
    cache.lastCalcTime = baseTime;
}
