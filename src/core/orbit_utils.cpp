#include "core/orbit_utils.h"
#include <esp_task_wdt.h>
#include "core/json_parser.h"
#include <LittleFS.h>
#include <math.h>
#include <vector>
#include "core/coord_transform.h"
#include "core/sun_calculator.h"
#include "core/recent_launch_item.h"

extern volatile bool recentLaunchDownloading;
extern bool recentLaunchCacheDirty;

extern bool isSatViewMode;
extern int focusSatIndex;
extern int NUM_SATELLITES;
extern SatProfile g_satellites[];
extern bool g_recentLaunchFocusMode;
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
    if (isTimeScrolling) {
        return; // 快进/调节时间期间完全跳过重算，确保极度丝滑
    }
    static uint32_t lastGlobalCalcMs = 0;
    
    // 限制物理时间上的计算频率。如果上一帧刚刚重算过轨道，那么在物理时间 120 毫秒内，
    // 任何卫星都不能进行轨道线重算（除非是首次计算），确保在任何高速按键或滑动操作下的丝滑帧率。
    // 如果是焦点卫星强制刷新（forceUpdate），则绕过该物理冷却锁。
    if (!forceUpdate && cache.lastCalcTime != 0 && millis() - lastGlobalCalcMs < 120) {
        return;
    }
    
    // Only recalculate orbit path if simulated time has advanced by more than 5 minutes (300 seconds)
    // 如果是焦点卫星强制刷新，则时间发生微小的 60 秒以上改变（即哪怕点按一下时间微调）就进行重新预测
    bool needsCalc = false;
    if (cache.lastCalcTime == 0) {
        needsCalc = true;
    } else {
        if (forceUpdate) {
            needsCalc = (abs((int)baseTime - (int)cache.lastCalcTime) > 60);
        } else {
            needsCalc = (abs((int)baseTime - (int)cache.lastCalcTime) > 300);
        }
    }

    if (needsCalc) {
        if (!forceUpdate && calcCount >= 1) { // Max 1 expensive calculation per frame to prevent lag spikes
            return;
        }
        if (!forceUpdate) {
            lastGlobalCalcMs = millis();
            calcCount++;
        }
        
        cache.past.clear();
        cache.future.clear();
        
        double teme_x = 0, teme_y = 0, teme_z = 0;
        double vx = 0, vy = 0, vz = 0;
        
        // 默认周期 90 分钟 (5400秒)
        double periodSec = 5400.0;
        if (calc.getTEME(baseTime, teme_x, teme_y, teme_z, vx, vy, vz)) {
            // 确保位置和速度均不是 NaN 或 Inf
            if (!std::isnan(teme_x) && !std::isnan(teme_y) && !std::isnan(teme_z) &&
                !std::isinf(teme_x) && !std::isinf(teme_y) && !std::isinf(teme_z) &&
                !std::isnan(vx) && !std::isnan(vy) && !std::isnan(vz) &&
                !std::isinf(vx) && !std::isinf(vy) && !std::isinf(vz)) {
                
                double r = sqrt(teme_x * teme_x + teme_y * teme_y + teme_z * teme_z);
                double v = sqrt(vx * vx + vy * vy + vz * vz);
                double mu = 398600.4418; // 地球重力常数 km^3/s^2
                
                if (r > 0.1) { // 避免除以 0
                    double inv_a = 2.0 / r - (v * v) / mu;
                    if (inv_a > 0.0) {
                        double a = 1.0 / inv_a;
                        double calculatedPeriod = 2.0 * M_PI * sqrt((a * a * a) / mu);
                        // 限制周期在 10 秒到 12 小时（43200 秒）之间，防止溢出或深空模型过载
                        if (!std::isnan(calculatedPeriod) && !std::isinf(calculatedPeriod) && 
                            calculatedPeriod > 10.0 && calculatedPeriod < 12.0 * 3600.0) {
                            periodSec = calculatedPeriod;
                        } else if (calculatedPeriod >= 12.0 * 3600.0) {
                            periodSec = 12.0 * 3600.0; // 超过 12 小时（如 GEO 24 小时卫星），截断到 12 小时以确保 SDP4 稳定性与性能
                        }
                    }
                }
            }
        }
        
        // 动态点数优化：为了确保高轨与中轨卫星在三维地球上的弧线极其圆滑、无多边形折线，
        // 焦点卫星使用 48 对点（共 96 点），非焦点/背景卫星使用 12 对点（共 24 点），兼顾视觉效果与堆内存占用。
        int steps = forceUpdate ? 48 : 12;
        double stepSizeSec = (periodSec * 0.5) / steps;
        
        // 过去半个周期的轨迹 [-T/2, 0]
        for (int i = steps; i >= 0; i--) {
            uint32_t sub = (uint32_t)(i * stepSizeSec);
            uint32_t t = (baseTime >= sub) ? (baseTime - sub) : 0; // 防范下溢至 42 亿秒导致 SDP4 深空异常算力崩溃
            if (calc.getTEME(t, teme_x, teme_y, teme_z)) {
                if (std::isnan(teme_x) || std::isnan(teme_y) || std::isnan(teme_z) ||
                    std::isinf(teme_x) || std::isinf(teme_y) || std::isinf(teme_z)) {
                    continue;
                }
                double gmst = CoordTransform::getGMST(CoordTransform::unixToJulian(t));
                ECEFCoord ecef = CoordTransform::temeToECEF(teme_x, teme_y, teme_z, gmst);
                GeodeticCoord geo = CoordTransform::ecefToGeodetic(ecef);
                if (!std::isnan(geo.lat) && !std::isnan(geo.lon) && !std::isnan(geo.alt) &&
                    !std::isinf(geo.lat) && !std::isinf(geo.lon) && !std::isinf(geo.alt)) {
                    cache.past.push_back(geo);
                }
            }
        }
        
        // 未来半个周期的轨迹 [0, +T/2]
        for (int i = 0; i <= steps; i++) {
            uint32_t add = (uint32_t)(i * stepSizeSec);
            uint32_t t = baseTime;
            if (0xFFFFFFFF - baseTime >= add) {
                t = baseTime + add;
            } else {
                t = 0xFFFFFFFF; // 防范上溢
            }
            if (calc.getTEME(t, teme_x, teme_y, teme_z)) {
                if (std::isnan(teme_x) || std::isnan(teme_y) || std::isnan(teme_z) ||
                    std::isinf(teme_x) || std::isinf(teme_y) || std::isinf(teme_z)) {
                    continue;
                }
                double gmst = CoordTransform::getGMST(CoordTransform::unixToJulian(t));
                ECEFCoord ecef = CoordTransform::temeToECEF(teme_x, teme_y, teme_z, gmst);
                GeodeticCoord geo = CoordTransform::ecefToGeodetic(ecef);
                if (!std::isnan(geo.lat) && !std::isnan(geo.lon) && !std::isnan(geo.alt) &&
                    !std::isinf(geo.lat) && !std::isinf(geo.lon) && !std::isinf(geo.alt)) {
                    cache.future.push_back(geo);
                }
            }
        }
        
        cache.lastCalcTime = baseTime;
    }
}
