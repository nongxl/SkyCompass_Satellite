#include <Arduino.h>
#include "core/log_manager.h"
#include <M5Cardputer.h>
#include "core/tle_data.h"
#include "core/sgp4_calc.h"
#include "core/coord_transform.h"
#include "core/earth_renderer.h"
#include "core/observation_predictor.h"
#include "core/tle_updater.h"

// Helper to convert UTC date/time to Unix timestamp
uint32_t convertGNSSDateToUnix(int year, int month, int day, int hour, int min, int sec) {
    int days = 0;
    for (int y = 1970; y < year; ++y) days += (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
    const int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    for (int m = 1; m < month; ++m) {
        days += days_in_month[m - 1];
        if (m == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) days++;
    }
    days += day - 1;
    return ((days * 24 + hour) * 60 + min) * 60 + sec;
}

#include "hal/hal_imu.h"
#include "hal/hal_gnss.h"
#include "hal/hal_wifi.h"
#include "core/attitude_estimator.h"
#include "core/position_manager.h"
#include "core/sun_calculator.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "M5Chain.h"

Chain M5Chain;

enum MonoState {
    MONO_STATE_NONE,       // 未定义状态，用于开机强制刷新
    MONO_STATE_IDLE,       // 默认呼吸圆圈状态
    MONO_STATE_COUNTDOWN,  // 倒计时滚动字符状态
    MONO_STATE_PASSING     // 正在过境像素闪烁状态
};

#include "core/mono_animator.h"

// Set to 1 if you have an external M5Chain Mono 8x8 screen module attached to Grove Port.
// Set to 0 (default) to keep Grove port free, which prevents keyboard I2C/UART sharing conflicts on Cardputer.
#define ENABLE_CHAIN_MONO 1

bool isMonoInitialized = false;
uint8_t mono_id = 0;
uint8_t operation_status = 0;

#include "core/mono_icons.h"

void drawCortanaCircle(uint8_t* buffer) {
    drawMonoVisualAnimation(buffer);
}



extern HalImu* imu;
extern HalGnss* gnss;

EarthRenderer* earth_renderer = nullptr;

enum AppState {
    STATE_MAIN,
    STATE_WIFI_SETUP,
    STATE_SAT_SELECT
};
AppState appState = STATE_MAIN;

std::vector<WiFiNetwork> wifiNetworks;
int wifiSelectedIndex = 0;
bool wifiIsScanning = false;
bool wifiIsInputtingPassword = false;
char wifiPasswordBuffer[64] = {0};
int wifiPasswordLen = 0;

int satSelectedIndex = 0;

AttitudeEstimator* attitude = nullptr;
PositionManager* pos_manager = nullptr;
SunCalculator* sun_calc = nullptr;

// Cache structures to avoid running heavy SGP4 math every frame
struct OrbitCache {
    uint32_t lastCalcTime = 0;
    std::vector<GeodeticCoord> past;
    std::vector<GeodeticCoord> future;
};

enum SatelliteType {
    SAT_TYPE_VISUAL,
    SAT_TYPE_HAM,
    SAT_TYPE_WEATHER,
    SAT_TYPE_SPACE_STATION,
    SAT_TYPE_HISTORICAL
};

struct SatProfile {
    int noradId;
    String name;
    uint16_t color;
    int baseScore;
    double stdMag;
    bool selected;
    SatIconType iconType;
    const char* description;
    String downlinkFreq;
    String radioMode;
    String uplinkFreq;
    String tone;
    TLEData tle;
    SGP4Calc calc;
    OrbitCache cache;
    SatelliteType type;
};

struct SatRealtimeCache {
    GeodeticCoord lastGeo;
    bool lastGeoValid = false;
    bool lastInShadow = false;
    bool isVisible = false;
};

enum SatSelectTab {
    TAB_ENCYCLOPEDIA = 0,
    TAB_RECENT_LAUNCH = 1
};

#include <memory>

struct RecentLaunchRealtimeCache {
    GeodeticCoord lastGeo;
    bool lastGeoValid = false;
    bool lastInShadow = false;
    bool isVisible = false;
    OrbitCache cache;
};

struct RecentLaunchItem {
    String batchId;            // 国际标识符前 5 位 (如 "26042")
    String displayName;        // 智能提取出的星座/卫星公共名称前缀
    int satelliteCount;        // 组内包含的卫星数
    bool isGroup;              // 是否为成组任务
    bool selected;             // 用户是否勾选观测
    uint32_t epoch = 0;        // TLE 历元时间戳缓存
    float inclination = 0.0f;  // 轨道倾角缓存
    float avgAlt = 0.0f;       // 平均高度缓存
    String repSatName;         // 缓存的代表卫星名称
    
    std::shared_ptr<SGP4Calc> calc;
    RecentLaunchRealtimeCache cache;

    // New fields for Mission Formation Visualization
    std::vector<FormationPoint> proxyFormation;
    float occupancy = 0.0f;
    float occupancyStartPhase = 0.0f;
    float occupancyEndPhase = 0.0f;
    float repAlongTrackPhase = 0.0f;
    String shortName;
    SatIconType iconType = ICON_SATELLITE;
};



// 全局变量定义
static uint32_t parseTleEpoch(const String& line1) {
    if (line1.length() < 32) return 0;
    String yrStr = line1.substring(18, 20);
    String dayStr = line1.substring(20, 32);
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
extern SatSelectTab currentSatTab;
extern std::vector<RecentLaunchItem> g_recentLaunches;
extern int recentLaunchSelectedIndex;
extern bool g_recentLaunchFocusMode;
extern String recentLaunchActiveBatchId;
extern volatile bool recentLaunchDownloading;
extern volatile bool recentLaunchDownloadSuccess;
extern String recentLaunchErrorMsg;

SatSelectTab currentSatTab = TAB_ENCYCLOPEDIA;
std::vector<RecentLaunchItem> g_recentLaunches;
int recentLaunchSelectedIndex = 0;
bool g_recentLaunchFocusMode = false;
String recentLaunchActiveBatchId = "";
volatile bool recentLaunchDownloading = false;
volatile bool recentLaunchDownloadSuccess = false;
volatile bool g_timeSynced = false;

volatile bool g_networkActive = false;
struct NetworkActiveGuard {
    NetworkActiveGuard() { g_networkActive = true; }
    ~NetworkActiveGuard() { g_networkActive = false; }
};

std::vector<RecentLaunchItem> g_pendingRecentLaunches;
volatile bool g_recentLaunchesPending = false;

// 唯一代表卫星及其缓存（仅用于 Focus 追踪模式）
TLEData g_repSatTLE;
SGP4Calc g_repSatCalc;
RecentLaunchRealtimeCache g_repSatCache;
bool g_repSatInitialized = false;
String g_repSatName = "";
uint32_t recentLaunchDownloadFinishedMs = 0;
bool showListHelp = false;
bool isCameraTransitioning = false;
float targetZoom = 0.95f;

// Level 3 Objects 分页数据结构与状态
bool recentLaunchInObjectsView = false;
int recentLaunchObjectPage = 0;

struct LazyObjectItem {
    String name;
    TLEData tle;
    SGP4Calc calc;
    GeodeticCoord lastGeo;
    bool lastGeoValid = false;
    bool isVisible = false;
    OrbitCache cache;
};
std::vector<LazyObjectItem> g_level3Objects;


static String getShortNameForDisplay(const String& fullName, uint32_t epoch) {
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

static void assignShortNameAndIcon(RecentLaunchItem& item) {
    item.shortName = getShortNameForDisplay(item.displayName, item.epoch);
    item.iconType = ICON_SATELLITE;
}

void calculateFormationsForItems(std::vector<RecentLaunchItem>& items) {
    if (items.empty()) return;
    
    if (!LittleFS.exists("/tle_recent_raw.txt")) {
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
    
    // Store original Mean Anomalies for each item index
    std::vector<std::vector<float>> rawPhases(items.size());
    
    File f = LittleFS.open("/tle_recent_raw.txt", "r");
    if (!f) return;
    
    while (f.available()) {
        String name = f.readStringUntil('\n'); name.trim();
        if (name.length() == 0) break;
        String line1 = f.readStringUntil('\n'); line1.trim();
        String line2 = f.readStringUntil('\n'); line2.trim();
        
        if (line1.length() < 14 || line1.charAt(0) != '1' || line2.length() < 14 || line2.charAt(0) != '2') {
            continue;
        }
        
        String batchId = line1.substring(9, 14);
        for (size_t i = 0; i < items.size(); i++) {
            if (items[i].batchId == batchId) {
                if (line2.length() >= 51) {
                    float ma = line2.substring(43, 51).toFloat();
                    rawPhases[i].push_back(ma);
                }
                break;
            }
        }
    }
    f.close();
    
    for (size_t i = 0; i < items.size(); i++) {
        auto& item = items[i];
        auto& phases = rawPhases[i];
        
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
        std::sort(phases.begin(), phases.end());
        
        float maxGap = 0.0f;
        float gapStart = phases.back();
        float gapEnd = phases.front();
        
        if (phases.size() == 1) {
            item.occupancy = 0.0f;
            item.occupancyStartPhase = phases[0];
            item.occupancyEndPhase = phases[0];
        } else {
            for (size_t j = 0; j < phases.size(); j++) {
                float p1 = phases[j];
                float p2 = phases[(j + 1) % phases.size()];
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
        int N = phases.size();
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
        for (float p : phases) {
            clusters.push_back({p, 1});
        }
        
        while (clusters.size() > (size_t)K) {
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
}


void initRecentLaunchCalcs(RecentLaunchItem& item) {
    if (!item.selected) {
        item.calc.reset();
        return;
    }
    File f = LittleFS.open("/tle_recent_raw.txt", "r");
    if (f) {
        while (f.available()) {
            String name = f.readStringUntil('\n'); name.trim();
            if (name.length() == 0) break;
            String line1 = f.readStringUntil('\n'); line1.trim();
            String line2 = f.readStringUntil('\n'); line2.trim();
            
            if (line1.length() < 14 || line1.charAt(0) != '1' || line2.length() < 14 || line2.charAt(0) != '2') {
                continue;
            }
            if (line1.substring(9, 14) == item.batchId) {
                TLEData tle;
                tle.name = name;
                tle.line1 = line1;
                tle.line2 = line2;
                tle.baseScore = 0;
                item.calc = std::make_shared<SGP4Calc>();
                item.calc->init(tle);
                item.cache.lastGeoValid = false;
                item.cache.isVisible = false;
                
                if (item.batchId == recentLaunchActiveBatchId) {
                    g_repSatTLE = tle;
                    g_repSatCalc = *(item.calc);
                    g_repSatName = name;
                    g_repSatInitialized = true;
                    g_repSatCache = item.cache;
                }
                break;
            }
        }
        f.close();
    }
    LOG_I("RECENT_LAUNCH", "Initialized representative satellite for batch %s: %s", item.batchId.c_str(), item.repSatName.c_str());
}

void loadLevel3ObjectsPage(const RecentLaunchItem& item, int page) {
    g_level3Objects.clear();
    File f = LittleFS.open("/tle_recent_raw.txt", "r");
    if (!f) return;
    
    int skipCount = page * 5;
    int loadCount = 0;
    int matchIndex = 0;
    
    while (f.available() && loadCount < 5) {
        String name = f.readStringUntil('\n'); name.trim();
        if (name.length() == 0) break;
        String line1 = f.readStringUntil('\n'); line1.trim();
        String line2 = f.readStringUntil('\n'); line2.trim();
        if (line1.length() < 14 || line1.charAt(0) != '1' || line2.length() < 14 || line2.charAt(0) != '2') {
            continue;
        }
        
        if (line1.substring(9, 14) == item.batchId) {
            if (matchIndex >= skipCount) {
                LazyObjectItem obj;
                obj.name = name;
                obj.tle.name = name;
                obj.tle.line1 = line1;
                obj.tle.line2 = line2;
                obj.calc.init(obj.tle);
                obj.lastGeoValid = false;
                obj.isVisible = false;
                g_level3Objects.push_back(obj);
                loadCount++;
            }
            matchIndex++;
        }
    }
    f.close();
    LOG_I("RECENT_LAUNCH", "Loaded %d items for page %d (match index starts at %d)", loadCount, page, skipCount);
}

void getRepresentativeOrbitParams(const String& line2, float& inclination, float& avgAlt) {
    if (line2.length() < 63) {
        inclination = 0;
        avgAlt = 0;
        return;
    }
    // Inclination: characters 9-16 (0-indexed, 8 to 16)
    inclination = line2.substring(8, 16).toFloat();
    // Eccentricity: characters 27-33 (0-indexed, 26 to 33)
    float ecc = ("0." + line2.substring(26, 33)).toFloat();
    // Mean Motion: characters 53-63 (0-indexed, 52 to 63)
    float meanMotion = line2.substring(52, 63).toFloat();
    if (meanMotion > 0) {
        double n = meanMotion * 2.0 * 3.141592653589793 / 86400.0;
        double mu = 3.986004418e14;
        double a = pow(mu / (n * n), 1.0 / 3.0) / 1000.0;
        avgAlt = a - 6378.137;
    } else {
        avgAlt = 0;
    }
}
String recentLaunchErrorMsg = "";

const int MAX_SATELLITES = 50;
SatRealtimeCache g_satCaches[MAX_SATELLITES];
const int NUM_BUILTIN_SATELLITES = 20;
int NUM_SATELLITES = NUM_BUILTIN_SATELLITES;

SatProfile g_satellites[MAX_SATELLITES] = {
    {25544, "ISS", TFT_YELLOW, 2, -1.8, true, ICON_STATION, "International Space Station. The largest human-made structure in space, visible as a very bright moving star.", "145.800", "FM/SSTV", "", "", {}, {}, {}, SAT_TYPE_SPACE_STATION},
    {48274, "Tiangong", TFT_GREEN, 1, -0.5, true, ICON_STATION, "China's Tiangong Space Station. A permanent modular space station in LEO.", "", "", "", "", {}, {}, {}, SAT_TYPE_SPACE_STATION},
    {20580, "Hubble", TFT_CYAN, 0, 1.5, true, ICON_TELESCOPE, "Hubble Space Telescope. A vital observatory that revolutionized our understanding of the universe.", "", "", "", "", {}, {}, {}, SAT_TYPE_VISUAL},
    {33591, "NOAA 19", TFT_ORANGE, 0, 3.5, false, ICON_WEATHER, "NOAA weather satellite. Known for transmitting APT weather images back to Earth.", "137.100", "APT", "", "", {}, {}, {}, SAT_TYPE_WEATHER},
    {50463, "JWST", TFT_GOLD, 0, 10.0, false, ICON_DEEPSPACE, "James Webb Space Telescope. Located at L2 point 1.5 million km away, observing in infrared.", "", "", "", "", {}, {}, {}, SAT_TYPE_VISUAL},
    {53807, "BlueWalker 3", TFT_WHITE, 0, 1.0, false, ICON_BLUEWALKER3, "AST SpaceMobile's prototype. Features a massive 64 sqm array, very bright and controversial.", "", "", "", "", {}, {}, {}, SAT_TYPE_VISUAL},
    {118, "Ablestar R/B", TFT_LIGHTGRAY, 0, 4.0, false, ICON_ROCKET, "Ablestar rocket body.", "", "", "", "", {}, {}, {}, SAT_TYPE_VISUAL},
    {25732, "CZ-4B R/B", TFT_ORANGE, 0, 4.0, true, ICON_ROCKET, "Long March 4B rocket body.", "", "", "", "", {}, {}, {}, SAT_TYPE_VISUAL},
    {6155, "Centaur R/B", TFT_LIGHTGRAY, 0, 4.0, false, ICON_ROCKET, "Centaur rocket body.", "", "", "", "", {}, {}, {}, SAT_TYPE_VISUAL},
    {28499, "Ariane 5 R/B", TFT_LIGHTGRAY, 0, 4.0, false, ICON_ROCKET, "Ariane 5 rocket body.", "", "", "", "", {}, {}, {}, SAT_TYPE_VISUAL},
    {41882, "Fengyun-4A", TFT_BLUE, 0, 10.0, false, ICON_WEATHER, "Chinese geostationary meteorological satellite, located 35,786 km above the equator.", "", "", "", "", {}, {}, {}, SAT_TYPE_VISUAL},
    {43539, "BeiDou-3", TFT_RED, 0, 10.0, false, ICON_NAVIGATION, "Medium Earth Orbit navigation satellite part of the BeiDou system (BDS).", "", "", "", "", {}, {}, {}, SAT_TYPE_VISUAL},
    {27386, "Envisat", TFT_LIGHTGRAY, 0, 2.5, false, ICON_SATELLITE, "A huge 8-ton inactive Earth observation satellite. Now one of the largest pieces of space debris.", "", "", "", "", {}, {}, {}, SAT_TYPE_VISUAL},
    {4382, "DFH-1", TFT_RED, 0, 6.0, true, ICON_DFH1, "Dong Fang Hong I. China's first satellite launched in 1970, still orbiting today as a silent monument.\n\nLaunch: 1970-04-24\nStatus: Inactive\nComms: Unavailable\nHAM: Not Supported", "20.009", "Beacon", "", "", {}, {}, {}, SAT_TYPE_HISTORICAL},
    {25994, "Terra", TFT_PINK, 0, 3.0, false, ICON_SATELLITE, "NASA's flagship Earth Observing System satellite.", "", "", "", "", {}, {}, {}, SAT_TYPE_VISUAL},
    {27424, "Aqua", TFT_MAGENTA, 0, 3.0, false, ICON_SATELLITE, "NASA Earth observation satellite focusing on the water cycle.", "", "", "", "", {}, {}, {}, SAT_TYPE_VISUAL},
    {43166, "Iridium 127", TFT_WHITE, 0, 4.0, false, ICON_COMMUNICATION, "Iridium NEXT network. The original 1st-gen Iridium satellites produced legendary 'flares' up to mag -8.", "", "", "", "", {}, {}, {}, SAT_TYPE_VISUAL},
    {57165, "Meteor-M2", TFT_WHITE, 0, 3.5, false, ICON_WEATHER, "Russian meteorological satellite transmitting LRPT weather images.", "137.100", "LRPT", "", "", {}, {}, {}, SAT_TYPE_WEATHER},
    {27607, "SO-50", TFT_GREEN, 0, 6.5, false, ICON_COMMUNICATION, "SaudiSat 1C (SO-50). A long-lived, highly active FM voice repeater amateur satellite, very popular for quick handheld contacts.", "145.850", "FM", "436.795", "67.0", {}, {}, {}, SAT_TYPE_HAM},
    {43017, "AO-91", TFT_MAGENTA, 0, 6.0, true, ICON_COMMUNICATION, "RadFxSat (AO-91). A Fox-1B series amateur radio satellite carrying a U/V FM voice repeater.", "145.960", "FM", "435.250", "67.0", {}, {}, {}, SAT_TYPE_HAM}
};

// We use a simulated time starting near the TLE epoch for Phase 3 offline testing
uint32_t current_unix = 0; // Will be set in setup()
int32_t timeMachineOffset = 0;
unsigned long last_update = 0;
unsigned long gnssStartTime = 0;
bool gnssManualMode = false;
bool gnssTimedOut = false;
bool gnssLocationFixed = false; // True once GNSS provides a real position fix
bool isSatViewMode = false;
int focusSatIndex = -1;
float currentZoom = 0.95f;
uint8_t currentBrightness = 128;

// Default GNSS location (Beijing for public release)
// double baseUserLat = 22.85; // Nanning (test location)
// double baseUserLon = 108.33;
double baseUserLat = 39.90; // Beijing
double baseUserLon = 116.40;
double baseUserAlt = 0.0; // Altitude in meters

// --- Base64 encoder for screenshot transfer ---
const char b64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
String base64_encode(const uint8_t* data, size_t len) {
    String ret;
    ret.reserve((len * 4 / 3) + 4);
    int i = 0;
    uint8_t a3[3], a4[4];
    while (len--) {
        a3[i++] = *(data++);
        if (i == 3) {
            a4[0] = (a3[0] & 0xfc) >> 2;
            a4[1] = ((a3[0] & 0x03) << 4) + ((a3[1] & 0xf0) >> 4);
            a4[2] = ((a3[1] & 0x0f) << 2) + ((a3[2] & 0xc0) >> 6);
            a4[3] = a3[2] & 0x3f;
            for (i = 0; i < 4; i++) ret += b64_chars[a4[i]];
            i = 0;
        }
    }
    if (i) {
        for (int j = i; j < 3; j++) a3[j] = '\0';
        a4[0] = (a3[0] & 0xfc) >> 2;
        a4[1] = ((a3[0] & 0x03) << 4) + ((a3[1] & 0xf0) >> 4);
        a4[2] = ((a3[1] & 0x0f) << 2) + ((a3[2] & 0xc0) >> 6);
        a4[3] = a3[2] & 0x3f;
        for (int j = 0; j < i + 1; j++) ret += b64_chars[a4[j]];
        while (i++ < 3) ret += '=';
    }
    return ret;
}

void doScreenshot() {
    log_i("[Screenshot] Capturing screen...");
    if (!earth_renderer || !earth_renderer->getCanvas()) {
        log_e("[Screenshot] Canvas not ready!");
        return;
    }
    auto* canvas = earth_renderer->getCanvas();
    int w = canvas->width();
    int h = canvas->height();
    
    // Use log_i for markers (same output channel as other logs)
    log_i("==SKYCOMPASS_RAW_START==%d,%d", w, h);
    delay(10); // Let marker flush
    
    const uint8_t* buf = (const uint8_t*)canvas->getBuffer();
    size_t total = w * h * 2; // RGB565 = 2 bytes per pixel
    size_t offset = 0;
    const size_t CHUNK = 768; // Must be multiple of 3 for clean Base64
    while (offset < total) {
        size_t n = (total - offset > CHUNK) ? CHUNK : (total - offset);
        String b64 = base64_encode(buf + offset, n);
        log_i("==SKYCOMPASS_DATA==%s", b64.c_str());
        offset += n;
        delay(15); // Add delay to prevent serial transmit buffer overflow (1024 chars @ 115200bps takes ~90ms, but hardware buffer needs breathing room)
    }
    log_i("==SKYCOMPASS_RAW_END==");
    log_i("[Screenshot] Done. Sent %d bytes raw RGB565 (%dx%d)", total, w, h);
}
// Helper to pre-calculate orbits with caching
void calculateOrbit(SGP4Calc& calc, uint32_t baseTime, OrbitCache& cache, int& calcCount, bool isFastForwarding) {
    static uint32_t lastGlobalCalcMs = 0;
    if (isFastForwarding) {
        // Fast forwarding: DO NOT recalculate heavy orbit paths to ensure smooth input.
        return;
    }
    
    // 限制物理时间上的计算频率。如果上一帧刚刚重算过轨道，那么在物理时间 120 毫秒内，
    // 任何卫星都不能进行轨道线重算（除非是首次计算），确保在任何高速按键或滑动操作下的丝滑帧率。
    if (cache.lastCalcTime != 0 && millis() - lastGlobalCalcMs < 120) {
        return;
    }
    
    // Only recalculate orbit path if simulated time has advanced by more than 5 minutes (300 seconds)
    if (cache.lastCalcTime == 0 || abs((int)baseTime - (int)cache.lastCalcTime) > 300) {
        if (calcCount >= 1) { // Max 1 expensive calculation per frame to prevent lag spikes
            return;
        }
        lastGlobalCalcMs = millis();
        calcCount++;
        
        cache.past.clear();
        cache.future.clear();
        
        double teme_x, teme_y, teme_z;
        
        // Past 45 minutes, every 3 minutes (lower resolution to save CPU)
        for (int i = 45; i > 0; i -= 3) {
            uint32_t t = baseTime - i * 60;
            if (calc.getTEME(t, teme_x, teme_y, teme_z)) {
                double gmst = CoordTransform::getGMST(CoordTransform::unixToJulian(t));
                ECEFCoord ecef = CoordTransform::temeToECEF(teme_x, teme_y, teme_z, gmst);
                cache.past.push_back(CoordTransform::ecefToGeodetic(ecef));
            }
        }
        
        // Future 45 minutes, every 3 minutes
        for (int i = 3; i <= 45; i += 3) {
            uint32_t t = baseTime + i * 60;
            if (calc.getTEME(t, teme_x, teme_y, teme_z)) {
                double gmst = CoordTransform::getGMST(CoordTransform::unixToJulian(t));
                ECEFCoord ecef = CoordTransform::temeToECEF(teme_x, teme_y, teme_z, gmst);
                cache.future.push_back(CoordTransform::ecefToGeodetic(ecef));
            }
        }
        
        cache.lastCalcTime = baseTime;
    }
}

#include "core/observation_predictor.h"

TaskHandle_t predictorTaskHandle = NULL;
std::vector<PassEvent> recommendedPasses;
bool showRecommendations = false;
int passScrollIndex = 0;

struct TreeItem {
    bool isCategory;
    int categoryIndex; // 0=Tonight, 1=This Week, 2=This Month, 3=Favorites
    int passIndex;     // Index in recommendedPasses
};
bool catExpanded[4] = {false, false, false, false};
std::vector<TreeItem> displayTree;
int selectedPassIndex = -1; // For detail view

void rebuildTree(uint32_t current_unix) {
    displayTree.clear();
    for (int c = 0; c < 4; c++) {
        displayTree.push_back({true, c, -1});
        if (catExpanded[c]) {
            for (int i = 0; i < recommendedPasses.size(); i++) {
                const auto& p = recommendedPasses[i];
                bool match = false;
                if (c == 0 && p.aosTime >= current_unix && p.aosTime < current_unix + 24*3600) match = true;
                else if (c == 1 && p.aosTime >= current_unix && p.aosTime < current_unix + 7*24*3600) match = true;
                else if (c == 2 && p.score >= 4 && p.aosTime >= current_unix) match = true;
                else if (c == 3 && p.aosTime >= current_unix) match = true;
                
                if (match) {
                    displayTree.push_back({false, c, i});
                }
            }
        }
    }
}


// IMU Lock State
bool isImuLocked = false;
float lockedPitch = 0;
float lockedRoll = 0;
float lockedYaw = 0;

unsigned long bootTime = 0;
bool showHelp = false;
bool showHud = true;
bool isManualLocationMode = false;
// removed duplicate isSatViewMode
float basePitch = 0.0f; // Stores initial pitch for relative view
float baseRoll = 0.0f;  // Stores initial roll for relative view
bool predictionsReady = false;
int predictionProgress = 0;
volatile bool cancelPrediction = false;
uint32_t lastPredictionBaseTime = 0;
bool manualWifiToggle = false;
std::vector<int> entrySelectedSatellites;

// Custom Satellite Input State
String noradInput = "";
String downloadErrorMsg = "";
int deleteConfirmIndex = -1;
bool isDownloadingCustom = false;
portMUX_TYPE passMutex = portMUX_INITIALIZER_UNLOCKED;

volatile bool triggerPrediction = true;

void predictorTask(void* parameter) {
    while (true) {
        if (!triggerPrediction || g_networkActive || !g_timeSynced) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        // Wait 2 seconds to let the system finish recycling WiFi/TCP/SSL memory
        vTaskDelay(pdMS_TO_TICKS(2000));
        
        if (g_networkActive) {
            continue;
        }
        
        triggerPrediction = false;
        cancelPrediction = false; // 重置取消状态
        g_orbitCalculating = true;
        
        ObservationPredictor predictor(baseUserLat, baseUserLon, baseUserAlt / 1000.0);
        std::vector<PassEvent> allPasses;
        allPasses.reserve(150);
        
        // Use simulated time for predictions
        uint32_t startTime = current_unix + timeMachineOffset;
        
        int numSatsToPredict = 0;
        RecentLaunchItem* activeGroup = nullptr;
        if (g_recentLaunchFocusMode) {
            numSatsToPredict = 1;
        } else {
            for (int i = 0; i < NUM_SATELLITES; i++) {
                if (g_satellites[i].selected) numSatsToPredict++;
            }
        }
        
        predictionProgress = 0;
        int completedCount = 0;
        
        if (g_recentLaunchFocusMode) {
            if (g_repSatInitialized && g_repSatTLE.line1.length() >= 14 && g_repSatTLE.line2.length() >= 14) {
                auto passes = predictor.predictPasses(g_repSatTLE, 3.0, startTime, 7);
                
                // Cap passes to prevent OOM
                if (passes.size() > 8) {
                    std::sort(passes.begin(), passes.end(), [](const PassEvent& a, const PassEvent& b) {
                        return a.score > b.score;
                    });
                    passes.resize(8);
                }
                
                for (auto& p : passes) {
                    p.satSelected = true;
                    p.satIndex = -100; // Representative sat fixed to -100
                }
                allPasses.insert(allPasses.end(), passes.begin(), passes.end());
            }
            completedCount = 1;
            predictionProgress = 100;
        } else {
            for (int i = 0; i < NUM_SATELLITES; i++) {
                if (triggerPrediction || cancelPrediction) break;
                
                if (g_satellites[i].selected) {
                    const auto& tle = g_satellites[i].tle;
                    if (tle.line1.length() < 14 || tle.line2.length() < 14) {
                        completedCount++;
                        predictionProgress = (completedCount * 100) / (numSatsToPredict > 0 ? numSatsToPredict : 1);
                        continue;
                    }
                    
                    auto passes = predictor.predictPasses(tle, g_satellites[i].stdMag, startTime, 7);
                    
                    // Cap passes to prevent OOM
                    if (passes.size() > 8) {
                        std::sort(passes.begin(), passes.end(), [](const PassEvent& a, const PassEvent& b) {
                            return a.score > b.score;
                        });
                        passes.resize(8);
                    }
                    
                    for (auto& p : passes) {
                        p.satSelected = true;
                        p.satIndex = i;
                    }
                    allPasses.insert(allPasses.end(), passes.begin(), passes.end());
                    vTaskDelay(pdMS_TO_TICKS(1));
                    completedCount++;
                }
                predictionProgress = (completedCount * 100) / (numSatsToPredict > 0 ? numSatsToPredict : 1);
            }
        }
        
        if (triggerPrediction || cancelPrediction) {
            if (cancelPrediction) {
                cancelPrediction = false;
                g_orbitCalculating = false; // 强行熄灭 Chain Mono 的计算动画
            }
            continue;
        }
        
        // Filter out past passes relative to the simulated time
        std::vector<PassEvent> upcomingPasses;
        for (const auto& pass : allPasses) {
            if (pass.aosTime > current_unix + timeMachineOffset) {
                upcomingPasses.push_back(pass);
            }
        }
        
        // Sort by score descending, then by start time ascending
        std::sort(upcomingPasses.begin(), upcomingPasses.end(), [](const PassEvent& a, const PassEvent& b) {
            if (a.score != b.score) return a.score > b.score;
            return a.aosTime < b.aosTime;
        });
        
        portENTER_CRITICAL(&passMutex);
        recommendedPasses = upcomingPasses;
        predictionsReady = true;
        lastPredictionBaseTime = startTime; // 写入本次成功的基准时间缓存
        
        // Auto-expand first category on finish
        catExpanded[0] = true;
        catExpanded[1] = false;
        catExpanded[2] = false;
        catExpanded[3] = false;
        
        rebuildTree(current_unix + timeMachineOffset);
        portEXIT_CRITICAL(&passMutex);
        
        if (g_orbitCalculating) {
            g_orbitCalculating = false;
            g_readyStartTime = millis(); // Trigger 2-second READY effect
        }
    }
}

struct NetworkParams {
    String ssid;
    String pass;
    bool shouldSave;
};


void fetchFrequencies() {
    WiFiClientSecure *client = new WiFiClientSecure;
    if (!client) return;
    client->setInsecure();
    
    HTTPClient http;
    http.setTimeout(15000);
    http.setConnectTimeout(15000);
    http.begin(*client, "https://raw.githubusercontent.com/nongxl/SkyCompass_Satellite/main/data/frequencies.json");
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
        if (httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
            String newUrl = http.getLocation();
            http.end();
            http.begin(*client, newUrl);
            httpCode = http.GET();
        }
    }
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        payload.trim();
        if (payload.length() > 0 && payload.length() < 10240 && payload.startsWith("{")) {
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload);
            if (!error) {
                for (int i = 0; i < NUM_SATELLITES; i++) {
                    String idStr = String(g_satellites[i].noradId);
                    if (doc.containsKey(idStr)) {
                        g_satellites[i].downlinkFreq = doc[idStr]["freq"].as<String>();
                        g_satellites[i].radioMode = doc[idStr]["mode"].as<String>();
                        if (doc[idStr].containsKey("uplink")) {
                            g_satellites[i].uplinkFreq = doc[idStr]["uplink"].as<String>();
                        }
                        if (doc[idStr].containsKey("tone")) {
                            g_satellites[i].tone = doc[idStr]["tone"].as<String>();
                        }
                        if (g_satellites[i].type == SAT_TYPE_VISUAL) {
                            g_satellites[i].type = SAT_TYPE_HAM;
                        }
                    }
                }
            }
        } else {
            LOG_I("APP", "Frequencies payload skipped (size: %d)", payload.length());
        }
    }
    http.end();
    delete client;
}

String extractPrefix(String name) {
    name.trim();
    // 1. 如果最后一个字符是字母 (如 'A', 'B' 等)，且倒数第二个是空格或减号，先去掉
    int len = name.length();
    if (len > 2) {
        char last = name.charAt(len - 1);
        char prev = name.charAt(len - 2);
        if (((last >= 'A' && last <= 'Z') || (last >= 'a' && last <= 'z')) && (prev == ' ' || prev == '-')) {
            name = name.substring(0, len - 2);
            name.trim();
            len = name.length();
        }
    }
    // 2. 如果末尾是连续的数字，我们数一下它的长度
    int i = len - 1;
    int digitCount = 0;
    while (i >= 0 && (name.charAt(i) >= '0' && name.charAt(i) <= '9')) {
        digitCount++;
        i--;
    }
    // 如果数字长度 >= 3，且前面有 separator，则切掉
    if (digitCount >= 3 && i >= 0 && (name.charAt(i) == '-' || name.charAt(i) == ' ' || name.charAt(i) == '#' || name.charAt(i) == '_')) {
        name = name.substring(0, i);
        name.trim();
    } else if (digitCount > 0 && i >= 0 && name.charAt(i) == '-') {
        // 如果是像 G10-1 这种，去掉尾部的 -1
        name = name.substring(0, i);
        name.trim();
    }
    return name;
}

String readValLine(WiFiClient* stream) {
    String line = "";
    unsigned long startMs = millis();
    while (stream->connected() || stream->available()) {
        if (millis() - startMs > 5000) { // 5秒超时
            break;
        }
        if (stream->available()) {
            char c = stream->read();
            if (c == '\n') {
                break;
            }
            if (c != '\r') {
                line += c;
            }
        } else {
            delay(1);
        }
    }
    line.trim();
    return line;
}

int drawWrappedText(LGFX_Sprite* canvas, String text, int x, int y, int w, int lineH, bool draw = true) {
    int start = 0;
    int lines = 0;
    while (start < text.length()) {
        lines++;
        int fitChars = w / 6; // roughly 6px per char for setTextSize(1)
        if (start + fitChars >= text.length()) {
            if (draw) canvas->drawString(text.substring(start).c_str(), x, y);
            break;
        }
        int end = start + fitChars;
        int lastSpace = text.substring(start, end).lastIndexOf(' ');
        if (lastSpace > start && lastSpace > start + fitChars/2) {
            end = lastSpace;
        }
        if (draw) canvas->drawString(text.substring(start, end).c_str(), x, y);
        start = end + 1;
        y += lineH;
    }
    return lines;
}

void recentLaunchNetworkTask(void* parameter) {
    NetworkActiveGuard guard;
    recentLaunchDownloading = true;
    recentLaunchDownloadSuccess = false;
    recentLaunchErrorMsg = "";
    
    // 1. WiFi Connection
    if (!HalWifi::isConnected()) {
        String ssid = "";
        String pass = "";
        HalWifi::loadCredentials(ssid, pass);
        
        if (ssid.length() == 0) {
            recentLaunchErrorMsg = "No WiFi Configured!";
            recentLaunchDownloading = false;
            vTaskDelete(NULL);
            return;
        }
        
        recentLaunchErrorMsg = "Connecting WiFi...";
        HalWifi::begin(ssid.c_str(), pass.c_str());
        
        if (!HalWifi::isConnected()) {
            recentLaunchErrorMsg = "WiFi Connect Failed!";
            recentLaunchDownloading = false;
            vTaskDelete(NULL);
            return;
        }
    }
    
    // 2. Sync Time (NTP)
    recentLaunchErrorMsg = "Syncing NTP time...";
    HalWifi::syncNTPTime();
    uint32_t ntpTime = HalWifi::getUnixTime();
    if (ntpTime > 0) {
        current_unix = ntpTime;
        g_timeSynced = true;
        LOG_I("RECENT_LAUNCH", "Time synced to UTC: %u", current_unix);
    }
    
    // 3. Download & Process TLE Stream
    recentLaunchErrorMsg = "Downloading TLEs...";
    WiFiClientSecure *client = new WiFiClientSecure;
    if (!client) {
        recentLaunchErrorMsg = "SSL Client Init Failed!";
        recentLaunchDownloading = false;
        vTaskDelete(NULL);
        return;
    }
    client->setInsecure();
    client->setTimeout(30000); // 30 seconds connection and handshake timeout
    client->setHandshakeTimeout(25); // 25 seconds SSL handshake timeout
    
    HTTPClient http;
    http.setTimeout(60000); // 60 seconds HTTP timeout for large TLE stream
    http.setConnectTimeout(30000); // 30 seconds TCP/SSL connection timeout
    String url = "https://celestrak.org/NORAD/elements/gp.php?GROUP=last-30-days&FORMAT=tle";
    http.begin(*client, url);
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
        if (httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
            String newUrl = http.getLocation();
            http.end();
            http.begin(*client, newUrl);
            httpCode = http.GET();
        }
    }
    
    if (httpCode == HTTP_CODE_OK) {
        recentLaunchErrorMsg = "Grouping Satellites...";
        WiFiClient* stream = http.getStreamPtr();
        
        File f = LittleFS.open("/tle_recent_raw.txt", "w");
        int rawTleCount = 0;
        
        std::vector<RecentLaunchItem> tempLaunches;
        tempLaunches.reserve(30);
        
        while (stream->connected() || stream->available()) {
            if (!recentLaunchDownloading) {
                break;
            }
            String name = readValLine(stream);
            if (name.length() == 0) {
                if (!stream->available()) break;
                continue;
            }
            String line1 = readValLine(stream);
            String line2 = readValLine(stream);
            
            if (line1.length() < 14 || line1.charAt(0) != '1' || line2.length() < 14 || line2.charAt(0) != '2') {
                continue;
            }
            
            String batchId = line1.substring(9, 14);
            
            if (f) {
                f.println(name);
                f.println(line1);
                f.println(line2);
                rawTleCount++;
            }
            
            int foundIdx = -1;
            for (size_t i = 0; i < tempLaunches.size(); i++) {
                if (tempLaunches[i].batchId == batchId) {
                    foundIdx = i;
                    break;
                }
            }
            
            if (foundIdx != -1) {
                if (tempLaunches[foundIdx].satelliteCount < 60) {
                    tempLaunches[foundIdx].satelliteCount++;
                }
                tempLaunches[foundIdx].isGroup = true;
            } else {
                RecentLaunchItem item;
                item.batchId = batchId;
                item.displayName = extractPrefix(name);
                item.satelliteCount = 1;
                item.isGroup = false;
                item.selected = false;
                item.epoch = parseTleEpoch(line1);
                getRepresentativeOrbitParams(line2, item.inclination, item.avgAlt);
                item.repSatName = name;
                tempLaunches.push_back(item);
            }
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        
        if (f) {
            f.close();
        }
        
        http.end();
        if (client) {
            delete client;
            client = nullptr;
        }
        
        if (recentLaunchDownloading && rawTleCount > 0) {
            g_pendingRecentLaunches = std::move(tempLaunches);
            calculateFormationsForItems(g_pendingRecentLaunches);
            g_recentLaunchesPending = true;
            recentLaunchSelectedIndex = 0;
            recentLaunchDownloadSuccess = true;
            recentLaunchErrorMsg = "Downloaded successfully!";
            LOG_I("RECENT_LAUNCH", "Loaded %d unique batches saved directly to LittleFS.", g_pendingRecentLaunches.size());
        }
    } else {
        recentLaunchErrorMsg = "HTTP Error: " + String(httpCode);
        LOG_I("RECENT_LAUNCH", "Celestrak fetch failed, code: %d", httpCode);
        http.end();
        if (client) {
            delete client;
            client = nullptr;
        }
    }
    
    recentLaunchDownloading = false;
    g_networkActive = false;
    vTaskDelete(NULL);
}

void networkTask(void* parameter) {
    NetworkActiveGuard guard;
    g_wifiConnecting = true;
    g_dataUpdating = false;
    
    String ssid = "";
    String pass = "";
    bool shouldSave = false;
    
    if (parameter != NULL) {
        NetworkParams* params = (NetworkParams*)parameter;
        ssid = params->ssid;
        pass = params->pass;
        shouldSave = params->shouldSave;
        delete params;
    } else {
        HalWifi::loadCredentials(ssid, pass);
    }
    
    if (ssid.length() == 0) {
        LOG_I("APP", "No WiFi credentials available. Falling back to WiFi Setup.");
        appState = STATE_WIFI_SETUP;
        wifiIsScanning = true;
        wifiIsInputtingPassword = false;
        g_wifiConnecting = false;
        g_dataUpdating = false;
        g_networkActive = false;
        vTaskDelete(NULL);
        return;
    }

    // 1. Connect WiFi
    HalWifi::begin(ssid.c_str(), pass.c_str());
    
    if (!HalWifi::isConnected()) {
        LOG_I("APP", "WiFi connection failed. Falling back to WiFi Setup.");
        if (appState == STATE_SAT_SELECT) {
            downloadErrorMsg = "WiFi Connection Failed!";
        }
        appState = STATE_WIFI_SETUP;
        wifiIsScanning = true;
        wifiIsInputtingPassword = false;
        g_wifiConnecting = false;
        g_dataUpdating = false;
        g_networkActive = false;
        vTaskDelete(NULL);
        return;
    }
    
    if (HalWifi::isConnected() && shouldSave) {
        HalWifi::saveCredentials(ssid, pass);
    }
    
    if (HalWifi::isConnected()) {
        g_wifiConnecting = false;
        g_dataUpdating = true;
        
        if (appState == STATE_SAT_SELECT) {
            downloadErrorMsg = "WiFi Connected! Syncing time...";
        }
        
        // Online timezone removed per user request (relies on offline grid)
        
        // 2. Fetch NTP
        HalWifi::syncNTPTime();
        
        // 3. Update time
        uint32_t ntpTime = HalWifi::getUnixTime();
        if (ntpTime > 0) {
            current_unix = ntpTime;
            g_timeSynced = true;
            LOG_I("APP", "Time synced to UTC: %u", current_unix);
        }

        if (appState == STATE_SAT_SELECT) {
            downloadErrorMsg = "WiFi Connected! Syncing frequencies...";
        }

        // 3.5 Fetch Frequencies
        fetchFrequencies();

        if (appState == STATE_SAT_SELECT) {
            downloadErrorMsg = "WiFi Connected! Syncing TLEs...";
        }

        // 4. Fetch TLEs
        bool updated = false;
        WiFiClientSecure* sharedClient = new WiFiClientSecure;
        if (sharedClient) {
            sharedClient->setInsecure();
        }
        
        for (int i = 0; i < NUM_SATELLITES; i++) {
            TLEData new_tle;
            if (TLEUpdater::getTLE(g_satellites[i].noradId, new_tle, 2 * 24 * 3600, sharedClient)) {
                new_tle.baseScore = g_satellites[i].baseScore;
                g_satellites[i].tle = new_tle;
                g_satellites[i].calc.init(g_satellites[i].tle);
                updated = true;
            }
            vTaskDelay(pdMS_TO_TICKS(10)); // Yield CPU to prevent Task Watchdog starvation
        }
        
        if (sharedClient) {
            delete sharedClient;
        }
        
        if (updated) {
            LOG_I("APP", "TLE Data is ready and models updated!");
            
            // Rerun predictor with new data
            portENTER_CRITICAL(&passMutex);
            predictionsReady = false;
            lastPredictionBaseTime = 0; // 缓存失效
            portEXIT_CRITICAL(&passMutex);
            triggerPrediction = true;
            
            if (appState == STATE_SAT_SELECT) {
                downloadErrorMsg = "TLEs & Frequencies Updated!";
            }
        } else {
            if (appState == STATE_SAT_SELECT) {
                downloadErrorMsg = "Frequencies Updated! TLEs are fresh.";
            }
        }
        if (!manualWifiToggle) {
            LOG_I("APP", "Network tasks complete. Turning off WiFi to save power.");
            HalWifi::disconnect();
        } else {
            LOG_I("APP", "Network tasks complete. WiFi remains connected.");
        }
    }
    
    if (g_dataUpdating) {
        g_dataUpdating = false;
        g_readyStartTime = millis(); // Trigger 2-second READY effect
    }
    g_wifiConnecting = false;
    g_timeSynced = true; // Fallback to allow offline mock calculations if WiFi failed/finished
    triggerPrediction = true; // Wake up the prediction loop immediately
    g_networkActive = false;
    vTaskDelete(NULL);
}

void tryLoadRecentLaunchCache() {
    if (!LittleFS.exists("/tle_recent_raw.txt")) {
        LOG_I("RECENT_LAUNCH", "No local cache file found.");
        return;
    }
    
    std::vector<RecentLaunchItem> tempLaunches;
    tempLaunches.reserve(30);
    
    File rf = LittleFS.open("/tle_recent_raw.txt", "r");
    if (!rf) {
        return;
    }
    
    uint32_t firstEpoch = 0;
    while (rf.available()) {
        String name = rf.readStringUntil('\n');
        name.trim();
        if (name.length() == 0) break;
        String line1 = rf.readStringUntil('\n');
        line1.trim();
        String line2 = rf.readStringUntil('\n');
        line2.trim();
        
        if (line1.length() < 14 || line1.charAt(0) != '1' || line2.length() < 14 || line2.charAt(0) != '2') {
            continue;
        }
        
        if (firstEpoch == 0) {
            firstEpoch = parseTleEpoch(line1);
        }
        
        String batchId = line1.substring(9, 14);
        
        int foundIdx = -1;
        for (size_t i = 0; i < tempLaunches.size(); i++) {
            if (tempLaunches[i].batchId == batchId) {
                foundIdx = i;
                break;
            }
        }
        
        if (foundIdx != -1) {
            if (tempLaunches[foundIdx].satelliteCount < 60) {
                tempLaunches[foundIdx].satelliteCount++;
            }
            tempLaunches[foundIdx].isGroup = true;
        } else {
            RecentLaunchItem item;
            item.batchId = batchId;
            item.displayName = extractPrefix(name);
            item.satelliteCount = 1;
            item.isGroup = false;
            item.selected = false;
            item.epoch = parseTleEpoch(line1);
            getRepresentativeOrbitParams(line2, item.inclination, item.avgAlt);
            item.repSatName = name;
            tempLaunches.push_back(item);
        }
    }
    rf.close();
    
    if (tempLaunches.empty()) {
        return;
    }
    
    uint32_t nowTime = current_unix + timeMachineOffset;
    bool fresh = false;
    if (firstEpoch > 0) {
        uint32_t age = 0;
        if (nowTime >= firstEpoch) {
            age = nowTime - firstEpoch;
        }
        if (age <= 3 * 86400 || nowTime < firstEpoch) {
            fresh = true;
        }
    }
    
    if (fresh) {
        g_recentLaunches = tempLaunches;
        calculateFormationsForItems(g_recentLaunches);
        recentLaunchDownloadSuccess = true;
        recentLaunchSelectedIndex = 0;
        recentLaunchErrorMsg = "Loaded from local cache.";
        LOG_I("RECENT_LAUNCH", "Loaded %d launches from local cache, age is fresh.", g_recentLaunches.size());
    } else {
        LOG_I("RECENT_LAUNCH", "Local cache TLE is expired. Needs download.");
    }
}

void saveCustomSatellites() {
    Preferences prefs;
    prefs.begin("satellites", false);
    String idList = "";
    for (int i = NUM_BUILTIN_SATELLITES; i < NUM_SATELLITES; i++) {
        idList += String(g_satellites[i].noradId);
        if (i < NUM_SATELLITES - 1) idList += ",";
    }
    prefs.putString("customIds", idList);
    prefs.end();
}

void imuTask(void* pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(10); // 10ms (100Hz) high precision interval
    
    while (true) {
        if (imu && attitude) {
            imu->update();
            attitude->update();
        }
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void setup() {
    
    Serial.begin(115200);
    // Remove the 4 second delay to boot instantly
    LOG_I("APP", "\n\n--- SkyCompass Satellite: Phase 4 ---");

    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setBrightness(currentBrightness);
    
    earth_renderer = new EarthRenderer(&M5Cardputer.Display);
    earth_renderer->begin();

    // Initialize IMU
    if (imu && imu->begin()) {
        attitude = new AttitudeEstimator(imu);
        attitude->begin();
        LOG_I("APP", "IMU Initialized");
        
        // Spawn high-precision background IMU sampling task on Core 0 (I2C reading & sensor fusion integration)
        // Fixed at 100Hz to prevent step sizes from fluctuating during heavy 3D rendering on Core 1
        xTaskCreatePinnedToCore(
            imuTask,
            "ImuTask",
            4096,
            NULL,
            3, // High priority
            NULL,
            0  // Pinned to Core 0 (same core as networking and protocol tasks)
        );
    }
    
    // Initialize Position & Sun Calculator
    pos_manager = new PositionManager(gnss);
    pos_manager->begin(); 
    
    sun_calc = new SunCalculator(pos_manager);
    sun_calc->begin();
    
    // Setup LittleFS for TLE Cache
    TLEUpdater::begin();
    
    // Set default offline time first so getTLE works properly if needed
    current_unix = 0; // We start at 0 so TLEUpdater uses cache regardless of age
    
    // Offline initialization: Try to load from cache
    for (int i = 0; i < NUM_SATELLITES; i++) {
        TLEData loaded_tle;
        if (TLEUpdater::getTLE(g_satellites[i].noradId, loaded_tle)) {
            loaded_tle.baseScore = g_satellites[i].baseScore;
            g_satellites[i].tle = loaded_tle;
        } else {
            // Fallback for first 3 if no cache
            if (i == 0) g_satellites[i].tle = TLEManager::getISS_TLE();
            else if (i == 1) g_satellites[i].tle = TLEManager::getTiangong_TLE();
            else if (i == 2) g_satellites[i].tle = TLEManager::getHubble_TLE();
            else if (g_satellites[i].noradId == 50463) g_satellites[i].tle = TLEManager::getJWST_TLE();
            else if (g_satellites[i].noradId == 27607) g_satellites[i].tle = TLEManager::getSO50_TLE();
            else if (g_satellites[i].noradId == 43017) g_satellites[i].tle = TLEManager::getAO91_TLE();
        }
        
        if (g_satellites[i].tle.line1.length() > 0) {
            g_satellites[i].calc.init(g_satellites[i].tle);
        }
    }
    
    // Set default offline time to mock anchor if still 0
    current_unix = TLEManager::getMockTimeAnchor();
    LOG_I("APP", "Offline boot: Loaded cached TLEs. Using Mock Time Anchor.");
    
    // Load Custom Satellites from Preferences
    Preferences prefs;
    prefs.begin("satellites", true);
    String customIds = prefs.getString("customIds", "");
    prefs.end();
    
    bool needsSaveCleanup = false;
    if (customIds.length() > 0) {
        int start = 0;
        int end = customIds.indexOf(',');
        while (start < customIds.length()) {
            String idStr;
            if (end == -1) {
                idStr = customIds.substring(start);
                start = customIds.length();
            } else {
                idStr = customIds.substring(start, end);
                start = end + 1;
                end = customIds.indexOf(',', start);
            }
            
            int id = idStr.toInt();
            if (id > 0) {
                bool isPreset = false;
                for (int pIdx = 0; pIdx < NUM_BUILTIN_SATELLITES; pIdx++) {
                    if (g_satellites[pIdx].noradId == id) {
                        isPreset = true;
                        break;
                    }
                }
                if (isPreset) {
                    needsSaveCleanup = true;
                    continue;
                }
                
                LOG_I("APP", "Loading Custom: %d", id);
                TLEData loaded_tle;
                if (TLEUpdater::getTLE(id, loaded_tle)) {
                    SatProfile p;
                    p.noradId = id;
                    p.name = loaded_tle.name;
                    p.color = TFT_WHITE;
                    p.baseScore = 0;
                    p.selected = true;
                    p.iconType = ICON_SATELLITE;
                    p.description = "Custom added satellite.\n\nPress 'd' to delete this satellite.";
                    p.tle = loaded_tle;
                    p.calc.init(p.tle);
                    p.type = SAT_TYPE_VISUAL;
                    if (NUM_SATELLITES < MAX_SATELLITES) {
                        g_satellites[NUM_SATELLITES++] = p;
                    }
                }
            }
        }
    }
    
    if (needsSaveCleanup) {
        LOG_I("APP", "Built-in satellites found in custom list. Performing Preferences cleanup.");
        saveCustomSatellites();
    }
    
    // Start predictor task on Core 0 for offline data (UI runs on Core 1)
    xTaskCreatePinnedToCore(
        predictorTask,
        "PredictorTask",
        16384,
        NULL,
        1,
        &predictorTaskHandle,
        0
    );
    
    // Start network task on Core 0 to handle WiFi and TLE fetching in background
    // Auto connects at boot and auto disconnects when done
    manualWifiToggle = false;
    xTaskCreatePinnedToCore(networkTask, "NetworkTask", 16384, NULL, 1, NULL, 0);

    // Initialize Chain Mono on Serial2 (Grove Port) at setup tail
    // This allows the Chain Mono module's internal MCU enough time to boot up completely.
#if ENABLE_CHAIN_MONO
    bool skipMonoProbe = false;
    if (M5.getBoard() == m5::board_t::board_M5Cardputer) {
        skipMonoProbe = true;
        LOG_I("APP", "Original Cardputer detected. Grove pins are used for internal I2C. Skipping Mono probe.");
    }
    if (gnss && !skipMonoProbe) {
        GnssConfig gnssCfg = gnss->getConfig();
        if (gnssCfg.rxPin == 2) {
            skipMonoProbe = true;
            LOG_I("APP", "Grove port is occupied by GNSS (pin 2/1). Skipping Chain Mono probe.");
        }
    }

    bool foundChain = false;
    uint8_t usedRx = 2;
    uint8_t usedTx = 1;
    uint16_t device_nums = 0;
    
    if (!skipMonoProbe) {
        LOG_I("APP", "Initializing Chain Mono on Serial2 (Auto-detecting pins)...");
        
        // Attempt Config A: RX=2, TX=1 (Standard G2=RX, G1=TX for host)
        M5Chain.begin(&Serial2, 115200, 2, 1);
        delay(100);
        int retry = 2;
        while (retry > 0) {
            if (M5Chain.getDeviceNum(&device_nums, 150) == CHAIN_OK && device_nums > 0) {
                foundChain = true;
                usedRx = 2;
                usedTx = 1;
                break;
            }
            retry--;
            if (retry > 0) delay(50);
        }
        
        // Attempt Config B: RX=1, TX=2 (Swapped G1=RX, G2=TX for host)
        if (!foundChain) {
            LOG_I("APP", "Chain Mono not found on RX=2,TX=1. Swapping pins (RX=1,TX=2) and retrying...");
            Serial2.end();
            delay(50);
            M5Chain.begin(&Serial2, 115200, 1, 2);
            delay(100);
            retry = 2;
            while (retry > 0) {
                if (M5Chain.getDeviceNum(&device_nums, 150) == CHAIN_OK && device_nums > 0) {
                    foundChain = true;
                    usedRx = 1;
                    usedTx = 2;
                    break;
                }
                retry--;
                if (retry > 0) delay(50);
            }
        }
        
        if (foundChain) {
            LOG_I("APP", "Chain Mono successfully detected on RX=%d, TX=%d! Device count: %d", usedRx, usedTx, device_nums);
            device_info_t *infos = (device_info_t *)malloc(sizeof(device_info_t) * device_nums);
            if (infos != nullptr) {
                memset(infos, 0, sizeof(device_info_t) * device_nums); // Safe guard!
                device_list_t devices;
                devices.count = device_nums;
                devices.devices = infos;
                if (M5Chain.getDeviceList(&devices, 150)) {
                    for (uint8_t i = 0; i < devices.count; i++) {
                        if (devices.devices[i].device_type == CHAIN_MONO_TYPE_CODE) {
                            mono_id = devices.devices[i].id;
                            isMonoInitialized = true;
                            break;
                        }
                    }
                }
                free(infos);
            }
        }
    }
    
    if (isMonoInitialized) {
        LOG_I("APP", "Chain Mono found on Grove port. ID: %d", mono_id);
        M5Chain.setMonoMode(mono_id, MONO_PIXEL_MODE, &operation_status);
        M5Chain.setMonoRotation(mono_id, MONO_ROTATION_0, &operation_status);
        M5Chain.setMonoBrightness(mono_id, MONO_BRIGHTNESS_LEVEL_7, &operation_status);
        M5Chain.setMonoClear(mono_id, &operation_status);
        
        // 既然已经找到并占用了 Mono，就把 GNSS 的 Grove 探测永久关掉，防干扰
        if (gnss) {
            GnssConfig gnssCfg = gnss->getConfig();
            gnssCfg.enableGroveProbe = false;
            gnss->setConfig(gnssCfg);
        }
    } else {
        if (!skipMonoProbe) {
            LOG_I("APP", "Chain Mono module not detected. Releasing Grove pins for GNSS.");
            Serial2.end();
            
            // Late probe for Grove GNSS since Grove port is free
            if (gnss) {
                LOG_I("APP", "Triggering late GNSS Grove port probe...");
                gnss->probeGrove();
            }
        } else {
            LOG_I("APP", "Skipped Chain Mono probe as Grove is occupied by GNSS.");
        }
    }
#else
    isMonoInitialized = false;
    if (gnss) {
        LOG_I("APP", "Chain Mono is disabled. Probing Grove port late for GNSS.");
        gnss->probeGrove();
    }
#endif
    tryLoadRecentLaunchCache();
}

void drawWiFiSetupPage() {
    auto canvas = earth_renderer->getCanvas();
    uint16_t width = canvas->width();
    uint16_t height = canvas->height();
    
    // Background
    canvas->fillRect(0, 0, width, height, canvas->color565(15, 20, 25));
    
    // Top Bar
    canvas->fillRect(0, 0, width, 25, canvas->color565(30, 60, 100));
    canvas->setTextColor(TFT_WHITE);
    canvas->setTextSize(2);
    canvas->drawString("WiFi Setup", 10, 5);
    
    canvas->setTextColor(TFT_WHITE);
    canvas->setTextSize(1);
    
    if (wifiIsScanning) {
        canvas->drawString("Scanning for networks...", 20, 50);
        return; // Will be handled in main loop
    }
    
    if (wifiNetworks.empty() && !wifiIsScanning) {
        canvas->drawString("No networks found.", 20, 80);
        canvas->drawString("Press [R] to rescan", 20, 100);
    } else {
        if (wifiIsInputtingPassword) {
            canvas->drawString("Connect to:", 20, 40);
            canvas->setTextColor(TFT_GREEN);
            String ssid = wifiNetworks[wifiSelectedIndex].ssid;
            canvas->drawString(ssid.c_str(), 20, 55);
            
            canvas->setTextColor(TFT_WHITE);
            canvas->drawString("Password:", 20, 80);
            
            canvas->fillRect(20, 95, width - 40, 25, canvas->color565(50, 50, 50));
            canvas->drawRect(20, 95, width - 40, 25, TFT_WHITE);
            
            char displayStr[66];
            sprintf(displayStr, "%s_", wifiPasswordBuffer);
            canvas->drawString(displayStr, 25, 100);
            
            canvas->setTextColor(TFT_LIGHTGRAY);
            canvas->drawString("[Enter] Connect   [ESC] Cancel", 10, height - 15);
        } else {
            canvas->drawString("Select Network:", 10, 30);
            
            int yPos = 45;
            int itemsPerPage = 4;
            int startIndex = (wifiSelectedIndex / itemsPerPage) * itemsPerPage;
            
            for (int i = 0; i < itemsPerPage && (startIndex + i) < wifiNetworks.size(); i++) {
                int index = startIndex + i;
                if (index == wifiSelectedIndex) {
                    canvas->fillRect(5, yPos - 2, width - 10, 18, canvas->color565(50, 100, 150));
                    canvas->setTextColor(TFT_WHITE);
                } else {
                    canvas->setTextColor(TFT_LIGHTGRAY);
                }
                
                String ssidStr = wifiNetworks[index].ssid;
                if (ssidStr.length() > 18) ssidStr = ssidStr.substring(0, 15) + "...";
                
                canvas->drawString(ssidStr.c_str(), 10, yPos);
                
                char rssiStr[16];
                sprintf(rssiStr, "%ddBm", wifiNetworks[index].rssi);
                canvas->drawString(rssiStr, width - 50, yPos);
                
                yPos += 20;
            }
            
            canvas->setTextColor(TFT_LIGHTGRAY);
            canvas->drawString("[^/v] Sel [Enter] Input [R] Scan [ESC] Exit", 5, height - 15);
        }
    }
}

void drawSatSelectPage() {
    static bool lastDownloading = false;
    if (lastDownloading && !recentLaunchDownloading) {
        recentLaunchDownloadFinishedMs = millis();
    }
    lastDownloading = recentLaunchDownloading;

    auto canvas = earth_renderer->getCanvas();
    uint16_t width = canvas->width();
    uint16_t height = canvas->height();
    
    bool showBanner = false;
    if (currentSatTab == TAB_RECENT_LAUNCH) {
        if (recentLaunchDownloading || (recentLaunchDownloadFinishedMs > 0 && (millis() - recentLaunchDownloadFinishedMs < 3000))) {
            showBanner = true;
        }
    }
    int bottomLimit = showBanner ? (height - 11) : height;

    
    // Background
    canvas->fillRect(0, 0, width, height, canvas->color565(20, 30, 40));
    
    // Top Bar - Dual Tabs
    canvas->fillRect(0, 0, width, 20, canvas->color565(30, 40, 50));
    
    // Tab 1: Encyclopedia
    uint16_t tab1Bg = (currentSatTab == TAB_ENCYCLOPEDIA) ? canvas->color565(100, 50, 200) : canvas->color565(30, 40, 50);
    canvas->fillRect(0, 0, width/2, 20, tab1Bg);
    canvas->setTextColor(TFT_WHITE);
    canvas->setTextSize(1);
    canvas->drawString("Encyclopedia", 70 - canvas->textWidth("Encyclopedia")/2, 6);
    
    // Tab 2: Recent Launch
    uint16_t tab2Bg = (currentSatTab == TAB_RECENT_LAUNCH) ? canvas->color565(100, 50, 200) : canvas->color565(30, 40, 50);
    canvas->fillRect(width/2, 0, width/2, 20, tab2Bg);
    canvas->drawString("Recent Launch", 166 - canvas->textWidth("Recent Launch")/2, 6);
    
    // Bottom border for Top Bar
    canvas->drawFastHLine(0, 20, width, TFT_DARKGREY);
    
    // Draw Top Bar Status Icons
    {
        // 1. WiFi Icon on Top-Left
        bool isConnected = HalWifi::isConnected();
        bool isConnecting = !isConnected && (g_wifiConnecting || (recentLaunchDownloading && recentLaunchErrorMsg.indexOf("WiFi") >= 0));
        
        int wifiX = 10; // Center of WiFi icon
        int wifiY = 16; // Bottom of WiFi icon
        
        if (isConnecting) {
            int flashStep = (millis() / 250) % 3;
            uint16_t c0 = (flashStep >= 0) ? TFT_GREEN : TFT_DARKGREY;
            uint16_t c1 = (flashStep >= 1) ? TFT_GREEN : TFT_DARKGREY;
            uint16_t c2 = (flashStep >= 2) ? TFT_GREEN : TFT_DARKGREY;
            
            canvas->fillCircle(wifiX, wifiY - 1, 1, c0);
            canvas->drawArc(wifiX, wifiY - 1, 4, 5, 225.0f, 315.0f, c1);
            canvas->drawArc(wifiX, wifiY - 1, 8, 9, 225.0f, 315.0f, c2);
        } else {
            uint16_t wifiColor = isConnected ? TFT_GREEN : TFT_DARKGREY;
            canvas->fillCircle(wifiX, wifiY - 1, 1, wifiColor);
            canvas->drawArc(wifiX, wifiY - 1, 4, 5, 225.0f, 315.0f, wifiColor);
            canvas->drawArc(wifiX, wifiY - 1, 8, 9, 225.0f, 315.0f, wifiColor);
        }
        
        // 2. Battery Icon on Top-Right
        int batPct = M5Cardputer.Power.getBatteryLevel();
        if (batPct > 100) batPct = 100;
        if (batPct < 0) batPct = 0;
        
        uint16_t batColor = TFT_GREEN;
        if (batPct < 10) {
            batColor = TFT_RED;
        } else if (batPct < 70) {
            batColor = TFT_YELLOW;
        } else {
            batColor = TFT_GREEN;
        }
        
        int batX = width - 26;
        int batY = 4;
        int batW = 20;
        int batH = 12;
        
        // Hollow battery body
        canvas->drawRect(batX, batY, batW, batH, batColor);
        // Nipple on the right
        canvas->fillRect(batX + batW, batY + 4, 2, 4, batColor);
        
        // Battery percentage text inside (centered)
        canvas->setTextColor(batColor);
        String pctStr = String(batPct);
        int textX = batX + (batW - canvas->textWidth(pctStr.c_str())) / 2;
        int textY = batY + 2; // standard char height is 8
        canvas->drawString(pctStr.c_str(), textX, textY);
    }

    
    if (currentSatTab == TAB_ENCYCLOPEDIA) {
        // Left Panel (List)
        int yPos = 25;
        int itemsPerPage = showBanner ? 6 : 7;
        int itemSpacing = showBanner ? 16 : 15;
        int startIndex = (satSelectedIndex / itemsPerPage) * itemsPerPage;
        
        for (int i = 0; i < itemsPerPage && (startIndex + i) <= NUM_SATELLITES; i++) {
            int index = startIndex + i;
            if (index == satSelectedIndex) {
                canvas->fillRect(2, yPos - 2, 82, itemSpacing - 1, canvas->color565(0, 120, 255));
                canvas->setTextColor(TFT_WHITE);
            } else {
                canvas->setTextColor(TFT_LIGHTGRAY);
            }
            
            if (index < NUM_SATELLITES) {
                String checkBox = g_satellites[index].selected ? "[x]" : "[ ]";
                canvas->drawString(checkBox.c_str(), 4, yPos);
                
                String nameStr = g_satellites[index].name;
                if (nameStr.length() > 9) nameStr = nameStr.substring(0, 7) + "..";
                canvas->drawString(nameStr.c_str(), 28, yPos);
            } else {
                String text = isDownloadingCustom ? "Downloading..." : ("[+] " + noradInput + "_");
                canvas->drawString(text.c_str(), 4, yPos);
            }
            
            yPos += itemSpacing;
        }
        
        // Right Panel (Description)
        canvas->drawFastVLine(85, 20, bottomLimit - 20, TFT_DARKGREY);
        
        int rightX = 89;
        int descY = 25;
        if (satSelectedIndex < NUM_SATELLITES) {
            SatProfile& selSat = g_satellites[satSelectedIndex];
            
            // Draw 3x Scaled Icon
            int iconX = rightX + 21;
            int iconY = descY + 12;
            uint16_t satColor = selSat.color;
            SatIconType t = selSat.iconType;
            
            if (t == ICON_STATION) {
                canvas->fillRect(iconX - 6, iconY - 3, 15, 9, TFT_WHITE);
                canvas->fillRect(iconX - 21, iconY - 9, 12, 21, satColor);
                canvas->fillRect(iconX + 12, iconY - 9, 12, 21, satColor);
            } else if (t == ICON_TELESCOPE) {
                canvas->fillRect(iconX - 6, iconY - 9, 15, 21, TFT_WHITE);
                canvas->fillRect(iconX - 9, iconY - 12, 21, 6, TFT_LIGHTGRAY);
                canvas->fillRect(iconX - 18, iconY, 9, 6, satColor);
                canvas->fillRect(iconX + 12, iconY, 9, 6, satColor);
            } else if (t == ICON_DEEPSPACE) {
                canvas->fillRect(iconX - 1, iconY - 15, 3, 31, satColor);
                canvas->fillRect(iconX - 15, iconY - 1, 31, 3, satColor);
                for (int i = -1; i <= 1; i++) {
                    canvas->drawLine(iconX - 6 + i, iconY - 6, iconX + 6 + i, iconY + 6, TFT_WHITE);
                    canvas->drawLine(iconX - 6 + i, iconY + 6, iconX + 6 + i, iconY - 6, TFT_WHITE);
                }
            } else if (t == ICON_ROCKET) {
                canvas->fillRect(iconX - 5, iconY - 8, 11, 16, TFT_WHITE);
                canvas->fillTriangle(iconX - 5, iconY - 8, iconX + 5, iconY - 8, iconX, iconY - 15, satColor);
                canvas->fillRect(iconX - 5, iconY + 8, 4, 4, TFT_ORANGE);
                canvas->fillRect(iconX + 2, iconY + 8, 4, 4, TFT_ORANGE);
            } else if (t == ICON_DFH1) {
                canvas->fillCircle(iconX, iconY, 9, TFT_WHITE);
                canvas->drawLine(iconX - 6, iconY - 6, iconX - 18, iconY - 18, satColor);
                canvas->drawLine(iconX + 6, iconY - 6, iconX + 18, iconY - 18, satColor);
                canvas->drawLine(iconX - 6, iconY + 6, iconX - 18, iconY + 18, satColor);
                canvas->drawLine(iconX + 6, iconY + 6, iconX + 18, iconY + 18, satColor);
            } else if (t == ICON_BLUEWALKER3) {
                canvas->fillRect(iconX - 3, iconY - 3, 9, 9, TFT_WHITE);
                canvas->fillRect(iconX - 21, iconY - 9, 15, 21, satColor);
                canvas->fillRect(iconX + 9, iconY - 9, 15, 21, satColor);
                canvas->drawFastVLine(iconX - 15, iconY - 9, 21, TFT_BLACK);
                canvas->drawFastVLine(iconX - 9, iconY - 9, 21, TFT_BLACK);
                canvas->drawFastVLine(iconX + 15, iconY - 9, 21, TFT_BLACK);
                canvas->drawFastVLine(iconX + 21, iconY - 9, 21, TFT_BLACK);
                canvas->drawFastHLine(iconX - 21, iconY, 15, TFT_BLACK);
                canvas->drawFastHLine(iconX + 9, iconY, 15, TFT_BLACK);
            } else if (t == ICON_WEATHER) {
                canvas->fillRect(iconX - 3, iconY - 6, 9, 15, TFT_WHITE);
                canvas->drawLine(iconX - 6, iconY, iconX - 18, iconY - 6, satColor);
                canvas->fillRect(iconX - 24, iconY - 12, 9, 9, satColor);
                canvas->fillRect(iconX + 6, iconY - 3, 6, 3, satColor);
                canvas->fillRect(iconX + 9, iconY - 6, 3, 3, satColor);
            } else if (t == ICON_NAVIGATION) {
                canvas->fillRect(iconX - 3, iconY - 6, 9, 15, TFT_WHITE);
                canvas->fillRect(iconX - 24, iconY - 3, 9, 9, satColor);
                canvas->fillRect(iconX + 15, iconY - 3, 9, 9, satColor);
                canvas->drawFastHLine(iconX - 15, iconY + 1, 12, TFT_LIGHTGRAY);
                canvas->drawFastHLine(iconX + 6, iconY + 1, 9, TFT_LIGHTGRAY);
                canvas->fillRect(iconX - 1, iconY + 9, 3, 6, satColor);
                canvas->fillCircle(iconX, iconY + 15, 3, satColor);
            } else if (t == ICON_COMMUNICATION) {
                canvas->fillCircle(iconX, iconY, 6, TFT_WHITE);
                canvas->drawLine(iconX, iconY - 6, iconX - 9, iconY - 18, satColor);
                canvas->drawLine(iconX, iconY - 6, iconX + 9, iconY - 18, satColor);
                canvas->drawFastVLine(iconX, iconY + 6, 6, satColor);
                canvas->drawFastHLine(iconX - 6, iconY + 12, 13, satColor);
                canvas->drawFastHLine(iconX - 3, iconY + 13, 7, satColor);
            } else {
                canvas->fillRect(iconX - 3, iconY - 3, 9, 9, TFT_WHITE);
                canvas->fillRect(iconX - 15, iconY - 3, 9, 9, satColor);
                canvas->fillRect(iconX - 6, iconY - 1, 3, 3, TFT_LIGHTGRAY);
            }
            
            canvas->setTextColor(selSat.color);
            canvas->drawString(selSat.name.c_str(), rightX + 48, descY + 8);
            
            if (selSat.tle.line1.length() >= 32) {
                uint32_t currentSimTime = current_unix + timeMachineOffset;
                uint32_t satEpoch = parseTleEpoch(selSat.tle.line1);
                int ageDays = -1;
                if (satEpoch > 0 && currentSimTime >= satEpoch) {
                    ageDays = (currentSimTime - satEpoch) / 86400;
                }
                
                char ageBuf[32];
                uint16_t ageColor = TFT_GREEN;
                if (ageDays < 0) {
                    sprintf(ageBuf, "GP Age:N/A");
                    ageColor = TFT_RED;
                } else {
                    sprintf(ageBuf, "GP Age:%dd", ageDays);
                    if (ageDays <= 7) ageColor = TFT_GREEN;
                    else if (ageDays <= 14) ageColor = TFT_ORANGE;
                    else ageColor = TFT_RED;
                }
                canvas->setTextColor(ageColor);
                int ageW = canvas->textWidth(ageBuf);
                canvas->drawString(ageBuf, width - ageW - 4, descY - 2);
            } else {
                canvas->setTextColor(TFT_RED);
                canvas->drawString("GP Age:N/A", width - canvas->textWidth("GP Age:N/A") - 4, descY - 2);
            }
            
            descY += 32;
            
            double tx, ty, tz;
            bool isTracking = false;
            double az = 0, el = 0, dist = 0;
            
            if (selSat.calc.getTEME(current_unix + timeMachineOffset, tx, ty, tz)) {
                double gmst = CoordTransform::getGMST(CoordTransform::unixToJulian(current_unix + timeMachineOffset));
                ECEFCoord satEcef = CoordTransform::temeToECEF(tx, ty, tz, gmst);
                GeodeticCoord obsGeo = {baseUserLat, baseUserLon, baseUserAlt / 1000.0};
                TopocentricCoord topo = CoordTransform::ecefToTopocentric(obsGeo, satEcef);
                az = topo.az; el = topo.el; dist = topo.range;
                if (el > 0) isTracking = true;
            }
            
            int radioY = 124;
            int requiredLines = 0;
            
            if (isTracking) {
                requiredLines = 1;
                if (selSat.type == SAT_TYPE_WEATHER) {
                    requiredLines = 3;
                } else if (selSat.type == SAT_TYPE_SPACE_STATION && selSat.noradId == 25544) {
                    requiredLines = 4;
                } else if (selSat.type == SAT_TYPE_HAM) {
                    if (selSat.uplinkFreq.length() > 0) {
                        requiredLines = 4;
                    } else {
                        requiredLines = 3;
                    }
                }
            } else {
                if (selSat.type == SAT_TYPE_WEATHER) {
                    requiredLines = 3;
                } else if (selSat.type == SAT_TYPE_SPACE_STATION && selSat.noradId == 25544) {
                    requiredLines = 3;
                } else if (selSat.type == SAT_TYPE_HAM) {
                    PassEvent nextPass;
                    bool foundNext = false;
                    for (const auto& pass : recommendedPasses) {
                        if (pass.satName == selSat.name && pass.aosTime >= current_unix + timeMachineOffset) {
                            nextPass = pass;
                            foundNext = true;
                            break;
                        }
                    }
                    int baseLines = (selSat.uplinkFreq.length() > 0) ? 3 : 2;
                    if (foundNext) {
                        requiredLines = baseLines + 2;
                    } else {
                        requiredLines = baseLines;
                    }
                }
            }
            
            if (requiredLines > 0) {
                radioY = bottomLimit - (requiredLines * 11 + 2);
            } else {
                radioY = bottomLimit;
            }
            
            canvas->setTextColor(TFT_LIGHTGRAY);
            if (selSat.description) {
                int descAreaHeight = radioY - descY - 2;
                int totalLines = drawWrappedText(canvas, selSat.description, rightX, descY, width - rightX - 5, 10, false);
                int totalHeight = totalLines * 10;
                
                int yOffset = 0;
                if (totalHeight > descAreaHeight && descAreaHeight > 10) {
                    int scrollRange = totalHeight - descAreaHeight + 10;
                    int cycleTime = scrollRange * 33 + 2000;
                    int t = millis() % cycleTime;
                    if (t < 1000) yOffset = 0;
                    else if (t < cycleTime - 1000) yOffset = (t - 1000) / 33;
                    else yOffset = scrollRange;
                }
                
                canvas->setClipRect(rightX, descY, width - rightX, descAreaHeight);
                drawWrappedText(canvas, selSat.description, rightX, descY - yOffset, width - rightX - 5, 10);
                canvas->clearClipRect();
            } else {
                canvas->drawString("No description.", rightX, descY);
            }
            
            if (satSelectedIndex >= NUM_BUILTIN_SATELLITES && requiredLines == 0) {
                canvas->setTextColor(TFT_YELLOW);
                canvas->drawString("Press 'd' to delete", rightX, bottomLimit - 12);
            }
            
            if (requiredLines > 0) {
                canvas->fillRect(rightX - 2, radioY - 2, width - rightX - 2, requiredLines * 11 + 2, canvas->color565(30, 40, 50));
                
                if (isTracking) {
                    double tx_prev, ty_prev, tz_prev;
                    double dist_prev = dist;
                    if (selSat.calc.getTEME(current_unix + timeMachineOffset - 1, tx_prev, ty_prev, tz_prev)) {
                        double gmst_prev = CoordTransform::getGMST(CoordTransform::unixToJulian(current_unix + timeMachineOffset - 1));
                        ECEFCoord ecef_prev = CoordTransform::temeToECEF(tx_prev, ty_prev, tz_prev, gmst_prev);
                        GeodeticCoord obsGeo = {baseUserLat, baseUserLon, baseUserAlt / 1000.0};
                        TopocentricCoord topo_prev = CoordTransform::ecefToTopocentric(obsGeo, ecef_prev);
                        dist_prev = topo_prev.range;
                    }
                    double radialVel = dist - dist_prev;
                    
                    double dlFreq = selSat.downlinkFreq.toDouble();
                    double dopplerShiftHz = 0;
                    if (dlFreq > 0) {
                        dopplerShiftHz = -(radialVel / 299792.458) * (dlFreq * 1e6);
                    }
                    
                    canvas->setTextColor(TFT_YELLOW);
                    char posBuf[32];
                    sprintf(posBuf, "Az:%03.0f El:%02.0f", az, el);
                    canvas->drawString(posBuf, rightX, radioY);
                    
                    if (selSat.type == SAT_TYPE_WEATHER) {
                        canvas->setTextColor(TFT_GREEN);
                        char freqBuf[32];
                        sprintf(freqBuf, "Rx:%s", selSat.downlinkFreq.c_str());
                        canvas->drawString(freqBuf, rightX, radioY + 11);
                        
                        canvas->setTextColor(dopplerShiftHz > 0 ? TFT_CYAN : TFT_ORANGE);
                        char dopBuf[32];
                        sprintf(dopBuf, "%+dHz", (int)dopplerShiftHz);
                        canvas->drawString(dopBuf, rightX + canvas->textWidth(freqBuf) + 2, radioY + 11);
                        
                        canvas->setTextColor(TFT_LIGHTGRAY);
                        canvas->drawString("Mode: " + selSat.radioMode, rightX, radioY + 22);
                    }
                    else if (selSat.type == SAT_TYPE_SPACE_STATION && selSat.noradId == 25544) {
                        double dlFreq2 = 145.825;
                        double dopplerShiftHz2 = -(radialVel / 299792.458) * (dlFreq2 * 1e6);
                        
                        canvas->setTextColor(TFT_GREEN);
                        char f1[32];
                        sprintf(f1, "APRS:%07.3f", dlFreq2 + dopplerShiftHz2/1e6);
                        canvas->drawString(f1, rightX, radioY + 11);
                        
                        char f2[32];
                        sprintf(f2, "SSTV:%07.3f", dlFreq + dopplerShiftHz/1e6);
                        canvas->drawString(f2, rightX, radioY + 22);
                        
                        canvas->setTextColor(TFT_LIGHTGRAY);
                        canvas->drawString("Mode: FM/Packet", rightX, radioY + 33);
                    }
                    else if (selSat.type == SAT_TYPE_HAM) {
                        canvas->setTextColor(TFT_GREEN);
                        char freqBuf[32];
                        sprintf(freqBuf, "Rx:%s", selSat.downlinkFreq.c_str());
                        canvas->drawString(freqBuf, rightX, radioY + 11);
                        
                        canvas->setTextColor(dopplerShiftHz > 0 ? TFT_CYAN : TFT_ORANGE);
                        char dopBuf[32];
                        sprintf(dopBuf, "%+dHz", (int)dopplerShiftHz);
                        canvas->drawString(dopBuf, rightX + canvas->textWidth(freqBuf) + 2, radioY + 11);
                        
                        int nextLineY = radioY + 22;
                        if (selSat.uplinkFreq.length() > 0) {
                            canvas->setTextColor(TFT_RED);
                            String txStr = "Tx:" + selSat.uplinkFreq;
                            if (selSat.tone.length() > 0) txStr += " T:" + selSat.tone;
                            canvas->drawString(txStr.c_str(), rightX, nextLineY);
                            nextLineY += 11;
                        }
                        
                        canvas->setTextColor(TFT_LIGHTGRAY);
                        canvas->drawString("Mode: " + selSat.radioMode, rightX, nextLineY);
                    }
                } else {
                    if (selSat.type == SAT_TYPE_WEATHER) {
                        canvas->setTextColor(TFT_GREEN);
                        canvas->drawString("Rx: " + selSat.downlinkFreq + " MHz", rightX, radioY);
                        canvas->setTextColor(TFT_CYAN);
                        canvas->drawString("Mode: " + selSat.radioMode, rightX, radioY + 11);
                        canvas->setTextColor(TFT_LIGHTGRAY);
                        canvas->drawString("Weather Imaging", rightX, radioY + 22);
                    }
                    else if (selSat.type == SAT_TYPE_SPACE_STATION && selSat.noradId == 25544) {
                        canvas->setTextColor(TFT_GREEN);
                        canvas->drawString("APRS: 145.825 MHz", rightX, radioY);
                        canvas->drawString("SSTV: 145.800 MHz", rightX, radioY + 11);
                        canvas->setTextColor(TFT_CYAN);
                        canvas->drawString("Mode: FM/Packet", rightX, radioY + 22);
                    }
                    else if (selSat.type == SAT_TYPE_HAM) {
                        canvas->setTextColor(TFT_GREEN);
                        canvas->drawString("Rx: " + selSat.downlinkFreq + " MHz", rightX, radioY);
                        
                        int nextLineY = radioY + 11;
                        if (selSat.uplinkFreq.length() > 0) {
                            canvas->setTextColor(TFT_RED);
                            String txStr = "Tx:" + selSat.uplinkFreq;
                            if (selSat.tone.length() > 0) txStr += " T:" + selSat.tone;
                            canvas->drawString(txStr.c_str(), rightX, nextLineY);
                            nextLineY += 11;
                        }
                        
                        canvas->setTextColor(TFT_CYAN);
                        canvas->drawString("Mode: " + selSat.radioMode, rightX, nextLineY);
                        nextLineY += 11;
                        
                        PassEvent nextPass;
                        bool foundNext = false;
                        for (const auto& pass : recommendedPasses) {
                            if (pass.satName == selSat.name && pass.aosTime >= current_unix + timeMachineOffset) {
                                nextPass = pass;
                                foundNext = true;
                                break;
                            }
                        }
                        if (foundNext) {
                            canvas->setTextColor(TFT_YELLOW);
                            time_t aosTime = nextPass.aosTime;
                            time_t losTime = nextPass.losTime;
                            
                            struct tm* aos_info = localtime(&aosTime);
                            char aosBuf[16];
                            strftime(aosBuf, sizeof(aosBuf), "%H:%M:%S", aos_info);
                            
                            struct tm* los_info = localtime(&losTime);
                            char losBuf[16];
                            strftime(losBuf, sizeof(losBuf), "%H:%M:%S", los_info);
                            
                            char aosStr[32];
                            char losStr[32];
                            sprintf(aosStr, "AOS: %s", aosBuf);
                            sprintf(losStr, "LOS: %s El:%02d", losBuf, (int)nextPass.maxElevation);
                            
                            canvas->drawString(aosStr, rightX, nextLineY);
                            canvas->drawString(losStr, rightX, nextLineY + 11);
                        }
                    }
                }
            }
        } else {
            if (downloadErrorMsg.length() > 0) {
                canvas->setTextColor(TFT_RED);
                drawWrappedText(canvas, downloadErrorMsg.c_str(), rightX, descY, width - rightX - 5, 10);
            } else {
                canvas->setTextColor(TFT_LIGHTGRAY);
                int lines = drawWrappedText(canvas, "Enter 5-digit NORAD ID to add custom satellite.", rightX, descY, width - rightX - 5, 10);
                
                canvas->setTextColor(TFT_YELLOW);
                canvas->drawString("Source: celestrak.org", rightX, descY + lines * 10 + 4);
            }
        }
    } else {
        // TAB_RECENT_LAUNCH Tab
        if (!recentLaunchDownloadSuccess && g_recentLaunches.empty()) {
            if (recentLaunchDownloading) {
                canvas->setTextColor(TFT_YELLOW);
                canvas->drawString("Downloading TLEs...", width/2 - canvas->textWidth("Downloading TLEs...")/2, height/2 - 10);
                if (recentLaunchErrorMsg.length() > 0) {
                    canvas->setTextColor(TFT_LIGHTGRAY);
                    canvas->drawString(recentLaunchErrorMsg.c_str(), width/2 - canvas->textWidth(recentLaunchErrorMsg.c_str())/2, height/2 + 5);
                }
            } else {
                canvas->fillRect(10, 30, width - 20, height - 55, canvas->color565(35, 45, 55));
                canvas->drawRect(10, 30, width - 20, height - 55, TFT_YELLOW);
                
                canvas->setTextColor(TFT_YELLOW);
                canvas->drawString("Recent Launch is an online feature.", width/2 - canvas->textWidth("Recent Launch is an online feature.")/2, height/2 - 20);
                canvas->setTextColor(TFT_WHITE);
                canvas->drawString("Press 'w' to connect WiFi", width/2 - canvas->textWidth("Press 'w' to connect WiFi")/2, height/2 - 2);
                canvas->drawString("& download latest launcher groups.", width/2 - canvas->textWidth("& download latest launcher groups.")/2, height/2 + 8);
                
                if (recentLaunchErrorMsg.length() > 0) {
                    canvas->setTextColor(TFT_RED);
                    canvas->drawString(recentLaunchErrorMsg.c_str(), width/2 - canvas->textWidth(recentLaunchErrorMsg.c_str())/2, height/2 + 22);
                }
            }
        } else {
            int yPos = 25;
            int itemsPerPage = showBanner ? 6 : 7;
            int itemSpacing = showBanner ? 16 : 15;
            int startIndex = (recentLaunchSelectedIndex / itemsPerPage) * itemsPerPage;
            int totalItems = g_recentLaunches.size();
            
            for (int i = 0; i < itemsPerPage && (startIndex + i) < totalItems; i++) {
                int index = startIndex + i;
                if (index == recentLaunchSelectedIndex) {
                    canvas->fillRect(2, yPos - 2, 82, itemSpacing - 1, canvas->color565(0, 120, 255));
                    canvas->setTextColor(TFT_WHITE);
                } else {
                    canvas->setTextColor(TFT_LIGHTGRAY);
                }
                
                String checkBox = g_recentLaunches[index].selected ? "[x]" : "[ ]";
                canvas->drawString(checkBox.c_str(), 4, yPos);
                
                String nameStr = g_recentLaunches[index].displayName;
                if (g_recentLaunches[index].isGroup) {
                    nameStr = nameStr + " (" + String(g_recentLaunches[index].satelliteCount) + ")";
                }
                if (nameStr.length() > 9) nameStr = nameStr.substring(0, 7) + "..";
                canvas->drawString(nameStr.c_str(), 28, yPos);
                
                yPos += itemSpacing;
            }
            
            canvas->drawFastVLine(85, 20, bottomLimit - 20, TFT_DARKGREY);
            
            int rightX = 89;
            if (recentLaunchSelectedIndex < totalItems) {
                RecentLaunchItem& item = g_recentLaunches[recentLaunchSelectedIndex];
                
                uint32_t epoch = item.epoch;
                float inclination = item.inclination;
                float avgAlt = item.avgAlt;
                
                uint32_t currentSimTime = current_unix + timeMachineOffset;
                int ageDays = -1;
                if (epoch > 0 && currentSimTime >= epoch) {
                    ageDays = (currentSimTime - epoch) / 86400;
                }
                
                // 1. Calculate Recommended Stars based on occupancy (compact trains get higher score)
                // Use ASCII stars '*' and '-' to avoid font rendering blocks on Cardputer TFT screen
                const char* stars = "**---";
                if (item.occupancy < 10.0f) stars = "*****";
                else if (item.occupancy < 30.0f) stars = "****-";
                else if (item.occupancy < 90.0f) stars = "***--";
                else stars = "**---";
                
                if (!recentLaunchInObjectsView) {
                    canvas->setTextColor(TFT_GOLD);
                    canvas->drawString(item.displayName.c_str(), rightX, 23);
                    
                    canvas->setTextColor(TFT_YELLOW);
                    int starsW = canvas->textWidth(stars);
                    canvas->drawString(stars, width - starsW - 4, 23);
                    
                    canvas->setTextColor(TFT_CYAN);
                    canvas->drawString(("Batch: " + item.batchId).c_str(), rightX, 33);
                    
                    // Age placement on the right
                    char ageBuf[16];
                    uint16_t ageColor = TFT_GREEN;
                    if (ageDays < 0) {
                        sprintf(ageBuf, "Age: N/A");
                        ageColor = TFT_RED;
                    } else {
                        sprintf(ageBuf, "%dd", ageDays);
                        if (ageDays <= 7) ageColor = TFT_GREEN;
                        else if (ageDays <= 14) ageColor = TFT_ORANGE;
                        else ageColor = TFT_RED;
                    }
                    canvas->setTextColor(ageColor);
                    int ageW = canvas->textWidth(ageBuf);
                    canvas->drawString(ageBuf, width - ageW - 4, 33);
                    
                    char dateBuf[32];
                    if (epoch > 0) {
                        time_t tEpoch = (time_t)epoch;
                        struct tm* timeinfo = gmtime(&tEpoch);
                        strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", timeinfo);
                    } else {
                        sprintf(dateBuf, "N/A");
                    }
                    canvas->setTextColor(TFT_LIGHTGRAY);
                    canvas->drawString(("Launch: " + String(dateBuf)).c_str(), rightX, 43);
                    
                    // Display real count of objects and clustered proxies
                    char satsBuf[64];
                    int proxyCount = item.proxyFormation.size();
                    sprintf(satsBuf, "Objects: %d | Proxy: %d", item.satelliteCount, proxyCount);
                    canvas->drawString(satsBuf, rightX, 53);
                    
                    char orbitBuf[48];
                    sprintf(orbitBuf, "Orbit: %dkm, %.1f*", (int)avgAlt, inclination);
                    canvas->drawString(orbitBuf, rightX, 63);
                    
                    // Formation State & Occupancy degree
                    canvas->setTextColor(TFT_GREEN);
                    canvas->drawString("Status:", rightX, 73);
                    canvas->setTextColor(TFT_WHITE);
                    const char* formState = "Operational";
                    if (item.occupancy < 15.0f) formState = "Tight Train";
                    else if (item.occupancy < 60.0f) formState = "Train Formation";
                    else if (item.occupancy < 120.0f) formState = "Expanding";
                    canvas->drawString(formState, rightX + 45, 73);
                    
                    char occBuf[32];
                    sprintf(occBuf, "Occ: %d*", (int)item.occupancy);
                    canvas->setTextColor(TFT_CYAN);
                    int occW = canvas->textWidth(occBuf);
                    canvas->drawString(occBuf, width - occW - 4, 94);
                    
                    // Distribution indicator (8 refined blocks)
                    canvas->setTextColor(TFT_GREEN);
                    canvas->drawString("Distribution:", rightX, 85);
                    
                    int barY = 95;
                    int filledCount = (int)((item.occupancy / 360.0f) * 8.0f + 0.5f);
                    if (filledCount < 1 && item.occupancy > 0.0f) filledCount = 1;
                    if (filledCount > 8) filledCount = 8;
                    
                    for (int k = 0; k < 8; k++) {
                        int bx = rightX + k * 10;
                        if (k < filledCount) {
                            canvas->fillRect(bx, barY, 8, 6, 0x07FF); // High-contrast Cyan block
                        } else {
                            canvas->drawRect(bx, barY, 8, 6, TFT_DARKGREY); // Empty block
                        }
                    }
                    
                    canvas->setTextColor(TFT_GREEN);
                    canvas->drawString("Visibility:", rightX, 107);
                    canvas->setTextColor(TFT_YELLOW);
                    if (avgAlt >= 250 && avgAlt <= 600) {
                        canvas->drawString("Excellent", rightX + 65, 107);
                    } else if (avgAlt > 0) {
                        canvas->drawString("Moderate", rightX + 65, 107);
                    } else {
                        canvas->drawString("N/A", rightX + 65, 107);
                    }
                    
                    canvas->setTextColor(TFT_LIGHTGRAY);
                    canvas->drawString("Press 'O' for Objects", rightX, bottomLimit - 8);
                } else {
                    canvas->setTextColor(TFT_GOLD);
                    String title = item.displayName;
                    if (title.length() > 10) title = title.substring(0, 8) + "..";
                    canvas->drawString((title + " Objects").c_str(), rightX, 25);
                    
                    canvas->setTextColor(TFT_CYAN);
                    int startNum = recentLaunchObjectPage * 5 + 1;
                    int endNum = startNum + g_level3Objects.size() - 1;
                    char pageBuf[32];
                    sprintf(pageBuf, "Page %d (%d-%d)", recentLaunchObjectPage + 1, startNum, endNum);
                    canvas->drawString(pageBuf, rightX, 35);
                    
                    int memY = 48;
                    for (size_t s = 0; s < g_level3Objects.size(); s++) {
                        auto& obj = g_level3Objects[s];
                        String satName = obj.name;
                        if (satName.length() > 16) satName = satName.substring(0, 14) + "..";
                        
                        canvas->setTextColor(TFT_LIGHTGRAY);
                        canvas->drawString(("- " + satName).c_str(), rightX, memY + s * 13);
                        
                        if (obj.lastGeoValid) {
                            char hBuf[16];
                            sprintf(hBuf, "%dkm", (int)obj.lastGeo.alt);
                            canvas->setTextColor(TFT_GREEN);
                            canvas->drawString(hBuf, width - 36, memY + s * 13);
                        }
                    }
                    
                    if (g_level3Objects.empty()) {
                        canvas->setTextColor(TFT_RED);
                        canvas->drawString("No Objects Found.", rightX, memY);
                    }
                }
            }
        }
    }
    
    // Draw Bottom Guide Banner (Only when updating or showing feedback)
    if (showBanner) {
        canvas->fillRect(0, height - 11, width, 11, canvas->color565(15, 20, 25));
        canvas->drawFastHLine(0, height - 11, width, TFT_DARKGREY);
        
        if (recentLaunchDownloading) {
            canvas->setTextColor(TFT_YELLOW);
            String msg = "Recent Launch Updating: " + recentLaunchErrorMsg;
            if (canvas->textWidth(msg.c_str()) > width - 8) {
                msg = msg.substring(0, 35) + "...";
            }
            canvas->drawString(msg.c_str(), 4, height - 9);
        } else {
            if (recentLaunchDownloadSuccess) {
                canvas->setTextColor(TFT_GREEN);
                canvas->drawString("Update Success: TLE cache overwritten!", 4, height - 9);
            } else {
                canvas->setTextColor(TFT_RED);
                String failMsg = "Update Failed: " + recentLaunchErrorMsg;
                if (canvas->textWidth(failMsg.c_str()) > width - 8) {
                    failMsg = failMsg.substring(0, 35) + "...";
                }
                canvas->drawString(failMsg.c_str(), 4, height - 9);
            }
        }
    }
    
    // Draw Delete Confirm Popup
    if (deleteConfirmIndex >= NUM_BUILTIN_SATELLITES && deleteConfirmIndex < NUM_SATELLITES && currentSatTab == TAB_ENCYCLOPEDIA) {
        canvas->fillRect(40, height/2 - 20, width - 80, 40, TFT_RED);
        canvas->drawRect(40, height/2 - 20, width - 80, 40, TFT_WHITE);
        canvas->setTextColor(TFT_WHITE);
        canvas->drawString("Delete Custom Sat?", 45, height/2 - 15);
        canvas->drawString("[y] Yes  [n] No", 45, height/2 + 5);
    }
    
    // Draw List Selection Page Help Overlay
    if (showListHelp) {
        uint16_t w = 200, h = 108;
        int x = (width - w) / 2;
        int y = (height - h) / 2;
        
        canvas->fillRect(x, y, w, h, canvas->color565(20, 30, 40));
        canvas->drawRect(x, y, w, h, TFT_LIGHTGRAY);
        
        canvas->setTextColor(TFT_WHITE);
        canvas->setTextSize(1);
        canvas->drawString("--- Setup Shortcuts ---", x + 25, y + 5);
        
        auto drawHotKey = [&](const char* word, char keyChar, int dx, int dy) {
            int cx = dx;
            bool highlighted = false;
            for (int i = 0; word[i] != '\0'; i++) {
                if (!highlighted && tolower(word[i]) == tolower(keyChar) && keyChar != '\0') {
                    canvas->setTextColor(TFT_YELLOW);
                    highlighted = true;
                } else {
                    canvas->setTextColor(TFT_LIGHTGRAY);
                }
                char cstr[2] = {word[i], '\0'};
                canvas->drawString(cstr, cx, dy);
                cx += canvas->textWidth(cstr);
            }
        };
        
        int ty = y + 20;
        if (currentSatTab == TAB_ENCYCLOPEDIA) {
            drawHotKey("Move( ; / . )", ';', x + 5, ty);
            drawHotKey("Tab( / )", '/', x + 105, ty); ty += 12;
            
            drawHotKey("Select(Ent)", 'e', x + 5, ty);
            drawHotKey("Del(Back)", 'd', x + 105, ty); ty += 12;
            
            drawHotKey("WiFi(w)", 'w', x + 5, ty);
            drawHotKey("Exit(Esc)", 'x', x + 105, ty); ty += 12;
        } else {
            if (recentLaunchInObjectsView) {
                drawHotKey("Page[ [ / ] ]", '[', x + 5, ty); ty += 12;
                drawHotKey("Back(Esc)", 'b', x + 5, ty); ty += 12;
            } else {
                drawHotKey("Move( ; / . )", ';', x + 5, ty);
                drawHotKey("Tab( / )", '/', x + 105, ty); ty += 12;
                
                drawHotKey("Select(Ent)", 'e', x + 5, ty);
                drawHotKey("Objects(o)", 'o', x + 105, ty); ty += 12;
                
                drawHotKey("WiFi(w)", 'w', x + 5, ty);
                drawHotKey("Exit(Esc)", 'x', x + 105, ty); ty += 12;
            }
        }
        
        canvas->setTextColor(TFT_YELLOW);
        canvas->drawString("Press any key to Close", x + 35, y + h - 14);
    }


}

void loop() {
    if (g_recentLaunchesPending) {
        g_recentLaunches = std::move(g_pendingRecentLaunches);
        g_pendingRecentLaunches.clear();
        g_recentLaunchesPending = false;
        bool hasSelected = false;
        for (auto& item : g_recentLaunches) {
            if (item.selected) {
                if (item.batchId == recentLaunchActiveBatchId) {
                    hasSelected = true;
                }
                initRecentLaunchCalcs(item);
            }
        }
        if (!hasSelected && g_recentLaunchFocusMode) {
            for (auto& item : g_recentLaunches) {
                if (item.selected) {
                    recentLaunchActiveBatchId = item.batchId;
                    initRecentLaunchCalcs(item);
                    hasSelected = true;
                    break;
                }
            }
            if (!hasSelected) {
                g_recentLaunchFocusMode = false;
                recentLaunchActiveBatchId = "";
                g_repSatInitialized = false;
            }
        }
        LOG_I("APP", "Applied new recent launches safely on main core.");
    }

    bool isFastForwarding = false;
    M5Cardputer.update();

    // BtnG0 (side button): trigger screenshot transfer via serial
    if (M5Cardputer.BtnA.wasPressed()) {
        doScreenshot();
    }


    
    if (gnss) {
        gnss->update();
    }

    // Render at 30 FPS (33ms)
    if (millis() - last_update >= 33) {
        last_update = millis();
        
        // Handle continuous keyboard input (Time Machine or Manual Location)
        static unsigned long keyHoldStartTime = 0;
        static char lastKey = 0;
        static unsigned long lastKeyRepeat = 0;
        isFastForwarding = false;
        static double targetFocusAlt = 0.0;
        
        if (appState == STATE_MAIN) {
            char currentKey = 0;
            if (M5Cardputer.Keyboard.isKeyPressed(',')) currentKey = ',';
            else if (M5Cardputer.Keyboard.isKeyPressed('/')) currentKey = '/';
            else if (M5Cardputer.Keyboard.isKeyPressed(';')) currentKey = ';';
            else if (M5Cardputer.Keyboard.isKeyPressed('.')) currentKey = '.';
            else if (M5Cardputer.Keyboard.isKeyPressed('-') || M5Cardputer.Keyboard.isKeyPressed('_')) currentKey = '-';
            else if (M5Cardputer.Keyboard.isKeyPressed('=') || M5Cardputer.Keyboard.isKeyPressed('+')) currentKey = '=';
            else if (M5Cardputer.Keyboard.isKeyPressed(' ')) currentKey = ' ';
            else if (M5Cardputer.Keyboard.isKeyPressed('[')) currentKey = '[';
            else if (M5Cardputer.Keyboard.isKeyPressed(']')) currentKey = ']';
            
            auto handleContinuousKey = [&](char key) {
                if (showRecommendations) {
                    if (selectedPassIndex == -1) {
                        if (key == ';') { if (passScrollIndex > 0) passScrollIndex--; }
                        else if (key == '.') { if (passScrollIndex < (int)displayTree.size() - 1) passScrollIndex++; }
                    }
                } else if ((isSatViewMode || (!isManualLocationMode)) && !showRecommendations) {
                    if (key == ',') timeMachineOffset -= 60;
                    else if (key == '/') timeMachineOffset += 60;
                    else if (key == '[') {
                        if (currentBrightness >= 32) currentBrightness -= 16;
                        else currentBrightness = 16;
                        M5Cardputer.Display.setBrightness(currentBrightness);
                    } else if (key == ']') {
                        if (currentBrightness <= 239) currentBrightness += 16;
                        else currentBrightness = 255;
                        M5Cardputer.Display.setBrightness(currentBrightness);
                    }
                } else if (isManualLocationMode) {
                    // Step size based on zoom level, finer control when zoomed in
                    float step = 1.0f / currentZoom;
                    bool locChanged = false;
                    if (key == ';') { baseUserLat += step; if (baseUserLat > 90) baseUserLat = 90; locChanged = true; }
                    else if (key == '.') { baseUserLat -= step; if (baseUserLat < -90) baseUserLat = -90; locChanged = true; }
                    else if (key == ',') { baseUserLon -= step; if (baseUserLon < -180) baseUserLon += 360; locChanged = true; }
                    else if (key == '/') { baseUserLon += step; if (baseUserLon > 180) baseUserLon -= 360; locChanged = true; }
                    else if (key == '[') { baseUserAlt -= 10.0; if (baseUserAlt < -500) baseUserAlt = -500; locChanged = true; }
                    else if (key == ']') { baseUserAlt += 10.0; if (baseUserAlt > 9000) baseUserAlt = 9000; locChanged = true; }
                    
                    if (locChanged) {
                        portENTER_CRITICAL(&passMutex);
                        lastPredictionBaseTime = 0; // 缓存失效
                        predictionsReady = false;
                        portEXIT_CRITICAL(&passMutex);
                    }
                }
                
                if (key == ' ') {
                    isImuLocked = !isImuLocked;
                }
                
                if (key == '-' || key == '_') {
                    targetZoom -= 0.2f;
                    float minLimit = 0.95f;
                    if (isSatViewMode) {
                        double visualAlt = targetFocusAlt;
                        if (visualAlt > 20000.0f) visualAlt = 20000.0f;
                        if (visualAlt < 0.0f) visualAlt = 0.0f;
                        minLimit = 62.0f / (55.0f + sqrtf(visualAlt) * 0.4f);
                    }
                    if (targetZoom < minLimit) targetZoom = minLimit;
                } else if (key == '=' || key == '+') {
                    targetZoom += 0.2f;
                    if (targetZoom > 20.0f) targetZoom = 20.0f;
                }

            };
            
            static unsigned long keyReleaseTime = 0;
            if (currentKey != 0) {
                keyReleaseTime = 0;
                if (lastKey != currentKey) {
                    // Initial press
                    lastKey = currentKey;
                    keyHoldStartTime = millis();
                    lastKeyRepeat = millis();
                    handleContinuousKey(currentKey);
                } else {
                    // Held down
                    if (millis() - keyHoldStartTime > 400) { // 400ms delay before repeat
                        if (!isManualLocationMode && !showRecommendations) isFastForwarding = true; // Flag for rendering optimization
                        if (millis() - lastKeyRepeat > 33) { // ~30Hz repeat rate
                            lastKeyRepeat = millis();
                            handleContinuousKey(currentKey);
                        }
                    }
                }
            } else {
                if (lastKey != 0) {
                    if (keyReleaseTime == 0) keyReleaseTime = millis();
                    if (millis() - keyReleaseTime > 150) { // 150ms debounce for I2C drops
                        lastKey = 0;
                    } else if (millis() - keyHoldStartTime > 400) {
                        // Keep fast forwarding flag alive during debounce
                        if (!isManualLocationMode && !showRecommendations) isFastForwarding = true;
                    }
                }
            }
        }

        
        // Handle discrete keyboard input
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            if (appState == STATE_MAIN) {
                if (M5Cardputer.Keyboard.isKeyPressed('c')) {
                    if (isSatViewMode) {
                        isSatViewMode = false;
                        targetZoom = 0.95f;
                    }

                    isManualLocationMode = !isManualLocationMode;
                    if (!isManualLocationMode) {
                        portENTER_CRITICAL(&passMutex);
                        predictionsReady = false;
                        lastPredictionBaseTime = 0; // 缓存失效
                        portEXIT_CRITICAL(&passMutex);
                        triggerPrediction = true;
                    }
                } else if (M5Cardputer.Keyboard.isKeyPressed('r') || M5Cardputer.Keyboard.isKeyPressed('R')) {
                    if (!showRecommendations && !showHelp) {
                        timeMachineOffset = 0;
                        if (isManualLocationMode) {
                            baseUserLat = 39.90; // Beijing default
                            baseUserLon = 116.40;
                            baseUserAlt = 0.0;
                        }
                        portENTER_CRITICAL(&passMutex);
                        lastPredictionBaseTime = 0; // 按 r 重置时，缓存失效，强制重新计算
                        predictionsReady = false;
                        portEXIT_CRITICAL(&passMutex);
                        triggerPrediction = true;
                    }
                } else if (M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE)) {
                    showHud = !showHud;
                } else if (M5Cardputer.Keyboard.isKeyPressed(27) || M5Cardputer.Keyboard.isKeyPressed('`')) {
                    if (showRecommendations) {
                        if (selectedPassIndex != -1) {
                            selectedPassIndex = -1; // Back to tree
                        } else {
                            showRecommendations = false; // Close panel
                            cancelPrediction = true;     // 中止后台可能正在进行的计算
                        }
                    } else if (showHelp) {
                        showHelp = false;
                    } else if (isManualLocationMode) {
                        isManualLocationMode = false;
                    } else if (isSatViewMode) {
                        isSatViewMode = false;
                        targetZoom = 0.95f;
                    }

                } else if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
                    if (appState == STATE_MAIN && !showRecommendations) {
                        showRecommendations = true;
                        passScrollIndex = 0;
                        
                        uint32_t targetTime = current_unix + timeMachineOffset;
                        bool isCacheValid = false;
                        portENTER_CRITICAL(&passMutex);
                        if (predictionsReady && lastPredictionBaseTime != 0) {
                            // 获取时区秒数
                            int tzOffsetSec = pos_manager ? pos_manager->getTimezoneManager()->getTimezoneOffset(baseUserLat, baseUserLon) : ((int)round(baseUserLon / 15.0) * 3600);
                            uint32_t day1 = (lastPredictionBaseTime + tzOffsetSec) / 86400;
                            uint32_t day2 = (targetTime + tzOffsetSec) / 86400;
                            if (day1 == day2) {
                                isCacheValid = true;
                            }
                        }
                        portEXIT_CRITICAL(&passMutex);
                        
                        if (!isCacheValid && !g_orbitCalculating) {
                            triggerPrediction = true;
                            portENTER_CRITICAL(&passMutex);
                            predictionsReady = false;
                            portEXIT_CRITICAL(&passMutex);
                        }
                        rebuildTree(current_unix + timeMachineOffset);
                    } else if (showRecommendations) {
                        if (selectedPassIndex != -1) {
                            selectedPassIndex = -1; // Back to tree
                        } else {
                            // Toggle category or open detail
                            if (passScrollIndex >= 0 && passScrollIndex < displayTree.size()) {
                                auto& item = displayTree[passScrollIndex];
                                if (item.isCategory) {
                                    catExpanded[item.categoryIndex] = !catExpanded[item.categoryIndex];
                                    rebuildTree(current_unix);
                                } else {
                                    selectedPassIndex = item.passIndex;
                                }
                            }
                        }
                    }
                } else if (M5Cardputer.Keyboard.isKeyPressed('w')) {
                    if (!g_networkActive) {
                        if (!HalWifi::isConnected()) {
                            manualWifiToggle = true;
                            xTaskCreatePinnedToCore(networkTask, "NetworkTask", 12288, NULL, 1, NULL, 0);
                        } else {
                            WiFi.disconnect(true);
                            WiFi.mode(WIFI_OFF);
                        }
                    }
                } else if (M5Cardputer.Keyboard.isKeyPressed('s')) {
                    appState = STATE_SAT_SELECT;
                    currentSatTab = TAB_ENCYCLOPEDIA;
                    entrySelectedSatellites.clear();
                    for (int i = 0; i < NUM_SATELLITES; i++) {
                        if (g_satellites[i].selected) {
                            entrySelectedSatellites.push_back(g_satellites[i].noradId);
                        }
                    }
                } else if (M5Cardputer.Keyboard.isKeyPressed('h')) {
                    showHelp = !showHelp;
                } else if (M5Cardputer.Keyboard.isKeyPressed('g') || M5Cardputer.Keyboard.isKeyPressed('G')) {
                    if (gnss) {
                        if (gnss->isInStandbyMode()) {
                            gnss->exitStandbyMode();
                            gnssManualMode = true;
                            gnssTimedOut = false;
                            gnssStartTime = millis();
                        } else {
                            gnss->enterStandbyMode();
                            gnssManualMode = false;
                        }
                    }
                } else if (M5Cardputer.Keyboard.isKeyPressed('v') || M5Cardputer.Keyboard.isKeyPressed('V')) {
                    isSatViewMode = !isSatViewMode;
                    if (isSatViewMode) {
                        bool found = false;
                        if (g_recentLaunchFocusMode) {
                            found = true;
                        } else if (focusSatIndex >= 0 && focusSatIndex < NUM_SATELLITES && g_satellites[focusSatIndex].selected) {
                            found = true;
                        } else {
                            for (int i = 0; i < NUM_SATELLITES; i++) {
                                if (g_satellites[i].selected) {
                                    focusSatIndex = i;
                                    found = true;
                                    break;
                                }
                            }
                        }
                        if (!found) {
                            isSatViewMode = false;
                        } else {
                            isCameraTransitioning = true;
                            
                            if (attitude && imu) {
                                AttitudeData att = attitude->getAttitude();
                                basePitch = att.pitch;
                                baseRoll = att.roll;
                            }
                        }
                    } else {
                        // Keep current targetZoom
                    }

                }
                else if (M5Cardputer.Keyboard.isKeyPressed(';')) {
                    if (isSatViewMode && !showRecommendations) {
                        struct FocusTarget {
                            int type; // 0 = Regular Sat, 1 = Recent Launch
                            int index;
                        };
                        std::vector<FocusTarget> targets;
                        
                        for (int i = 0; i < NUM_SATELLITES; i++) {
                            if (g_satellites[i].selected) {
                                targets.push_back({0, i});
                            }
                        }
                        for (size_t i = 0; i < g_recentLaunches.size(); i++) {
                            if (g_recentLaunches[i].selected) {
                                targets.push_back({1, (int)i});
                            }
                        }
                        
                        if (!targets.empty()) {
                            int currentIdx = -1;
                            if (focusSatIndex >= 0) {
                                for (size_t i = 0; i < targets.size(); i++) {
                                    if (targets[i].type == 0 && targets[i].index == focusSatIndex) {
                                        currentIdx = i;
                                        break;
                                    }
                                }
                            } else if (g_recentLaunchFocusMode) {
                                for (size_t i = 0; i < targets.size(); i++) {
                                    if (targets[i].type == 1 && g_recentLaunches[targets[i].index].batchId == recentLaunchActiveBatchId) {
                                        currentIdx = i;
                                        break;
                                    }
                                }
                            }
                            
                            if (currentIdx != -1) {
                                int prevIdx = (currentIdx - 1 + targets.size()) % targets.size();
                                const auto& prevTarget = targets[prevIdx];
                                if (prevTarget.type == 0) {
                                    focusSatIndex = prevTarget.index;
                                    isCameraTransitioning = true;
                                } else {
                                    focusSatIndex = -1;
                                    g_recentLaunchFocusMode = true;
                                    auto& item = g_recentLaunches[prevTarget.index];
                                    recentLaunchActiveBatchId = item.batchId;
                                    initRecentLaunchCalcs(item);
                                    isCameraTransitioning = true;
                                }
                            }
                        }
                    }
                } else if (M5Cardputer.Keyboard.isKeyPressed('.')) {
                    if (isSatViewMode && !showRecommendations) {
                        struct FocusTarget {
                            int type; // 0 = Regular Sat, 1 = Recent Launch
                            int index;
                        };
                        std::vector<FocusTarget> targets;
                        
                        for (int i = 0; i < NUM_SATELLITES; i++) {
                            if (g_satellites[i].selected) {
                                targets.push_back({0, i});
                            }
                        }
                        for (size_t i = 0; i < g_recentLaunches.size(); i++) {
                            if (g_recentLaunches[i].selected) {
                                targets.push_back({1, (int)i});
                            }
                        }
                        
                        if (!targets.empty()) {
                            int currentIdx = -1;
                            if (focusSatIndex >= 0) {
                                for (size_t i = 0; i < targets.size(); i++) {
                                    if (targets[i].type == 0 && targets[i].index == focusSatIndex) {
                                        currentIdx = i;
                                        break;
                                    }
                                }
                            } else if (g_recentLaunchFocusMode) {
                                for (size_t i = 0; i < targets.size(); i++) {
                                    if (targets[i].type == 1 && g_recentLaunches[targets[i].index].batchId == recentLaunchActiveBatchId) {
                                        currentIdx = i;
                                        break;
                                    }
                                }
                            }
                            
                            if (currentIdx != -1) {
                                int nextIdx = (currentIdx + 1) % targets.size();
                                const auto& nextTarget = targets[nextIdx];
                                if (nextTarget.type == 0) {
                                    focusSatIndex = nextTarget.index;
                                    isCameraTransitioning = true;
                                } else {
                                    focusSatIndex = -1;
                                    g_recentLaunchFocusMode = true;
                                    auto& item = g_recentLaunches[nextTarget.index];
                                    recentLaunchActiveBatchId = item.batchId;
                                    initRecentLaunchCalcs(item);
                                    isCameraTransitioning = true;
                                }
                            }
                        }
                    }
                }


            } else if (appState == STATE_WIFI_SETUP) {
                if (M5Cardputer.Keyboard.isKeyPressed(27) || M5Cardputer.Keyboard.isKeyPressed('`')) {
                    if (wifiIsInputtingPassword) {
                        wifiIsInputtingPassword = false;
                    } else {
                        appState = STATE_MAIN;
                    }
                } else if (!wifiIsInputtingPassword && M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE)) {
                    appState = STATE_MAIN;
                } else if (wifiIsInputtingPassword) {
                    if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
                        // Connect
                        appState = STATE_MAIN;
                        NetworkParams* params = new NetworkParams();
                        params->ssid = wifiNetworks[wifiSelectedIndex].ssid;
                        params->pass = String(wifiPasswordBuffer);
                        params->shouldSave = true;
                        
                        manualWifiToggle = true; // Stay connected since user explicitly set it up
                        xTaskCreatePinnedToCore(
                            networkTask, "NetworkTask", 16384, params, 1, NULL, 0
                        );
                    } else if (M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE)) {
                        if (wifiPasswordLen > 0) {
                            wifiPasswordBuffer[--wifiPasswordLen] = '\0';
                        }
                    } else {
                        for (auto c : M5Cardputer.Keyboard.keysState().word) {
                            if (wifiPasswordLen < 63 && c >= ' ' && c <= '~') {
                                wifiPasswordBuffer[wifiPasswordLen++] = c;
                                wifiPasswordBuffer[wifiPasswordLen] = '\0';
                            }
                        }
                    }
                } else {
                    if (M5Cardputer.Keyboard.isKeyPressed('r')) {
                        wifiIsScanning = true;
                    } else if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
                        if (!wifiNetworks.empty()) {
                            wifiIsInputtingPassword = true;
                            memset(wifiPasswordBuffer, 0, sizeof(wifiPasswordBuffer));
                            wifiPasswordLen = 0;
                        }
                    } else if (M5Cardputer.Keyboard.isKeyPressed(';')) { // UP arrow
                        if (!wifiNetworks.empty()) {
                            if (wifiSelectedIndex > 0) wifiSelectedIndex--;
                            else wifiSelectedIndex = wifiNetworks.size() - 1;
                        }
                    } else if (M5Cardputer.Keyboard.isKeyPressed('.')) { // DOWN arrow
                        if (!wifiNetworks.empty()) {
                            wifiSelectedIndex = (wifiSelectedIndex + 1) % wifiNetworks.size();
                        }
                    }
                }
            } else if (appState == STATE_SAT_SELECT) {
                if (showListHelp) {
                    if (M5Cardputer.Keyboard.isKeyPressed('h') || M5Cardputer.Keyboard.isKeyPressed('H') || 
                        M5Cardputer.Keyboard.isKeyPressed(27) || M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE) || 
                        M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER) || M5Cardputer.Keyboard.isKeyPressed('`')) {
                        showListHelp = false;
                    }
                } else if (deleteConfirmIndex >= 0 && currentSatTab == TAB_ENCYCLOPEDIA) {
                    if (deleteConfirmIndex < NUM_BUILTIN_SATELLITES) {
                        deleteConfirmIndex = -1;
                    } else if (M5Cardputer.Keyboard.isKeyPressed('y')) {
                        if (deleteConfirmIndex >= NUM_BUILTIN_SATELLITES && deleteConfirmIndex < NUM_SATELLITES) {
                            for (int i = deleteConfirmIndex; i < NUM_SATELLITES - 1; i++) {
                                g_satellites[i] = g_satellites[i + 1];
                            }
                            NUM_SATELLITES--;
                            if (focusSatIndex == deleteConfirmIndex) focusSatIndex = -1;
                            else if (focusSatIndex > deleteConfirmIndex) focusSatIndex--;
                            if (satSelectedIndex >= NUM_SATELLITES) satSelectedIndex = NUM_SATELLITES;
                            saveCustomSatellites();
                        }
                        deleteConfirmIndex = -1;
                    } else if (M5Cardputer.Keyboard.isKeyPressed('n') || M5Cardputer.Keyboard.isKeyPressed(27)) {
                        deleteConfirmIndex = -1;
                    }
                } else if (M5Cardputer.Keyboard.isKeyPressed(',') || M5Cardputer.Keyboard.isKeyPressed('/')) {
                    currentSatTab = (currentSatTab == TAB_ENCYCLOPEDIA) ? TAB_RECENT_LAUNCH : TAB_ENCYCLOPEDIA;
                    noradInput = "";
                    downloadErrorMsg = "";
                    if (currentSatTab == TAB_ENCYCLOPEDIA) {
                        if (g_recentLaunchFocusMode) {
                            g_recentLaunchFocusMode = false;
                            recentLaunchActiveBatchId = "";
                            g_repSatInitialized = false;
                            portENTER_CRITICAL(&passMutex);
                            predictionsReady = false;
                            lastPredictionBaseTime = 0;
                            portEXIT_CRITICAL(&passMutex);
                            triggerPrediction = true;
                        }
                    } else {
                        bool hasSelected = false;
                        for (auto& item : g_recentLaunches) {
                            if (item.selected) {
                                if (!hasSelected) {
                                    g_recentLaunchFocusMode = true;
                                    recentLaunchActiveBatchId = item.batchId;
                                    hasSelected = true;
                                }
                                initRecentLaunchCalcs(item);
                            }
                        }
                        if (!hasSelected) {
                            g_recentLaunchFocusMode = false;
                            recentLaunchActiveBatchId = "";
                            g_repSatInitialized = false;
                        }
                    }
                } else if (M5Cardputer.Keyboard.isKeyPressed('h') || M5Cardputer.Keyboard.isKeyPressed('H')) {
                    showListHelp = true;
                } else if (M5Cardputer.Keyboard.isKeyPressed('w') || M5Cardputer.Keyboard.isKeyPressed('W')) {
                    if (currentSatTab == TAB_RECENT_LAUNCH) {
                        if (g_networkActive) {
                            recentLaunchErrorMsg = "System Busy... Wait.";
                            drawSatSelectPage();
                            earth_renderer->getCanvas()->pushSprite(0, 0);
                        } else if (!recentLaunchDownloading) {
                            recentLaunchDownloading = true;
                            recentLaunchErrorMsg = "Connecting WiFi...";
                            drawSatSelectPage();
                            earth_renderer->getCanvas()->pushSprite(0, 0);
                            BaseType_t res = xTaskCreatePinnedToCore(recentLaunchNetworkTask, "RecentLaunchNetworkTask", 12288, NULL, 1, NULL, 0);
                            if (res != pdPASS) {
                                recentLaunchDownloading = false;
                                recentLaunchErrorMsg = "Task Init Failed!";
                                drawSatSelectPage();
                                earth_renderer->getCanvas()->pushSprite(0, 0);
                            }
                        }
                    } else {
                        if (g_networkActive) {
                            downloadErrorMsg = "System Busy... Wait.";
                            drawSatSelectPage();
                            earth_renderer->getCanvas()->pushSprite(0, 0);
                        } else if (!HalWifi::isConnected()) {
                            manualWifiToggle = true;
                            downloadErrorMsg = "Connecting to WiFi...";
                            drawSatSelectPage();
                            earth_renderer->getCanvas()->pushSprite(0, 0);
                            BaseType_t res = xTaskCreatePinnedToCore(networkTask, "NetworkTask", 12288, NULL, 1, NULL, 0);
                            if (res != pdPASS) {
                                downloadErrorMsg = "Task Init Failed!";
                                drawSatSelectPage();
                                earth_renderer->getCanvas()->pushSprite(0, 0);
                            }
                        } else {
                            WiFi.disconnect(true);
                            WiFi.mode(WIFI_OFF);
                            downloadErrorMsg = "WiFi Disconnected.";
                        }
                    }
                } else if (currentSatTab == TAB_RECENT_LAUNCH) {
                    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE) || M5Cardputer.Keyboard.isKeyPressed(27) || M5Cardputer.Keyboard.isKeyPressed('`')) {
                        if (recentLaunchInObjectsView) {
                            recentLaunchInObjectsView = false;
                            g_level3Objects.clear();
                            g_level3Objects.shrink_to_fit();
                        } else {
                            appState = STATE_MAIN;
                        }
                    } else if (M5Cardputer.Keyboard.isKeyPressed('o') || M5Cardputer.Keyboard.isKeyPressed('O')) {
                        if (recentLaunchInObjectsView) {
                            recentLaunchInObjectsView = false;
                            g_level3Objects.clear();
                            g_level3Objects.shrink_to_fit();
                        } else if (recentLaunchSelectedIndex >= 0 && recentLaunchSelectedIndex < (int)g_recentLaunches.size()) {
                            recentLaunchInObjectsView = true;
                            recentLaunchObjectPage = 0;
                            loadLevel3ObjectsPage(g_recentLaunches[recentLaunchSelectedIndex], 0);
                        }
                    } else if (M5Cardputer.Keyboard.isKeyPressed('[')) {
                        if (recentLaunchInObjectsView && recentLaunchSelectedIndex >= 0) {
                            if (recentLaunchObjectPage > 0) {
                                recentLaunchObjectPage--;
                                loadLevel3ObjectsPage(g_recentLaunches[recentLaunchSelectedIndex], recentLaunchObjectPage);
                            }
                        }
                    } else if (M5Cardputer.Keyboard.isKeyPressed(']')) {
                        if (recentLaunchInObjectsView && recentLaunchSelectedIndex >= 0) {
                            int maxPage = (g_recentLaunches[recentLaunchSelectedIndex].satelliteCount - 1) / 5;
                            if (recentLaunchObjectPage < maxPage) {
                                recentLaunchObjectPage++;
                                loadLevel3ObjectsPage(g_recentLaunches[recentLaunchSelectedIndex], recentLaunchObjectPage);
                            }
                        }
                    } else if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
                        if (recentLaunchSelectedIndex >= 0 && recentLaunchSelectedIndex < (int)g_recentLaunches.size()) {
                            RecentLaunchItem& targetItem = g_recentLaunches[recentLaunchSelectedIndex];
                            targetItem.selected = !targetItem.selected;
                            if (targetItem.selected) {
                                g_recentLaunchFocusMode = true;
                                recentLaunchActiveBatchId = targetItem.batchId;
                                initRecentLaunchCalcs(targetItem);
                            } else {
                                if (recentLaunchActiveBatchId == targetItem.batchId) {
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
                                } else {
                                    bool hasAny = false;
                                    for (const auto& item : g_recentLaunches) {
                                        if (item.selected) { hasAny = true; break; }
                                    }
                                    if (!hasAny) {
                                        g_recentLaunchFocusMode = false;
                                        recentLaunchActiveBatchId = "";
                                        g_repSatInitialized = false;
                                    }
                                }
                            }

                            portENTER_CRITICAL(&passMutex);
                            predictionsReady = false;
                            lastPredictionBaseTime = 0;
                            portEXIT_CRITICAL(&passMutex);
                            triggerPrediction = true;
                        }
                    } else if (M5Cardputer.Keyboard.isKeyPressed(';')) { // UP
                        if (recentLaunchSelectedIndex > 0) recentLaunchSelectedIndex--;
                        else if (!g_recentLaunches.empty()) recentLaunchSelectedIndex = g_recentLaunches.size() - 1;
                        if (recentLaunchInObjectsView) {
                            recentLaunchObjectPage = 0;
                            loadLevel3ObjectsPage(g_recentLaunches[recentLaunchSelectedIndex], 0);
                        }
                    } else if (M5Cardputer.Keyboard.isKeyPressed('.')) { // DOWN
                        if (!g_recentLaunches.empty()) {
                            recentLaunchSelectedIndex = (recentLaunchSelectedIndex + 1) % g_recentLaunches.size();
                        }
                        if (recentLaunchInObjectsView) {
                            recentLaunchObjectPage = 0;
                            loadLevel3ObjectsPage(g_recentLaunches[recentLaunchSelectedIndex], 0);
                        }
                    }
                } else {
                    // TAB_ENCYCLOPEDIA
                    if (satSelectedIndex == NUM_SATELLITES) {
                        // Inputting NORAD ID
                        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE)) {
                            if (noradInput.length() > 0) noradInput.remove(noradInput.length() - 1);
                            downloadErrorMsg = "";
                        } else if (M5Cardputer.Keyboard.isKeyPressed(27) || M5Cardputer.Keyboard.isKeyPressed('`')) {
                            appState = STATE_MAIN;
                        } else if (M5Cardputer.Keyboard.isKeyPressed(';')) {
                            if (satSelectedIndex > 0) satSelectedIndex--;
                        } else if (M5Cardputer.Keyboard.isKeyPressed('.')) {
                            satSelectedIndex = 0;
                        } else if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
                            if (noradInput.length() == 5 && !isDownloadingCustom) {
                                isDownloadingCustom = true;
                                drawSatSelectPage();
                                earth_renderer->getCanvas()->pushSprite(0, 0);
                                
                                int id = noradInput.toInt();
                                TLEData loaded_tle;
                                if (TLEUpdater::getTLE(id, loaded_tle)) {
                                    SatProfile p;
                                    p.noradId = id;
                                    p.name = loaded_tle.name;
                                    p.color = TFT_WHITE;
                                    p.baseScore = 0;
                                    p.stdMag = 3.0;
                                    p.selected = true;
                                    p.iconType = ICON_SATELLITE;
                                    p.tle = loaded_tle;
                                    p.calc.init(p.tle);
                                    p.description = "Custom added satellite.\n\nPress 'd' to delete this satellite.";
                                    p.type = SAT_TYPE_VISUAL;
                                    if (NUM_SATELLITES < MAX_SATELLITES) {
                                        g_satellites[NUM_SATELLITES++] = p;
                                        saveCustomSatellites();
                                    }
                                    noradInput = "";
                                } else {
                                    if (WiFi.status() != WL_CONNECTED) {
                                        downloadErrorMsg = "Error: WiFi not connected! Press 'w' on main screen.";
                                    } else {
                                        downloadErrorMsg = "Error: Download failed or ID not found.";
                                    }
                                }
                                isDownloadingCustom = false;
                            }
                        } else {
                            for (auto c : M5Cardputer.Keyboard.keysState().word) {
                                if (c >= '0' && c <= '9' && noradInput.length() < 5) {
                                    noradInput += c;
                                    downloadErrorMsg = "";
                                }
                            }
                        }
                    } else {
                        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE) || M5Cardputer.Keyboard.isKeyPressed(27) || M5Cardputer.Keyboard.isKeyPressed('`')) {
                            appState = STATE_MAIN;
                            bool selectionChanged = false;
                            std::vector<int> currentSelected;
                            for (int i = 0; i < NUM_SATELLITES; i++) {
                                if (g_satellites[i].selected) {
                                    currentSelected.push_back(g_satellites[i].noradId);
                                }
                            }
                            if (currentSelected.size() != entrySelectedSatellites.size()) {
                                selectionChanged = true;
                            } else {
                                for (size_t i = 0; i < currentSelected.size(); i++) {
                                    if (currentSelected[i] != entrySelectedSatellites[i]) {
                                        selectionChanged = true;
                                        break;
                                    }
                                }
                            }
                            if (selectionChanged) {
                                portENTER_CRITICAL(&passMutex);
                                predictionsReady = false;
                                lastPredictionBaseTime = 0;
                                portEXIT_CRITICAL(&passMutex);
                                triggerPrediction = true;
                            }
                        } else if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
                            g_satellites[satSelectedIndex].selected = !g_satellites[satSelectedIndex].selected;
                        } else if (M5Cardputer.Keyboard.isKeyPressed('d') && satSelectedIndex >= NUM_BUILTIN_SATELLITES && satSelectedIndex < NUM_SATELLITES) {
                            deleteConfirmIndex = satSelectedIndex;
                        } else if (M5Cardputer.Keyboard.isKeyPressed(';')) {
                            if (satSelectedIndex > 0) satSelectedIndex--;
                            else satSelectedIndex = NUM_SATELLITES;
                        } else if (M5Cardputer.Keyboard.isKeyPressed('.')) {
                            satSelectedIndex = (satSelectedIndex + 1) % (NUM_SATELLITES + 1);
                        }
                    }
                }
            }
        }
        
        if (appState == STATE_WIFI_SETUP) {
            drawWiFiSetupPage();
            earth_renderer->getCanvas()->pushSprite(0, 0);
            
            if (wifiIsScanning) {
                wifiNetworks = HalWifi::scanNetworks();
                wifiIsScanning = false;
                wifiSelectedIndex = 0;
            }
            return;
        } else if (appState == STATE_SAT_SELECT) {
            drawSatSelectPage();
            earth_renderer->getCanvas()->pushSprite(0, 0);
            return;
        }

        // Advance time in real-time (1s per 1000ms)
        static unsigned long last_unix = millis();
        if (millis() - last_unix >= 1000) {
            current_unix += 1; 
            last_unix = millis();
        }
        
        // GNSS Power Management
        if (gnssStartTime == 0) gnssStartTime = millis();
        if (gnss && !gnss->isInStandbyMode()) {
            if (gnss->getStatus() == GNSS_STATUS_LOCKED) {
                GnssData gData = gnss->getData();
                if (gData.isValid) {
                    double oldLat = baseUserLat;
                    double oldLon = baseUserLon;
                    baseUserLat = gData.latitude;
                    baseUserLon = gData.longitude;
                    gnssLocationFixed = true; // Mark that we have a real location
                    if (abs(baseUserLat - oldLat) > 0.0001 || abs(baseUserLon - oldLon) > 0.0001) {
                        portENTER_CRITICAL(&passMutex);
                        lastPredictionBaseTime = 0; // 缓存失效
                        predictionsReady = false;
                        portEXIT_CRITICAL(&passMutex);
                    }
                }
                
                static bool gnssTimeSynced = false;
                if (gData.timeValid && gData.dateValid && !gnssTimeSynced) {
                    current_unix = convertGNSSDateToUnix(gData.year, gData.month, gData.day, gData.hour, gData.minute, gData.second);
                    gnssTimeSynced = true;
                    g_timeSynced = true;
                    LOG_I("APP", "Time synced to GNSS UTC: %u", current_unix);
                    
                    // Trigger predictor again with correct time
                    portENTER_CRITICAL(&passMutex);
                    predictionsReady = false;
                    lastPredictionBaseTime = 0; // 缓存失效
                    portEXIT_CRITICAL(&passMutex);
                    triggerPrediction = true;
                }
                
                gnssTimedOut = false;
                LOG_I("APP", "GNSS Locked. Location/Time synced. Entering standby mode to save power.");
                gnss->enterStandbyMode();
            } else {
                unsigned long timeoutDuration = gnssManualMode ? 600000 : 300000;
                if (millis() - gnssStartTime > timeoutDuration) {
                    LOG_I("APP", "GNSS Timeout. Entering standby mode to save power.");
                    gnssTimedOut = true;
                    gnss->enterStandbyMode();
                }
            }
        }
        
        // Target camera values for smooth transitions
        double targetViewLat = 0.0;
        double targetViewLon = 0.0;
        float targetPitch = 0.0f;
        float targetRoll = 0.0f;
        float targetYaw = 0.0f;
        int targetOffsetX = 0;
        int targetOffsetY = 0;
        targetFocusAlt = 0.0;
        
        static bool prevSatViewMode = false;
        if (isSatViewMode) {
            bool hasFocalPos = false;
            GeodeticCoord focalGeo;
            if (g_recentLaunchFocusMode && focusSatIndex == -1) {
                double tx, ty, tz;
                if (g_repSatCalc.getTEME(current_unix + timeMachineOffset, tx, ty, tz)) {
                    double gmst = CoordTransform::getGMST(CoordTransform::unixToJulian(current_unix + timeMachineOffset));
                    ECEFCoord ecef = CoordTransform::temeToECEF(tx, ty, tz, gmst);
                    focalGeo = CoordTransform::ecefToGeodetic(ecef);
                    hasFocalPos = true;
                }
            }
            else if (focusSatIndex >= 0 && focusSatIndex < NUM_SATELLITES && g_satellites[focusSatIndex].selected) {
                double tx, ty, tz;
                if (g_satellites[focusSatIndex].calc.getTEME(current_unix + timeMachineOffset, tx, ty, tz)) {
                    double gmst = CoordTransform::getGMST(CoordTransform::unixToJulian(current_unix + timeMachineOffset));
                    ECEFCoord ecef = CoordTransform::temeToECEF(tx, ty, tz, gmst);
                    focalGeo = CoordTransform::ecefToGeodetic(ecef);
                    hasFocalPos = true;
                }
            }

            
            if (hasFocalPos) {
                targetFocusAlt = focalGeo.alt;
                
                float visualAlt = targetFocusAlt;
                if (visualAlt > 20000.0f) visualAlt = 20000.0f;
                if (visualAlt < 0.0f) visualAlt = 0.0f;
                
                prevSatViewMode = true;


                
                if (!(attitude && imu)) {
                    targetViewLat = focalGeo.lat;
                    targetViewLon = focalGeo.lon;
                }
            } else {
                targetOffsetX = 0; targetOffsetY = 0;
                targetFocusAlt = 0;
            }
            
            targetOffsetX = 0; targetOffsetY = 0;
            if (attitude && imu && hasFocalPos) {
                if (!isImuLocked) {
                    AttitudeData att = attitude->getAttitude();
                    lockedPitch = att.pitch - basePitch;
                    lockedRoll = att.roll - baseRoll;
                }
                
                float visualAlt = targetFocusAlt;
                if (visualAlt > 20000.0f) visualAlt = 20000.0f;
                if (visualAlt < 0.0f) visualAlt = 0.0f;
                float adaptiveZoom = 62.0f / (55.0f + sqrtf(visualAlt) * 0.4f);
                
                float minZoom = adaptiveZoom;
                float t = currentZoom - minZoom;
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                float globeFactor = 1.0f - t;
                
                targetViewLat = focalGeo.lat - lockedPitch * globeFactor;
                targetViewLon = focalGeo.lon - lockedRoll * globeFactor;
                
                if (targetViewLat > 90.0) targetViewLat = 90.0;
                if (targetViewLat < -90.0) targetViewLat = -90.0;
                
                float zoomScale = t;
                targetPitch = -lockedPitch * zoomScale;
                targetRoll = -lockedRoll * zoomScale;
                targetYaw = 0;
                
                float maxAngle = 75.0f;
                
                if (targetPitch > maxAngle) targetPitch = maxAngle;
                if (targetPitch < -maxAngle) targetPitch = -maxAngle;
                if (targetRoll > maxAngle) targetRoll = maxAngle;
                if (targetRoll < -maxAngle) targetRoll = -maxAngle;
            }
        } else {
            prevSatViewMode = false;
            if (isManualLocationMode) {
                targetViewLat = baseUserLat;
                targetViewLon = baseUserLon;
                targetOffsetX = 0; targetOffsetY = 0;
                targetFocusAlt = 0;
            } else if (attitude && imu) {
                if (!isImuLocked) {
                    AttitudeData att = attitude->getAttitude();
                    lockedPitch = att.pitch;
                    lockedRoll = att.roll;
                }
                
                float t = currentZoom - 0.95f;
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                float globeFactor = 1.0f - t;
                
                targetViewLat = baseUserLat - lockedPitch * globeFactor;
                targetViewLon = baseUserLon - lockedRoll * globeFactor;
                
                if (targetViewLat > 90.0) targetViewLat = 90.0;
                if (targetViewLat < -90.0) targetViewLat = -90.0;
                
                float zoomScale = t;
                targetPitch = -lockedPitch * zoomScale;
                targetRoll = -lockedRoll * zoomScale;
                targetYaw = 0;
                targetOffsetX = 0;
                targetOffsetY = 0;
                
                float maxAngle = 75.0f;
                
                if (targetPitch > maxAngle) targetPitch = maxAngle;
                if (targetPitch < -maxAngle) targetPitch = -maxAngle;
                if (targetRoll > maxAngle) targetRoll = maxAngle;
                if (targetRoll < -maxAngle) targetRoll = -maxAngle;
            }
        }
        
        // Smoothly interpolate currentZoom to targetZoom
        currentZoom += (targetZoom - currentZoom) * 0.15f;
        earth_renderer->setZoom(currentZoom);
        
        static double smoothViewLat = baseUserLat;
        static double smoothViewLon = baseUserLon;
        static float smoothPitch = 0.0f;
        static float smoothRoll = 0.0f;
        static float smoothYaw = 0.0f;
        static float smoothOffsetX = 0.0f;
        static float smoothOffsetY = 0.0f;
        static double smoothFocusAlt = 0.0;
        
        // Handle longitude wrap-around for interpolation
        double lonDiff = targetViewLon - smoothViewLon;
        if (lonDiff > 180.0) targetViewLon -= 360.0;
        else if (lonDiff < -180.0) targetViewLon += 360.0;
        
        float dt = 0.15f;
        if (isFastForwarding) {
            dt = 1.0f;
        } else if (isSatViewMode) {
            if (isCameraTransitioning) {
                dt = 0.15f;
            } else {
                dt = 1.0f;
            }
        }
        
        smoothViewLat += (targetViewLat - smoothViewLat) * dt;
        smoothViewLon += (targetViewLon - smoothViewLon) * dt;
        if (smoothViewLon > 180.0) smoothViewLon -= 360.0;
        if (smoothViewLon < -180.0) smoothViewLon += 360.0;
        
        if (isCameraTransitioning && isSatViewMode) {
            double latErr = abs(targetViewLat - smoothViewLat);
            double lonErr = abs(targetViewLon - smoothViewLon);
            if (lonErr > 180.0) lonErr = 360.0 - lonErr;
            if (latErr < 0.5 && lonErr < 0.5) {
                isCameraTransitioning = false; // Transition completed
            }
        }

        
        smoothPitch += (targetPitch - smoothPitch) * dt;
        smoothRoll += (targetRoll - smoothRoll) * dt;
        smoothYaw += (targetYaw - smoothYaw) * dt;
        
        smoothOffsetX += (targetOffsetX - smoothOffsetX) * dt;
        smoothOffsetY += (targetOffsetY - smoothOffsetY) * dt;
        
        smoothFocusAlt += (targetFocusAlt - smoothFocusAlt) * dt;
        
        earth_renderer->setCameraFocusAlt(smoothFocusAlt);
        earth_renderer->setCenterOffset((int)smoothOffsetX, (int)smoothOffsetY);
        earth_renderer->setCameraAttitude(smoothPitch, smoothRoll, smoothYaw);
        
        double viewLat = smoothViewLat;
        double viewLon = smoothViewLon;

        // Update Sun Position
        if (sun_calc) {
            SunPositionData sunPos = sun_calc->calculatePosition(current_unix + timeMachineOffset, viewLat, viewLon);
            earth_renderer->setSunPosition(sunPos.subsolarLat, sunPos.subsolarLon);
        }

        static uint32_t lastLogTime = 0;
        static bool lastWasFastForwarding = false;
        
        uint32_t simTime = current_unix + timeMachineOffset;
        
        // 捕获刚刚停止快进的瞬间
        bool stopFastForwarding = (lastWasFastForwarding && !isFastForwarding);
        lastWasFastForwarding = isFastForwarding;

        // 日志触发条件：
        // 1. 正常运行（非快进）且模拟时间改变，且到了10秒整除时间
        // 2. 刚刚停止快进的瞬间，立即输出一次以显示最新模拟时间结果
        // 3. 第一次运行 (lastLogTime == 0)
        bool shouldLogNow = (simTime != lastLogTime && (
            (!isFastForwarding && (simTime % 10 == 0)) ||
            stopFastForwarding ||
            (lastLogTime == 0)
        ));

        if (shouldLogNow && appState == STATE_MAIN) {
            lastLogTime = simTime;
            int offset = 8; // Nanning uses China Standard Time (UTC+8), while simple geo math gave +7
            time_t local_unix = simTime + offset * 3600;
            struct tm *ti = gmtime(&local_unix);
            log_i("--- Satellite Positions at Local Time: %04d-%02d-%02d %02d:%02d:%02d ---", 
                  ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday, ti->tm_hour, ti->tm_min, ti->tm_sec);
        }

        static uint32_t lastSimTime = 0;
        bool timeChanged = (simTime != lastSimTime);
        if (timeChanged) {
            lastSimTime = simTime;
        }

        double current_gmst = CoordTransform::getGMST(CoordTransform::unixToJulian(simTime));
        SunPositionData view_sun_pos;
        SunPositionData observer_sun_pos;
        if (sun_calc) {
            view_sun_pos = sun_calc->calculatePosition(simTime, viewLat, viewLon);
            observer_sun_pos = sun_calc->calculatePosition(simTime, baseUserLat, baseUserLon);
        }
        static std::vector<SatRenderData> sats;
        sats.clear();
        int orbitsCalculatedThisFrame = 0;
        if (g_recentLaunchFocusMode) {
            for (auto& item : g_recentLaunches) {
                if (!item.selected) continue;
                
                if (item.batchId == recentLaunchActiveBatchId) {
                    if (g_repSatInitialized) {
                        bool runCalculation = (timeChanged || !g_repSatCache.lastGeoValid);
                        
                        if (runCalculation) {
                            double tx, ty, tz;
                            if (g_repSatCalc.getTEME(simTime, tx, ty, tz)) {
                                ECEFCoord ecef = CoordTransform::temeToECEF(tx, ty, tz, current_gmst);
                                GeodeticCoord geo = CoordTransform::ecefToGeodetic(ecef);
                                
                                bool inShadow = false;
                                if (sun_calc) {
                                    SunPositionData& sPos = view_sun_pos;
                                    float latR = geo.lat * DEG_TO_RAD;
                                    float lonR = geo.lon * DEG_TO_RAD;
                                    float subLatR = sPos.subsolarLat * DEG_TO_RAD;
                                    float subLonR = sPos.subsolarLon * DEG_TO_RAD;
                                    float cos_theta = sinf(subLatR)*sinf(latR) + cosf(subLatR)*cosf(latR)*cosf(lonR - subLonR);
                                    if (cos_theta < 0) {
                                        float r = 6371.0f + (float)geo.alt;
                                        float dist_sq = r * r * (1.0f - cos_theta * cos_theta);
                                        inShadow = (dist_sq < 6371.0f * 6371.0f);
                                    }
                                }
                                
                                g_repSatCache.lastGeo = geo;
                                g_repSatCache.lastInShadow = inShadow;
                                g_repSatCache.lastGeoValid = true;
                            } else {
                                g_repSatCache.lastGeoValid = false;
                            }
                        }
                        
                        if (g_repSatCache.lastGeoValid) {
                            bool isVisible = false;
                            if (sun_calc) {
                                GeodeticCoord observerPos = {baseUserLat, baseUserLon, baseUserAlt / 1000.0};
                                ECEFCoord satEcef = CoordTransform::geodeticToECEF(g_repSatCache.lastGeo);
                                TopocentricCoord topo = CoordTransform::ecefToTopocentric(observerPos, satEcef);
                                float el = topo.el;
                                
                                if (el > -5.0f && el < 15.0f) {
                                    float r = 1.02f / tanf((el + 10.3f / (el + 5.11f)) * DEG_TO_RAD);
                                    el += r / 60.0f;
                                }
                                
                                SunPositionData& sPos = observer_sun_pos;
                                float uLatR = baseUserLat * DEG_TO_RAD;
                                float uLonR = baseUserLon * DEG_TO_RAD;
                                float subLatR = sPos.subsolarLat * DEG_TO_RAD;
                                float subLonR = sPos.subsolarLon * DEG_TO_RAD;
                                float sun_cos_dist = sinf(uLatR)*sinf(subLatR) + cosf(uLatR)*cosf(subLatR)*cosf(uLonR - subLonR);
                                float sun_alt = asinf(sun_cos_dist) * RAD_TO_DEG;
                                bool isNight = sun_alt < -6.0f;
                                
                                if (isSatViewMode) {
                                    isVisible = !g_repSatCache.lastInShadow;
                                } else {
                                    isVisible = (isNight && (el >= 10.0f) && !g_repSatCache.lastInShadow);
                                }
                            }
                            g_repSatCache.isVisible = isVisible;
                            
                            SatRenderData data;
                            data.name = g_repSatName.c_str();
                            data.iconType = item.iconType;
                            data.currentPos = g_repSatCache.lastGeo;
                            data.color = TFT_CYAN;
                            data.isVisible = g_repSatCache.isVisible;
                            data.isRecentLaunchBatch = true;
                            data.totalSatellitesInBatch = item.satelliteCount;
                            data.launchEpoch = item.epoch;
                            data.simTime = simTime;
                            
                            calculateOrbit(g_repSatCalc, simTime, g_repSatCache.cache, orbitsCalculatedThisFrame, isFastForwarding);
                            
                            data.pastOrbit = &(g_repSatCache.cache.past);
                            data.futureOrbit = &(g_repSatCache.cache.future);
                            data.lastCalcTime = g_repSatCache.cache.lastCalcTime;
                            
                            // Set mission formation fields
                            data.proxyFormation = &(item.proxyFormation);
                            data.occupancy = item.occupancy;
                            data.occupancyStartPhase = item.occupancyStartPhase;
                            data.occupancyEndPhase = item.occupancyEndPhase;
                            data.repAlongTrackPhase = item.repAlongTrackPhase;
                            data.shortName = item.shortName.c_str();
                            
                            sats.push_back(data);
                        } else {
                            g_repSatCache.isVisible = false;
                        }
                    }
                } else {
                    if (item.calc) {
                        bool runCalculation = (timeChanged || !item.cache.lastGeoValid);
                        
                        if (runCalculation) {
                            double tx, ty, tz;
                            if (item.calc->getTEME(simTime, tx, ty, tz)) {
                                ECEFCoord ecef = CoordTransform::temeToECEF(tx, ty, tz, current_gmst);
                                GeodeticCoord geo = CoordTransform::ecefToGeodetic(ecef);
                                
                                bool inShadow = false;
                                if (sun_calc) {
                                    SunPositionData& sPos = view_sun_pos;
                                    float latR = geo.lat * DEG_TO_RAD;
                                    float lonR = geo.lon * DEG_TO_RAD;
                                    float subLatR = sPos.subsolarLat * DEG_TO_RAD;
                                    float subLonR = sPos.subsolarLon * DEG_TO_RAD;
                                    float cos_theta = sinf(subLatR)*sinf(latR) + cosf(subLatR)*cosf(latR)*cosf(lonR - subLonR);
                                    if (cos_theta < 0) {
                                        float r = 6371.0f + (float)geo.alt;
                                        float dist_sq = r * r * (1.0f - cos_theta * cos_theta);
                                        inShadow = (dist_sq < 6371.0f * 6371.0f);
                                    }
                                }
                                
                                item.cache.lastGeo = geo;
                                item.cache.lastInShadow = inShadow;
                                item.cache.lastGeoValid = true;
                            } else {
                                item.cache.lastGeoValid = false;
                            }
                        }
                        
                        if (item.cache.lastGeoValid) {
                            bool isVisible = false;
                            if (sun_calc) {
                                GeodeticCoord observerPos = {baseUserLat, baseUserLon, baseUserAlt / 1000.0};
                                ECEFCoord satEcef = CoordTransform::geodeticToECEF(item.cache.lastGeo);
                                TopocentricCoord topo = CoordTransform::ecefToTopocentric(observerPos, satEcef);
                                float el = topo.el;
                                
                                if (el > -5.0f && el < 15.0f) {
                                    float r = 1.02f / tanf((el + 10.3f / (el + 5.11f)) * DEG_TO_RAD);
                                    el += r / 60.0f;
                                }
                                
                                SunPositionData& sPos = observer_sun_pos;
                                float uLatR = baseUserLat * DEG_TO_RAD;
                                float uLonR = baseUserLon * DEG_TO_RAD;
                                float subLatR = sPos.subsolarLat * DEG_TO_RAD;
                                float subLonR = sPos.subsolarLon * DEG_TO_RAD;
                                float sun_cos_dist = sinf(uLatR)*sinf(subLatR) + cosf(uLatR)*cosf(subLatR)*cosf(uLonR - subLonR);
                                float sun_alt = asinf(sun_cos_dist) * RAD_TO_DEG;
                                bool isNight = sun_alt < -6.0f;
                                
                                if (isSatViewMode) {
                                    isVisible = !item.cache.lastInShadow;
                                } else {
                                    isVisible = (isNight && (el >= 10.0f) && !item.cache.lastInShadow);
                                }
                            }
                            item.cache.isVisible = isVisible;
                            
                            SatRenderData data;
                            data.name = item.repSatName.c_str();
                            data.iconType = ICON_SATELLITE;
                            data.currentPos = item.cache.lastGeo;
                            data.color = TFT_CYAN;
                            data.isVisible = item.cache.isVisible;
                            data.isRecentLaunchBatch = true;
                            data.totalSatellitesInBatch = item.satelliteCount;
                            data.launchEpoch = item.epoch;
                            data.simTime = simTime;
                            
                            calculateOrbit(*(item.calc), simTime, item.cache.cache, orbitsCalculatedThisFrame, isFastForwarding);
                            
                            data.pastOrbit = &(item.cache.cache.past);
                            data.futureOrbit = &(item.cache.cache.future);
                            data.lastCalcTime = item.cache.cache.lastCalcTime;
                            
                            // Set mission formation fields for non-focus representitive sat render
                            data.proxyFormation = &(item.proxyFormation);
                            data.occupancy = item.occupancy;
                            data.occupancyStartPhase = item.occupancyStartPhase;
                            data.occupancyEndPhase = item.occupancyEndPhase;
                            data.repAlongTrackPhase = item.repAlongTrackPhase;
                            data.shortName = item.shortName.c_str();
                            
                            sats.push_back(data);
                        } else {
                            item.cache.isVisible = false;
                        }
                    }
                }
            }
            
            // Render Level 3 micro satellites if active
            if (recentLaunchInObjectsView) {
                for (size_t i = 0; i < g_level3Objects.size(); i++) {
                    auto& obj = g_level3Objects[i];
                    bool runCalculation = (timeChanged || !obj.lastGeoValid);
                    
                    if (runCalculation) {
                        double tx, ty, tz;
                        if (obj.calc.getTEME(simTime, tx, ty, tz)) {
                            ECEFCoord ecef = CoordTransform::temeToECEF(tx, ty, tz, current_gmst);
                            GeodeticCoord geo = CoordTransform::ecefToGeodetic(ecef);
                            obj.lastGeo = geo;
                            obj.lastGeoValid = true;
                        } else {
                            obj.lastGeoValid = false;
                        }
                    }
                    
                    if (obj.lastGeoValid) {
                        SatRenderData data;
                        data.name = obj.name.c_str();
                        data.iconType = ICON_SATELLITE;
                        data.currentPos = obj.lastGeo;
                        data.color = TFT_GREEN;
                        data.isVisible = true;
                        
                        calculateOrbit(obj.calc, simTime, obj.cache, orbitsCalculatedThisFrame, isFastForwarding);
                        
                        data.pastOrbit = &(obj.cache.past);
                        data.futureOrbit = &(obj.cache.future);
                        
                        // Uniform display names for all launch target objects with its launch epoch dates
                        static String sNameCache[5];
                        if (recentLaunchSelectedIndex < (int)g_recentLaunches.size()) {
                            sNameCache[i] = getShortNameForDisplay(obj.name, g_recentLaunches[recentLaunchSelectedIndex].epoch);
                        } else {
                            sNameCache[i] = obj.name;
                        }
                        data.shortName = sNameCache[i].c_str();
                        
                        sats.push_back(data);
                    }
                }
            }
        }
        
        // Always compute and load selected encyclopedia satellites
        for (int i = 0; i < NUM_SATELLITES; i++) {
                if (!g_satellites[i].selected) {
                    g_satCaches[i].lastGeoValid = false;
                    g_satCaches[i].isVisible = false;
                    continue;
                }
                
                bool runCalculation = (timeChanged || !g_satCaches[i].lastGeoValid);
                
                if (runCalculation) {
                    double tx, ty, tz;
                    if (g_satellites[i].calc.getTEME(simTime, tx, ty, tz)) {
                        ECEFCoord ecef = CoordTransform::temeToECEF(tx, ty, tz, current_gmst);
                        GeodeticCoord geo = CoordTransform::ecefToGeodetic(ecef);
                        
                        bool inShadow = false;
                        if (sun_calc) {
                            SunPositionData& sPos = view_sun_pos;
                            float latR = geo.lat * DEG_TO_RAD;
                            float lonR = geo.lon * DEG_TO_RAD;
                            float subLatR = sPos.subsolarLat * DEG_TO_RAD;
                            float subLonR = sPos.subsolarLon * DEG_TO_RAD;
                            float cos_theta = sinf(subLatR)*sinf(latR) + cosf(subLatR)*cosf(latR)*cosf(lonR - subLonR);
                            if (cos_theta < 0) {
                                float r = 6371.0f + (float)geo.alt;
                                float dist_sq = r * r * (1.0f - cos_theta * cos_theta);
                                inShadow = (dist_sq < 6371.0f * 6371.0f);
                            }
                        }
                        
                        g_satCaches[i].lastGeo = geo;
                        g_satCaches[i].lastInShadow = inShadow;
                        g_satCaches[i].lastGeoValid = true;
                    } else {
                        g_satCaches[i].lastGeoValid = false;
                    }
                }
                
                if (g_satCaches[i].lastGeoValid) {
                    bool isVisible = false;
                    if (sun_calc) {
                        GeodeticCoord observerPos = {baseUserLat, baseUserLon, baseUserAlt / 1000.0};
                        ECEFCoord satEcef = CoordTransform::geodeticToECEF(g_satCaches[i].lastGeo);
                        TopocentricCoord topo = CoordTransform::ecefToTopocentric(observerPos, satEcef);
                        float el = topo.el;
                        
                        if (el > -5.0f && el < 15.0f) {
                            float r = 1.02f / tanf((el + 10.3f / (el + 5.11f)) * DEG_TO_RAD);
                            el += r / 60.0f;
                        }
                        
                        SunPositionData& sPos = observer_sun_pos;
                        float uLatR = baseUserLat * DEG_TO_RAD;
                        float uLonR = baseUserLon * DEG_TO_RAD;
                        float subLatR = sPos.subsolarLat * DEG_TO_RAD;
                        float subLonR = sPos.subsolarLon * DEG_TO_RAD;
                        float sun_cos_dist = sinf(uLatR)*sinf(subLatR) + cosf(uLatR)*cosf(subLatR)*cosf(uLonR - subLonR);
                        float sun_alt = asinf(sun_cos_dist) * RAD_TO_DEG;
                        bool isNight = sun_alt < -6.0f;
                        
                        if (isSatViewMode) {
                            isVisible = !g_satCaches[i].lastInShadow;
                        } else {
                            isVisible = (isNight && (el >= 10.0f) && !g_satCaches[i].lastInShadow);
                        }
                    }
                    g_satCaches[i].isVisible = isVisible;
                    
                    if (shouldLogNow && appState == STATE_MAIN) {
                        log_i("[%s] Lat: %.2f, Lon: %.2f, Alt: %.1f km, Shadow: %s, Visible: %s", 
                              g_satellites[i].name.c_str(), 
                              g_satCaches[i].lastGeo.lat, 
                              g_satCaches[i].lastGeo.lon, 
                              g_satCaches[i].lastGeo.alt, 
                              g_satCaches[i].lastInShadow ? "YES" : "NO",
                              g_satCaches[i].isVisible ? "YES" : "NO");
                    }
                    
                    SatRenderData data;
                    data.name = g_satellites[i].name.c_str();
                    data.iconType = g_satellites[i].iconType;
                    data.currentPos = g_satCaches[i].lastGeo;
                    data.color = g_satellites[i].color;
                    data.isVisible = g_satCaches[i].isVisible;
                    
                    calculateOrbit(g_satellites[i].calc, simTime, g_satellites[i].cache, orbitsCalculatedThisFrame, isFastForwarding);
                    
                    data.pastOrbit = &(g_satellites[i].cache.past);
                    data.futureOrbit = &(g_satellites[i].cache.future);
                    
                    sats.push_back(data);
                } else {
                    g_satCaches[i].isVisible = false;
                }
            }
        
        // Render scene
        double renderUserLat = baseUserLat;
        if (isManualLocationMode && ((millis() / 500) % 2 == 0)) {
            renderUserLat = 999.0; // Blink marker by putting it off-planet
        }
        earth_renderer->setObserverConstrained(!isSatViewMode);
        earth_renderer->setFastForwarding(isFastForwarding);
        earth_renderer->setUnixTime(current_unix + timeMachineOffset);
        earth_renderer->render(viewLat, viewLon, renderUserLat, baseUserLon, sats);
        
        // Draw coordinate overlay
        if (!showRecommendations && !showHelp && appState == STATE_MAIN && showHud) {
            earth_renderer->getCanvas()->setTextSize(1);
            
            char latDir = baseUserLat >= 0 ? 'N' : 'S';
            char lonDir = baseUserLon >= 0 ? 'E' : 'W';
            double alt = baseUserAlt;
            if (gnss && gnss->getStatus() == GNSS_STATUS_LOCKED) {
                alt = gnss->getData().altitude;
                baseUserAlt = alt; // Keep in sync
            }
            
            char latStr[20], lonStr[20], altStr[16];
            if (isManualLocationMode) {
                // Manual mode: show in cyan with '*' marker
                snprintf(latStr, sizeof(latStr), "%c%.2f*", latDir, abs(baseUserLat));
                snprintf(lonStr, sizeof(lonStr), "%c%.2f*", lonDir, abs(baseUserLon));
                earth_renderer->getCanvas()->setTextColor(TFT_CYAN);
            } else if (!gnssLocationFixed) {
                // No GPS fix: show in orange with '?' to warn user predictions may be wrong
                snprintf(latStr, sizeof(latStr), "%c%.2f?", latDir, abs(baseUserLat));
                snprintf(lonStr, sizeof(lonStr), "%c%.2f?", lonDir, abs(baseUserLon));
                earth_renderer->getCanvas()->setTextColor(TFT_ORANGE);
            } else {
                // GPS fixed: show in green
                snprintf(latStr, sizeof(latStr), "%c%.2f", latDir, abs(baseUserLat));
                snprintf(lonStr, sizeof(lonStr), "%c%.2f", lonDir, abs(baseUserLon));
                earth_renderer->getCanvas()->setTextColor(TFT_GREEN);
            }
            snprintf(altStr, sizeof(altStr), "%.0fm", alt);
            
            earth_renderer->getCanvas()->drawString(latStr, 5, 5);
            earth_renderer->getCanvas()->drawString(lonStr, 5, 17);
            earth_renderer->getCanvas()->setTextColor(TFT_LIGHTGRAY);
            earth_renderer->getCanvas()->drawString(altStr, 5, 29);
        }
        
        if (showHelp && appState == STATE_MAIN) {
            auto canvas = earth_renderer->getCanvas();
            uint16_t w = 200, h = 108;
            int x = (canvas->width() - w) / 2;
            int y = (canvas->height() - h) / 2;
            
            canvas->fillRect(x, y, w, h, canvas->color565(20, 30, 40));
            canvas->drawRect(x, y, w, h, TFT_LIGHTGRAY);
            
            canvas->setTextColor(TFT_WHITE);
            canvas->setTextSize(1);
            canvas->drawString("--- Help & Shortcuts ---", x + 25, y + 5);
            
            auto drawHotKey = [&](const char* word, char keyChar, int dx, int dy) {
                int cx = dx;
                bool highlighted = false;
                for (int i = 0; word[i] != '\0'; i++) {
                    if (!highlighted && tolower(word[i]) == tolower(keyChar) && keyChar != '\0') {
                        canvas->setTextColor(TFT_YELLOW);
                        highlighted = true;
                    } else {
                        canvas->setTextColor(TFT_LIGHTGRAY);
                    }
                    char cstr[2] = {word[i], '\0'};
                    canvas->drawString(cstr, cx, dy);
                    cx += canvas->textWidth(cstr);
                }
            };

            int ty = y + 20;
            drawHotKey("Bright[ ]", '[', x + 5, ty);
            drawHotKey("GNSS", 'g', x + 105, ty); ty += 12;
            
            drawHotKey("Help", 'h', x + 5, ty);
            drawHotKey("HUD[Del]", 'd', x + 105, ty); ty += 12;
            
            drawHotKey("Lock[Spc]", 'l', x + 5, ty);
            drawHotKey("PassList[Ent]", 'e', x + 105, ty); ty += 12;
            
            drawHotKey("Satellites", 's', x + 5, ty);
            drawHotKey("Time( , / . )", ',', x + 105, ty); ty += 12;
            
            drawHotKey("View(Sat)", 'v', x + 5, ty);
            drawHotKey("WiFi", 'w', x + 105, ty); ty += 12;
            
            drawHotKey("Config(Loc&Alt[])", 'c', x + 5, ty);
            drawHotKey("RealTime(Reset)", 'r', x + 105, ty); ty += 12;
        }
        
        if (showRecommendations) {
            // Draw semi-transparent dark overlay on the left side (width: 140)
            earth_renderer->getCanvas()->fillRect(0, 0, 140, 135, earth_renderer->getCanvas()->color565(15, 20, 25));
            earth_renderer->getCanvas()->drawFastVLine(140, 0, 135, TFT_DARKGREY); // separator line
            
            earth_renderer->getCanvas()->setTextColor(TFT_WHITE);
            earth_renderer->getCanvas()->setTextSize(1);
            earth_renderer->getCanvas()->drawString(" RECOMMENDED PASSES", 2, 5);
            
            portENTER_CRITICAL(&passMutex);
            if (!predictionsReady) {
                earth_renderer->getCanvas()->setTextColor(TFT_YELLOW);
                char buf[32];
                sprintf(buf, "Calculating... %d%%", predictionProgress);
                earth_renderer->getCanvas()->drawString(buf, 5, 30);
            } else {
                if (recommendedPasses.empty()) {
                    earth_renderer->getCanvas()->setTextColor(TFT_LIGHTGRAY);
                    earth_renderer->getCanvas()->drawString("No passes in 7 days", 5, 30);
                } else if (selectedPassIndex != -1) {
                    // Draw Detail View
                    const auto& p = recommendedPasses[selectedPassIndex];
                    earth_renderer->getCanvas()->setTextColor(TFT_CYAN);
                    earth_renderer->getCanvas()->drawString("Name:", 5, 25);
                    earth_renderer->getCanvas()->setTextColor(TFT_WHITE);
                    earth_renderer->getCanvas()->drawString(p.satName.c_str(), 40, 25);
                    
                    // Score: (y=37)
                    earth_renderer->getCanvas()->setTextColor(TFT_CYAN);
                    earth_renderer->getCanvas()->drawString("Score:", 5, 37);
                    String stars = "";
                    for(int s=0;s<p.score;s++) stars += "*";
                    uint16_t starColor = (p.score==5) ? TFT_GOLD : (p.score>=3 ? TFT_GREEN : TFT_LIGHTGRAY);
                    earth_renderer->getCanvas()->setTextColor(starColor);
                    earth_renderer->getCanvas()->drawString(stars.c_str(), 45, 37);
                    
                    // Orbit: MM/DD (y=49)
                    earth_renderer->getCanvas()->setTextColor(TFT_CYAN);
                    earth_renderer->getCanvas()->drawString("Orbit:", 5, 49);
                    
                    int tzOffsetSec = pos_manager ? pos_manager->getTimezoneManager()->getTimezoneOffset(baseUserLat, baseUserLon) : 8*3600;
                    time_t aos_t = (time_t)p.aosTime + tzOffsetSec;
                    time_t los_t = (time_t)p.losTime + tzOffsetSec;
                    struct tm aos_tm;
                    struct tm los_tm;
                    gmtime_r(&aos_t, &aos_tm);
                    gmtime_r(&los_t, &los_tm);
                    
                    char dateStr[32];
                    sprintf(dateStr, "%02d/%02d", aos_tm.tm_mon + 1, aos_tm.tm_mday);
                    earth_renderer->getCanvas()->setTextColor(TFT_WHITE);
                    earth_renderer->getCanvas()->drawString(dateStr, 45, 49);
                    
                    // Time: HH:MM:SS - HH:MM:SS (y=61)
                    char timeStr[64];
                    sprintf(timeStr, "%02d:%02d:%02d-%02d:%02d:%02d", 
                            aos_tm.tm_hour, aos_tm.tm_min, aos_tm.tm_sec, 
                            los_tm.tm_hour, los_tm.tm_min, los_tm.tm_sec);
                    earth_renderer->getCanvas()->setTextColor(TFT_LIGHTGRAY);
                    earth_renderer->getCanvas()->drawString(timeStr, 5, 61);
                    
                    // Mag & Peak (y=73)
                    earth_renderer->getCanvas()->setTextColor(TFT_CYAN);
                    earth_renderer->getCanvas()->drawString("Mag:", 5, 73);
                    earth_renderer->getCanvas()->setTextColor(TFT_WHITE);
                    char magBuf[16];
                    if (p.maxBrightness < 98.0) {
                        sprintf(magBuf, "%.1f", p.maxBrightness);
                    } else {
                        sprintf(magBuf, "N/A");
                    }
                    earth_renderer->getCanvas()->drawString(magBuf, 35, 73);
                    
                    earth_renderer->getCanvas()->setTextColor(TFT_CYAN);
                    earth_renderer->getCanvas()->drawString("Peak:", 65, 73);
                    earth_renderer->getCanvas()->setTextColor(TFT_WHITE);
                    earth_renderer->getCanvas()->drawString((String((int)p.maxElevation) + "deg").c_str(), 100, 73);
                    
                    // Reason: (y=85)
                    earth_renderer->getCanvas()->setTextColor(TFT_CYAN);
                    earth_renderer->getCanvas()->drawString("Reason:", 5, 85);
                    String reason = "Dark sky";
                    if (p.maxElevation > 60) reason += "+Zenith";
                    if (p.visibleDuration > 300) reason += "+Long";
                    earth_renderer->getCanvas()->setTextColor(TFT_LIGHTGRAY);
                    earth_renderer->getCanvas()->drawString(reason.c_str(), 50, 85);
                    
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
                            
                            earth_renderer->getCanvas()->setTextColor(TFT_GREEN);
                            char azaltBuf[32];
                            sprintf(azaltBuf, "Az:%03d Alt:%02d", (int)az, (int)el);
                            earth_renderer->getCanvas()->drawString(azaltBuf, 5, 97);
                            
                            if (satType == SAT_TYPE_SPACE_STATION && noradId == 25544) {
                                double freq_aprs = 145.825;
                                double freq_sstv = 145.800;
                                double shift_aprs = (freq_aprs * -range_rate / 299792.458) * 1000.0;
                                double shift_sstv = (freq_sstv * -range_rate / 299792.458) * 1000.0;
                                
                                char rx1Buf[32];
                                char rx2Buf[32];
                                sprintf(rx1Buf, "Rx1:%07.3f", freq_aprs + shift_aprs/1000.0);
                                sprintf(rx2Buf, "Rx2:%07.3f", freq_sstv + shift_sstv/1000.0);
                                earth_renderer->getCanvas()->drawString(rx1Buf, 5, 109);
                                earth_renderer->getCanvas()->drawString(rx2Buf, 5, 121);
                            }
                            else if (satType == SAT_TYPE_WEATHER) {
                                if (downlinkFreq.length() > 0) {
                                    double freq_mhz = downlinkFreq.toDouble();
                                    double shift_khz = (freq_mhz * -range_rate / 299792.458) * 1000.0;
                                    char rxBuf[32];
                                    sprintf(rxBuf, "Rx:%s (%+.1f)", downlinkFreq.c_str(), shift_khz);
                                    earth_renderer->getCanvas()->drawString(rxBuf, 5, 109);
                                }
                            }
                            else if (satType == SAT_TYPE_HAM) {
                                if (downlinkFreq.length() > 0) {
                                    double freq_mhz = downlinkFreq.toDouble();
                                    double shift_khz = (freq_mhz * -range_rate / 299792.458) * 1000.0;
                                    char rxBuf[32];
                                    sprintf(rxBuf, "Rx:%s (%+.1f)", downlinkFreq.c_str(), shift_khz);
                                    earth_renderer->getCanvas()->drawString(rxBuf, 5, 109);
                                }
                                if (uplinkFreq.length() > 0) {
                                    earth_renderer->getCanvas()->setTextColor(TFT_ORANGE);
                                    String txStr = "Tx:" + uplinkFreq;
                                    if (tone.length() > 0) txStr += " T:" + tone;
                                    earth_renderer->getCanvas()->drawString(txStr.c_str(), 5, 121);
                                }
                            }
                        }
                    }
                    
                } else {
                    // Draw Tree View
                    const char* catNames[] = {"Tonight", "Next 7 Days", "Highly Recommended", "All Passes"};
                    int y = 20;
                    int itemsPerPage = 8;
                    int startIndex = (passScrollIndex / itemsPerPage) * itemsPerPage;
                    
                    for (int i = 0; i < itemsPerPage && (startIndex + i) < displayTree.size(); i++) {
                        int idx = startIndex + i;
                        const auto& item = displayTree[idx];
                        
                        if (idx == passScrollIndex) {
                            earth_renderer->getCanvas()->fillRect(2, y-1, 136, 11, earth_renderer->getCanvas()->color565(0, 120, 255));
                        }
                        
                        if (item.isCategory) {
                            earth_renderer->getCanvas()->setTextColor(idx == passScrollIndex ? TFT_WHITE : TFT_CYAN);
                            String prefix = catExpanded[item.categoryIndex] ? "[-] " : "[+] ";
                            earth_renderer->getCanvas()->drawString((prefix + catNames[item.categoryIndex]).c_str(), 5, y);
                        } else {
                            const auto& p = recommendedPasses[item.passIndex];
                            earth_renderer->getCanvas()->setTextColor(idx == passScrollIndex ? TFT_WHITE : TFT_LIGHTGRAY);
                            String name = String(p.satName.c_str());
                            if (name.length() > 8) name = name.substring(0, 7) + ".";
                            earth_renderer->getCanvas()->drawString(name.c_str(), 15, y);
                            
                            // Draw stars
                            String stars = "";
                            for(int s=0;s<p.score;s++) stars += "*";
                            uint16_t starColor = (p.score==5) ? TFT_GOLD : (p.score>=3 ? TFT_GREEN : TFT_LIGHTGRAY);
                            if (idx == passScrollIndex) starColor = TFT_WHITE;
                            earth_renderer->getCanvas()->setTextColor(starColor);
                            earth_renderer->getCanvas()->drawString(stars.c_str(), 70, y);
                            
                            // Draw day if not tonight
                            if (item.categoryIndex != 0) {
                                int tzOffsetSec = pos_manager ? pos_manager->getTimezoneManager()->getTimezoneOffset(baseUserLat, baseUserLon) : 8*3600;
                                time_t aos_t = (time_t)p.aosTime + tzOffsetSec;
                                struct tm aos_tm;
                                gmtime_r(&aos_t, &aos_tm);
                                char dayStr[16];
                                sprintf(dayStr, "%02d/%02d", aos_tm.tm_mon + 1, aos_tm.tm_mday);
                                earth_renderer->getCanvas()->setTextColor(TFT_DARKGREY);
                                earth_renderer->getCanvas()->drawString(dayStr, 105, y);
                            }
                        }
                        y += 11;
                    }
                    
                    if (displayTree.size() > itemsPerPage) {
                        earth_renderer->getCanvas()->setTextColor(TFT_DARKGREY);
                        earth_renderer->getCanvas()->drawString("[^/v]", 110, 5);
                    }
                }
            }
            portEXIT_CRITICAL(&passMutex);
            
            // Draw GNSS and WiFi Status at the bottom of the panel
            earth_renderer->getCanvas()->drawFastHLine(0, 108, 140, TFT_DARKGREY);
            
            // Draw WiFi Status
            if (HalWifi::isConnected()) {
                earth_renderer->getCanvas()->setTextColor(TFT_GREEN);
                earth_renderer->getCanvas()->drawString("WF:ON", 5, 115);
            } else {
                earth_renderer->getCanvas()->setTextColor(TFT_LIGHTGRAY);
                earth_renderer->getCanvas()->drawString("WF:OFF", 5, 115);
            }
            
            // Draw GNSS Status
            if (gnss) {
                if (gnss->getStatus() == GNSS_STATUS_LOCKED) {
                    earth_renderer->getCanvas()->setTextColor(TFT_GREEN);
                    earth_renderer->getCanvas()->drawString("GP:FIX", 52, 115);
                } else if (gnss->isInStandbyMode()) {
                    if (gnssTimedOut) {
                        earth_renderer->getCanvas()->setTextColor(TFT_RED);
                        earth_renderer->getCanvas()->drawString("GP:TMO", 52, 115);
                    } else {
                        earth_renderer->getCanvas()->setTextColor(TFT_LIGHTGRAY);
                        earth_renderer->getCanvas()->drawString("GP:OFF", 52, 115);
                    }
                } else {
                    earth_renderer->getCanvas()->setTextColor(TFT_YELLOW);
                    earth_renderer->getCanvas()->drawString("GP:SCH", 52, 115);
                }
            }
            
            // Draw M5Chain Mono Status
            if (isMonoInitialized) {
                earth_renderer->getCanvas()->setTextColor(TFT_GREEN);
                earth_renderer->getCanvas()->drawString("MN:OK", 100, 115);
            } else {
                earth_renderer->getCanvas()->setTextColor(TFT_DARKGREY);
                earth_renderer->getCanvas()->drawString("MN:ND", 100, 115); // Not Detected
            }
            
            // Draw TLE Version
            String tleEpoch = "TLE Epoch: ";
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
                snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, month + 1, doy);
                tleEpoch += buf;
            } else {
                tleEpoch += "Unknown";
            }
            earth_renderer->getCanvas()->setTextColor(TFT_LIGHTGRAY);
            earth_renderer->getCanvas()->drawString(tleEpoch.c_str(), 5, 125);
        }
        
        // Draw Time Machine at bottom right
        if (appState == STATE_MAIN && showHud && !showHelp && !showRecommendations) {
            char timeStr[32];
            int tzOffsetSec = pos_manager ? pos_manager->getTimezoneManager()->getTimezoneOffset(baseUserLat, baseUserLon) : ((int)round(baseUserLon / 15.0) * 3600);
            time_t local_t = current_unix + timeMachineOffset + tzOffsetSec;
            struct tm *ptm = gmtime(&local_t);
            snprintf(timeStr, sizeof(timeStr), "%02d-%02d %02d:%02d", ptm->tm_mon+1, ptm->tm_mday, ptm->tm_hour, ptm->tm_min);
            
            earth_renderer->getCanvas()->setTextSize(1);
            if (timeMachineOffset != 0) {
                earth_renderer->getCanvas()->setTextColor(TFT_YELLOW);
            } else {
                earth_renderer->getCanvas()->setTextColor(TFT_WHITE);
            }
            int textWidth = earth_renderer->getCanvas()->textWidth(timeStr);
            earth_renderer->getCanvas()->drawString(timeStr, 238 - textWidth, 125);
            
            if (isSatViewMode) {
                String satName = "";
                uint16_t satColor = TFT_WHITE;
                bool hasSatInfo = false;
                
                SGP4Calc* currentCalc = nullptr;
                SatelliteType currentType = SAT_TYPE_VISUAL;
                int currentNoradId = 0;
                String downlinkFreq = "";
                
                if (focusSatIndex >= 0 && focusSatIndex < NUM_SATELLITES) {
                    satName = g_satellites[focusSatIndex].name;
                    satColor = g_satellites[focusSatIndex].color;
                    currentCalc = &(g_satellites[focusSatIndex].calc);
                    currentType = g_satellites[focusSatIndex].type;
                    currentNoradId = g_satellites[focusSatIndex].noradId;
                    downlinkFreq = g_satellites[focusSatIndex].downlinkFreq;
                    hasSatInfo = true;
                } else if (g_recentLaunchFocusMode) {
                    satName = g_repSatName;
                    
                    double ageDays = 30.0;
                    if (!g_recentLaunches.empty()) {
                        for (const auto& item : g_recentLaunches) {
                            if (item.batchId == recentLaunchActiveBatchId) {
                                if (item.epoch > 0 && (current_unix + timeMachineOffset) >= item.epoch) {
                                    ageDays = (double)((current_unix + timeMachineOffset) - item.epoch) / 86400.0;
                                }
                                break;
                            }
                        }
                    }
                    
                    uint16_t baseCol = TFT_WHITE;
                    if (ageDays <= 2.0) baseCol = TFT_WHITE;
                    else if (ageDays <= 14.0) baseCol = 0x07FF; // TFT_CYAN
                    else if (ageDays >= 365.0) baseCol = earth_renderer->getCanvas()->color565(150, 150, 150);
                    
                    satColor = baseCol;
                    currentCalc = &g_repSatCalc;
                    hasSatInfo = true;
                }
                
                if (hasSatInfo && currentCalc != nullptr) {
                    earth_renderer->getCanvas()->setTextColor(satColor);
                    earth_renderer->getCanvas()->drawString("Sat View", 180, 5);
                    
                    double tx, ty, tz;
                    if (currentCalc->getTEME(current_unix + timeMachineOffset, tx, ty, tz)) {
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
                        if (currentCalc->getTEME(current_unix + timeMachineOffset - 1, tx_prev, ty_prev, tz_prev)) {
                            double gmst_prev = CoordTransform::getGMST(CoordTransform::unixToJulian(current_unix + timeMachineOffset - 1));
                            ECEFCoord ecef_prev = CoordTransform::temeToECEF(tx_prev, ty_prev, tz_prev, gmst_prev);
                            TopocentricCoord topo_prev = CoordTransform::ecefToTopocentric(obsGeo, ecef_prev);
                            dist_prev = topo_prev.range;
                        }
                        
                        double range_rate = dist - dist_prev;
                        
                        earth_renderer->getCanvas()->setTextColor(satColor);
                        
                        char azBuf[32];
                        char elBuf[32];
                        sprintf(azBuf, "Az : %03d", (int)az);
                        sprintf(elBuf, "Alt: %02d", (int)el);
                        
                        if (currentType == SAT_TYPE_SPACE_STATION && currentNoradId == 25544) {
                            double freq_aprs = 145.825;
                            double freq_sstv = 145.800;
                            double shift_aprs = (freq_aprs * -range_rate / 299792.458) * 1000.0;
                            double shift_sstv = (freq_sstv * -range_rate / 299792.458) * 1000.0;
                            
                            earth_renderer->getCanvas()->drawString(azBuf, 5, 95);
                            earth_renderer->getCanvas()->drawString(elBuf, 5, 105);
                            
                            char rx1Buf[32];
                            char rx2Buf[32];
                            sprintf(rx1Buf, "Rx1: %07.3f", freq_aprs + shift_aprs/1000.0);
                            sprintf(rx2Buf, "Rx2: %07.3f", freq_sstv + shift_sstv/1000.0);
                            earth_renderer->getCanvas()->drawString(rx1Buf, 5, 115);
                            earth_renderer->getCanvas()->drawString(rx2Buf, 5, 125);
                        } else {
                            earth_renderer->getCanvas()->drawString(azBuf, 5, 105);
                            earth_renderer->getCanvas()->drawString(elBuf, 5, 115);
                            
                            if (currentType == SAT_TYPE_HAM || currentType == SAT_TYPE_WEATHER) {
                                if (downlinkFreq.length() > 0) {
                                    double freq_mhz = downlinkFreq.toDouble();
                                    double shift_khz = (freq_mhz * -range_rate / 299792.458) * 1000.0;
                                    char freqBuf[32];
                                    sprintf(freqBuf, "Rx : %s (%+.1f)", downlinkFreq.c_str(), shift_khz);
                                    earth_renderer->getCanvas()->drawString(freqBuf, 5, 125);
                                }
                            }
                        }
                    }
                }
            }

        }
        
        earth_renderer->getCanvas()->pushSprite(0, 0);
    }

    // Update Chain Mono Display (dynamic interval: 100ms normally, skipped during fast forwarding to prevent lag)
    static unsigned long lastChainMonoTick = 0;
    if (isMonoInitialized && !isFastForwarding && millis() - lastChainMonoTick >= 100) {
        lastChainMonoTick = millis();
        
        bool anyVisibleNow = false;
        String visibleSatName = "";
        SatIconType visibleSatIconType = ICON_SATELLITE;
        
        if (isSatViewMode && focusSatIndex >= 0 && focusSatIndex < NUM_SATELLITES) {
            anyVisibleNow = true;
            visibleSatName = g_satellites[focusSatIndex].name;
            visibleSatIconType = g_satellites[focusSatIndex].iconType;
        } else {
            if (g_recentLaunchFocusMode) {
                if (g_repSatCache.lastGeoValid && g_repSatCache.isVisible) {
                    anyVisibleNow = true;
                    visibleSatName = g_repSatName;
                    visibleSatIconType = ICON_SATELLITE;
                }
            } else {
                for (int i = 0; i < NUM_SATELLITES; i++) {
                    if (g_satellites[i].selected && g_satCaches[i].lastGeoValid && g_satCaches[i].isVisible) {
                        anyVisibleNow = true;
                        visibleSatName = g_satellites[i].name;
                        visibleSatIconType = g_satellites[i].iconType;
                        break;
                    }
                }
            }
        }
        
        static MonoState state = MONO_STATE_NONE;
        static int lastDispMinutes = -1;
        static int lastDispSeconds = -1;
        
        bool isUpcomingPass = false;
        int timeDiff = -1;
        
        if (!anyVisibleNow) {
            // 寻找即将到来的最早可见过境倒计时（全局搜索最小的 aosTime）
            PassEvent nextPass;
            bool foundNextPass = false;
            uint32_t earliestAos = 0xFFFFFFFF;
            
            portENTER_CRITICAL(&passMutex);
            for (const auto& pass : recommendedPasses) {
                uint32_t passTime = pass.aosTime;
                uint32_t currentSimTime = current_unix + timeMachineOffset;
                if (passTime > currentSimTime && pass.isVisible) {
                    if (passTime < earliestAos) {
                        earliestAos = passTime;
                        nextPass = pass;
                        foundNextPass = true;
                    }
                }
            }
            portEXIT_CRITICAL(&passMutex);
            
            if (foundNextPass) {
                timeDiff = nextPass.aosTime - (current_unix + timeMachineOffset);
                // 只有在未来 10 分钟（600 秒）内发生的过境，才在 Chain 屏显示倒计时
                if (timeDiff >= 0 && timeDiff <= 600) {
                    isUpcomingPass = true;
                    visibleSatName = nextPass.satName;
                    // 遍历 g_satellites 寻找匹配的 iconType
                    for (int i = 0; i < NUM_SATELLITES; i++) {
                        if (g_satellites[i].name == nextPass.satName) {
                            visibleSatIconType = g_satellites[i].iconType;
                            break;
                        }
                    }
                }
            }
        }
        
        if (anyVisibleNow) {
            // --- 状态 1：当前有可见过境，以缓慢呼吸效果显示飞行器图标 ---
            if (state != MONO_STATE_PASSING) {
                state = MONO_STATE_PASSING;
                lastDispMinutes = -1;
                lastDispSeconds = -1;
                M5Chain.setMonoMode(mono_id, MONO_PIXEL_MODE, &operation_status);
                M5Chain.setMonoClear(mono_id, &operation_status);
            }
            
            // 选择对应的 8x8 像素图标
            const uint8_t* icon = mono_icon_satellite;
            if (visibleSatIconType == ICON_STATION) icon = mono_icon_station;
            else if (visibleSatIconType == ICON_TELESCOPE) icon = mono_icon_telescope;
            else if (visibleSatIconType == ICON_ROCKET) icon = mono_icon_rocket;
            else if (visibleSatIconType == ICON_DEEPSPACE) icon = mono_icon_deepspace;
            else if (visibleSatIconType == ICON_DFH1) icon = mono_icon_dfh1;
            else if (visibleSatIconType == ICON_BLUEWALKER3) icon = mono_icon_bw3;
            else if (visibleSatIconType == ICON_WEATHER) icon = mono_icon_weather;
            else if (visibleSatIconType == ICON_NAVIGATION) icon = mono_icon_navi;
            else if (visibleSatIconType == ICON_COMMUNICATION) icon = mono_icon_comm;
            
            // 缓慢呼吸效果 design：正在过境 2.5 秒一个周期
            float theta = millis() * 0.00251f;
            float breatheVal = 0.5f + 0.5f * sinf(theta - 1.57079f); // 从最暗起步
            
            // 亮度在 1 到 7 之间变化
            mono_brightness_level_t brightness = (mono_brightness_level_t)(MONO_BRIGHTNESS_LEVEL_1 + (uint8_t)(breatheVal * 6.0f));
            M5Chain.setMonoBrightness(mono_id, brightness, &operation_status);
            
            uint8_t temp[8];
            memcpy(temp, icon, 8);
            M5Chain.setMonoBufferRefresh(mono_id, temp, &operation_status);
            
        } else if (isUpcomingPass) {
            // --- 状态 2：10分钟倒计时阶段，显示静止数字，从60秒开始每秒刷新 ---
            if (state != MONO_STATE_COUNTDOWN) {
                state = MONO_STATE_COUNTDOWN;
                lastDispMinutes = -1;
                lastDispSeconds = -1;
                M5Chain.setMonoMode(mono_id, MONO_PIXEL_MODE, &operation_status);
                M5Chain.setMonoBrightness(mono_id, MONO_BRIGHTNESS_LEVEL_6, &operation_status);
                M5Chain.setMonoClear(mono_id, &operation_status);
            }
            
            if (timeDiff <= 60) {
                // 秒阶段：每秒刷新，从 60 秒到 0 秒
                if (timeDiff != lastDispSeconds) {
                    lastDispSeconds = timeDiff;
                    lastDispMinutes = -1;
                    
                    uint8_t D1 = timeDiff / 10;
                    uint8_t D2 = timeDiff % 10;
                    
                    uint8_t temp[8] = {0};
                    for (int r = 0; r < 5; r++) {
                        temp[r + 2] = (font_3x5[D1][r] << 5) | font_3x5[D2][r];
                    }
                    M5Chain.setMonoBufferRefresh(mono_id, temp, &operation_status);
                }
            } else {
                // 分钟阶段：10分到1分静止显示，不闪烁。采用两位数显示（如 05），使其完全对称
                int minutes = (timeDiff == 600) ? 10 : (timeDiff / 60);
                if (minutes != lastDispMinutes) {
                    lastDispMinutes = minutes;
                    lastDispSeconds = -1;
                    
                    uint8_t D1 = minutes / 10;
                    uint8_t D2 = minutes % 10;
                    
                    uint8_t temp[8] = {0};
                    for (int r = 0; r < 5; r++) {
                        temp[r + 2] = (font_3x5[D1][r] << 5) | font_3x5[D2][r];
                    }
                    M5Chain.setMonoBufferRefresh(mono_id, temp, &operation_status);
                }
            }
        } else {
            // --- 状态 3：无临近过境，显示 Cortana 动态圆圈 ---
            if (state != MONO_STATE_IDLE) {
                state = MONO_STATE_IDLE;
                lastDispMinutes = -1;
                lastDispSeconds = -1;
                M5Chain.setMonoMode(mono_id, MONO_PIXEL_MODE, &operation_status);
                M5Chain.setMonoBrightness(mono_id, MONO_BRIGHTNESS_LEVEL_6, &operation_status);
                M5Chain.setMonoClear(mono_id, &operation_status);
            }
            
            uint8_t temp[8];
            drawCortanaCircle(temp);
            M5Chain.setMonoBufferRefresh(mono_id, temp, &operation_status);
        }
    }
}
