#include <Arduino.h>
#include "core/log_manager.h"
#include <M5Cardputer.h>
#include "core/tle_data.h"
#include "core/sgp4_calc.h"
#include "core/coord_transform.h"
#include "core/earth_renderer.h"
#include "core/observation_predictor.h"
#include "core/tle_updater.h"
#include "core/orbit_data_provider.h"
#include "core/i18n.h"
#include "core/encyclopedia.h"

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

#include <memory>
#include "hal/hal_imu.h"
#include "hal/hal_gnss.h"
#include "hal/hal_wifi.h"
#include "core/attitude_estimator.h"
#include "core/position_manager.h"
#include "core/sun_calculator.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "M5Chain.h"
#include <esp_task_wdt.h>

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

void applyNightVisionFilter(LGFX_Sprite* canvas) {
    if (!canvas) return;
    int w = canvas->width();
    int h = canvas->height();
    uint16_t* buf = (uint16_t*)canvas->getBuffer();
    if (!buf) return;
    
    int totalPixels = w * h;
    for (int i = 0; i < totalPixels; i++) {
        uint16_t rawCol = buf[i];
        // Byte swap from Big Endian (display format) to Little Endian (CPU format)
        uint16_t pixel = (rawCol >> 8) | (rawCol << 8);
        
        // Extract RGB565 channels
        uint16_t r = (pixel >> 11) & 0x1F;
        uint16_t g = (pixel >> 5) & 0x3F;
        uint16_t b = pixel & 0x1F;
        
        // Calculate average brightness
        uint16_t gray = (r * 3 + (g >> 1) * 6 + b) / 10;
        if (gray > 0x1F) gray = 0x1F;
        
        // Form new pixel (Red channel only)
        uint16_t new_pixel = (gray << 11);
        
        // Byte swap back to Big Endian
        buf[i] = (new_pixel >> 8) | (new_pixel << 8);
    }
}

void pushCanvasWithFilter() {
    if (!earth_renderer) return;
    LGFX_Sprite* canvas = earth_renderer->getCanvas();
    if (!canvas) return;
    if (earth_renderer->getVisualMode() == 1) {
        applyNightVisionFilter(canvas);
    }
    canvas->pushSprite(0, 0);
}

enum AppState {
    STATE_MAIN,
    STATE_WIFI_SETUP,
    STATE_SAT_SELECT,
    STATE_LANG_SELECT
};
AppState appState = STATE_MAIN;
int langSelectedIndex = 0;
void saveCustomSatellites();

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

#include "core/recent_launch_item.h"
#include "core/orbit_data_provider.h"
#include "core/json_parser.h"



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

// FreeRTOS Mutex to protect g_satellites data structure from concurrent read/write race conditions
SemaphoreHandle_t g_satMutex = NULL;

void lockSatMutex() {
    if (g_satMutex) xSemaphoreTake(g_satMutex, portMAX_DELAY);
}

void unlockSatMutex() {
    if (g_satMutex) xSemaphoreGive(g_satMutex);
}

SemaphoreHandle_t g_passMutex = NULL;

void lockPassMutex() {
    if (g_passMutex) xSemaphoreTake(g_passMutex, portMAX_DELAY);
}

void unlockPassMutex() {
    if (g_passMutex) xSemaphoreGive(g_passMutex);
}

volatile bool g_networkActive = false;
struct NetworkActiveGuard {
    NetworkActiveGuard() { g_networkActive = true; }
    ~NetworkActiveGuard() { g_networkActive = false; }
};
extern TaskHandle_t predictorTaskHandle;

struct PredictorTaskSuspendGuard {
    NetworkActiveGuard netActiveGuard;
    PredictorTaskSuspendGuard() {
        LOG_I("APP", "Predictor task cooperative yield for network ops");
    }
    ~PredictorTaskSuspendGuard() {
        LOG_I("APP", "Predictor task cooperative resume after network ops");
    }
};

std::vector<RecentLaunchItem> g_pendingRecentLaunches;
volatile bool g_recentLaunchesPending = false;
volatile bool g_recentLaunchRefreshPending = false;

// 唯一代表卫星及其缓存（仅用于 Focus 追踪模式）
TLEData g_repSatTLE;
SGP4Calc g_repSatCalc;
RecentLaunchRealtimeCache g_repSatCache;
bool g_repSatInitialized = false;
String g_repSatName = "";
uint32_t recentLaunchDownloadFinishedMs = 0;
uint32_t downloadFinishedMs = 0;
AppState g_wifiSetupReturnState = STATE_SAT_SELECT;

void exitWiFiSetupScreen() {
    appState = g_wifiSetupReturnState;
    wifiIsScanning = false;
    wifiIsInputtingPassword = false;
    wifiNetworks.clear();
    wifiNetworks.shrink_to_fit();
    if (!HalWifi::isConnected()) {
        HalWifi::disconnect();
    }
}
bool showListHelp = false;
bool isCameraTransitioning = false;
float targetZoom = 0.95f;

// 百科详情页手动翻页状态
bool g_descManualScrolled = false;
int g_descManualYOffset = 0;
int g_descMaxScroll = 0;

// Level 3 Objects 分页数据结构与状态
bool recentLaunchInObjectsView = false;
int recentLaunchObjectPage = 0;


std::vector<LazyObjectItem> g_level3Objects;

void autoAssignIconAndColor(const String& name, SatIconType& icon, uint16_t& color) {
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
    else if (nameUpper.indexOf("HUBBLE") != -1 || nameUpper.indexOf("JWST") != -1 || nameUpper.indexOf("TELESCOPE") != -1) {
        icon = ICON_TELESCOPE;
        color = TFT_CYAN;
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

static double getGeoSlotLongitude(uint32_t noradId, const String& slotStr) {
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

static void calculateGeoSatPosition(double satLonDeg, double userLatDeg, double userLonDeg, double userAltMeters, GeodeticCoord& outGeo, ECEFCoord& outEcef, TopocentricCoord& outTopo, double& outSkewDeg) {
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
    if (recentLaunchDownloading) return; // Prevent file read collision during background download
    if (items.empty()) return;
    
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
    
    // Store original Mean Anomalies for each item index
    std::vector<std::vector<float>>* rawPhases = new std::vector<std::vector<float>>(items.size());
    if (!rawPhases) return;
    
    File f = LittleFS.open("/json_recent_raw.jsonl", "r");
    if (!f) {
        delete rawPhases;
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
                    (*rawPhases)[i].push_back(record.meanAnomaly);
                    break;
                }
            }
        }
        
        // Feed watchdog every 5 lines to prevent WDT timeout
        if (calcLineCount % 5 == 0) {
            esp_task_wdt_reset();
            vTaskDelay(2);
        }
    }
    f.close();
    
    for (size_t i = 0; i < items.size(); i++) {
        auto& item = items[i];
        auto& phases = (*rawPhases)[i];
        
        // Feed watchdog for each item during heavy formation computation
        esp_task_wdt_reset();
        if (i % 3 == 0) vTaskDelay(1);
        
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
    delete rawPhases;
}


void initRecentLaunchCalcs(RecentLaunchItem& item) {
    if (recentLaunchDownloading) return; // Prevent file read collision during background download
    if (!item.selected) {
        item.calc.reset();
        return;
    }
    File f = LittleFS.open("/json_recent_raw.jsonl", "r");
    if (f) {
        JSONParser parser;
        while (f.available()) {
            String singleLine = f.readStringUntil('\n');
            singleLine.trim();
            if (singleLine.length() == 0) continue;
            
            OrbitRecord record;
            if (parser.parse(singleLine, record)) {
                if (record.getBatchId() == item.batchId) {
                    item.calc = std::make_shared<SGP4Calc>();
                    item.calc->init(record);
                    item.cache.lastGeoValid = false;
                    item.cache.isVisible = false;
                    
                    if (item.batchId == recentLaunchActiveBatchId) {
                        g_repSatTLE.name = record.name;
                        g_repSatTLE.baseScore = 0;
                        SGP4Calc::buildPseudoTle(record, g_repSatTLE.line1, g_repSatTLE.line2);
                        g_repSatCalc = *(item.calc);
                        g_repSatName = record.name;
                        g_repSatInitialized = true;
                        g_repSatCache = item.cache;
                    }
                    break;
                }
            }
        }
        f.close();
    }
    LOG_I("RECENT_LAUNCH", "Initialized representative satellite for batch %s: %s", item.batchId.c_str(), item.repSatName.c_str());
}

void loadLevel3ObjectsPage(const RecentLaunchItem& item, int page) {
    OrbitDataProvider::loadLevel3ObjectsPage(item, page);
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
bool recentLaunchBypassed = false;
const int MAX_SATELLITES = 70;
SatRealtimeCache g_satCaches[MAX_SATELLITES];
int NUM_BUILTIN_SATELLITES = 0;
int NUM_SATELLITES = 0;

SatProfile g_satellites[MAX_SATELLITES];

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

// 校验并自动修复 Sat View 模式下的焦点卫星与最近发射选中状态
void validateSatViewFocusState() {
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
void calculateOrbit(SGP4Calc& calc, uint32_t baseTime, OrbitCache& cache, int& calcCount, bool isFastForwarding, bool forceUpdate = false) {
    static uint32_t lastGlobalCalcMs = 0;
    if (isFastForwarding && !forceUpdate) {
        // Fast forwarding: DO NOT recalculate heavy orbit paths to ensure smooth input.
        return;
    }
    
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
        
        double teme_x, teme_y, teme_z;
        
        // Past 45 minutes, every 3 minutes (lower resolution to save CPU)
        for (int i = 45; i >= 0; i -= 3) {
            uint32_t t = baseTime - i * 60;
            if (calc.getTEME(t, teme_x, teme_y, teme_z)) {
                double gmst = CoordTransform::getGMST(CoordTransform::unixToJulian(t));
                ECEFCoord ecef = CoordTransform::temeToECEF(teme_x, teme_y, teme_z, gmst);
                cache.past.push_back(CoordTransform::ecefToGeodetic(ecef));
            }
        }
        
        // Future 45 minutes, every 3 minutes
        for (int i = 0; i <= 45; i += 3) {
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
TaskHandle_t imuTaskHandle = NULL; // IMU task handle, used to pause IMU during Grove bus probing
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
                if (c == 0 && p.losTime >= current_unix && p.aosTime < current_unix + 24*3600) match = true;
                else if (c == 1 && p.losTime >= current_unix && p.aosTime < current_unix + 7*24*3600) match = true;
                else if (c == 2 && p.score >= 4 && p.losTime >= current_unix) match = true;
                else if (c == 3 && p.losTime >= current_unix) match = true;
                
                if (match) {
                    displayTree.push_back({false, c, i});
                }
            }
        }
    }
}

void updateChainMonoDisplay() {
    // Update Chain Mono Display (dynamic interval: 100ms normally)
    static unsigned long lastChainMonoTick = 0;
    if (isMonoInitialized && millis() - lastChainMonoTick >= 100) {
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
            
            lockPassMutex();
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
            unlockPassMutex();
            
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
            else if (visibleSatIconType == ICON_DEBRIS) icon = mono_icon_debris;
            
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

void rebuildTreeLocal(std::vector<TreeItem>& tree, const std::vector<PassEvent>& passes, uint32_t current_unix) {
    tree.clear();
    for (int c = 0; c < 4; c++) {
        tree.push_back({true, c, -1});
        if (catExpanded[c]) {
            for (int i = 0; i < passes.size(); i++) {
                const auto& p = passes[i];
                bool match = false;
                if (c == 0 && p.losTime >= current_unix && p.aosTime < current_unix + 24*3600) match = true;
                else if (c == 1 && p.losTime >= current_unix && p.aosTime < current_unix + 7*24*3600) match = true;
                else if (c == 2 && p.score >= 4 && p.losTime >= current_unix) match = true;
                else if (c == 3 && p.losTime >= current_unix) match = true;
                
                if (match) {
                    tree.push_back({false, c, i});
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


volatile bool triggerPrediction = true;
uint32_t lastTimeAdjustMillis = 0;
volatile uint32_t g_currentPredictingBaseTime = 0;

void predictorTask(void* parameter) {
    while (true) {
        static unsigned long lastLoopPrintMs = 0;
        // Heartbeat print disabled to prevent Serial multi-core deadlocks
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
        
        std::unique_ptr<ObservationPredictor> predictor(new ObservationPredictor(baseUserLat, baseUserLon, baseUserAlt / 1000.0, pos_manager));
        std::vector<PassEvent> allPasses;
        allPasses.reserve(150);
        
        // Use simulated time for predictions
        uint32_t startTime = current_unix + timeMachineOffset;
        g_currentPredictingBaseTime = startTime;
        
        int numSatsToPredict = 0;
        RecentLaunchItem* activeGroup = nullptr;
        if (g_recentLaunchFocusMode) {
            numSatsToPredict = 1;
        } else {
            for (int i = 0; i < NUM_SATELLITES; i++) {
                if (g_satellites[i].selected && g_satellites[i].type != SAT_TYPE_GEO_TV && g_satellites[i].type != SAT_TYPE_DEEP_SPACE) {
                    numSatsToPredict++;
                }
            }
        }
        
        if (!g_recentLaunchFocusMode && numSatsToPredict == 0) {
            std::vector<PassEvent> emptyPasses;
            std::vector<TreeItem> emptyTree;
            rebuildTreeLocal(emptyTree, emptyPasses, current_unix + timeMachineOffset);
            
            lockPassMutex();
            recommendedPasses.swap(emptyPasses);
            displayTree.swap(emptyTree);
            predictionsReady = true;
            lastPredictionBaseTime = startTime;
            g_currentPredictingBaseTime = 0;
            unlockPassMutex();
            
            g_orbitCalculating = false;
            triggerPrediction = false;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        predictionProgress = 0;
        int completedCount = 0;
        
        if (g_recentLaunchFocusMode) {
            if (g_repSatInitialized && g_repSatTLE.line1.length() >= 14 && g_repSatTLE.line2.length() >= 14) {
                auto passes = predictor->predictPasses(g_repSatTLE, 3.0, startTime, 7);
                
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
            // === PHASE 1: Fast 24-Hour (Tonight) Pass Calculation (< 300ms) ===
            std::vector<PassEvent> phase1Passes;
            phase1Passes.reserve(50);
            
            for (int i = 0; i < NUM_SATELLITES; i++) {
                vTaskDelay(1); // Yield CPU 0 to IDLE0 task to feed WDT
                if (triggerPrediction || cancelPrediction || g_networkActive) break;
                
                SatelliteType type = SAT_TYPE_VISUAL;
                bool isSelected = false;
                TLEData tle;
                float stdMag = 3.0;
                
                lockSatMutex();
                isSelected = g_satellites[i].selected;
                if (isSelected) {
                    type = g_satellites[i].type;
                    tle = g_satellites[i].tle;
                    stdMag = g_satellites[i].stdMag;
                }
                unlockSatMutex();
                
                if (!isSelected) continue;
                
                if (type == SAT_TYPE_GEO_TV || type == SAT_TYPE_DEEP_SPACE) {
                    completedCount++;
                    continue;
                }
                
                if (tle.line1.length() < 14 || tle.line2.length() < 14) {
                    completedCount++;
                    continue;
                }
                
                // Fast 1-day prediction for Phase 1
                auto passes1 = predictor->predictPasses(tle, stdMag, startTime, 1);
                for (auto& p : passes1) {
                    p.satSelected = true;
                    p.satIndex = i;
                }
                phase1Passes.insert(phase1Passes.end(), passes1.begin(), passes1.end());
                completedCount++;
                predictionProgress = (completedCount * 50) / (numSatsToPredict > 0 ? numSatsToPredict : 1);
            }
            
            if (triggerPrediction || cancelPrediction || g_networkActive) {
                if (cancelPrediction) {
                    cancelPrediction = false;
                    g_orbitCalculating = false;
                    g_currentPredictingBaseTime = 0;
                }
                continue;
            }
            
            // Publish Phase 1 (Tonight's passes) IMMEDIATELY to UI in ~300ms!
            std::vector<PassEvent> upcomingPhase1;
            for (const auto& pass : phase1Passes) {
                if (pass.losTime >= current_unix + timeMachineOffset) {
                    upcomingPhase1.push_back(pass);
                }
            }
            std::sort(upcomingPhase1.begin(), upcomingPhase1.end(), [](const PassEvent& a, const PassEvent& b) {
                if (a.score != b.score) return a.score > b.score;
                return a.aosTime < b.aosTime;
            });
            
            std::vector<TreeItem> tempDisplayTree1;
            rebuildTreeLocal(tempDisplayTree1, upcomingPhase1, current_unix + timeMachineOffset);
            
            lockPassMutex();
            recommendedPasses = upcomingPhase1;
            displayTree = tempDisplayTree1;
            predictionsReady = true;
            lastPredictionBaseTime = startTime;
            unlockPassMutex();
            
            g_orbitCalculating = false; // Turn off "Calculating..." status immediately!
            g_readyStartTime = millis();
            
            // === PHASE 2: Background 7-Day Full Pass Calculation ===
            completedCount = 0;
            for (int i = 0; i < NUM_SATELLITES; i++) {
                vTaskDelay(1); // Yield CPU 0
                if (triggerPrediction || cancelPrediction || g_networkActive) break;
                
                SatelliteType type = SAT_TYPE_VISUAL;
                bool isSelected = false;
                TLEData tle;
                float stdMag = 3.0;
                
                lockSatMutex();
                isSelected = g_satellites[i].selected;
                if (isSelected) {
                    type = g_satellites[i].type;
                    tle = g_satellites[i].tle;
                    stdMag = g_satellites[i].stdMag;
                }
                unlockSatMutex();
                
                if (!isSelected) continue;
                
                if (type == SAT_TYPE_GEO_TV || type == SAT_TYPE_DEEP_SPACE || tle.line1.length() < 14 || tle.line2.length() < 14) {
                    completedCount++;
                    predictionProgress = 50 + (completedCount * 50) / (numSatsToPredict > 0 ? numSatsToPredict : 1);
                    continue;
                }
                
                // Full 7-day prediction for Phase 2
                auto passes = predictor->predictPasses(tle, stdMag, startTime, 7);
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
                completedCount++;
                predictionProgress = 50 + (completedCount * 50) / (numSatsToPredict > 0 ? numSatsToPredict : 1);
            }
            predictionProgress = 100;
        }
        
        if (triggerPrediction || cancelPrediction) {
            if (cancelPrediction) {
                cancelPrediction = false;
                g_orbitCalculating = false; // 强行熄灭 Chain Mono 的计算动画
                g_currentPredictingBaseTime = 0;
            }
            continue;
        }
        
        // Filter out past passes relative to the simulated time
        std::vector<PassEvent> upcomingPasses;
        for (const auto& pass : allPasses) {
            if (pass.losTime >= current_unix + timeMachineOffset) {
                upcomingPasses.push_back(pass);
            }
        }
        // Serial.printf("[Debug] Predictor: upcomingPasses size: %d (allPasses size: %d), current_unix: %u, offset: %d\n", (int)upcomingPasses.size(), (int)allPasses.size(), current_unix, timeMachineOffset);
        
        // Sort by score descending, then by start time ascending
        std::sort(upcomingPasses.begin(), upcomingPasses.end(), [](const PassEvent& a, const PassEvent& b) {
            if (a.score != b.score) return a.score > b.score;
            return a.aosTime < b.aosTime;
        });
        
        // Auto-expand first category on finish
        catExpanded[0] = true;
        catExpanded[1] = false;
        catExpanded[2] = false;
        catExpanded[3] = false;

        // Compute local temporary variables outside the critical section to prevent malloc/OOM within spinlocks
        std::vector<PassEvent> tempRecommendedPasses = upcomingPasses;
        std::vector<TreeItem> tempDisplayTree;
        rebuildTreeLocal(tempDisplayTree, tempRecommendedPasses, current_unix + timeMachineOffset);
        
        lockPassMutex();
        recommendedPasses.swap(tempRecommendedPasses);
        displayTree.swap(tempDisplayTree);
        predictionsReady = true;
        lastPredictionBaseTime = startTime; // 写入本次成功的基准时间缓存
        g_currentPredictingBaseTime = 0;
        unlockPassMutex();
        
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

// 自动向 SatNOGS 开放数据库 (db.satnogs.org API) 联机查询任意 NORAD ID 的下行/上行无线电频率与调制模式
bool fetchSatNogsFrequency(int noradId, String& outDl, String& outUl, String& outMode) {
    PredictorTaskSuspendGuard predGuard;
    delay(50);
    WiFiClient client;
    client.setTimeout(4000);
    
    HTTPClient http;
    http.setTimeout(4000);
    http.setConnectTimeout(4000);
    
    String url = "http://db.satnogs.org/api/transmitters/?format=json&norad_cat_id=" + String(noradId);
    http.begin(client, url);
    int httpCode = http.GET();
    bool success = false;
    
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        payload.trim();
        if (payload.length() > 0 && payload.startsWith("[")) {
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload);
            if (!error && doc.is<JsonArray>()) {
                JsonArray arr = doc.as<JsonArray>();
                for (JsonObject item : arr) {
                    const char* status = item["status"];
                    if (status && (strcmp(status, "active") == 0 || strcmp(status, "alive") == 0)) {
                        double dlHz = item["down_low"].as<double>();
                        double ulHz = item["up_low"].as<double>();
                        const char* modeStr = item["mode"];
                        
                        if (dlHz > 1e6) {
                            char buf[16];
                            snprintf(buf, sizeof(buf), "%.3f", dlHz / 1e6);
                            outDl = String(buf);
                        }
                        if (ulHz > 1e6) {
                            char buf[16];
                            snprintf(buf, sizeof(buf), "%.3f", ulHz / 1e6);
                            outUl = String(buf);
                        }
                        if (modeStr && strlen(modeStr) > 0) {
                            outMode = modeStr;
                        }
                        if (outDl.length() > 0) {
                            success = true;
                            break;
                        }
                    }
                }
            }
        }
    }
    
    http.end();
    return success;
}

void fetchFrequencies() {
    PredictorTaskSuspendGuard predGuard;
    delay(50);
    std::unique_ptr<WiFiClient> client(new WiFiClient());
    if (!client) return;
    client->setTimeout(4000);
    
    std::unique_ptr<HTTPClient> http(new HTTPClient());
    if (!http) return;
    
    http->setTimeout(4000);
    http->setConnectTimeout(4000);
    http->begin(*client, "http://raw.staticdn.net/nongxl/SkyCompass_Satellite/main/data/frequencies.json");
    int httpCode = http->GET();
    if (httpCode != HTTP_CODE_OK) {
        http->end();
        http->begin(*client, "http://raw.githubusercontent.com/nongxl/SkyCompass_Satellite/main/data/frequencies.json");
        httpCode = http->GET();
    }
    if (httpCode == HTTP_CODE_OK) {
        String payload = http->getString();
        http->end();
        
        payload.trim();
        if (payload.length() > 0 && payload.length() < 10240 && payload.startsWith("{")) {
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload);
            if (!error) {
                for (int i = 0; i < NUM_SATELLITES; i++) {
                    String idStr = String(g_satellites[i].noradId);
                    if (doc.containsKey(idStr)) {
                        String dl = doc[idStr]["freq"].as<String>();
                        String rm = doc[idStr]["mode"].as<String>();
                        String ul = "";
                        String tn = "";
                        if (doc[idStr].containsKey("uplink")) {
                            ul = doc[idStr]["uplink"].as<String>();
                        }
                        if (doc[idStr].containsKey("tone")) {
                            tn = doc[idStr]["tone"].as<String>();
                        }
                        
                        lockSatMutex();
                        g_satellites[i].downlinkFreq = dl;
                        g_satellites[i].radioMode = rm;
                        g_satellites[i].uplinkFreq = ul;
                        g_satellites[i].tone = tn;
                        if (g_satellites[i].type == SAT_TYPE_VISUAL) {
                            g_satellites[i].type = SAT_TYPE_HAM;
                        }
                        unlockSatMutex();
                    }
                }
            }
        } else {
            LOG_I("APP", "Frequencies payload skipped (size: %d)", payload.length());
        }
    } else {
        http->end();
    }
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
std::vector<String> g_descWrappedLines;
int g_descLastSatIndex = -1;
int g_descLastLang = -1;
uint32_t g_lastSatSelectTime = 0;
int g_descLabelAreaHeight = 20;

void wrapTextIntoLines(LGFX_Sprite* canvas, const String& text, int w, std::vector<String>& outLines) {
    int start = 0;
    while (start < text.length()) {
        int len = 0;
        bool foundNewline = false;
        
        while (start + len < text.length()) {
            if (text[start + len] == '\n') {
                foundNewline = true;
                break;
            }
            
            int charLen = 1;
            unsigned char head = (unsigned char)text[start + len];
            if (head >= 0xF0) charLen = 4;
            else if (head >= 0xE0) charLen = 3;
            else if (head >= 0xC0) charLen = 2;
            
            if (start + len + charLen > text.length()) {
                charLen = text.length() - (start + len);
            }
            
            String sub = text.substring(start, start + len + charLen);
            int subW = canvas->textWidth(sub.c_str());
            if (subW > w) {
                break;
            }
            len += charLen;
        }
        
        if (len == 0 && !foundNewline) {
            int charLen = 1;
            unsigned char head = (unsigned char)text[start];
            if (head >= 0xF0) charLen = 4;
            else if (head >= 0xE0) charLen = 3;
            else if (head >= 0xC0) charLen = 2;
            if (start + charLen > text.length()) charLen = text.length() - start;
            len = charLen;
        }
        
        outLines.push_back(text.substring(start, start + len));
        
        if (foundNewline) {
            start += len + 1; // skip '\n'
        } else {
            start += len;
        }
    }
}

String truncateUtf8Chars(const String& str, size_t maxChars) {
    size_t charCount = 0;
    size_t byteIdx = 0;
    size_t len = str.length();
    
    while (byteIdx < len && charCount < maxChars) {
        unsigned char c = (unsigned char)str[byteIdx];
        size_t charBytes = 1;
        if ((c & 0x80) == 0) charBytes = 1;
        else if ((c & 0xE0) == 0xC0) charBytes = 2;
        else if ((c & 0xF0) == 0xE0) charBytes = 3;
        else if ((c & 0xF8) == 0xF0) charBytes = 4;
        
        if (byteIdx + charBytes > len) break;
        byteIdx += charBytes;
        charCount++;
    }
    
    if (byteIdx < len) {
        return str.substring(0, byteIdx) + "..";
    }
    return str;
}

int drawWrappedText(LGFX_Sprite* canvas, String text, int x, int y, int w, int lineH, bool draw = true) {
    int start = 0;
    int lines = 0;
    while (start < text.length()) {
        lines++;
        int len = 0;
        bool foundNewline = false;
        
        while (start + len < text.length()) {
            if (text[start + len] == '\n') {
                foundNewline = true;
                break;
            }
            
            int charLen = 1;
            unsigned char head = (unsigned char)text[start + len];
            if (head >= 0xF0) charLen = 4;
            else if (head >= 0xE0) charLen = 3;
            else if (head >= 0xC0) charLen = 2;
            
            if (start + len + charLen > text.length()) {
                charLen = text.length() - (start + len);
            }
            
            String sub = text.substring(start, start + len + charLen);
            int subW = canvas->textWidth(sub.c_str());
            if (subW > w) {
                break;
            }
            len += charLen;
        }
        
        if (len == 0 && !foundNewline) {
            int charLen = 1;
            unsigned char head = (unsigned char)text[start];
            if (head >= 0xF0) charLen = 4;
            else if (head >= 0xE0) charLen = 3;
            else if (head >= 0xC0) charLen = 2;
            if (start + charLen > text.length()) charLen = text.length() - start;
            len = charLen;
        }
        
        int end = start + len;
        if (end < text.length() && !foundNewline) {
            auto isAlphaNum = [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
            };
            if (isAlphaNum(text[end]) && isAlphaNum(text[end - 1])) {
                int lastSpace = text.substring(start, end).lastIndexOf(' ');
                if (lastSpace > start && lastSpace > start + len / 2) {
                    end = lastSpace;
                    len = end - start;
                }
            }
        }
        
        if (draw) {
            canvas->drawString(text.substring(start, end).c_str(), x, y);
        }
        
        start = end;
        if (foundNewline) {
            start++; // skip the '\n'
        } else {
            if (start < text.length() && text[start] == ' ') {
                start++;
            }
        }
        y += lineH;
    }
    return lines;
}

void drawScrollingText(LGFX_Sprite* canvas, const char* text, int x, int y, int maxWidth, uint16_t color) {
    canvas->setTextColor(color);
    int textWidth = canvas->textWidth(text);
    if (textWidth <= maxWidth) {
        canvas->drawString(text, x, y);
        return;
    }
    
    int gap = 30;
    int cycleWidth = textWidth + gap;
    int speed = 25; // pixels per second
    int offset = (int)(millis() * speed / 1000) % cycleWidth;
    
    int fh = canvas->fontHeight();
    
    canvas->setClipRect(x, y, maxWidth, fh);
    canvas->drawString(text, x - offset, y);
    canvas->drawString(text, x - offset + cycleWidth, y);
    canvas->clearClipRect();
}

struct WiFiDisconnectGuard {
    ~WiFiDisconnectGuard() {
        LOG_I("RECENT_LAUNCH", "Recent Launch task complete. Turning off WiFi to save power.");
        HalWifi::disconnect();
    }
};

void recentLaunchNetworkTaskImpl() {
    NetworkActiveGuard guard;
    PredictorTaskSuspendGuard predGuard;
    WiFiDisconnectGuard wifiGuard;
    recentLaunchDownloading = true;
    recentLaunchDownloadSuccess = false;
    recentLaunchErrorMsg = "";
    
    // 1. WiFi Connection
    if (!HalWifi::isConnected()) {
        String ssid = "";
        String pass = "";
        HalWifi::loadCredentials(ssid, pass);
        
        if (ssid.length() > 0) {
            recentLaunchErrorMsg = "Connecting WiFi...";
            HalWifi::begin(ssid.c_str(), pass.c_str());
        }
        
        // If auto-connect with saved credentials failed or no credentials saved -> pop up WiFi setup screen
        if (!HalWifi::isConnected()) {
            recentLaunchErrorMsg = "WiFi Connect Failed!";
            recentLaunchDownloading = false;
            g_wifiSetupReturnState = STATE_SAT_SELECT;
            appState = STATE_WIFI_SETUP;
            wifiIsScanning = true;
            wifiIsInputtingPassword = false;
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
    
    // 3. Check local update timestamp to enforce 2-hour rate limiting
    bool success = false;
    bool usingCache = false;
    std::vector<RecentLaunchItem> tempLaunches;
    
    recentLaunchBypassed = false;
    
    uint32_t lastUpdate = 0;
    if (LittleFS.exists("/recent_last_update.txt")) {
        File timeFile = LittleFS.open("/recent_last_update.txt", "r");
        if (timeFile) {
            String timeStr = timeFile.readString();
            timeFile.close();
            timeStr.trim();
            lastUpdate = (uint32_t)timeStr.toInt();
        }
    }
    
    // Only apply 2-hour rate limiting for background auto-sync. Manual user keypress (manualWifiToggle) always forces fresh download.
    if (!manualWifiToggle && current_unix > 0 && lastUpdate > 0 && (current_unix - lastUpdate) < 7200 && LittleFS.exists("/json_recent_raw.jsonl")) {
        LOG_I("RECENT_LAUNCH", "Background auto-sync: Last update was %u sec ago (< 2h). Bypassing download.", (unsigned int)(current_unix - lastUpdate));
        success = true;
        recentLaunchBypassed = true;
    }
    
    if (!success) {
        recentLaunchErrorMsg = "Downloading GP JSON...";
        delay(200); // Give ESP32 stack and heap a brief breathing room to reclaim socket memory
        std::vector<RecentLaunchItem> dummy;
        int httpCode = 0;
        success = OrbitDataProvider::downloadRecentLaunches(dummy, &httpCode);
        if (success) {
            File timeFile = LittleFS.open("/recent_last_update.txt", "w", true);
            if (timeFile) {
                timeFile.print(current_unix);
                timeFile.close();
            }
        } else {
            if (httpCode < 0) {
                if (httpCode == -11) {
                    recentLaunchErrorMsg = "Download Timeout";
                } else if (httpCode == -5) {
                    recentLaunchErrorMsg = "Incomplete Download";
                } else {
                    recentLaunchErrorMsg = "Connection Refused";
                }
            } else if (httpCode == 404) {
                recentLaunchErrorMsg = "ID Not Found";
            } else {
                recentLaunchErrorMsg = "HTTP Error " + String(httpCode);
            }
        }
    }
    
    if (success) {
        g_recentLaunchRefreshPending = true;
    } else {
        if (recentLaunchErrorMsg == "Downloading GP JSON...") {
            recentLaunchErrorMsg = "Download Failed!";
        }
        LOG_I("RECENT_LAUNCH", "Celestrak JSON fetch failed");
        recentLaunchDownloading = false;
        recentLaunchDownloadFinishedMs = millis();
    }
}

void recentLaunchNetworkTask(void* parameter) {
    recentLaunchNetworkTaskImpl();
    vTaskDelete(NULL);
}

void forceRefreshSingleSatTask(void* parameter) {
    int targetIdx = (int)(intptr_t)parameter;
    if (targetIdx < 0 || targetIdx >= NUM_SATELLITES) {
        vTaskDelete(NULL);
        return;
    }
    
    {
        NetworkActiveGuard guard;
        PredictorTaskSuspendGuard predGuard;
        WiFiDisconnectGuard wifiGuard;
        
        uint32_t targetId = g_satellites[targetIdx].noradId;
        LOG_I("APP", "Force refreshing TLE for sat index %d, norad %u", targetIdx, (unsigned int)targetId);
        
        bool wifiReady = true;
        // 1. WiFi Connection
        if (!HalWifi::isConnected()) {
            String ssid = "";
            String pass = "";
            HalWifi::loadCredentials(ssid, pass);
            if (ssid.length() == 0) {
                downloadErrorMsg = "No WiFi Configured!";
                wifiReady = false;
            } else {
                downloadErrorMsg = "Connecting WiFi...";
                HalWifi::begin(ssid.c_str(), pass.c_str());
                if (!HalWifi::isConnected()) {
                    downloadErrorMsg = "WiFi Connect Failed!";
                    wifiReady = false;
                }
            }
        }
        
        if (wifiReady) {
            downloadErrorMsg = "Refreshing GP JSON...";
            TLEData new_tle;
            String fetchError = "";
            bool success = TLEUpdater::getTLE(targetId, new_tle, 0, nullptr, &fetchError);
            if (success && fetchError.length() == 0) {
                lockSatMutex();
                g_satellites[targetIdx].tle = new_tle;
                g_satellites[targetIdx].calc.init(new_tle);
                if (targetIdx >= NUM_BUILTIN_SATELLITES) {
                    if (new_tle.name.length() > 0) {
                        g_satellites[targetIdx].name = new_tle.name;
                    }
                    autoAssignIconAndColor(g_satellites[targetIdx].name, g_satellites[targetIdx].iconType, g_satellites[targetIdx].color);
                }
                unlockSatMutex();
                
                downloadErrorMsg = "Refresh Success!";
                
                lockPassMutex();
                predictionsReady = false;
                lastPredictionBaseTime = 0;
                unlockPassMutex();
                triggerPrediction = true;
            } else {
                if (fetchError.length() > 0) {
                    downloadErrorMsg = "Refresh Failed: " + fetchError;
                } else {
                    downloadErrorMsg = "Refresh Failed!";
                }
            }
        }
    } // All guards (wifiGuard, predGuard, guard) are safely destructed here!
    downloadFinishedMs = millis();
    vTaskDelete(NULL);
}

void downloadCustomSatTask(void* parameter) {
    int id = (int)(intptr_t)parameter;
    {
        NetworkActiveGuard guard;
        PredictorTaskSuspendGuard predGuard;
        
        bool wifiWasConnected = HalWifi::isConnected();
        bool wifiReady = true;
        
        if (!wifiWasConnected) {
            String ssid = "";
            String pass = "";
            HalWifi::loadCredentials(ssid, pass);
            if (ssid.length() == 0) {
                downloadErrorMsg = "No WiFi Configured!";
                wifiReady = false;
            } else {
                downloadErrorMsg = "Connecting WiFi...";
                HalWifi::begin(ssid.c_str(), pass.c_str());
                if (!HalWifi::isConnected()) {
                    downloadErrorMsg = "WiFi Connect Failed!";
                    wifiReady = false;
                }
            }
        }
        
        if (wifiReady) {
            downloadErrorMsg = "Downloading GP JSON...";
            TLEData loaded_tle;
            String fetchError = "";
            bool success = TLEUpdater::getTLE(id, loaded_tle, 2 * 24 * 3600, nullptr, &fetchError);
            if (success && fetchError.length() == 0) {
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
                p.description = "Custom added satellite.\n\n";
                p.type = SAT_TYPE_VISUAL;
                if (p.noradId == 57172 || p.name.indexOf("UMKA") != -1 || p.name.indexOf("RS40S") != -1) {
                    p.downlinkFreq = "437.625";
                    p.radioMode = "SSTV/BPSK";
                    p.type = SAT_TYPE_HAM;
                }
                autoAssignIconAndColor(p.name, p.iconType, p.color);
                
                bool exists = false;
                lockSatMutex();
                if (NUM_SATELLITES < MAX_SATELLITES) {
                    for (int i = 0; i < NUM_SATELLITES; i++) {
                        if (g_satellites[i].noradId == id) {
                            exists = true;
                            g_satellites[i].tle = loaded_tle;
                            g_satellites[i].calc.init(g_satellites[i].tle);
                            if (loaded_tle.name.length() > 0) {
                                g_satellites[i].name = loaded_tle.name;
                            }
                            if (g_satellites[i].noradId == 57172 || g_satellites[i].name.indexOf("UMKA") != -1 || g_satellites[i].name.indexOf("RS40S") != -1) {
                                g_satellites[i].downlinkFreq = "437.625";
                                g_satellites[i].radioMode = "SSTV/BPSK";
                                g_satellites[i].type = SAT_TYPE_HAM;
                            }
                            autoAssignIconAndColor(g_satellites[i].name, g_satellites[i].iconType, g_satellites[i].color);
                            break;
                        }
                    }
                    if (!exists) {
                        g_satellites[NUM_SATELLITES++] = p;
                    }
                }
                unlockSatMutex();
                
                if (!exists) {
                    saveCustomSatellites();
                }
                
                // 1. 优先尝试拉取全系统静态频段库 frequencies.json
                fetchFrequencies();
                
                // 2. 若静态库未收录，自动向 SatNOGS 开放 API (db.satnogs.org) 实时查询该卫星无线电频段
                for (int i = 0; i < NUM_SATELLITES; i++) {
                    if (g_satellites[i].noradId == id && g_satellites[i].downlinkFreq.length() == 0) {
                        String dl = "", ul = "", mode = "";
                        if (fetchSatNogsFrequency(id, dl, ul, mode)) {
                            lockSatMutex();
                            g_satellites[i].downlinkFreq = dl;
                            g_satellites[i].uplinkFreq = ul;
                            g_satellites[i].radioMode = mode;
                            if (g_satellites[i].type == SAT_TYPE_VISUAL) {
                                g_satellites[i].type = SAT_TYPE_HAM;
                            }
                            unlockSatMutex();
                            LOG_I("APP", "Fetched live SatNOGS radio specs for NORAD %d: %s MHz %s", id, dl.c_str(), mode.c_str());
                        }
                        break;
                    }
                }
                
                downloadErrorMsg = "Download Success!";
                noradInput = "";
                
                lockPassMutex();
                predictionsReady = false;
                lastPredictionBaseTime = 0;
                unlockPassMutex();
                triggerPrediction = true;
            } else {
                if (fetchError.length() > 0) {
                    downloadErrorMsg = "Error: " + fetchError;
                } else {
                    downloadErrorMsg = "Error: Download failed.";
                }
            }
        }
        
        if (!wifiWasConnected && HalWifi::isConnected()) {
            LOG_I("APP", "Auto disconnecting WiFi after custom satellite download.");
            HalWifi::disconnect();
        }
        
        if (!wifiReady && !wifiWasConnected) {
            appState = STATE_WIFI_SETUP;
            wifiIsScanning = true;
            wifiIsInputtingPassword = false;
        }
    }
    
    isDownloadingCustom = false;
    vTaskDelete(NULL);
}

void networkTaskImpl(void* parameter) {
    NetworkActiveGuard guard;
    PredictorTaskSuspendGuard predGuard;
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
        LOG_I("APP", "No WiFi credentials available. Offline mode active.");
        if (manualWifiToggle) {
            appState = STATE_WIFI_SETUP;
            wifiIsScanning = true;
            wifiIsInputtingPassword = false;
        }
        g_wifiConnecting = false;
        g_dataUpdating = false;
        g_timeSynced = true;
        triggerPrediction = true;
        return;
    }

    // 1. Connect WiFi
    HalWifi::begin(ssid.c_str(), pass.c_str());
    
    if (!HalWifi::isConnected()) {
        LOG_I("APP", "WiFi connection failed. Opening setup screen.");
        if (appState == STATE_SAT_SELECT) {
            downloadErrorMsg = "WiFi Connection Failed!";
        }
        g_wifiSetupReturnState = appState;
        appState = STATE_WIFI_SETUP;
        wifiIsScanning = true;
        wifiIsInputtingPassword = false;
        g_wifiConnecting = false;
        g_dataUpdating = false;
        g_timeSynced = true;
        triggerPrediction = true;
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
        
        // Auto-trigger recent launches download sequentially if active tab is Recent Launch
        if (currentSatTab == TAB_RECENT_LAUNCH || recentLaunchDownloading) {
            recentLaunchNetworkTaskImpl();
        }
        
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
            downloadErrorMsg = "WiFi Connected! Syncing GP JSONs...";
        }

        // 4. Fetch TLEs — Two-phase approach:
        //    Phase A: Read all fresh caches (no network needed)
        //    Phase B: Batch-fetch stale/missing ones in a SINGLE HTTP request
        bool updated = false;
        bool anyFetchFailed = false;
        String firstFetchError = "";
        uint32_t maxAge = manualWifiToggle ? 0 : (2 * 24 * 3600);
        uint32_t now = HalWifi::getUnixTime();

        if (appState == STATE_SAT_SELECT) {
            downloadErrorMsg = "Checking GP cache...";
        }

        // Phase A: determine which satellites need a network fetch
        std::vector<int>      staleIdx;   // indices into g_satellites
        std::vector<uint32_t> staleIds;   // NORAD IDs to batch-fetch

        for (int i = 0; i < NUM_SATELLITES; i++) {
            uint32_t noradId = g_satellites[i].noradId;
            if (g_satellites[i].type == SAT_TYPE_GEO_TV || g_satellites[i].type == SAT_TYPE_DEEP_SPACE) {
                // GEO broadcast satellites and Deep Space / Interplanetary / Classified probes skip CelesTrak GP queries
                continue;
            }
            if (noradId == 50463) {
                // JWST uses hardcoded TLE — no network needed
                TLEData wTle = TLEManager::getJWST_TLE();
                lockSatMutex();
                g_satellites[i].tle  = wTle;
                g_satellites[i].calc.init(wTle);
                unlockSatMutex();
                updated = true;
                continue;
            }

            TLEData cached;
            uint32_t cacheTime = 0;
            bool hasCache = TLEUpdater::loadFromCachePublic(noradId, cached, cacheTime);

            if (hasCache && now > 0 && !manualWifiToggle) {
                uint32_t tleEpoch = TLEUpdater::parseTleEpochPublic(cached.line1);
                uint32_t tleAge = (tleEpoch > 0 && now >= tleEpoch) ? (now - tleEpoch) : 0;
                uint32_t cacheAge = (cacheTime > 0 && now >= cacheTime) ? (now - cacheTime) : tleAge;

                if (cacheAge < maxAge && tleAge < (2 * 24 * 3600)) {
                    // Cache and TLE epoch are both fresh — use cache directly
                    lockSatMutex();
                    g_satellites[i].tle  = cached;
                    g_satellites[i].calc.init(cached);
                    unlockSatMutex();
                    continue;
                }
            }

            staleIdx.push_back(i);
            staleIds.push_back(noradId);
        }

        // Phase B: fetch stale/missing satellites via plain HTTP with 300ms throttle interval
        if (!staleIdx.empty()) {
            int totalStale = (int)staleIdx.size();
            LOG_I("APP", "Fetching %d stale satellites via HTTP (300ms throttled)...", totalStale);
            
            for (int k = 0; k < totalStale; k++) {
                int i = staleIdx[k];
                uint32_t noradId = g_satellites[i].noradId;
                
                if (appState == STATE_SAT_SELECT) {
                    char progBuf[64];
                    sprintf(progBuf, "Syncing GP JSONs (%d/%d)...", k + 1, totalStale);
                    downloadErrorMsg = progBuf;
                }
                
                OrbitRecord rec;
                int httpCode = 0;
                if (OrbitDataProvider::loadByCatalogNumber(noradId, rec, true, nullptr, &httpCode)) {
                    TLEData newTle;
                    newTle.name = rec.name;
                    newTle.baseScore = g_satellites[i].baseScore;
                    SGP4Calc::buildPseudoTle(rec, newTle.line1, newTle.line2);
                    TLEUpdater::saveToCache(noradId, newTle, now);

                    lockSatMutex();
                    g_satellites[i].tle = newTle;
                    g_satellites[i].calc.init(newTle);
                    if (i >= NUM_BUILTIN_SATELLITES && newTle.name.length() > 0) {
                        g_satellites[i].name = newTle.name;
                        autoAssignIconAndColor(g_satellites[i].name, g_satellites[i].iconType, g_satellites[i].color);
                    }
                    unlockSatMutex();
                    updated = true;
                } else {
                    anyFetchFailed = true;
                    if (firstFetchError.length() == 0) {
                        firstFetchError = (httpCode < 0) ? "Connection Refused" : ("HTTP " + String(httpCode));
                    }
                    // Refresh timestamp of existing cache (if any) to prevent infinite retry on next boot.
                    // If no cache exists, save a 404 failure record so we don't query CelesTrak again for 7 days!
                    TLEData cached;
                    uint32_t cacheTime = 0;
                    if (TLEUpdater::loadFromCachePublic(noradId, cached, cacheTime)) {
                        TLEUpdater::saveToCache(noradId, cached, now);
                    } else if (httpCode == 404) {
                        TLEData failTle;
                        failTle.name = g_satellites[i].name;
                        failTle.line1 = "404 NOT FOUND";
                        failTle.line2 = "404 NOT FOUND";
                        TLEUpdater::saveToCache(noradId, failTle, now);
                        LOG_I("APP", "Saved 404 failure cache for NORAD %u to suppress redundant queries on future boots.", noradId);
                    }
                }
                vTaskDelay(pdMS_TO_TICKS(300)); // 300ms delay to prevent CelesTrak WAF/rate-limiting
            }
        }

        if (appState == STATE_SAT_SELECT) {
            downloadErrorMsg = "WiFi Connected! Syncing frequencies...";
        }

        // 3.5 Fetch Frequencies
        fetchFrequencies();
        
        if (updated) {
            LOG_I("APP", "TLE Data is ready and models updated!");
            
            // Rerun predictor with new data
            lockPassMutex();
            predictionsReady = false;
            lastPredictionBaseTime = 0; // 缓存失效
            unlockPassMutex();
            triggerPrediction = true;
            
            if (appState == STATE_SAT_SELECT) {
                if (anyFetchFailed) {
                    downloadErrorMsg = "Updated with errors: " + firstFetchError;
                } else {
                    downloadErrorMsg = "GP JSONs & Frequencies Updated!";
                }
            }
        } else {
            if (appState == STATE_SAT_SELECT) {
                if (anyFetchFailed) {
                    downloadErrorMsg = "Update Failed: " + firstFetchError;
                } else {
                    downloadErrorMsg = "Frequencies Updated! GP Data is fresh.";
                }
            }
        }
        if (!manualWifiToggle) {
            LOG_I("APP", "Network tasks complete. Turning off WiFi to save power.");
            HalWifi::disconnect();
        } else {
            LOG_I("APP", "Network tasks complete. WiFi remains connected.");
        }
    }
    
    vTaskDelay(pdMS_TO_TICKS(150)); // Allow LwIP sockets and TCP buffers to be fully reclaimed by ESP32 heap
    g_wifiConnecting = false;
    g_timeSynced = true; // Fallback to allow offline mock calculations if WiFi failed/finished
    triggerPrediction = true; // Wake up the prediction loop immediately
    downloadFinishedMs = millis();
}

void networkTask(void* parameter) {
    networkTaskImpl(parameter);
    vTaskDelete(NULL);
}

void tryLoadRecentLaunchCache() {
    if (!LittleFS.exists("/json_recent_raw.jsonl")) {
        LOG_I("RECENT_LAUNCH", "No local cache JSONL file found.");
        return;
    }
    
    std::vector<RecentLaunchItem> tempLaunches;
    if (OrbitDataProvider::loadRecentLaunchesFromCache(tempLaunches) && !tempLaunches.empty()) {
        std::sort(tempLaunches.begin(), tempLaunches.end(), [](const RecentLaunchItem& a, const RecentLaunchItem& b) {
            auto getTrueYearAndNum = [](const String& id) -> std::pair<int, int> {
                if (id.length() < 5) return {0, 0};
                int yr = id.substring(0, 2).toInt();
                int trueYr = (yr >= 50) ? (1900 + yr) : (2000 + yr);
                int num = id.substring(2).toInt();
                return {trueYr, num};
            };
            auto valA = getTrueYearAndNum(a.batchId);
            auto valB = getTrueYearAndNum(b.batchId);
            if (valA.first != valB.first) {
                return valA.first > valB.first;
            }
            return valA.second > valB.second;
        });
        
        lockSatMutex();
        g_recentLaunches = tempLaunches;
        calculateFormationsForItems(g_recentLaunches);
        unlockSatMutex();
        recentLaunchDownloadSuccess = true;
        recentLaunchSelectedIndex = 0;
        recentLaunchErrorMsg = "Loaded from local cache.";
        LOG_I("RECENT_LAUNCH", "Loaded %d launches from local cache.", (int)g_recentLaunches.size());
    } else {
        LOG_I("RECENT_LAUNCH", "Failed to parse local cache JSONL.");
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

volatile bool g_isFastForwarding = false;

void imuTask(void* pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    while (true) {
        // 在手动调时/快进期间降低 IMU 轮询频率至 60ms(16Hz)，释放 I2C 总线带宽供键盘使用
        TickType_t xFrequency = pdMS_TO_TICKS(g_isFastForwarding ? 60 : 10);
        if (imu && attitude) {
            imu->update();
            attitude->update();
        }
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

volatile bool g_loadingFinished = false;
volatile int g_loadingProgress = 18;
String g_loadingStatusText = "";

void drawStartupScreen(int progressPercentage, bool showLangSelect = false, int selectedLangIndex = 0) {
    if (!earth_renderer) return;
    
    float pitch = 0.0f;
    float roll = 0.0f;
    if (attitude) {
        AttitudeData att = attitude->getAttitude();
        pitch = att.pitch;
        roll = att.roll;
    }
    
    // Set camera attitude to 0 (looking straight down) to pivot around the center of the Earth sphere
    earth_renderer->setCameraAttitude(0.0f, 0.0f, 0.0f);
    earth_renderer->setObserverConstrained(false);
    earth_renderer->setDrawDecorations(false);
    
    // Center of projection shifts with IMU pitch/roll to rotate the globe around its center
    double viewLat = baseUserLat - pitch;
    double viewLon = baseUserLon - roll;
    if (viewLat > 90.0) viewLat = 90.0;
    if (viewLat < -90.0) viewLat = -90.0;
    if (viewLon > 180.0) viewLon -= 360.0;
    if (viewLon < -180.0) viewLon += 360.0;
    
    // Draw the background Earth globe (no satellites list)
    earth_renderer->setUnixTime(current_unix == 0 ? 1783300000 : current_unix);
    earth_renderer->render(viewLat, viewLon, baseUserLat, baseUserLon, {});
    
    LGFX_Sprite* canvas = earth_renderer->getCanvas();
    if (!canvas) return;
    
    // Draw overlay UI
    canvas->setTextColor(TFT_WHITE);
    canvas->setTextSize(2);
    canvas->drawString("SkyCompass", 120 - canvas->textWidth("SkyCompass") / 2, 20);
    
    canvas->setTextColor(TFT_YELLOW);
    canvas->setTextSize(1);
    String statusStr = (g_loadingStatusText.length() > 0) ? g_loadingStatusText : I18N::get(TXT_LOADING_MODELS);
    canvas->drawString(statusStr.c_str(), 120 - canvas->textWidth(statusStr.c_str()) / 2, 50);
    
    // Draw progress bar
    canvas->drawRect(35, 108, 170, 8, TFT_DARKGREY);
    canvas->fillRect(37, 110, (int)(166.0f * (progressPercentage / 100.0f)), 4, TFT_GREEN);
    
    // Draw language selection dialog if needed
    if (showLangSelect) {
        int dialogW = 140;
        int dialogH = 70;
        int dialogX = 120 - dialogW / 2;
        int dialogY = 67 - dialogH / 2 - 10;
        
        canvas->fillRect(dialogX, dialogY, dialogW, dialogH, canvas->color565(30, 40, 50));
        canvas->drawRect(dialogX, dialogY, dialogW, dialogH, TFT_YELLOW);
        
        canvas->setTextColor(TFT_WHITE);
        canvas->drawString("Select Language", dialogX + (dialogW - canvas->textWidth("Select Language")) / 2, dialogY + 8);
        
        if (selectedLangIndex == 0) {
            canvas->setTextColor(TFT_GREEN);
            canvas->drawString("> English", dialogX + 25, dialogY + 28);
        } else {
            canvas->setTextColor(TFT_LIGHTGRAY);
            canvas->drawString("  English", dialogX + 25, dialogY + 28);
        }
        
        if (selectedLangIndex == 1) {
            canvas->setTextColor(TFT_GREEN);
            canvas->drawString("> 简体中文", dialogX + 25, dialogY + 45);
        } else {
            canvas->setTextColor(TFT_LIGHTGRAY);
            canvas->drawString("  简体中文", dialogX + 25, dialogY + 45);
        }
    }
    
    // Push to screen
    if (earth_renderer && earth_renderer->getVisualMode() == 1) {
        applyNightVisionFilter(canvas);
    }
    canvas->pushSprite(0, 0);
}

void drawLangSelectDialog(LGFX_Sprite* canvas) {
    if (!canvas) return;
    
    int w = 140, h = 70;
    int x = (canvas->width() - w) / 2;
    int y = (canvas->height() - h) / 2;
    
    canvas->fillRect(x, y, w, h, canvas->color565(30, 40, 50));
    canvas->drawRect(x, y, w, h, TFT_YELLOW);
    
    canvas->setTextColor(TFT_WHITE);
    canvas->setTextSize(1);
    canvas->drawString(I18N::get(TXT_LANGUAGE_MENU), x + (w - canvas->textWidth(I18N::get(TXT_LANGUAGE_MENU))) / 2, y + 8);
    
    if (langSelectedIndex == 0) {
        canvas->setTextColor(TFT_GREEN);
        canvas->drawString("> English", x + 25, y + 28);
    } else {
        canvas->setTextColor(TFT_LIGHTGRAY);
        canvas->drawString("  English", x + 25, y + 28);
    }
    
    if (langSelectedIndex == 1) {
        canvas->setTextColor(TFT_GREEN);
        canvas->drawString("> 简体中文", x + 25, y + 45);
    } else {
        canvas->setTextColor(TFT_LIGHTGRAY);
        canvas->drawString("  简体中文", x + 25, y + 45);
    }
}

void setup() {
    if (!g_satMutex) {
        g_satMutex = xSemaphoreCreateMutex();
    }
    if (!g_passMutex) {
        g_passMutex = xSemaphoreCreateMutex();
    }
    
    Serial.begin(115200);
    // Remove the 4 second delay to boot instantly
    LOG_I("APP", "\n\n--- SkyCompass Satellite: Phase 4 ---");

    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    I18N::begin();
    M5Cardputer.Display.setBrightness(currentBrightness);
    
    earth_renderer = new EarthRenderer(&M5Cardputer.Display);
    earth_renderer->begin();

    // Draw initial loading screen instantly to avoid black screen during setup
    drawStartupScreen(18);

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
            &imuTaskHandle,  // Save handle so we can suspend during Grove bus probing
            0  // Pinned to Core 0
        );
    }
    
    // Reset loader state
    g_loadingFinished = false;
    g_loadingProgress = 18;
    
    // Spawn background loader task on Core 0 to parse TLEs off the UI thread
    xTaskCreatePinnedToCore(
        [](void* p) {
            // Curated satellites data initialization
            NUM_BUILTIN_SATELLITES = Encyclopedia::getEntryCount();
            if (NUM_BUILTIN_SATELLITES > MAX_SATELLITES - 20) {
                NUM_BUILTIN_SATELLITES = MAX_SATELLITES - 20;
            }
            NUM_SATELLITES = NUM_BUILTIN_SATELLITES;
            
            const EncyclopediaEntry* entries = Encyclopedia::getEntries();
            for (int i = 0; i < NUM_BUILTIN_SATELLITES; i++) {
                g_satellites[i].noradId = entries[i].norad;
                g_satellites[i].name = entries[i].name;
                g_satellites[i].color = entries[i].color;
                g_satellites[i].baseScore = entries[i].baseScore;
                g_satellites[i].stdMag = entries[i].stdMag;
                g_satellites[i].selected = entries[i].defaultSelected;
                g_satellites[i].iconType = entries[i].icon;
                g_satellites[i].description = entries[i].description_en;
                g_satellites[i].downlinkFreq = entries[i].downlinkFreq;
                g_satellites[i].radioMode = entries[i].radioMode;
                g_satellites[i].uplinkFreq = entries[i].uplinkFreq;
                g_satellites[i].tone = entries[i].tone;
                g_satellites[i].type = entries[i].type;
            }

            // Initialize Position & Sun Calculator
            pos_manager = new PositionManager(gnss);
            pos_manager->begin(); 
            
            // Initialize Chain Mono on Serial2 (Grove Port) early so it lights up during boot progress
#if ENABLE_CHAIN_MONO
            bool skipMonoProbe = false;
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
                
                // CRITICAL: GPIO 1/2 are shared between Grove port (Serial2) and internal I²C (IMU).
                // Suspend IMU task before driving these pins with UART to prevent I²C bus corruption
                // which would cause the IMU to output garbage data and the globe to jump around.
                if (imuTaskHandle != NULL) {
                    vTaskSuspend(imuTaskHandle);
                    LOG_I("APP", "IMU task suspended for Grove bus probing");
                }
                
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
                
                // Restore I²C bus and resume IMU task regardless of probe result.
                // If Chain Mono was not found, release GPIO 1/2 back to I²C.
                // If Chain Mono was found, Wire.begin() re-asserts I²C so other I²C devices still work.
                if (!foundChain) {
                    Serial2.end(); // Release GPIO 1/2 from UART
                    LOG_I("APP", "Chain Mono not found. Released GPIO 1/2. Restoring I²C bus.");
                }
                Wire.begin(8, 9, 100000); // Re-assert internal I²C master explicitly on GPIO 8 (SDA) / GPIO 9 (SCL)
                delay(10);    // Short stabilisation before resuming IMU reads
                if (imuTaskHandle != NULL) {
                    vTaskResume(imuTaskHandle);
                    LOG_I("APP", "IMU task resumed after Grove bus probing");
                }
                
                if (foundChain) {
                    LOG_I("APP", "Chain Mono successfully detected on RX=%d, TX=%d! Device count: %d", usedRx, usedTx, device_nums);
                    device_info_t *infos = (device_info_t *)malloc(sizeof(device_info_t) * device_nums);
                    if (infos != nullptr) {
                        memset(infos, 0, sizeof(device_info_t) * device_nums);
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
                
                if (gnss) {
                    GnssConfig gnssCfg = gnss->getConfig();
                    gnssCfg.enableGroveProbe = false;
                    gnss->setConfig(gnssCfg);
                }
            } else {
                if (!skipMonoProbe) {
                    LOG_I("APP", "Chain Mono module not detected. Releasing Grove pins for GNSS.");
                    Serial2.end();
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
            
            bool isZh = (I18N::getLanguage() == LANG_ZH);
            g_loadingStatusText = isZh ? "初始化传感器与外设..." : "Initializing Hardware...";
            g_loadingProgress = 10;
            
            // Load cached position from Preferences
            Preferences posPrefs;
            if (posPrefs.begin("position", true)) {
                if (posPrefs.isKey("cached_lat")) {
                    baseUserLat = posPrefs.getDouble("cached_lat", 39.90);
                    baseUserLon = posPrefs.getDouble("cached_lon", 116.40);
                    baseUserAlt = posPrefs.getDouble("cached_alt", 0.0);
                    isManualLocationMode = posPrefs.getBool("use_manual_pos", false);
                    gnssLocationFixed = false; // Loaded from cache, not a live GNSS fix yet!
                    
                    if (abs(baseUserLat) < 0.0001 && abs(baseUserLon) < 0.0001) {
                        baseUserLat = 39.90; // Beijing
                        baseUserLon = 116.40;
                        baseUserAlt = 50.0;
                        LOG_I("APP", "Cached position was zero (0, 0). Fallback to Beijing default coordinates.");
                    }
                    
                    // Sync loaded position to pos_manager
                    PositionData pos = {baseUserLat, baseUserLon, baseUserAlt};
                    if (isManualLocationMode) {
                        pos_manager->setManualPosition(pos);
                        pos_manager->enableManualPosition(true);
                    } else {
                        pos_manager->setPosition(pos);
                        pos_manager->enableManualPosition(false);
                    }
                    
                    LOG_I("APP", "Loaded cached position: lat=%.6f, lon=%.6f, alt=%.1f, useManual=%d", 
                          baseUserLat, baseUserLon, baseUserAlt, isManualLocationMode);
                }
                posPrefs.end();
            }
            
            g_loadingStatusText = isZh ? "载入观测坐标与太阳模型..." : "Loading Location & Sun Data...";
            g_loadingProgress = 25;
            
            sun_calc = new SunCalculator(pos_manager);
            sun_calc->begin();
            
            // Setup LittleFS for TLE Cache
            TLEUpdater::begin();
            
            // One-off cache cleanup to clear the previous stale cache bug
            if (!LittleFS.exists("/cache_cleared_v2.txt")) {
                for (int i = 0; i < NUM_SATELLITES; i++) {
                    String tlePath = "/tle_" + String(g_satellites[i].noradId) + ".txt";
                    String catPath = "/cat_" + String(g_satellites[i].noradId) + ".json";
                    if (LittleFS.exists(tlePath)) LittleFS.remove(tlePath);
                    if (LittleFS.exists(catPath)) LittleFS.remove(catPath);
                }
                File f = LittleFS.open("/cache_cleared_v2.txt", "w", true);
                if (f) {
                    f.println("cleared");
                    f.close();
                }
            }
            
            // Set default offline time first so getTLE works properly if needed
            current_unix = 0; // We start at 0 so TLEUpdater uses cache regardless of age
            
            // Offline TLE Cache Loading
            for (int i = 0; i < NUM_SATELLITES; i++) {
                if (g_satellites[i].type == SAT_TYPE_GEO_TV || g_satellites[i].type == SAT_TYPE_DEEP_SPACE) {
                    continue;
                }
                g_loadingStatusText = isZh ? ("解析轨道: " + g_satellites[i].name) : ("Parsing Orbit: " + g_satellites[i].name);
                TLEData loaded_tle;
                if (TLEUpdater::getTLE(g_satellites[i].noradId, loaded_tle)) {
                    loaded_tle.baseScore = g_satellites[i].baseScore;
                    lockSatMutex();
                    g_satellites[i].tle = loaded_tle;
                    unlockSatMutex();
                } else {
                    // Fallback using noradId instead of hardcoded index
                    uint32_t norad = g_satellites[i].noradId;
                    lockSatMutex();
                    if (norad == 25544) g_satellites[i].tle = TLEManager::getISS_TLE();
                    else if (norad == 48274) g_satellites[i].tle = TLEManager::getTiangong_TLE();
                    else if (norad == 20580) g_satellites[i].tle = TLEManager::getHubble_TLE();
                    else if (norad == 50463) g_satellites[i].tle = TLEManager::getJWST_TLE();
                    else if (norad == 27607) g_satellites[i].tle = TLEManager::getSO50_TLE();
                    else if (norad == 43017) g_satellites[i].tle = TLEManager::getAO91_TLE();
                    unlockSatMutex();
                }
                
                if (g_satellites[i].tle.line1.length() > 0) {
                    lockSatMutex();
                    g_satellites[i].calc.init(g_satellites[i].tle);
                    unlockSatMutex();
                }
                
                // Slowly progress progress to 65%
                g_loadingProgress = 30 + (int)(35.0f * (float)(i + 1) / (float)NUM_SATELLITES);
            }
            
            g_loadingProgress = 65;
            
            // Find the latest TLE Epoch as the initial system time anchor
            uint32_t latestEpoch = TLEManager::getMockTimeAnchor();
            for (int i = 0; i < NUM_SATELLITES; i++) {
                if (g_satellites[i].tle.line1.length() >= 32) {
                    uint32_t ep = parseTleEpoch(g_satellites[i].tle.line1);
                    if (ep > latestEpoch) {
                        latestEpoch = ep;
                    }
                }
            }
            current_unix = latestEpoch;
            g_timeSynced = true;
            LOG_I("APP", "Offline boot: Loaded cached TLEs. System time anchor set to: %u", current_unix);
            
            g_loadingStatusText = isZh ? "解算自定义目标与频段数据..." : "Loading Custom Satellites...";
            g_loadingProgress = 75;
            
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
                            p.description = "Custom added satellite.\n\n";
                            p.tle = loaded_tle;
                            p.calc.init(p.tle);
                            p.type = SAT_TYPE_VISUAL;
                            if (p.noradId == 57172 || p.name.indexOf("UMKA") != -1 || p.name.indexOf("RS40S") != -1) {
                                p.downlinkFreq = "437.625";
                                p.radioMode = "SSTV/BPSK";
                                p.type = SAT_TYPE_HAM;
                            }
                            autoAssignIconAndColor(p.name, p.iconType, p.color);
                            if (NUM_SATELLITES < MAX_SATELLITES) {
                                lockSatMutex();
                                g_satellites[NUM_SATELLITES++] = p;
                                unlockSatMutex();
                            }
                        }
                    }
                }
            }
            
            if (needsSaveCleanup) {
                LOG_I("APP", "Built-in satellites found in custom list. Performing Preferences cleanup.");
                saveCustomSatellites();
            }
            
            g_loadingStatusText = isZh ? "构建火箭与群编队数据..." : "Building Launch Formations...";
            g_loadingProgress = 85;
            tryLoadRecentLaunchCache();
            
            g_loadingStatusText = isZh ? "启动核心推算引擎..." : "Starting Predictor Engine...";
            g_loadingProgress = 95;
            
            // Start predictor task on Core 0 for offline data (UI runs on Core 1)
            xTaskCreatePinnedToCore(
                predictorTask,
                "PredictorTask",
                8192,
                NULL,
                1,
                &predictorTaskHandle,
                0
            );
            
            // Start network task on Core 0 to handle WiFi and TLE fetching in background
            manualWifiToggle = false;
            xTaskCreatePinnedToCore(networkTask, "NetworkTask", 16384, NULL, 1, NULL, 0);

            g_loadingStatusText = isZh ? "加载完成，准备就绪！" : "Ready!";
            g_loadingProgress = 100;
            delay(100);
            g_loadingFinished = true;
            vTaskDelete(NULL);
        },
        "SetupLoader",
        12288,
        NULL,
        2, // Slightly lower than IMU but higher than predictor
        NULL,
        0
    );
    
    // Smooth 30 FPS rendering loop on Core 1 (main setup thread)
    bool needsLangSelect = I18N::isFirstStart();
    int selectedLangIdx = 0;
    while (!g_loadingFinished || needsLangSelect) {
        M5Cardputer.update();
        if (needsLangSelect) {
            static bool lastSemi = false;
            static bool lastDot = false;
            static bool lastEnter = false;
            bool currSemi = M5Cardputer.Keyboard.isKeyPressed(';');
            bool currDot = M5Cardputer.Keyboard.isKeyPressed('.');
            bool currEnter = M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER);
            if (currSemi && !lastSemi) {
                selectedLangIdx = (selectedLangIdx == 0) ? 1 : 0;
            }
            if (currDot && !lastDot) {
                selectedLangIdx = (selectedLangIdx == 0) ? 1 : 0;
            }
            if (currEnter && !lastEnter) {
                I18N::setLanguage(selectedLangIdx == 0 ? LANG_EN : LANG_ZH);
                I18N::setFirstStartDone();
                needsLangSelect = false;
            }
            lastSemi = currSemi;
            lastDot = currDot;
            lastEnter = currEnter;
        }
        drawStartupScreen(g_loadingProgress, needsLangSelect, selectedLangIdx);
        updateChainMonoDisplay();
        delay(33); // ~30 FPS
    }
    
    // Draw the final complete state and pause slightly to show completion
    drawStartupScreen(100, false, 0);
    delay(50);
    
    // Restore decorations for main system view
    if (earth_renderer) {
        earth_renderer->setDrawDecorations(true);
    }
    
    // Calibrate IMU to current orientation on boot (IMU task has stabilized during setup)
    if (attitude) {
        attitude->calibrateHeading();
        LOG_I("APP", "IMU calibrated on boot completed. Center aligned.");
    }
}

void drawWiFiSetupPage() {
    auto canvas = earth_renderer->getCanvas();
    uint16_t width = canvas->width();
    uint16_t height = canvas->height();
    
    // Explicitly set Chinese UTF-8 font
    canvas->setFont(&fonts::efontCN_12);
    canvas->setTextSize(1);
    
    // Background
    canvas->fillRect(0, 0, width, height, canvas->color565(15, 20, 25));
    
    // Top Bar
    canvas->fillRect(0, 0, width, 25, canvas->color565(30, 60, 100));
    canvas->setTextColor(TFT_WHITE);
    canvas->drawString(I18N::get(TXT_WIFI_SETUP), 10, 5);
    
    canvas->setTextColor(TFT_WHITE);
    
    if (wifiIsScanning) {
        canvas->drawString(I18N::get(TXT_SCANNING_NETWORKS), 20, 50);
        return; // Will be handled in main loop
    }
    
    if (wifiNetworks.empty() && !wifiIsScanning) {
        wifiIsInputtingPassword = false;
        canvas->drawString(I18N::get(TXT_NO_NETWORKS_FOUND), 20, 80);
        canvas->drawString(I18N::get(TXT_PRESS_R_RESCAN), 20, 100);
    } else {
        if (wifiIsInputtingPassword && wifiSelectedIndex >= 0 && wifiSelectedIndex < (int)wifiNetworks.size()) {
            canvas->drawString(I18N::get(TXT_CONNECT_TO), 20, 40);
            canvas->setTextColor(TFT_GREEN);
            String ssid = truncateUtf8Chars(wifiNetworks[wifiSelectedIndex].ssid, 16);
            canvas->drawString(ssid.c_str(), 20, 55);
            
            canvas->setTextColor(TFT_WHITE);
            canvas->drawString(I18N::get(TXT_PASSWORD), 20, 80);
            
            canvas->fillRect(20, 95, width - 40, 25, canvas->color565(50, 50, 50));
            canvas->drawRect(20, 95, width - 40, 25, TFT_WHITE);
            
            char displayStr[66];
            sprintf(displayStr, "%s_", wifiPasswordBuffer);
            canvas->drawString(displayStr, 25, 100);
            
            canvas->setTextColor(TFT_LIGHTGRAY);
            canvas->drawString(I18N::get(TXT_WIFI_HELP_CONN), 10, height - 15);
        } else {
            canvas->drawString(I18N::get(TXT_SELECT_NETWORK), 10, 30);
            
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
                
                String ssidStr = truncateUtf8Chars(wifiNetworks[index].ssid, 14);
                canvas->drawString(ssidStr.c_str(), 10, yPos);
                
                char rssiStr[16];
                sprintf(rssiStr, "%ddBm", wifiNetworks[index].rssi);
                canvas->drawString(rssiStr, width - 50, yPos);
                
                yPos += 20;
            }
            
            canvas->setTextColor(TFT_LIGHTGRAY);
            canvas->drawString(I18N::get(TXT_WIFI_HELP_SEL), 5, height - 15);
        }
    }
}

void drawSatSelectPage() {
    auto getBannerTextColor = [](const String& msg) -> uint16_t {
        String lower = msg;
        lower.toLowerCase();
        
        if (lower.indexOf("success") != -1 || msg.indexOf(u8"成功") != -1 ||
            lower.indexOf("updated") != -1 || msg.indexOf(u8"已更新") != -1 ||
            lower.indexOf("fresh") != -1 || lower.indexOf("ready") != -1) {
            return TFT_GREEN;
        }
        
        if (lower.indexOf("connecting") != -1 || msg.indexOf(u8"连接") != -1 ||
            lower.indexOf("refreshing") != -1 || msg.indexOf(u8"刷新") != -1 ||
            lower.indexOf("downloading") != -1 || msg.indexOf(u8"下载") != -1 ||
            lower.indexOf("syncing") != -1 || msg.indexOf(u8"同步") != -1 ||
            lower.indexOf("checking") != -1 || msg.indexOf(u8"检查") != -1 ||
            lower.indexOf("busy") != -1 || msg.indexOf(u8"繁忙") != -1 ||
            lower.indexOf("loading") != -1 || msg.indexOf(u8"加载") != -1 ||
            lower.indexOf("%") != -1) {
            return TFT_YELLOW;
        }
        
        if (lower.indexOf("failed") != -1 || msg.indexOf(u8"失败") != -1 ||
            lower.indexOf("error") != -1 || msg.indexOf(u8"错误") != -1 ||
            lower.indexOf("no wifi") != -1 || msg.indexOf(u8"未连接") != -1 ||
            lower.indexOf("refused") != -1) {
            return TFT_RED;
        }
        
        return TFT_LIGHTGRAY;
    };

    if (!g_networkActive) {
        if (downloadErrorMsg == I18N::get(TXT_SYS_BUSY)) {
            downloadErrorMsg = "";
        }
        if (recentLaunchErrorMsg == I18N::get(TXT_SYS_BUSY)) {
            recentLaunchErrorMsg = "";
        }
    }

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
    } else if (currentSatTab == TAB_ENCYCLOPEDIA) {
        if (g_wifiConnecting || g_networkActive || (downloadFinishedMs > 0 && (millis() - downloadFinishedMs < 3000))) {
            if (downloadErrorMsg.length() > 0) {
                showBanner = true;
            }
        } else {
            if (downloadErrorMsg.length() > 0) {
                downloadErrorMsg = "";
            }
        }
    }
    int bottomLimit = showBanner ? (height - 13) : height;

    
    // Background
    canvas->fillRect(0, 0, width, height, canvas->color565(20, 30, 40));
    
    // Top Bar - Dual Tabs
    canvas->fillRect(0, 0, width, 20, canvas->color565(30, 40, 50));
    
    // Tab 1: Encyclopedia
    uint16_t tab1Bg = (currentSatTab == TAB_ENCYCLOPEDIA) ? canvas->color565(100, 50, 200) : canvas->color565(30, 40, 50);
    canvas->fillRect(0, 0, width/2, 20, tab1Bg);
    canvas->setTextColor(TFT_WHITE);
    canvas->setTextSize(1);
    canvas->drawString(I18N::get(TXT_TAB_ENCYCLOPEDIA), 70 - canvas->textWidth(I18N::get(TXT_TAB_ENCYCLOPEDIA))/2, 6);
    
    // Tab 2: Recent Launch
    uint16_t tab2Bg = (currentSatTab == TAB_RECENT_LAUNCH) ? canvas->color565(100, 50, 200) : canvas->color565(30, 40, 50);
    canvas->fillRect(width/2, 0, width/2, 20, tab2Bg);
    canvas->drawString(I18N::get(TXT_TAB_RECENT_LAUNCH), 166 - canvas->textWidth(I18N::get(TXT_TAB_RECENT_LAUNCH))/2, 6);
    
    // Draw Memory Usage Progress Bar on Top Bar Divider Line (y = 19..20)
    {
        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t totalHeap = ESP.getHeapSize();
        float memRatio = 0.0f;
        if (totalHeap > 0) {
            memRatio = (float)(totalHeap - freeHeap) / (float)totalHeap;
        }
        int fillWidth = (int)(width * memRatio);
        if (fillWidth > width) fillWidth = width;
        if (fillWidth < 0) fillWidth = 0;

        uint16_t memColor;
        if (memRatio < 0.65f) {
            memColor = canvas->color565(0, 220, 255); // 青色 (正常 <65%)
        } else if (memRatio < 0.82f) {
            memColor = TFT_YELLOW;                   // 黄色 (预警 65%-82%)
        } else {
            memColor = TFT_RED;                      // 红色 (高占用 >82%)
        }

        // 绘制 2px 内存进度条充当分割线
        canvas->fillRect(0, 19, width, 2, canvas->color565(35, 45, 55)); // 轨道背景
        if (fillWidth > 0) {
            canvas->fillRect(0, 19, fillWidth, 2, memColor);             // 填充内存使用率
        }
    }
    
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
        
        // 2. Battery Icon on Top-Right with hysteresis filtering to prevent jitter
        static float filteredBat = -1.0f;
        static int lastDisplayedBat = -1;
        
        int rawBat = M5Cardputer.Power.getBatteryLevel();
        if (rawBat > 100) rawBat = 100;
        if (rawBat < 0) rawBat = 0;
        
        if (filteredBat < 0.0f) {
            filteredBat = (float)rawBat;
            lastDisplayedBat = rawBat;
        } else {
            // Smooth out high-frequency ADC voltage noise with a first-order low-pass filter
            filteredBat = filteredBat * 0.98f + (float)rawBat * 0.02f;
            // Apply 1.0% hysteresis band to prevent the integer display from toggling back-and-forth at boundary values
            if (fabsf(filteredBat - (float)lastDisplayedBat) >= 1.0f) {
                lastDisplayedBat = (int)(filteredBat + 0.5f);
            }
        }
        int batPct = lastDisplayedBat;
        
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
        canvas->setFont(&fonts::Font0);
        canvas->setTextColor(batColor);
        String pctStr = String(batPct);
        int textX = batX + (batW - canvas->textWidth(pctStr.c_str())) / 2;
        int textY = batY + 2; // standard char height is 8
        canvas->drawString(pctStr.c_str(), textX, textY);
        canvas->setFont(&fonts::efontCN_12);
    }

    
    if (currentSatTab == TAB_ENCYCLOPEDIA) {
        // Left Panel (List)
        int yPos = 25;
        int itemsPerPage = showBanner ? 6 : 8;
        int itemSpacing = 12;
        int startIndex = (satSelectedIndex / itemsPerPage) * itemsPerPage;
        
        for (int i = 0; i < itemsPerPage && (startIndex + i) <= NUM_SATELLITES; i++) {
            int index = startIndex + i;
            if (index == satSelectedIndex) {
                canvas->fillRect(2, yPos - 1, 82, 12, canvas->color565(0, 120, 255));
                canvas->setTextColor(TFT_WHITE);
            } else {
                canvas->setTextColor(TFT_LIGHTGRAY);
            }
            
            if (index < NUM_SATELLITES) {
                String checkBox = g_satellites[index].selected ? "[x]" : "[ ]";
                canvas->drawString(checkBox.c_str(), 4, yPos);
                
                if (index == satSelectedIndex) {
                    drawScrollingText(canvas, g_satellites[index].name.c_str(), 28, yPos, 56, TFT_WHITE);
                } else {
                    String nameStr = g_satellites[index].name;
                    if (nameStr.length() > 9) nameStr = nameStr.substring(0, 7) + "..";
                    canvas->drawString(nameStr.c_str(), 28, yPos);
                }
            } else {
                String text = isDownloadingCustom ? I18N::get(TXT_DOWNLOADING) : ("[+] " + noradInput + "_");
                canvas->drawString(text.c_str(), 4, yPos);
            }
            
            yPos += itemSpacing;
        }
        
        // Draw page index indicator
        {
            int totalCount = NUM_SATELLITES + 1;
            int totalPages = (totalCount + itemsPerPage - 1) / itemsPerPage;
            int currentPage = (satSelectedIndex / itemsPerPage) + 1;
            int currentIdx = satSelectedIndex + 1;
            char pageBuf[32];
            sprintf(pageBuf, "(%d %d/%d)", currentIdx, currentPage, totalPages);
            canvas->setTextColor(canvas->color565(110, 150, 180));
            canvas->drawString(pageBuf, 28, showBanner ? (bottomLimit - 12) : (bottomLimit - 13));
        }
        
        // Right Panel (Description)
        canvas->drawFastVLine(85, 20, bottomLimit - 20, TFT_DARKGREY);
        
        int rightX = 89;
        int descY = 25;
        if (satSelectedIndex < NUM_SATELLITES) {
            SatProfile selSat;
            lockSatMutex();
            selSat = g_satellites[satSelectedIndex];
            unlockSatMutex();
            
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
            } else if (t == ICON_DEBRIS) {
                // Space Debris: regular solar grid panel on left, jagged broken lines in middle, small drifting squares on right
                // 1. Regular panel on left
                canvas->fillRect(iconX - 18, iconY - 6, 18, 13, satColor);
                canvas->drawRect(iconX - 18, iconY - 6, 18, 13, TFT_BLACK);
                canvas->drawFastHLine(iconX - 18, iconY, 18, TFT_BLACK);
                canvas->drawFastVLine(iconX - 9, iconY - 6, 13, TFT_BLACK);
                
                // 2. Jagged edge and outline in middle
                canvas->drawLine(iconX, iconY - 6, iconX + 12, iconY - 3, satColor);
                canvas->drawLine(iconX + 12, iconY - 3, iconX + 6, iconY + 3, satColor);
                canvas->drawLine(iconX + 6, iconY + 3, iconX + 15, iconY + 7, satColor);
                canvas->drawLine(iconX + 15, iconY + 7, iconX, iconY + 7, satColor);
                canvas->drawLine(iconX, iconY, iconX + 9, iconY + 2, satColor);
                
                // 3. Detached debris chunks on right
                canvas->fillRect(iconX + 18, iconY - 9, 3, 3, satColor);
                canvas->fillRect(iconX + 15, iconY + 12, 4, 3, satColor);
                canvas->fillRect(iconX + 22, iconY + 2, 3, 4, satColor);
            } else if (t == ICON_SPACEPLANE) {
                // Spaceplane (X-37B) 3x — top-down delta wing, blunt nose, vertical tail
                // Nose
                canvas->fillRect(iconX - 3, iconY - 12, 9, 6, TFT_WHITE);
                // Fuselage
                canvas->fillRect(iconX - 6, iconY - 6, 15, 15, TFT_WHITE);
                // Delta wings (left & right triangles at widest point)
                canvas->fillTriangle(iconX - 12, iconY + 3, iconX - 3, iconY - 3, iconX - 3, iconY + 9, satColor);
                canvas->fillTriangle(iconX + 15, iconY + 3, iconX + 6, iconY - 3, iconX + 6, iconY + 9, satColor);
                // Vertical tail fin
                canvas->drawFastVLine(iconX + 3, iconY + 9, 9, satColor);
                canvas->drawFastHLine(iconX, iconY + 15, 9, satColor);
            } else if (t == ICON_SOLAR_PROBE) {
                // Solar Probe (Parker) 3x — wide heat shield disc + instrument boom + tiny solar wings
                // Heat shield (flat ellipse)
                canvas->fillEllipse(iconX, iconY - 3, 12, 9, TFT_LIGHTGRAY);
                canvas->drawEllipse(iconX, iconY - 3, 12, 9, satColor);
                // Instrument boom
                canvas->drawFastVLine(iconX, iconY + 6, 9, TFT_WHITE);
                // Tiny solar panels
                canvas->fillRect(iconX - 9, iconY + 9, 6, 3, satColor);
                canvas->fillRect(iconX + 4, iconY + 9, 6, 3, satColor);
            } else if (t == ICON_LANDER) {
                // Lander 3x — hexagonal body + top antenna + three landing legs with foot pads
                // Antenna
                canvas->drawFastVLine(iconX, iconY - 12, 6, satColor);
                canvas->drawPixel(iconX - 1, iconY - 12, satColor);
                canvas->drawPixel(iconX + 1, iconY - 12, satColor);
                // Main body
                canvas->fillRect(iconX - 6, iconY - 6, 15, 12, TFT_WHITE);
                // Three legs
                canvas->drawLine(iconX - 6, iconY + 6, iconX - 12, iconY + 12, satColor);
                canvas->drawLine(iconX + 1, iconY + 6, iconX + 1,  iconY + 12, satColor);
                canvas->drawLine(iconX + 8, iconY + 6, iconX + 14, iconY + 12, satColor);
                // Foot pads
                canvas->drawFastHLine(iconX - 15, iconY + 12, 6, satColor);
                canvas->drawFastHLine(iconX - 1, iconY + 13, 4, satColor);
                canvas->drawFastHLine(iconX + 12, iconY + 12, 6, satColor);
            } else {
                canvas->fillRect(iconX - 3, iconY - 3, 9, 9, TFT_WHITE);
                canvas->fillRect(iconX - 15, iconY - 3, 9, 9, satColor);
                canvas->fillRect(iconX - 6, iconY - 1, 3, 3, TFT_LIGHTGRAY);
            }
            
            drawScrollingText(canvas, selSat.name.c_str(), rightX + 48, descY + 6, width - rightX - 48 - 4, selSat.color);
            
            // Draw NORAD ID for tracking
            canvas->setTextColor(TFT_LIGHTGRAY);
            canvas->drawString((String(I18N::get(TXT_ID)) + String(selSat.noradId)).c_str(), rightX + 48, descY + 20);
            
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
                    sprintf(ageBuf, "%s", I18N::get(TXT_GP_AGE_NA));
                    ageColor = TFT_RED;
                } else {
                    sprintf(ageBuf, "%s%dd", I18N::get(TXT_GP_AGE), ageDays);
                    if (ageDays <= 7) ageColor = TFT_GREEN;
                    else if (ageDays <= 14) ageColor = TFT_ORANGE;
                    else ageColor = TFT_RED;
                }
                canvas->setTextColor(ageColor);
                int ageW = canvas->textWidth(ageBuf);
                canvas->drawString(ageBuf, width - ageW - 4, descY - 5);
            } else {
                canvas->setTextColor(TFT_RED);
                canvas->drawString(I18N::get(TXT_GP_AGE_NA), width - canvas->textWidth(I18N::get(TXT_GP_AGE_NA)) - 4, descY - 5);
            }
            
            descY += 36;
            
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
            
            int radioY = bottomLimit;
            canvas->setTextColor(TFT_LIGHTGRAY);
            
            // 1. 基础文字描述 (简介)
            bool isZh = (I18N::getLanguage() == LANG_ZH);
            String finalDesc = "";
            if (satSelectedIndex >= NUM_BUILTIN_SATELLITES) {
                if (selSat.description && strlen(selSat.description) > 0) {
                    finalDesc = selSat.description;
                } else {
                    finalDesc = isZh ? "自定义添加的目标卫星。" : "Custom added satellite.";
                }
            } else {
                const char* localDesc = I18N::getSatDescription(selSat.noradId);
                if (localDesc) {
                    finalDesc = localDesc;
                } else if (selSat.description) {
                    finalDesc = selSat.description;
                }
            }
            
            // 2. 拼接轨道参数与遥测细节 (COSPAR/周期/速度/倾角/高度/方位/仰角/频率)
            String specBlock = "";
            if (selSat.tle.line1.length() >= 60 && selSat.tle.line2.length() >= 60) {
                // 解析 COSPAR 国际标识
                String cospar = "";
                if (selSat.tle.line1.length() >= 17) {
                    String rawCospar = selSat.tle.line1.substring(9, 17);
                    rawCospar.trim();
                    if (rawCospar.length() >= 5) {
                        String yrStr = rawCospar.substring(0, 2);
                        int yr = yrStr.toInt();
                        String trueYr = (yr >= 50) ? ("19" + yrStr) : ("20" + yrStr);
                        cospar = trueYr + "-" + rawCospar.substring(2);
                    }
                }
                
                float inclination = selSat.tle.line2.substring(8, 16).toFloat();
                String eccRaw = selSat.tle.line2.substring(26, 33);
                eccRaw.trim();
                float eccentricity = 0.0f;
                if (eccRaw.length() > 0) {
                    eccentricity = ("0." + eccRaw).toFloat();
                }
                float meanMotion = selSat.tle.line2.substring(52, 63).toFloat();
                
                float periodMin = 0.0f;
                float perigee = 0.0f;
                float apogee = 0.0f;
                if (meanMotion > 0) {
                    periodMin = 1440.0f / meanMotion;
                    double n = meanMotion * 2.0 * 3.141592653589793 / 86400.0;
                    double mu = 3.986004418e14;
                    double a = pow(mu / (n * n), 1.0 / 3.0) / 1000.0;
                    perigee = a * (1.0f - eccentricity) - 6378.137f;
                    apogee = a * (1.0f + eccentricity) - 6378.137f;
                    if (perigee < 0) perigee = 0;
                    if (apogee < 0) apogee = 0;
                }
                
                // 实时 SGP4 速度计算 (km/s)
                double tx, ty, tz, vx, vy, vz;
                double realSpeed = 0.0;
                if (selSat.calc.getTEME(current_unix + timeMachineOffset, tx, ty, tz, vx, vy, vz)) {
                    realSpeed = sqrt(vx * vx + vy * vy + vz * vz);
                }
                
                char specBuf[256];
                if (isZh) {
                    if (periodMin >= 120.0f) {
                        snprintf(specBuf, sizeof(specBuf),
                                 "\n国际标识: %s\n"
                                 "轨道周期: %.2f小时\n"
                                 "运行速度: %.2f km/s\n"
                                 "轨道倾角: %.2f°\n"
                                 "近/远地点: %.0f/%.0f km",
                                 cospar.length() > 0 ? cospar.c_str() : "未知",
                                 periodMin / 60.0f,
                                 realSpeed > 0 ? realSpeed : 7.66,
                                 inclination,
                                 perigee, apogee);
                    } else {
                        snprintf(specBuf, sizeof(specBuf),
                                 "\n国际标识: %s\n"
                                 "轨道周期: %.1f分钟\n"
                                 "运行速度: %.2f km/s\n"
                                 "轨道倾角: %.2f°\n"
                                 "近/远地点: %.0f/%.0f km",
                                 cospar.length() > 0 ? cospar.c_str() : "未知",
                                 periodMin,
                                 realSpeed > 0 ? realSpeed : 7.66,
                                 inclination,
                                 perigee, apogee);
                    }
                } else {
                    if (periodMin >= 120.0f) {
                        snprintf(specBuf, sizeof(specBuf),
                                 "\nCOSPAR: %s\n"
                                 "Period: %.2fh\n"
                                 "Speed: %.2f km/s\n"
                                 "Incl: %.2f°\n"
                                 "Alt: %.0f/%.0f km",
                                 cospar.length() > 0 ? cospar.c_str() : "N/A",
                                 periodMin / 60.0f,
                                 realSpeed > 0 ? realSpeed : 7.66,
                                 inclination,
                                 perigee, apogee);
                    } else {
                        snprintf(specBuf, sizeof(specBuf),
                                 "\nCOSPAR: %s\n"
                                 "Period: %.1f min\n"
                                 "Speed: %.2f km/s\n"
                                 "Incl: %.2f°\n"
                                 "Alt: %.0f/%.0f km",
                                 cospar.length() > 0 ? cospar.c_str() : "N/A",
                                 periodMin,
                                 realSpeed > 0 ? realSpeed : 7.66,
                                 inclination,
                                 perigee, apogee);
                    }
                }
                specBlock += String(specBuf);
            }
            
            // 实时观察数据 (方位角 / 仰角) 分开单行显示，确保“仰角:”作为行首键名高亮为绿色
            if (isTracking) {
                char radioBuf[128];
                if (isZh) {
                    snprintf(radioBuf, sizeof(radioBuf), "\n方位角: %03.0f°\n仰角: %02.0f°", az, el);
                } else {
                    snprintf(radioBuf, sizeof(radioBuf), "\nAzimuth: %03.0f°\nElevation: %02.0f°", az, el);
                }
                specBlock += String(radioBuf);
            }
            
            // 运行状态与无线电频段细节
            if (selSat.type == SAT_TYPE_HISTORICAL || selSat.noradId == 4382 || selSat.noradId == 5 || selSat.noradId == 27386 || selSat.noradId == 25576) {
                char statusBuf[64];
                if (isZh) {
                    snprintf(statusBuf, sizeof(statusBuf), "\n运行状态: 已失效/默音在轨");
                } else {
                    snprintf(statusBuf, sizeof(statusBuf), "\nStatus: Inactive/Silent");
                }
                specBlock += String(statusBuf);
            }
            
            // 无线电频段细节 (下行 / 上行 / 亚音 / 调制模式)
            if (selSat.downlinkFreq.length() > 0) {
                char freqBuf[128];
                if (isZh) {
                    snprintf(freqBuf, sizeof(freqBuf), "\n下行: %s MHz", selSat.downlinkFreq.c_str());
                } else {
                    snprintf(freqBuf, sizeof(freqBuf), "\nRx: %s MHz", selSat.downlinkFreq.c_str());
                }
                specBlock += String(freqBuf);
            }
            if (selSat.uplinkFreq.length() > 0) {
                char txBuf[128];
                if (isZh) {
                    snprintf(txBuf, sizeof(txBuf), "\n上行: %s MHz", selSat.uplinkFreq.c_str());
                } else {
                    snprintf(txBuf, sizeof(txBuf), "\nTx: %s MHz", selSat.uplinkFreq.c_str());
                }
                specBlock += String(txBuf);
            }
            if (selSat.tone.length() > 0) {
                char toneBuf[64];
                if (isZh) {
                    snprintf(toneBuf, sizeof(toneBuf), "\n亚音: %s", selSat.tone.c_str());
                } else {
                    snprintf(toneBuf, sizeof(toneBuf), "\nTone: %s", selSat.tone.c_str());
                }
                specBlock += String(toneBuf);
            }
            if (selSat.radioMode.length() > 0) {
                char modeBuf[64];
                if (isZh) {
                    snprintf(modeBuf, sizeof(modeBuf), "\n调制模式: %s", selSat.radioMode.c_str());
                } else {
                    snprintf(modeBuf, sizeof(modeBuf), "\nMode: %s", selSat.radioMode.c_str());
                }
                specBlock += String(modeBuf);
            }
            
            String fullTextToRender = finalDesc + specBlock;
            
            if (fullTextToRender.length() > 0) {
                int currLang = I18N::getLanguage();
                if (satSelectedIndex != g_descLastSatIndex || currLang != g_descLastLang) {
                    g_descLastSatIndex = satSelectedIndex;
                    g_descLastLang = currLang;
                    g_descManualScrolled = false;
                    g_descManualYOffset = 0;
                    g_descWrappedLines.clear();
                    wrapTextIntoLines(canvas, fullTextToRender, width - rightX - 5, g_descWrappedLines);
                    
                    // 动态计算底部标签组占用的高度
                    g_descLabelAreaHeight = 20;
                    const EncyclopediaEntry* entry = Encyclopedia::getEntryByNorad(selSat.noradId);
                    if (entry) {
                        int simX = rightX;
                        int simY = 0;
                        String catName = Encyclopedia::getCategoryName(entry->category);
                        int catW = canvas->textWidth(catName.c_str()) + 6;
                        simX += catW + 4;
                        
                        uint32_t flags = entry->flags;
                        uint32_t allFlags[] = {
                            FLAG_VISIBLE, FLAG_CREWED, FLAG_HISTORIC, FLAG_ROCKET_BODY,
                            FLAG_DEBRIS, FLAG_WEATHER, FLAG_RADIO, FLAG_NAVIGATION,
                            FLAG_SCIENCE, FLAG_EARTH_OBS
                        };
                        for (uint32_t f : allFlags) {
                            if (flags & f) {
                                String fName = Encyclopedia::getFlagName(f);
                                if (fName.length() > 0) {
                                    if (catName.indexOf(fName) != -1 || fName.indexOf(catName) != -1) {
                                        continue;
                                    }
                                    int fW = canvas->textWidth(fName.c_str()) + 6;
                                    if (simX + fW > width - 4) {
                                        simY += 15;
                                        simX = rightX;
                                    }
                                    simX += fW + 4;
                                }
                            }
                        }
                        g_descLabelAreaHeight = simY + 20;
                    }
                    
                    g_lastSatSelectTime = millis();
                }

                if (!g_descWrappedLines.empty()) {
                    int descAreaHeight = bottomLimit - descY;
                    int totalLines = g_descWrappedLines.size();
                    int totalHeight = totalLines * 13 + g_descLabelAreaHeight;
                    g_descMaxScroll = (totalHeight > descAreaHeight) ? (totalHeight - descAreaHeight + 13) : 0;
                    
                    int yOffset = 0;
                    if (totalHeight > descAreaHeight && descAreaHeight > 13) {
                        if (g_descManualScrolled) {
                            // 手动按中括号翻页模式下，暂停自动滚动功能
                            yOffset = g_descManualYOffset;
                        } else {
                            // 默认自动循环滚动
                            int scrollSpeedMs = 66;
                            int holdTimeMs = 1500;
                            int scrollRange = totalHeight - descAreaHeight + 13;
                            int cycleTime = scrollRange * scrollSpeedMs + holdTimeMs * 2;
                            int t = (millis() - g_lastSatSelectTime) % cycleTime;
                            if (t < holdTimeMs) yOffset = 0;
                            else if (t < cycleTime - holdTimeMs) yOffset = (t - holdTimeMs) / scrollSpeedMs;
                            else yOffset = scrollRange;
                        }
                    }
                    
                    canvas->setClipRect(rightX, descY, width - rightX, descAreaHeight);
                    
                    // 1. 绘制简介与属性细节文本
                    for (int idx = 0; idx < totalLines; idx++) {
                        int lineY = descY + idx * 13 - yOffset;
                        if (lineY >= descY - 13 && lineY <= descY + descAreaHeight) {
                            String line = g_descWrappedLines[idx];
                            int colonIdx = line.indexOf(':');
                            if (colonIdx == -1) {
                                colonIdx = line.indexOf("：");
                            }
                            
                            if (colonIdx != -1) {
                                String namePart = line.substring(0, colonIdx + 1);
                                String valuePart = line.substring(colonIdx + 1);
                                
                                canvas->setTextColor(TFT_GREEN);
                                canvas->drawString(namePart.c_str(), rightX, lineY);
                                
                                int nameW = canvas->textWidth(namePart.c_str());
                                canvas->setTextColor(TFT_LIGHTGRAY);
                                canvas->drawString(valuePart.c_str(), rightX + nameW, lineY);
                            } else {
                                canvas->setTextColor(TFT_LIGHTGRAY);
                                canvas->drawString(line.c_str(), rightX, lineY);
                            }
                        }
                    }
                    
                    // 2. 绘制分类标签与属性标记（置于文本下方，完美随滚动滑动）
                    int badgeY = descY + totalLines * 13 + 6 - yOffset;
                    const EncyclopediaEntry* entry = Encyclopedia::getEntryByNorad(selSat.noradId);
                    if (entry) {
                        int currBadgeX = rightX;
                        int currBadgeY = badgeY;
                        
                        String catName = Encyclopedia::getCategoryName(entry->category);
                        int catW = canvas->textWidth(catName.c_str()) + 6;
                        
                        if (currBadgeY >= descY - 13 && currBadgeY <= descY + descAreaHeight) {
                            canvas->fillRoundRect(currBadgeX, currBadgeY, catW, 13, 2, canvas->color565(60, 80, 110));
                            canvas->setTextColor(TFT_WHITE);
                            canvas->drawString(catName.c_str(), currBadgeX + 3, currBadgeY + 1);
                        }
                        currBadgeX += catW + 4;
                        
                        uint32_t flags = entry->flags;
                        uint32_t allFlags[] = {
                            FLAG_VISIBLE, FLAG_CREWED, FLAG_HISTORIC, FLAG_ROCKET_BODY,
                            FLAG_DEBRIS, FLAG_WEATHER, FLAG_RADIO, FLAG_NAVIGATION,
                            FLAG_SCIENCE, FLAG_EARTH_OBS
                        };
                        for (uint32_t f : allFlags) {
                            if (flags & f) {
                                String fName = Encyclopedia::getFlagName(f);
                                if (fName.length() > 0) {
                                    if (catName.indexOf(fName) != -1 || fName.indexOf(catName) != -1) {
                                        continue;
                                    }
                                    int fW = canvas->textWidth(fName.c_str()) + 6;
                                    if (currBadgeX + fW > width - 4) {
                                        currBadgeY += 15;
                                        currBadgeX = rightX;
                                    }
                                    
                                    if (currBadgeY >= descY - 13 && currBadgeY <= descY + descAreaHeight) {
                                        uint16_t bgColor = canvas->color565(40, 50, 60);
                                        uint16_t textColor = TFT_LIGHTGRAY;
                                        if (f == FLAG_VISIBLE) { bgColor = canvas->color565(90, 80, 20); textColor = TFT_YELLOW; }
                                        else if (f == FLAG_CREWED) { bgColor = canvas->color565(20, 90, 50); textColor = TFT_GREEN; }
                                        else if (f == FLAG_HISTORIC) { bgColor = canvas->color565(90, 50, 20); textColor = TFT_ORANGE; }
                                        else if (f == FLAG_RADIO) { bgColor = canvas->color565(80, 30, 80); textColor = TFT_MAGENTA; }
                                        else if (f == FLAG_SCIENCE) { bgColor = canvas->color565(50, 30, 90); textColor = TFT_GOLD; }
                                        else if (f == FLAG_WEATHER) { bgColor = canvas->color565(20, 60, 90); textColor = TFT_CYAN; }
                                        else if (f == FLAG_EARTH_OBS) { bgColor = canvas->color565(30, 80, 80); textColor = TFT_GREEN; }
                                        else if (f == FLAG_NAVIGATION) { bgColor = canvas->color565(80, 20, 20); textColor = TFT_RED; }
                                        
                                        canvas->fillRoundRect(currBadgeX, currBadgeY, fW, 13, 2, bgColor);
                                        canvas->setTextColor(textColor);
                                        canvas->drawString(fName.c_str(), currBadgeX + 3, currBadgeY + 1);
                                    }
                                    currBadgeX += fW + 4;
                                }
                            }
                        }
                    }
                    
                    canvas->clearClipRect();
                } else {
                    canvas->drawString(I18N::get(TXT_NO_DESCRIPTION), rightX, descY);
                }
            } else {
                canvas->drawString(I18N::get(TXT_NO_DESCRIPTION), rightX, descY);
            }
        } else {
            if (downloadErrorMsg.length() > 0) {
                canvas->setTextColor(getBannerTextColor(downloadErrorMsg));
                drawWrappedText(canvas, downloadErrorMsg.c_str(), rightX, descY, width - rightX - 5, 13);
            } else {
                canvas->setTextColor(TFT_LIGHTGRAY);
                int lines = drawWrappedText(canvas, I18N::get(TXT_ENTER_NORAD_ADD), rightX, descY, width - rightX - 5, 13);
                
                canvas->setTextColor(TFT_YELLOW);
                canvas->drawString(I18N::get(TXT_SOURCE_CELESTRAK), rightX, descY + lines * 13 + 4);
            }
        }
    } else {
        // TAB_RECENT_LAUNCH Tab
        lockSatMutex();
        bool isLaunchEmpty = g_recentLaunches.empty();
        int totalItems = g_recentLaunches.size();
        unlockSatMutex();
        
        if (!recentLaunchDownloadSuccess && isLaunchEmpty) {
            if (recentLaunchDownloading) {
                canvas->setTextColor(TFT_YELLOW);
                canvas->drawString(I18N::get(TXT_DOWNLOADING_GP_JSONS), width/2 - canvas->textWidth(I18N::get(TXT_DOWNLOADING_GP_JSONS))/2, height/2 - 10);
                if (recentLaunchErrorMsg.length() > 0) {
                    canvas->setTextColor(TFT_LIGHTGRAY);
                    canvas->drawString(recentLaunchErrorMsg.c_str(), width/2 - canvas->textWidth(recentLaunchErrorMsg.c_str())/2, height/2 + 5);
                }
            } else {
                canvas->fillRect(10, 30, width - 20, height - 55, canvas->color565(35, 45, 55));
                canvas->drawRect(10, 30, width - 20, height - 55, TFT_YELLOW);
                
                canvas->setTextColor(TFT_YELLOW);
                canvas->drawString(I18N::get(TXT_RL_ONLINE_FEATURE), width/2 - canvas->textWidth(I18N::get(TXT_RL_ONLINE_FEATURE))/2, height/2 - 20);
                canvas->setTextColor(TFT_WHITE);
                canvas->drawString(I18N::get(TXT_PRESS_W_CONNECT_WIFI), width/2 - canvas->textWidth(I18N::get(TXT_PRESS_W_CONNECT_WIFI))/2, height/2 - 2);
                canvas->drawString(I18N::get(TXT_DOWNLOAD_LATEST_GROUPS), width/2 - canvas->textWidth(I18N::get(TXT_DOWNLOAD_LATEST_GROUPS))/2, height/2 + 8);
                
                if (recentLaunchErrorMsg.length() > 0) {
                    canvas->setTextColor(TFT_RED);
                    canvas->drawString(recentLaunchErrorMsg.c_str(), width/2 - canvas->textWidth(recentLaunchErrorMsg.c_str())/2, height/2 + 22);
                }
            }
        } else {
            int yPos = 25;
            int itemsPerPage = showBanner ? 6 : 8;
            int itemSpacing = 12;
            int startIndex = (recentLaunchSelectedIndex / itemsPerPage) * itemsPerPage;
            
            for (int i = 0; i < itemsPerPage && (startIndex + i) < totalItems; i++) {
                int index = startIndex + i;
                if (index == recentLaunchSelectedIndex) {
                    canvas->fillRect(2, yPos - 1, 82, 12, canvas->color565(0, 120, 255));
                    canvas->setTextColor(TFT_WHITE);
                } else {
                    canvas->setTextColor(TFT_LIGHTGRAY);
                }
                
                lockSatMutex();
                bool itemSel = (index < (int)g_recentLaunches.size()) ? g_recentLaunches[index].selected : false;
                String nameStr = (index < (int)g_recentLaunches.size()) ? g_recentLaunches[index].displayName : "";
                bool itemIsGroup = (index < (int)g_recentLaunches.size()) ? g_recentLaunches[index].isGroup : false;
                int itemSatCnt = (index < (int)g_recentLaunches.size()) ? g_recentLaunches[index].satelliteCount : 0;
                unlockSatMutex();

                String checkBox = itemSel ? "[x]" : "[ ]";
                canvas->drawString(checkBox.c_str(), 4, yPos);
                
                if (itemIsGroup) {
                    nameStr = nameStr + " (" + String(itemSatCnt) + ")";
                }
                if (index == recentLaunchSelectedIndex) {
                    drawScrollingText(canvas, nameStr.c_str(), 28, yPos, 56, TFT_WHITE);
                } else {
                    if (nameStr.length() > 9) nameStr = nameStr.substring(0, 7) + "..";
                    canvas->drawString(nameStr.c_str(), 28, yPos);
                }
                
                yPos += itemSpacing;
            }
            
            // Draw page index indicator for Recent Launch
            if (totalItems > 0) {
                int totalCount = totalItems;
                int totalPages = (totalCount + itemsPerPage - 1) / itemsPerPage;
                int currentPage = (recentLaunchSelectedIndex / itemsPerPage) + 1;
                int currentIdx = recentLaunchSelectedIndex + 1;
                char pageBuf[32];
                sprintf(pageBuf, "(%d %d/%d)", currentIdx, currentPage, totalPages);
                canvas->setTextColor(canvas->color565(110, 150, 180));
                canvas->drawString(pageBuf, 28, showBanner ? (bottomLimit - 12) : (bottomLimit - 13));
            }
            
            canvas->drawFastVLine(85, 20, bottomLimit - 20, TFT_DARKGREY);
            
            int rightX = 89;
            lockSatMutex();
            RecentLaunchItem itemCopy;
            bool hasSelectedItem = false;
            if (recentLaunchSelectedIndex >= 0 && recentLaunchSelectedIndex < (int)g_recentLaunches.size()) {
                itemCopy = g_recentLaunches[recentLaunchSelectedIndex];
                hasSelectedItem = true;
            }
            unlockSatMutex();

            if (hasSelectedItem) {
                RecentLaunchItem& item = itemCopy;
                
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
                    int y0 = (I18N::getLanguage() == LANG_ZH && !showBanner) ? 22 : 23;
                    auto getY = [&](int k) -> int {
                        if (I18N::getLanguage() == LANG_ZH && !showBanner) {
                            if (k <= 6) return y0 + k * 11;
                            if (k == 7) return y0 + 6 * 11 + 12; // 88 + 12 = 100
                            if (k == 8) return y0 + 6 * 11 + 24; // 88 + 24 = 112
                            if (k == 9) return y0 + 6 * 11 + 36; // 88 + 36 = 124
                        }
                        int localStep = (I18N::getLanguage() == LANG_ZH && !showBanner) ? 11 : 10;
                        return y0 + k * localStep;
                    };
                    
                    int starsW = canvas->textWidth(stars);
                    drawScrollingText(canvas, item.displayName.c_str(), rightX, getY(0), width - rightX - starsW - 8, TFT_GOLD);
                    
                    canvas->setTextColor(TFT_YELLOW);
                    canvas->drawString(stars, width - starsW - 4, getY(0));
                    
                    canvas->setTextColor(TFT_CYAN);
                    String formattedBatch = item.batchId;
                    if (item.batchId.length() == 5 && isdigit(item.batchId[0]) && isdigit(item.batchId[1])) {
                        int yr = item.batchId.substring(0, 2).toInt();
                        String century = (yr >= 50) ? "19" : "20";
                        formattedBatch = century + item.batchId.substring(0, 2) + "-" + item.batchId.substring(2);
                    }
                    canvas->drawString(("Batch: " + formattedBatch).c_str(), rightX, getY(1));
                    
                    // Age placement on the right
                    char ageBuf[32];
                    uint16_t ageColor = TFT_GREEN;
                    if (ageDays < 0) {
                        sprintf(ageBuf, "%sN/A", I18N::get(TXT_RL_AGE));
                        ageColor = TFT_RED;
                    } else {
                        sprintf(ageBuf, "%s%dd", I18N::get(TXT_RL_AGE), ageDays);
                        if (ageDays <= 7) ageColor = TFT_GREEN;
                        else if (ageDays <= 14) ageColor = TFT_ORANGE;
                        else ageColor = TFT_RED;
                    }
                    canvas->setTextColor(ageColor);
                    int ageW = canvas->textWidth(ageBuf);
                    canvas->drawString(ageBuf, width - ageW - 4, getY(1));
                    
                    char dateBuf[32];
                    if (epoch > 0) {
                        time_t tEpoch = (time_t)epoch;
                        struct tm* timeinfo = gmtime(&tEpoch);
                        strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", timeinfo);
                    } else {
                        sprintf(dateBuf, "N/A");
                    }
                    canvas->setTextColor(TFT_LIGHTGRAY);
                    canvas->drawString((String(I18N::get(TXT_RL_EPOCH)) + String(dateBuf)).c_str(), rightX, getY(2));
                    
                    // Draw representative satellite name
                    String repSatText = String(I18N::get(TXT_RL_REP)) + item.repSatName;
                    drawScrollingText(canvas, repSatText.c_str(), rightX, getY(3), width - rightX - 4, TFT_LIGHTGRAY);
                    
                    // Display real count of objects and clustered proxies
                    char satsBuf[64];
                    int proxyCount = item.proxyFormation.size();
                    sprintf(satsBuf, "%s%d | Proxy: %d", I18N::get(TXT_RL_OBJECTS), item.satelliteCount, proxyCount);
                    canvas->drawString(satsBuf, rightX, getY(4));
                    
                    char orbitBuf[48];
                    sprintf(orbitBuf, "%s%dkm, %.1f*", I18N::get(TXT_RL_ORBIT), (int)avgAlt, inclination);
                    canvas->drawString(orbitBuf, rightX, getY(5));
                    
                    // Formation State & Occupancy degree
                    canvas->setTextColor(TFT_GREEN);
                    canvas->drawString(I18N::get(TXT_RL_STATUS), rightX, getY(6));
                    canvas->setTextColor(TFT_WHITE);
                    const char* formState = I18N::get(TXT_FORM_OPERATIONAL);
                    if (item.occupancy < 15.0f) formState = I18N::get(TXT_FORM_TIGHT_TRAIN);
                    else if (item.occupancy < 60.0f) formState = I18N::get(TXT_FORM_TRAIN_FORMATION);
                    else if (item.occupancy < 120.0f) formState = I18N::get(TXT_FORM_EXPANDING);
                    canvas->drawString(formState, rightX + 45, getY(6));
                    
                    char occBuf[32];
                    sprintf(occBuf, "Occ: %d*", (int)item.occupancy);
                    canvas->setTextColor(TFT_CYAN);
                    int occW = canvas->textWidth(occBuf);
                    canvas->drawString(occBuf, width - occW - 4, getY(8));
                    
                    // Distribution indicator (8 refined blocks)
                    canvas->setTextColor(TFT_GREEN);
                    canvas->drawString("Distribution:", rightX, getY(7));
                    
                    int barY = getY(8);
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
                    canvas->drawString(I18N::get(TXT_RL_VISIBILITY), rightX, getY(9));
                    canvas->setTextColor(TFT_YELLOW);
                    if (avgAlt >= 250 && avgAlt <= 600) {
                        canvas->drawString(I18N::get(TXT_VIS_EXCELLENT), rightX + 65, getY(9));
                    } else if (avgAlt > 0) {
                        canvas->drawString(I18N::get(TXT_VIS_MODERATE), rightX + 65, getY(9));
                    } else {
                        canvas->drawString(I18N::get(TXT_VIS_NA), rightX + 65, getY(9));
                    }
                } else {
                    String title = item.displayName + " Objects";
                    drawScrollingText(canvas, title.c_str(), rightX, 25, width - rightX - 4, TFT_GOLD);
                    
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
                        if (satName.startsWith("STARLINK ")) {
                            satName = "SL " + satName.substring(9);
                        }
                        String lineText = "- " + satName + " (" + String(obj.orbit.catalogNumber) + ")";
                        int maxW = obj.lastGeoValid ? (width - rightX - 40) : (width - rightX - 4);
                        drawScrollingText(canvas, lineText.c_str(), rightX, memY + s * 13, maxW, TFT_LIGHTGRAY);
                        
                        if (obj.lastGeoValid) {
                            char hBuf[16];
                            sprintf(hBuf, "%dkm", (int)obj.lastGeo.alt);
                            canvas->setTextColor(TFT_GREEN);
                            canvas->drawString(hBuf, width - 36, memY + s * 13);
                        }
                    }
                    
                    if (g_level3Objects.empty()) {
                        canvas->setTextColor(TFT_RED);
                        canvas->drawString(I18N::get(TXT_NO_OBJECTS_FOUND), rightX, memY);
                    }
                }
            }
        }
    }
    
    // Draw Bottom Guide Banner (Unified design & color logic)
    if (showBanner) {
        canvas->fillRect(0, height - 13, width, 13, canvas->color565(15, 20, 25));
        canvas->drawFastHLine(0, height - 13, width, TFT_DARKGREY);
        
        String msg = "";
        uint16_t textColor = TFT_LIGHTGRAY;
        
        if (currentSatTab == TAB_RECENT_LAUNCH) {
            if (recentLaunchDownloading) {
                textColor = TFT_YELLOW;
                msg = String(I18N::get(TXT_TAB_RECENT_LAUNCH)) + " " + I18N::get(TXT_DOWNLOADING) + " " + recentLaunchErrorMsg;
            } else if (recentLaunchDownloadSuccess) {
                textColor = TFT_GREEN;
                msg = I18N::get(TXT_UPDATE_SUCCESS_CACHE);
            } else {
                textColor = getBannerTextColor(recentLaunchErrorMsg);
                msg = (recentLaunchErrorMsg.indexOf("Busy") != -1 || recentLaunchErrorMsg.indexOf(u8"繁忙") != -1) ? 
                      recentLaunchErrorMsg : (String(I18N::get(TXT_UPDATE_FAILED)) + recentLaunchErrorMsg);
            }
        } else if (currentSatTab == TAB_ENCYCLOPEDIA) {
            msg = downloadErrorMsg;
            textColor = getBannerTextColor(downloadErrorMsg);
        }
        
        if (msg.length() > 0) {
            canvas->setTextColor(textColor);
            if (canvas->textWidth(msg.c_str()) > width - 8) {
                msg = msg.substring(0, 35) + "...";
            }
            canvas->drawString(msg.c_str(), 4, height - 12);
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
        uint16_t w = 216, h = 126;
        int x = (width - w) / 2;
        int y = (height - h) / 2;
        
        canvas->fillRect(x, y, w, h, canvas->color565(20, 30, 40));
        canvas->drawRect(x, y, w, h, TFT_LIGHTGRAY);
        
        bool isZh = (I18N::getLanguage() == LANG_ZH);
        canvas->setTextColor(TFT_WHITE);
        canvas->setTextSize(1);
        canvas->drawString(isZh ? "--- 列表快捷键指南 ---" : "--- Setup Shortcuts ---", x + 35, y + 5);

        auto drawHotKey = [&](const char* word, char keyChar, int dx, int dy) {
            int cx = dx;
            bool highlighted = false;
            int i = 0;
            while (word[i] != '\0') {
                int charLen = 1;
                unsigned char head = (unsigned char)word[i];
                if (head >= 0xF0) charLen = 4;
                else if (head >= 0xE0) charLen = 3;
                else if (head >= 0xC0) charLen = 2;
                
                char cstr[5] = {0};
                for (int j = 0; j < charLen && word[i + j] != '\0'; j++) {
                    cstr[j] = word[i + j];
                }
                
                if (charLen == 1 && !highlighted && tolower((unsigned char)cstr[0]) == tolower((unsigned char)keyChar) && keyChar != '\0') {
                    canvas->setTextColor(TFT_YELLOW);
                    highlighted = true;
                } else {
                    canvas->setTextColor(TFT_LIGHTGRAY);
                }
                
                canvas->drawString(cstr, cx, dy);
                cx += canvas->textWidth(cstr);
                i += charLen;
            }
        };
        
        int ty = y + 20;
        if (currentSatTab == TAB_ENCYCLOPEDIA) {
            drawHotKey(isZh ? "移动[ ; / . ]" : "Move[ ; / . ]", ';', x + 8, ty);
            drawHotKey(isZh ? "切分类[/]" : "Tab[/]", '/', x + 112, ty); ty += 14;
            
            drawHotKey(isZh ? "详情翻页[ [/] ]" : "Page[ [/] ]", '[', x + 8, ty);
            drawHotKey(isZh ? "勾选[Enter]" : "Select[Enter]", 'e', x + 112, ty); ty += 14;
            
            drawHotKey(isZh ? "删除自定[d]" : "Del Custom[d]", 'd', x + 8, ty);
            drawHotKey(isZh ? "刷新星历[c]" : "Refresh GP[c]", 'c', x + 112, ty); ty += 14;
            
            drawHotKey(isZh ? "开关WiFi[w]" : "WiFi[w]", 'w', x + 8, ty);
            drawHotKey(isZh ? "主题模式[Tab]" : "Theme[Tab]", 't', x + 112, ty); ty += 14;
            
            drawHotKey(isZh ? "返回地图[Esc]" : "Exit[Esc]", 'x', x + 8, ty); ty += 14;
        } else {
            if (recentLaunchInObjectsView) {
                drawHotKey(isZh ? "清单翻页[ [/] ]" : "Page[ [/] ]", '[', x + 8, ty);
                drawHotKey(isZh ? "主题模式[Tab]" : "Theme[Tab]", 't', x + 112, ty); ty += 14;

                drawHotKey(isZh ? "退出清单[Esc/o]" : "Back List[Esc/o]", 'o', x + 8, ty); ty += 14;
            } else {
                drawHotKey(isZh ? "移动[ ; / . ]" : "Move[ ; / . ]", ';', x + 8, ty);
                drawHotKey(isZh ? "切分类[/]" : "Tab[/]", '/', x + 112, ty); ty += 14;
                
                drawHotKey(isZh ? "勾选[Enter]" : "Select[Enter]", 'e', x + 8, ty);
                drawHotKey(isZh ? "展开清单[o]" : "Sub-List[o]", 'o', x + 112, ty); ty += 14;
                
                drawHotKey(isZh ? "云端更新[w/c]" : "Update[w/c]", 'c', x + 8, ty);
                drawHotKey(isZh ? "开关WiFi[w]" : "WiFi[w]", 'w', x + 112, ty); ty += 14;
                
                drawHotKey(isZh ? "主题模式[Tab]" : "Theme[Tab]", 't', x + 8, ty);
                drawHotKey(isZh ? "返回地图[Esc]" : "Exit[Esc]", 'x', x + 112, ty); ty += 14;
            }
        }
        
        canvas->setTextColor(TFT_YELLOW);
        canvas->drawString(isZh ? "按任意键关闭" : "Press any key to Close", x + 35, y + h - 14);
    }
}

void loop() {
    // Resume suspended predictorTask after 500ms debounce of time machine adjustments
    if (lastTimeAdjustMillis != 0 && millis() - lastTimeAdjustMillis > 500) {
        lastTimeAdjustMillis = 0;
        if (predictorTaskHandle != NULL) {
            // Suspended check removed in cooperative mode
            
            // Only perform day-crossing prediction checks if the recommended passes panel is actually open
            if (showRecommendations) {
                // Check if timezone adjusted day boundary is crossed
                uint32_t targetTime = current_unix + timeMachineOffset;
                bool isCacheValid = false;
                lockPassMutex();
                uint32_t baseTime = 0;
                if (predictionsReady && lastPredictionBaseTime != 0) {
                    baseTime = lastPredictionBaseTime;
                } else if (g_orbitCalculating && g_currentPredictingBaseTime != 0) {
                    baseTime = g_currentPredictingBaseTime;
                }
                
                if (baseTime != 0) {
                    int tzOffsetSec = pos_manager ? pos_manager->getTimezoneManager()->getTimezoneOffset(baseUserLat, baseUserLon) : ((int)round(baseUserLon / 15.0) * 3600);
                    uint32_t day1 = (baseTime + tzOffsetSec) / 86400;
                    uint32_t day2 = (targetTime + tzOffsetSec) / 86400;
                    if (day1 == day2) {
                        isCacheValid = true;
                    }
                }
                unlockPassMutex();
                
                if (!isCacheValid) {
                    Serial.printf("[Debug] Time Machine resumed but cache invalid (day crossed). Resetting prediction. baseTime=%u, targetTime=%u\n", baseTime, targetTime);
                    lockPassMutex();
                    predictionsReady = false;
                    lastPredictionBaseTime = 0; // Invalid cache
                    g_currentPredictingBaseTime = 0;
                    cancelPrediction = true; // 跨天时必须打断当前进行的计算并重算
                    unlockPassMutex();
                    triggerPrediction = true;
                } else {
                    Serial.printf("[Debug] Time Machine resumed, cache is valid (same day). Continuing calculation or keeping cache. baseTime=%u\n", baseTime);
                }
            } else {
                Serial.println("[Debug] Time Machine resumed. Panel closed, skipping cross-day recalculation checks.");
            }
        }
    }

    // Debug helper to clear update timestamp via serial
    if (Serial.available()) {
        char debugChar = Serial.read();
        if (debugChar == 'c' || debugChar == 'C') {
            if (LittleFS.exists("/recent_last_update.txt")) {
                LittleFS.remove("/recent_last_update.txt");
                LOG_I("APP", "Local update timestamp cleared! Rate limit bypassed.");
            } else {
                LOG_I("APP", "No timestamp file found. Ready to download.");
            }
        }
    }

    // Sync coordinates and manual mode from pos_manager to main.cpp global variables
    if (pos_manager) {
        PositionData currentPos = pos_manager->getPosition();
        double oldLat = baseUserLat;
        double oldLon = baseUserLon;
        double oldAlt = baseUserAlt;
        
        baseUserLat = currentPos.latitude;
        baseUserLon = currentPos.longitude;
        baseUserAlt = currentPos.altitude;
        isManualLocationMode = pos_manager->isManualPositionEnabled();
        
        if (abs(baseUserLat - oldLat) > 0.01 || abs(baseUserLon - oldLon) > 0.01 || abs(baseUserAlt - oldAlt) > 100.0) {
            Serial.printf("[Debug] Cache reset due to main loop coords change: oldLat=%f, newLat=%f, oldLon=%f, newLon=%f, oldAlt=%f, newAlt=%f\n", 
                          oldLat, baseUserLat, oldLon, baseUserLon, oldAlt, baseUserAlt);
            lockPassMutex();
            lastPredictionBaseTime = 0; // 缓存失效
            predictionsReady = false;
            unlockPassMutex();
            if (showRecommendations) {
                triggerPrediction = true;
            }
        }
    }

    if (g_recentLaunchRefreshPending) {
        g_recentLaunchRefreshPending = false;
        recentLaunchDownloading = false; // Reset downloading flag early to unlock file reads for loading
        
        std::vector<RecentLaunchItem>* tempLaunches = new std::vector<RecentLaunchItem>();
        if (tempLaunches && OrbitDataProvider::loadRecentLaunchesFromCache(*tempLaunches) && !tempLaunches->empty()) {
            std::sort(tempLaunches->begin(), tempLaunches->end(), [](const RecentLaunchItem& a, const RecentLaunchItem& b) {
                auto getTrueYearAndNum = [](const String& id) -> std::pair<int, int> {
                    if (id.length() < 5) return {0, 0};
                    int yr = id.substring(0, 2).toInt();
                    int trueYr = (yr >= 50) ? (1900 + yr) : (2000 + yr);
                    int num = id.substring(2).toInt();
                    return {trueYr, num};
                };
                auto valA = getTrueYearAndNum(a.batchId);
                auto valB = getTrueYearAndNum(b.batchId);
                if (valA.first != valB.first) {
                    return valA.first > valB.first;
                }
                return valA.second > valB.second;
            });
            
            calculateFormationsForItems(*tempLaunches);
            g_recentLaunches = std::move(*tempLaunches);
            
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
            recentLaunchSelectedIndex = 0;
            recentLaunchDownloadSuccess = true;
            if (recentLaunchBypassed) {
                recentLaunchErrorMsg = I18N::get(TXT_RL_CACHED_LIMIT);
            } else {
                recentLaunchErrorMsg = I18N::get(TXT_UPDATE_SUCCESS_CACHE);
            }
            LOG_I("APP", "Applied new recent launches safely on main core.");
        } else {
            recentLaunchErrorMsg = I18N::get(TXT_PARSE_CACHE_FAILED);
        }
        delete tempLaunches;
        recentLaunchDownloadFinishedMs = millis();
    }

    M5Cardputer.update();
    bool isFastForwarding = (lastTimeAdjustMillis != 0) || showRecommendations;
    g_isFastForwarding = isFastForwarding;

    // BtnG0 (side button): trigger screenshot transfer via serial
    if (M5Cardputer.BtnA.wasPressed()) {
        doScreenshot();
    }

    // 手动调整时间/快进期间暂停 GNSS 串口解析，防止串口中断抢占 CPU/I2C 总线
    if (gnss && !g_isFastForwarding) {
        gnss->update();
    }

    // Render at 30 FPS (33ms)
    if (millis() - last_update >= 33) {
        last_update = millis();
        
        // Action keys state from last frame (for edge detection / single-press action)
        static bool lastSemi = false;
        static bool lastDot = false;
        static bool lastComma = false;
        static bool lastSlash = false;
        static bool lastO = false;
        static bool lastV = false;
        static bool lastEnter = false;
        static bool lastBack = false;
        static bool lastEsc = false;
        static bool lastTick = false;
        static bool lastBracketL = false;
        static bool lastBracketR = false;
        static bool lastC = false;
        static bool lastR = false;
        static bool lastW = false;
        static bool lastS = false;
        static bool lastH = false;
        static bool lastG = false;
        static bool lastY = false;
        static bool lastN = false;
        static bool lastD = false;
        static bool lastTab = false;
        static bool lastL = false;
        static bool lastSpace = false;

        bool currSemi = M5Cardputer.Keyboard.isKeyPressed(';');
        bool currDot = M5Cardputer.Keyboard.isKeyPressed('.');
        bool currComma = M5Cardputer.Keyboard.isKeyPressed(',');
        bool currSlash = M5Cardputer.Keyboard.isKeyPressed('/');
        bool currO = M5Cardputer.Keyboard.isKeyPressed('o') || M5Cardputer.Keyboard.isKeyPressed('O');
        bool currV = M5Cardputer.Keyboard.isKeyPressed('v') || M5Cardputer.Keyboard.isKeyPressed('V');
        bool currEnter = M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER);
        bool currBack = M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE);
        bool currEsc = M5Cardputer.Keyboard.isKeyPressed(27);  // ESC
        bool currTick = M5Cardputer.Keyboard.isKeyPressed('`');
        bool currBracketL = M5Cardputer.Keyboard.isKeyPressed('[');
        bool currBracketR = M5Cardputer.Keyboard.isKeyPressed(']');
        bool currC = M5Cardputer.Keyboard.isKeyPressed('c') || M5Cardputer.Keyboard.isKeyPressed('C');
        bool currR = M5Cardputer.Keyboard.isKeyPressed('r') || M5Cardputer.Keyboard.isKeyPressed('R');
        bool currW = M5Cardputer.Keyboard.isKeyPressed('w') || M5Cardputer.Keyboard.isKeyPressed('W');
        bool currS = M5Cardputer.Keyboard.isKeyPressed('s') || M5Cardputer.Keyboard.isKeyPressed('S');
        bool currH = M5Cardputer.Keyboard.isKeyPressed('h') || M5Cardputer.Keyboard.isKeyPressed('H');
        bool currG = M5Cardputer.Keyboard.isKeyPressed('g') || M5Cardputer.Keyboard.isKeyPressed('G');
        bool currY = M5Cardputer.Keyboard.isKeyPressed('y') || M5Cardputer.Keyboard.isKeyPressed('Y');
        bool currN = M5Cardputer.Keyboard.isKeyPressed('n') || M5Cardputer.Keyboard.isKeyPressed('N');
        bool currD = M5Cardputer.Keyboard.isKeyPressed('d') || M5Cardputer.Keyboard.isKeyPressed('D');
        bool currTab = M5Cardputer.Keyboard.isKeyPressed(KEY_TAB);
        bool currL = M5Cardputer.Keyboard.isKeyPressed('l') || M5Cardputer.Keyboard.isKeyPressed('L');
        bool currSpace = M5Cardputer.Keyboard.isKeyPressed(' ');

        bool justSemi = currSemi && !lastSemi;
        bool justDot = currDot && !lastDot;
        bool justComma = currComma && !lastComma;
        bool justSlash = currSlash && !lastSlash;
        bool justO = currO && !lastO;
        bool justV = currV && !lastV;
        bool justEnter = currEnter && !lastEnter;
        bool justBack = currBack && !lastBack;
        bool justEsc = currEsc && !lastEsc;
        bool justTick = currTick && !lastTick;
        bool justBracketL = currBracketL && !lastBracketL;
        bool justBracketR = currBracketR && !lastBracketR;
        bool justC = currC && !lastC;
        bool justR = currR && !lastR;
        bool justW = currW && !lastW;
        bool justS = currS && !lastS;
        bool justH = currH && !lastH;
        bool justG = currG && !lastG;
        bool justY = currY && !lastY;
        bool justN = currN && !lastN;
        bool justD = currD && !lastD;
        bool justTab = currTab && !lastTab;
        bool justL = currL && !lastL;
        bool justSpace = currSpace && !lastSpace;
        bool hasAnyKeyJustPressed = justSemi || justDot || justComma || justSlash || justO || justV || justEnter || justBack || justEsc || justTick || justBracketL || justBracketR || justC || justR || justW || justS || justH || justG || justY || justN || justD || justTab || justL || justSpace;

        if (showHelp) {
            if (millis() < 3000) {
                showHelp = false;
            } else if (hasAnyKeyJustPressed) {
                showHelp = false;
                currSemi = currDot = currComma = currSlash = currO = currV = currEnter = currBack = currEsc = currTick = currBracketL = currBracketR = currC = currR = currW = currS = currH = currG = currY = currN = currD = currTab = currL = currSpace = false;
                justSemi = justDot = justComma = justSlash = justO = justV = justEnter = justBack = justEsc = justTick = justBracketL = justBracketR = justC = justR = justW = justS = justH = justG = justY = justN = justD = justTab = false;
                hasAnyKeyJustPressed = false;
            }
        }
        
        if (showListHelp) {
            if (millis() < 3000) {
                showListHelp = false;
            } else if (hasAnyKeyJustPressed) {
                showListHelp = false;
                currSemi = currDot = currComma = currSlash = currO = currV = currEnter = currBack = currEsc = currTick = currBracketL = currBracketR = currC = currR = currW = currS = currH = currG = currY = currN = currD = currTab = false;
                justSemi = justDot = justComma = justSlash = justO = justV = justEnter = justBack = justEsc = justTick = justBracketL = justBracketR = justC = justR = justW = justS = justH = justG = justY = justN = justD = justTab = false;
                hasAnyKeyJustPressed = false;
            }
        }

        // Handle continuous keyboard input (Time Machine or Manual Location)
        static unsigned long keyHoldStartTime = 0;
        static char lastKey = 0;
        static unsigned long lastKeyRepeat = 0;
        isFastForwarding = (lastTimeAdjustMillis != 0) || showRecommendations;
        static double targetFocusAlt = 0.0;
        
        if (appState == STATE_MAIN) {
            char currentKey = 0;
            if (M5Cardputer.Keyboard.isKeyPressed(KEY_TAB)) {
                // Tab key: do nothing here, handled as discrete key justTab
            }
            else if (M5Cardputer.Keyboard.isKeyPressed(',')) currentKey = ',';
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
                    if (key == ',' || key == '/') {
                        lastTimeAdjustMillis = millis();
                        if (predictorTaskHandle != NULL) {
                            // Suspended check removed in cooperative mode
                        }
                    }
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
                        lockPassMutex();
                        lastPredictionBaseTime = 0; // 缓存失效
                        predictionsReady = false;
                        unlockPassMutex();
                        
                        if (pos_manager) {
                            PositionData pos = {baseUserLat, baseUserLon, baseUserAlt};
                            pos_manager->setManualPosition(pos);
                        }
                        Preferences posPrefs;
                        if (posPrefs.begin("position", false)) {
                            posPrefs.putDouble("cached_lat", baseUserLat);
                            posPrefs.putDouble("cached_lon", baseUserLon);
                            posPrefs.putDouble("cached_alt", baseUserAlt);
                            posPrefs.putBool("use_manual_pos", true);
                            posPrefs.end();
                        }
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
                    if (millis() - keyHoldStartTime > 300) { // 300ms delay before repeat
                        if (millis() - lastKeyRepeat > 33) { // ~30Hz repeat rate
                            lastKeyRepeat = millis();
                            handleContinuousKey(currentKey);
                        }
                    }
                }
            } else {
                if (lastKey != 0) {
                    if (keyReleaseTime == 0) keyReleaseTime = millis();
                    if (millis() - keyReleaseTime < 120) {
                        // 在 120ms 的 I2C 消抖窗口内，平滑保持上一有效按键的连续响应，防止 UART 中断导致断线卡顿
                        if (millis() - keyHoldStartTime > 300) {
                            if (millis() - lastKeyRepeat > 33) {
                                lastKeyRepeat = millis();
                                handleContinuousKey(lastKey);
                            }
                        }
                    } else {
                        lastKey = 0;
                    }
                }
            }
        }

        
        // Handle discrete keyboard input
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            if (appState == STATE_MAIN) {
                if (justTab) {
                    int nextMode = (earth_renderer->getVisualMode() + 1) % 3;
                    earth_renderer->setVisualMode(nextMode);
                } else if (justC) {
                    if (isSatViewMode) {
                        isSatViewMode = false;
                        targetZoom = 0.95f;
                    }

                    isManualLocationMode = !isManualLocationMode;
                    if (pos_manager) {
                        if (isManualLocationMode) {
                            PositionData currentPos = {baseUserLat, baseUserLon, baseUserAlt};
                            pos_manager->setManualPosition(currentPos);
                        } else {
                            // Sync manually adjusted coordinates to main active coordinates to prevent rollback
                            PositionData manualPos = pos_manager->getPosition();
                            pos_manager->setPosition(manualPos);
                        }
                        pos_manager->enableManualPosition(isManualLocationMode);
                    }
                    Preferences posPrefs;
                    if (posPrefs.begin("position", false)) {
                        posPrefs.putBool("use_manual_pos", isManualLocationMode);
                        posPrefs.end();
                    }
                    if (!isManualLocationMode) {
                        lockPassMutex();
                        predictionsReady = false;
                        lastPredictionBaseTime = 0; // 缓存失效
                        unlockPassMutex();
                        triggerPrediction = true;
                    }
                } else if (justR) {
                    if (!showRecommendations && !showHelp) {
                        // 1. Evaluate if time crosses a day boundary
                        uint32_t beforeTime = current_unix + timeMachineOffset;
                        uint32_t afterTime = current_unix;
                        bool timeCrossedDay = false;
                        int tzOffsetSec = pos_manager ? pos_manager->getTimezoneManager()->getTimezoneOffset(baseUserLat, baseUserLon) : ((int)round(baseUserLon / 15.0) * 3600);
                        uint32_t day1 = (beforeTime + tzOffsetSec) / 86400;
                        uint32_t day2 = (afterTime + tzOffsetSec) / 86400;
                        if (day1 != day2) {
                            timeCrossedDay = true;
                        }

                        // 2. Evaluate if location shifted significantly
                        bool locShifted = false;
                        if (isManualLocationMode) {
                            if (abs(baseUserLat - 39.90) > 0.01 || 
                                abs(baseUserLon - 116.40) > 0.01 || 
                                abs(baseUserAlt - 0.0) > 100.0) {
                                locShifted = true;
                            }
                        }

                        // 3. Apply reset actions
                        timeMachineOffset = 0;
                        if (isManualLocationMode) {
                            baseUserLat = 39.90; // Beijing default
                            baseUserLon = 116.40;
                            baseUserAlt = 0.0;
                            
                            if (pos_manager) {
                                PositionData pos = {baseUserLat, baseUserLon, baseUserAlt};
                                pos_manager->setManualPosition(pos);
                            }
                            Preferences posPrefs;
                            if (posPrefs.begin("position", false)) {
                                posPrefs.putDouble("cached_lat", baseUserLat);
                                posPrefs.putDouble("cached_lon", baseUserLon);
                                posPrefs.putDouble("cached_alt", baseUserAlt);
                                posPrefs.end();
                            }
                        }

                        // 4. Only recalculate if difference is beyond thresholds
                        if (timeCrossedDay || locShifted) {
                            Serial.printf("[Debug] Cache reset on justR: timeCrossedDay=%d, locShifted=%d\n", timeCrossedDay, locShifted);
                            lockPassMutex();
                            lastPredictionBaseTime = 0; // 缓存失效
                            predictionsReady = false;
                            unlockPassMutex();
                            triggerPrediction = true;
                        } else {
                            Serial.println("[Debug] justR reset applied silently. Coords/Time shift within thresholds.");
                        }
                    }
                } else if (justBack) {
                    showHud = !showHud;
                } else if (justEsc || justTick) {
                    if (showRecommendations) {
                        if (selectedPassIndex != -1) {
                            selectedPassIndex = -1; // Back to tree
                        } else {
                            showRecommendations = false; // Close panel
                            if (predictorTaskHandle != NULL) {
                                vTaskPrioritySet(predictorTaskHandle, 1);
                            }
                        }
                    } else if (showHelp) {
                        showHelp = false;
                    } else if (isManualLocationMode) {
                        isManualLocationMode = false;
                        if (pos_manager) {
                            // Sync manually adjusted coordinates to main active coordinates to prevent rollback
                            PositionData manualPos = pos_manager->getPosition();
                            pos_manager->setPosition(manualPos);
                            pos_manager->enableManualPosition(false);
                        }
                        Preferences posPrefs;
                        if (posPrefs.begin("position", false)) {
                            posPrefs.putBool("use_manual_pos", false);
                            posPrefs.end();
                        }
                        lockPassMutex();
                        predictionsReady = false;
                        lastPredictionBaseTime = 0; // 缓存失效
                        unlockPassMutex();
                        triggerPrediction = true;
                    } else if (isSatViewMode) {
                        isSatViewMode = false;
                        targetZoom = 0.95f;
                    }

                } else if (justEnter) {
                    if (appState == STATE_MAIN && !showRecommendations) {
                        showRecommendations = true;
                        passScrollIndex = 0;
                        
                        uint32_t targetTime = current_unix + timeMachineOffset;
                        bool isCacheValid = false;
                        lockPassMutex();
                        uint32_t baseTime = 0;
                        if (predictionsReady && lastPredictionBaseTime != 0) {
                            baseTime = lastPredictionBaseTime;
                        } else if (g_orbitCalculating && g_currentPredictingBaseTime != 0) {
                            baseTime = g_currentPredictingBaseTime;
                        }
                        
                        if (baseTime != 0) {
                            int tzOffsetSec = pos_manager ? pos_manager->getTimezoneManager()->getTimezoneOffset(baseUserLat, baseUserLon) : ((int)round(baseUserLon / 15.0) * 3600);
                            uint32_t day1 = (baseTime + tzOffsetSec) / 86400;
                            uint32_t day2 = (targetTime + tzOffsetSec) / 86400;
                            if (day1 == day2) {
                                isCacheValid = true;
                            }
                        }
                        unlockPassMutex();
                        
                        Serial.printf("[Debug] Enter Panel: predictionsReady=%d, lastPredictionBaseTime=%u, targetTime=%u, isCacheValid=%d, g_orbitCalculating=%d, triggerPrediction=%d\n", 
                                      predictionsReady, lastPredictionBaseTime, targetTime, isCacheValid, g_orbitCalculating, triggerPrediction);

                        bool needTrigger = !isCacheValid || (!predictionsReady && !g_orbitCalculating);
                        if (needTrigger && !g_orbitCalculating && !triggerPrediction) {
                            Serial.printf("[Debug] Triggering calculation. needTrigger=%d, isCacheValid=%d, predictionsReady=%d\n", needTrigger, isCacheValid, predictionsReady);
                            lockPassMutex();
                            predictionsReady = false;
                            lastPredictionBaseTime = 0;
                            g_currentPredictingBaseTime = 0;
                            unlockPassMutex();
                            triggerPrediction = true;
                        }
                        
                        rebuildTree(current_unix + timeMachineOffset);
                    } else if (showRecommendations) {
                        if (selectedPassIndex != -1) {
                            // 获取当前选中的过境事件
                            lockPassMutex();
                            if (selectedPassIndex >= 0 && selectedPassIndex < (int)recommendedPasses.size()) {
                                const auto& pass = recommendedPasses[selectedPassIndex];
                                
                                // 时间机器跳转到事件开始时间 (AOS)
                                timeMachineOffset = (int32_t)pass.aosTime - (int32_t)current_unix;
                                lastTimeAdjustMillis = millis(); // 触发时间防抖与挂起
                                
                                // 关闭推荐面板，直接退回 3D 地球视角观察，不改动任何追焦/视角模式
                                showRecommendations = false;
                            }
                            selectedPassIndex = -1; // 重置详情索引
                            unlockPassMutex();
                        } else {
                            // Toggle category or open detail
                            if (passScrollIndex >= 0 && passScrollIndex < (int)displayTree.size()) {
                                auto& item = displayTree[passScrollIndex];
                                if (item.isCategory) {
                                    catExpanded[item.categoryIndex] = !catExpanded[item.categoryIndex];
                                    rebuildTree(current_unix + timeMachineOffset);
                                } else {
                                    selectedPassIndex = item.passIndex;
                                }
                            }
                        }
                    }
                } else if (justW) {
                    if (!g_networkActive) {
                        if (!HalWifi::isConnected()) {
                            manualWifiToggle = true;
                            BaseType_t res = xTaskCreatePinnedToCore(networkTask, "NetworkTask", 16384, NULL, 1, NULL, 0);
                            if (res != pdPASS) {
                                LOG_I("APP", "Failed to create NetworkTask! Free Heap: %u", (unsigned int)ESP.getFreeHeap());
                            }
                        } else {
                            WiFi.disconnect(true);
                            WiFi.mode(WIFI_OFF);
                        }
                    }
                } else if (justS) {
                    appState = STATE_SAT_SELECT;
                    currentSatTab = TAB_ENCYCLOPEDIA;
                    entrySelectedSatellites.clear();
                    for (int i = 0; i < NUM_SATELLITES; i++) {
                        if (g_satellites[i].selected) {
                            entrySelectedSatellites.push_back(g_satellites[i].noradId);
                        }
                    }
                } else if (justL) {
                    appState = STATE_LANG_SELECT;
                    langSelectedIndex = (I18N::getLanguage() == LANG_EN) ? 0 : 1;
                } else if (justH) {
                    showHelp = !showHelp;
                } else if (justG) {
                    if (gnss && gnss->isModuleInitialized()) {
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
                } else if (justV) {
                    isSatViewMode = !isSatViewMode;
                    if (isSatViewMode) {
                        validateSatViewFocusState();
                        if (isSatViewMode) {
                            isCameraTransitioning = true;
                            if (attitude && imu) {
                                AttitudeData att = attitude->getAttitude();
                                basePitch = att.pitch;
                                baseRoll = att.roll;
                            }
                        }
                    }
                } else if (justSpace) {
                    if (attitude && imu) {
                        AttitudeData att = attitude->getAttitude();
                        basePitch = att.pitch;
                        baseRoll = att.roll;
                    }
                } else if (justSemi) {
                    if (isSatViewMode && !showRecommendations) {
                        validateSatViewFocusState();
                        if (isSatViewMode) {
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
                                if (g_recentLaunchFocusMode) {
                                    for (size_t i = 0; i < targets.size(); i++) {
                                        if (targets[i].type == 1 && g_recentLaunches[targets[i].index].batchId == recentLaunchActiveBatchId) {
                                            currentIdx = i;
                                            break;
                                        }
                                    }
                                } else if (focusSatIndex >= 0 && focusSatIndex < NUM_SATELLITES && g_satellites[focusSatIndex].selected) {
                                    for (size_t i = 0; i < targets.size(); i++) {
                                        if (targets[i].type == 0 && targets[i].index == focusSatIndex) {
                                            currentIdx = i;
                                            break;
                                        }
                                    }
                                }
                                
                                // 兜底：若之前选中的目标被取消勾选，默认从 0 号目标开始切换
                                if (currentIdx == -1) {
                                    currentIdx = 0;
                                }

                                int prevIdx = (currentIdx - 1 + targets.size()) % targets.size();
                                const auto& prevTarget = targets[prevIdx];
                                if (prevTarget.type == 0) {
                                    focusSatIndex = prevTarget.index;
                                    g_recentLaunchFocusMode = false;
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
                } else if (justDot) {
                    if (isSatViewMode && !showRecommendations) {
                        validateSatViewFocusState();
                        if (isSatViewMode) {
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
                                if (g_recentLaunchFocusMode) {
                                    for (size_t i = 0; i < targets.size(); i++) {
                                        if (targets[i].type == 1 && g_recentLaunches[targets[i].index].batchId == recentLaunchActiveBatchId) {
                                            currentIdx = i;
                                            break;
                                        }
                                    }
                                } else if (focusSatIndex >= 0 && focusSatIndex < NUM_SATELLITES && g_satellites[focusSatIndex].selected) {
                                    for (size_t i = 0; i < targets.size(); i++) {
                                        if (targets[i].type == 0 && targets[i].index == focusSatIndex) {
                                            currentIdx = i;
                                            break;
                                        }
                                    }
                                }
                                
                                // 兜底：若之前选中的目标被取消勾选，默认从 0 号目标开始切换
                                if (currentIdx == -1) {
                                    currentIdx = 0;
                                }

                                int nextIdx = (currentIdx + 1) % targets.size();
                                const auto& nextTarget = targets[nextIdx];
                                if (nextTarget.type == 0) {
                                    focusSatIndex = nextTarget.index;
                                    g_recentLaunchFocusMode = false;
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
                if (justEsc || justTick) {
                    if (wifiIsInputtingPassword) {
                        wifiIsInputtingPassword = false;
                    } else {
                        exitWiFiSetupScreen();
                    }
                } else if (!wifiIsInputtingPassword && justBack) {
                    exitWiFiSetupScreen();
                } else if (wifiIsInputtingPassword) {
                    if (justEnter) {
                        if (!wifiNetworks.empty() && wifiSelectedIndex >= 0 && wifiSelectedIndex < (int)wifiNetworks.size()) {
                            // Connect
                            appState = g_wifiSetupReturnState;
                            NetworkParams* params = new NetworkParams();
                            params->ssid = wifiNetworks[wifiSelectedIndex].ssid;
                            params->pass = String(wifiPasswordBuffer);
                            params->shouldSave = true;
                            
                            wifiNetworks.clear();
                            wifiNetworks.shrink_to_fit();
                            wifiIsScanning = false;
                            wifiIsInputtingPassword = false;
                            
                            manualWifiToggle = true; // Stay connected since user explicitly set it up
                            BaseType_t res = xTaskCreatePinnedToCore(
                                networkTask, "NetworkTask", 16384, params, 1, NULL, 0
                            );
                            if (res != pdPASS) {
                                LOG_I("APP", "Failed to create NetworkTask! Free Heap: %u", (unsigned int)ESP.getFreeHeap());
                                delete params;
                            }
                        }
                    } else if (justBack) {
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
                    if (justR) {
                        wifiIsScanning = true;
                    } else if (justEnter) {
                        if (!wifiNetworks.empty()) {
                            wifiIsInputtingPassword = true;
                            memset(wifiPasswordBuffer, 0, sizeof(wifiPasswordBuffer));
                            wifiPasswordLen = 0;
                        }
                    } else if (justSemi) { // UP arrow
                        if (!wifiNetworks.empty()) {
                            if (wifiSelectedIndex > 0) wifiSelectedIndex--;
                            else wifiSelectedIndex = wifiNetworks.size() - 1;
                        }
                    } else if (justDot) { // DOWN arrow
                        if (!wifiNetworks.empty()) {
                            wifiSelectedIndex = (wifiSelectedIndex + 1) % wifiNetworks.size();
                        }
                    }
                }
            } else if (appState == STATE_SAT_SELECT) {
                if (showListHelp) {
                    if (justH || justEsc || justBack || justEnter || justTick) {
                        showListHelp = false;
                    }
                } else if (justTab) {
                    int nextMode = (earth_renderer->getVisualMode() + 1) % 3;
                    earth_renderer->setVisualMode(nextMode);
                } else if (deleteConfirmIndex >= 0 && currentSatTab == TAB_ENCYCLOPEDIA) {
                    if (deleteConfirmIndex < NUM_BUILTIN_SATELLITES) {
                        deleteConfirmIndex = -1;
                    } else if (justY) {
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
                    } else if (justN || justEsc) {
                        deleteConfirmIndex = -1;
                    }
                } else if (justComma || justSlash) {
                    currentSatTab = (currentSatTab == TAB_ENCYCLOPEDIA) ? TAB_RECENT_LAUNCH : TAB_ENCYCLOPEDIA;
                    noradInput = "";
                    downloadErrorMsg = "";
                    if (currentSatTab == TAB_ENCYCLOPEDIA) {
                        if (g_recentLaunchFocusMode) {
                            g_recentLaunchFocusMode = false;
                            recentLaunchActiveBatchId = "";
                            g_repSatInitialized = false;
                            lockPassMutex();
                            predictionsReady = false;
                            lastPredictionBaseTime = 0;
                            unlockPassMutex();
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
                } else if (justH) {
                    showListHelp = true;
                } else if (justW || justC) {
                    if (currentSatTab == TAB_RECENT_LAUNCH) {
                        if (g_networkActive) {
                            recentLaunchErrorMsg = I18N::get(TXT_SYS_BUSY);
                            recentLaunchDownloadSuccess = false;
                            recentLaunchDownloadFinishedMs = millis();
                            drawSatSelectPage();
                            pushCanvasWithFilter();
                        } else if (!recentLaunchDownloading) {
                            if (justC) {
                                if (LittleFS.exists("/recent_last_update.txt")) {
                                    LittleFS.remove("/recent_last_update.txt");
                                    LOG_I("APP", "Bypassed rate limiting via physical C key");
                                }
                            }
                            manualWifiToggle = true;
                            recentLaunchDownloading = true;
                            recentLaunchErrorMsg = I18N::get(TXT_CONNECTING_WIFI);
                            drawSatSelectPage();
                            pushCanvasWithFilter();
                            BaseType_t res = xTaskCreatePinnedToCore(recentLaunchNetworkTask, "RecentLaunchNetworkTask", 8192, NULL, 1, NULL, 0);
                            if (res != pdPASS) {
                                recentLaunchDownloading = false;
                                recentLaunchErrorMsg = I18N::get(TXT_TASK_INIT_FAILED);
                                drawSatSelectPage();
                                pushCanvasWithFilter();
                            }
                        }
                    } else {
                        if (justC && currentSatTab == TAB_ENCYCLOPEDIA && satSelectedIndex >= 0 && satSelectedIndex < NUM_SATELLITES) {
                            if (g_networkActive) {
                                downloadErrorMsg = I18N::get(TXT_SYS_BUSY);
                                drawSatSelectPage();
                                pushCanvasWithFilter();
                            } else {
                                downloadErrorMsg = I18N::get(TXT_REFRESHING_GP);
                                drawSatSelectPage();
                                pushCanvasWithFilter();
                                BaseType_t res = xTaskCreatePinnedToCore(forceRefreshSingleSatTask, "ForceRefreshSingleSatTask", 8192, (void*)(intptr_t)satSelectedIndex, 1, NULL, 0);
                                if (res != pdPASS) {
                                    downloadErrorMsg = I18N::get(TXT_TASK_INIT_FAILED);
                                    drawSatSelectPage();
                                    pushCanvasWithFilter();
                                }
                            }
                        } else if (!justC) { // Prevent C from triggering WiFi toggle in other tabs
                            if (g_networkActive) {
                                downloadErrorMsg = I18N::get(TXT_SYS_BUSY);
                                drawSatSelectPage();
                                pushCanvasWithFilter();
                            } else if (!HalWifi::isConnected()) {
                                manualWifiToggle = true;
                                downloadErrorMsg = I18N::get(TXT_CONNECTING_WIFI);
                                drawSatSelectPage();
                                pushCanvasWithFilter();
                                BaseType_t res = xTaskCreatePinnedToCore(networkTask, "NetworkTask", 16384, NULL, 1, NULL, 0);
                                if (res != pdPASS) {
                                    downloadErrorMsg = I18N::get(TXT_TASK_INIT_FAILED);
                                    drawSatSelectPage();
                                    pushCanvasWithFilter();
                                }
                            } else {
                                WiFi.disconnect(true);
                                WiFi.mode(WIFI_OFF);
                                downloadErrorMsg = I18N::get(TXT_WIFI_DISCONNECTED);
                            }
                        }
                    }
                } else if (currentSatTab == TAB_RECENT_LAUNCH) {
                    if (justBack || justEsc || justTick) {
                        if (recentLaunchInObjectsView) {
                            recentLaunchInObjectsView = false;
                            g_level3Objects.clear();
                            g_level3Objects.shrink_to_fit();
                        } else {
                            appState = STATE_MAIN;
                            validateSatViewFocusState();
                        }
                    } else if (justO) {
                        if (recentLaunchInObjectsView) {
                            recentLaunchInObjectsView = false;
                            g_level3Objects.clear();
                            g_level3Objects.shrink_to_fit();
                        } else if (recentLaunchSelectedIndex >= 0 && recentLaunchSelectedIndex < (int)g_recentLaunches.size()) {
                            recentLaunchInObjectsView = true;
                            recentLaunchObjectPage = 0;
                            loadLevel3ObjectsPage(g_recentLaunches[recentLaunchSelectedIndex], 0);
                        }
                    } else if (justBracketL) {
                        if (recentLaunchInObjectsView && recentLaunchSelectedIndex >= 0) {
                            if (recentLaunchObjectPage > 0) {
                                recentLaunchObjectPage--;
                                loadLevel3ObjectsPage(g_recentLaunches[recentLaunchSelectedIndex], recentLaunchObjectPage);
                            }
                        }
                    } else if (justBracketR) {
                        if (recentLaunchInObjectsView && recentLaunchSelectedIndex >= 0) {
                            int maxPage = (g_recentLaunches[recentLaunchSelectedIndex].satelliteCount - 1) / 5;
                            if (recentLaunchObjectPage < maxPage) {
                                recentLaunchObjectPage++;
                                loadLevel3ObjectsPage(g_recentLaunches[recentLaunchSelectedIndex], recentLaunchObjectPage);
                            }
                        }
                    } else if (justEnter) {
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

                            lockPassMutex();
                            predictionsReady = false;
                            lastPredictionBaseTime = 0;
                            unlockPassMutex();
                            triggerPrediction = true;
                        }
                    } else if (justSemi) { // UP
                        if (recentLaunchSelectedIndex > 0) recentLaunchSelectedIndex--;
                        else if (!g_recentLaunches.empty()) recentLaunchSelectedIndex = g_recentLaunches.size() - 1;
                        if (recentLaunchInObjectsView) {
                            recentLaunchObjectPage = 0;
                            loadLevel3ObjectsPage(g_recentLaunches[recentLaunchSelectedIndex], 0);
                        }
                    } else if (justDot) { // DOWN
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
                        if (justBack) {
                            if (noradInput.length() > 0) noradInput.remove(noradInput.length() - 1);
                            downloadErrorMsg = "";
                        } else if (justEsc || justTick) {
                            appState = STATE_MAIN;
                            validateSatViewFocusState();
                        } else if (justSemi) {
                            if (satSelectedIndex > 0) satSelectedIndex--;
                        } else if (justDot) {
                            satSelectedIndex = 0;
                        } else if (justEnter) {
                            if ((noradInput.length() == 5 || noradInput.length() == 6) && !isDownloadingCustom) {
                                isDownloadingCustom = true;
                                downloadErrorMsg = "";
                                drawSatSelectPage();
                                pushCanvasWithFilter();
                                
                                int id = noradInput.toInt();
                                BaseType_t res = xTaskCreatePinnedToCore(downloadCustomSatTask, "DownloadCustomSatTask", 8192, (void*)(intptr_t)id, 1, NULL, 0);
                                if (res != pdPASS) {
                                    isDownloadingCustom = false;
                                    downloadErrorMsg = I18N::get(TXT_TASK_INIT_FAILED);
                                }
                            }
                        } else {
                            for (auto c : M5Cardputer.Keyboard.keysState().word) {
                                if (c >= '0' && c <= '9' && noradInput.length() < 6) {
                                    noradInput += c;
                                    downloadErrorMsg = "";
                                }
                            }
                        }
                    } else {
                        if (justBack || justEsc || justTick) {
                            appState = STATE_MAIN;
                            validateSatViewFocusState();
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
                                lockPassMutex();
                                predictionsReady = false;
                                lastPredictionBaseTime = 0;
                                unlockPassMutex();
                                triggerPrediction = true;
                            }
                        } else if (justEnter) {
                            g_satellites[satSelectedIndex].selected = !g_satellites[satSelectedIndex].selected;
                        } else if (justD && satSelectedIndex >= NUM_BUILTIN_SATELLITES && satSelectedIndex < NUM_SATELLITES) {
                            deleteConfirmIndex = satSelectedIndex;
                        } else if (justSemi) {
                            if (satSelectedIndex > 0) satSelectedIndex--;
                            else satSelectedIndex = NUM_SATELLITES;
                        } else if (justDot) {
                            satSelectedIndex = (satSelectedIndex + 1) % (NUM_SATELLITES + 1);
                        } else if (justBracketL) {
                            g_descManualScrolled = true;
                            g_descManualYOffset -= 39;
                            if (g_descManualYOffset < 0) g_descManualYOffset = 0;
                        } else if (justBracketR) {
                            g_descManualScrolled = true;
                            g_descManualYOffset += 39;
                            if (g_descManualYOffset > g_descMaxScroll) g_descManualYOffset = g_descMaxScroll;
                        }
                    }
                }
            } else if (appState == STATE_LANG_SELECT) {
                if (justEsc || justTick || justBack) {
                    appState = STATE_MAIN;
                } else if (justEnter) {
                    I18N::setLanguage(langSelectedIndex == 0 ? LANG_EN : LANG_ZH);
                    appState = STATE_MAIN;
                } else if (justSemi) { // UP
                    langSelectedIndex = (langSelectedIndex == 0) ? 1 : 0;
                } else if (justDot) { // DOWN
                    langSelectedIndex = (langSelectedIndex == 0) ? 1 : 0;
                }
            }
        }

        // Save action keys state for the next frame
        lastSemi = currSemi;
        lastDot = currDot;
        lastComma = currComma;
        lastSlash = currSlash;
        lastO = currO;
        lastV = currV;
        lastEnter = currEnter;
        lastBack = currBack;
        lastEsc = currEsc;
        lastTick = currTick;
        lastBracketL = currBracketL;
        lastBracketR = currBracketR;
        lastC = currC;
        lastR = currR;
        lastW = currW;
        lastS = currS;
        lastH = currH;
        lastG = currG;
        lastY = currY;
        lastN = currN;
        lastD = currD;
        lastTab = currTab;
        lastL = currL;
        lastSpace = currSpace;
        
        if (appState == STATE_WIFI_SETUP) {
            drawWiFiSetupPage();
            pushCanvasWithFilter();
            updateChainMonoDisplay();
            
            if (wifiIsScanning) {
                wifiNetworks = HalWifi::scanNetworks();
                wifiIsScanning = false;
                wifiSelectedIndex = 0;
            }
            return;
        } else if (appState == STATE_SAT_SELECT) {
            drawSatSelectPage();
            pushCanvasWithFilter();
            updateChainMonoDisplay();
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
        if (gnss && gnss->isModuleInitialized() && !gnss->isInStandbyMode()) {
            if (gnss->getStatus() == GNSS_STATUS_LOCKED) {
                GnssData gData = gnss->getData();
                if (gData.isValid && (abs(gData.latitude) > 0.0001 || abs(gData.longitude) > 0.0001)) {
                    double oldLat = baseUserLat;
                    double oldLon = baseUserLon;
                    double oldAlt = baseUserAlt;
                    baseUserLat = gData.latitude;
                    baseUserLon = gData.longitude;
                    baseUserAlt = gData.altitude;
                    gnssLocationFixed = true; // Mark that we have a real location
                    
                    // Sync to pos_manager
                    if (pos_manager) {
                        PositionData pos = {baseUserLat, baseUserLon, baseUserAlt};
                        pos_manager->setPosition(pos);
                    }
                    
                    if (abs(baseUserLat - oldLat) > 0.01 || abs(baseUserLon - oldLon) > 0.01 || abs(baseUserAlt - oldAlt) > 100.0) {
                        Serial.printf("[Debug] GNSS sync cache reset: oldLat=%f, newLat=%f, oldLon=%f, newLon=%f, oldAlt=%f, newAlt=%f\n", 
                                      oldLat, baseUserLat, oldLon, baseUserLon, oldAlt, baseUserAlt);
                        lockPassMutex();
                        lastPredictionBaseTime = 0; // 缓存失效
                        predictionsReady = false;
                        unlockPassMutex();
                    }
                    
                    // Save GNSS location to Preferences (NVS) at most once per 60 seconds
                    // and only when position has meaningfully changed. NVS writes are slow
                    // (10-200ms due to Flash wear-leveling page erasure) and must NOT occur
                    // every frame or they cause intermittent 1-2s hitches during time adjustment.
                    static unsigned long lastGnssNvsSaveMs = 0;
                    static double lastSavedLat = 999.0;
                    static double lastSavedLon = 999.0;
                    bool posChangedForSave = (abs(baseUserLat - lastSavedLat) > 0.01 || abs(baseUserLon - lastSavedLon) > 0.01);
                    if (posChangedForSave && (lastGnssNvsSaveMs == 0 || millis() - lastGnssNvsSaveMs > 60000)) {
                        lastGnssNvsSaveMs = millis();
                        lastSavedLat = baseUserLat;
                        lastSavedLon = baseUserLon;
                        Preferences posPrefs;
                        if (posPrefs.begin("position", false)) {
                            posPrefs.putDouble("cached_lat", baseUserLat);
                            posPrefs.putDouble("cached_lon", baseUserLon);
                            posPrefs.putDouble("cached_alt", baseUserAlt);
                            posPrefs.putBool("use_manual_pos", false);
                            posPrefs.end();
                        }
                    }
                }
                
                static bool gnssTimeSynced = false;
                if (gData.timeValid && gData.dateValid && !gnssTimeSynced) {
                    current_unix = convertGNSSDateToUnix(gData.year, gData.month, gData.day, gData.hour, gData.minute, gData.second);
                    gnssTimeSynced = true;
                    g_timeSynced = true;
                    LOG_I("APP", "Time synced to GNSS UTC: %u", current_unix);
                    
                    // Trigger predictor again with correct time
                    lockPassMutex();
                    predictionsReady = false;
                    lastPredictionBaseTime = 0; // 缓存失效
                    unlockPassMutex();
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
                if (g_satellites[focusSatIndex].type == SAT_TYPE_GEO_TV) {
                    double slotLon = getGeoSlotLongitude(g_satellites[focusSatIndex].noradId, g_satellites[focusSatIndex].uplinkFreq);
                    focalGeo = {0.0, slotLon, 35785.863};
                    hasFocalPos = true;
                } else {
                    double tx, ty, tz;
                    if (g_satellites[focusSatIndex].calc.getTEME(current_unix + timeMachineOffset, tx, ty, tz)) {
                        double gmst = CoordTransform::getGMST(CoordTransform::unixToJulian(current_unix + timeMachineOffset));
                        ECEFCoord ecef = CoordTransform::temeToECEF(tx, ty, tz, gmst);
                        focalGeo = CoordTransform::ecefToGeodetic(ecef);
                        hasFocalPos = true;
                    }
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
                            
                            // Visual effects fields
                            data.isSelected = (isSatViewMode && g_recentLaunchFocusMode);
                            data.calc = &g_repSatCalc;
                            
                            if (appState == STATE_MAIN) {
                                calculateOrbit(g_repSatCalc, simTime, g_repSatCache.cache, orbitsCalculatedThisFrame, isFastForwarding, (isSatViewMode && g_recentLaunchFocusMode));
                                data.pastOrbit = &(g_repSatCache.cache.past);
                                data.futureOrbit = &(g_repSatCache.cache.future);
                            } else {
                                data.pastOrbit = nullptr;
                                data.futureOrbit = nullptr;
                            }
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
                            
                            if (appState == STATE_MAIN) {
                                calculateOrbit(*(item.calc), simTime, item.cache.cache, orbitsCalculatedThisFrame, isFastForwarding, false);
                                data.pastOrbit = &(item.cache.cache.past);
                                data.futureOrbit = &(item.cache.cache.future);
                            } else {
                                data.pastOrbit = nullptr;
                                data.futureOrbit = nullptr;
                            }
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
                        
                        if (appState == STATE_MAIN) {
                            calculateOrbit(obj.calc, simTime, obj.cache, orbitsCalculatedThisFrame, isFastForwarding, false);
                            data.pastOrbit = &(obj.cache.past);
                            data.futureOrbit = &(obj.cache.future);
                        } else {
                            data.pastOrbit = nullptr;
                            data.futureOrbit = nullptr;
                        }
                        
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
        static String s_encSatNameCache[MAX_SATELLITES];
        for (int i = 0; i < NUM_SATELLITES; i++) {
                lockSatMutex();
                bool selectedCopy = g_satellites[i].selected;
                SGP4Calc calcCopy = g_satellites[i].calc;
                String nameCopy = g_satellites[i].name;
                SatIconType iconCopy = g_satellites[i].iconType;
                uint16_t colorCopy = g_satellites[i].color;
                SatelliteType typeCopy = g_satellites[i].type;
                uint32_t noradIdCopy = g_satellites[i].noradId;
                String uplinkFreqCopy = g_satellites[i].uplinkFreq;
                unlockSatMutex();

                if (!selectedCopy) {
                    g_satCaches[i].lastGeoValid = false;
                    g_satCaches[i].isVisible = false;
                    continue;
                }

                s_encSatNameCache[i] = nameCopy;

                bool runCalculation = (timeChanged || !g_satCaches[i].lastGeoValid);
                
                if (runCalculation) {
                    if (typeCopy == SAT_TYPE_GEO_TV) {
                        double slotLon = getGeoSlotLongitude(noradIdCopy, uplinkFreqCopy);
                        GeodeticCoord geo;
                        ECEFCoord ecef;
                        TopocentricCoord topo;
                        double skew = 0.0;
                        calculateGeoSatPosition(slotLon, baseUserLat, baseUserLon, baseUserAlt, geo, ecef, topo, skew);
                        
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
                        double tx, ty, tz;
                        if (calcCopy.getTEME(simTime, tx, ty, tz)) {
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
                        
                        if (typeCopy == SAT_TYPE_GEO_TV) {
                            isVisible = true;
                        } else if (isSatViewMode) {
                            isVisible = !g_satCaches[i].lastInShadow;
                        } else {
                            isVisible = (isNight && (el >= 10.0f) && !g_satCaches[i].lastInShadow);
                        }
                    }
                    g_satCaches[i].isVisible = isVisible;
                    
                    if (shouldLogNow && appState == STATE_MAIN) {
                        log_i("[%s] Lat: %.2f, Lon: %.2f, Alt: %.1f km, Shadow: %s, Visible: %s", 
                              s_encSatNameCache[i].c_str(), 
                              g_satCaches[i].lastGeo.lat, 
                              g_satCaches[i].lastGeo.lon, 
                              g_satCaches[i].lastGeo.alt, 
                              g_satCaches[i].lastInShadow ? "YES" : "NO",
                              g_satCaches[i].isVisible ? "YES" : "NO");
                    }
                    
                    SatRenderData data;
                    data.name = s_encSatNameCache[i].c_str();
                    data.iconType = iconCopy;
                    data.currentPos = g_satCaches[i].lastGeo;
                    data.color = colorCopy;
                    data.isVisible = g_satCaches[i].isVisible;
                    
                    // Visual effects fields
                    data.isSelected = (isSatViewMode && (focusSatIndex == i));
                    data.calc = nullptr; // Do not pass direct SGP4Calc pointer across threads
                    data.simTime = simTime;
                    
                    if (appState == STATE_MAIN) {
                        if (typeCopy == SAT_TYPE_GEO_TV) {
                            double slotLon = getGeoSlotLongitude(noradIdCopy, uplinkFreqCopy);
                            g_satCaches[i].cache.past.clear();
                            g_satCaches[i].cache.future.clear();
                            GeodeticCoord p = {0.0, slotLon, 35785.863};
                            g_satCaches[i].cache.past.push_back(p);
                            g_satCaches[i].cache.future.push_back(p);
                        } else {
                            calculateOrbit(calcCopy, simTime, g_satCaches[i].cache, orbitsCalculatedThisFrame, isFastForwarding, (isSatViewMode && (focusSatIndex == i) && !g_recentLaunchFocusMode));
                        }
                        data.pastOrbit = &(g_satCaches[i].cache.past);
                        data.futureOrbit = &(g_satCaches[i].cache.future);
                    } else {
                        data.pastOrbit = nullptr;
                        data.futureOrbit = nullptr;
                    }
                    
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
        bool isGnssSearching = (gnss && gnss->isModuleInitialized() && !gnss->isInStandbyMode() && gnss->getStatus() != GNSS_STATUS_LOCKED);
        earth_renderer->setGnssSearching(isGnssSearching);
        earth_renderer->setObserverConstrained(!isSatViewMode);
        earth_renderer->setFastForwarding(isFastForwarding);
        earth_renderer->setUnixTime(current_unix + timeMachineOffset);
        earth_renderer->render(viewLat, viewLon, renderUserLat, baseUserLon, sats);
        
        // Draw coordinate overlay
        if (!showRecommendations && !showHelp && (appState == STATE_MAIN || appState == STATE_LANG_SELECT) && showHud) {
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
            uint16_t w = 216, h = 114;
            int x = (canvas->width() - w) / 2;
            int y = (canvas->height() - h) / 2;
            
            canvas->fillRect(x, y, w, h, canvas->color565(20, 30, 40));
            canvas->drawRect(x, y, w, h, TFT_LIGHTGRAY);
            
            bool isZh = (I18N::getLanguage() == LANG_ZH);
            canvas->setTextColor(TFT_WHITE);
            canvas->setTextSize(1);
            canvas->drawString(I18N::get(TXT_HELP_TITLE), x + 35, y + 5);
            
            auto drawHotKey = [&](const char* word, char keyChar, int dx, int dy) {
                int cx = dx;
                bool highlighted = false;
                int i = 0;
                while (word[i] != '\0') {
                    int charLen = 1;
                    unsigned char head = (unsigned char)word[i];
                    if (head >= 0xF0) charLen = 4;
                    else if (head >= 0xE0) charLen = 3;
                    else if (head >= 0xC0) charLen = 2;
                    
                    char cstr[5] = {0};
                    for (int j = 0; j < charLen && word[i + j] != '\0'; j++) {
                        cstr[j] = word[i + j];
                    }
                    
                    if ((charLen == 1 && !highlighted && tolower((unsigned char)cstr[0]) == tolower((unsigned char)keyChar) && keyChar != '\0') ||
                        (keyChar == ' ' && !highlighted && (strcmp(cstr, "Spc") == 0 || strcmp(cstr, " ") == 0))) {
                        canvas->setTextColor(TFT_YELLOW);
                        highlighted = true;
                    } else {
                        canvas->setTextColor(TFT_LIGHTGRAY);
                    }
                    
                    canvas->drawString(cstr, cx, dy);
                    cx += canvas->textWidth(cstr);
                    i += charLen;
                }
            };

            int ty = y + 20;
            drawHotKey(I18N::get(TXT_HELP_BRIGHT), '[', x + 8, ty);
            drawHotKey(I18N::get(TXT_HELP_GNSS), 'g', x + 112, ty); ty += 13;
            
            drawHotKey(I18N::get(TXT_HELP_HELP), 'h', x + 8, ty);
            drawHotKey(I18N::get(TXT_HELP_HUD), 'b', x + 112, ty); ty += 13;
            
            drawHotKey(I18N::get(TXT_HELP_LOCK), ' ', x + 8, ty);
            drawHotKey(I18N::get(TXT_HELP_PASSLIST), 'e', x + 112, ty); ty += 13;
            
            drawHotKey(I18N::get(TXT_HELP_SATS), 's', x + 8, ty);
            drawHotKey(I18N::get(TXT_HELP_TIME), ',', x + 112, ty); ty += 13;
            
            drawHotKey(I18N::get(TXT_HELP_VIEW), 'v', x + 8, ty);
            drawHotKey(I18N::get(TXT_HELP_WIFI), 'w', x + 112, ty); ty += 13;
            
            drawHotKey(I18N::get(TXT_HELP_CONFIG), 'c', x + 8, ty);
            drawHotKey(I18N::get(TXT_HELP_REALTIME), 'r', x + 112, ty); ty += 13;
            
            drawHotKey(I18N::get(TXT_HELP_TAB), 't', x + 8, ty); ty += 13;
        }
        
        if (showRecommendations) {
            // Draw semi-transparent dark overlay on the left side (width: 140)
            earth_renderer->getCanvas()->fillRect(0, 0, 140, 135, earth_renderer->getCanvas()->color565(15, 20, 25));
            earth_renderer->getCanvas()->drawFastVLine(140, 0, 135, TFT_DARKGREY); // separator line
            
            earth_renderer->getCanvas()->setTextColor(TFT_WHITE);
            earth_renderer->getCanvas()->setTextSize(1);
            earth_renderer->getCanvas()->drawString(I18N::get(TXT_RECOMMENDED_PASSES), 2, 5);
            
            bool localPredictionsReady = false;
            int localPredictionProgress = 0;
            bool localTimeSynced = false;
            std::vector<PassEvent> localRecommendedPasses;
            std::vector<TreeItem> localDisplayTree;

            lockPassMutex();
            localPredictionsReady = predictionsReady;
            localPredictionProgress = predictionProgress;
            localTimeSynced = g_timeSynced;
            if (localPredictionsReady) {
                localRecommendedPasses = recommendedPasses;
                localDisplayTree = displayTree;
            }
            unlockPassMutex();

            if (!localPredictionsReady) {
                earth_renderer->getCanvas()->setTextColor(TFT_YELLOW);
                if (!localTimeSynced) {
                    earth_renderer->getCanvas()->drawString(I18N::get(TXT_WAITING_TIME_SYNC), 5, 30);
                } else {
                    char buf[32];
                    sprintf(buf, "%s %d%%", I18N::get(TXT_PASS_CALCULATING), localPredictionProgress);
                    earth_renderer->getCanvas()->drawString(buf, 5, 30);
                }
            } else {
                if (localRecommendedPasses.empty()) {
                    earth_renderer->getCanvas()->setTextColor(TFT_LIGHTGRAY);
                    earth_renderer->getCanvas()->drawString(I18N::get(TXT_NO_PASSES_7D), 5, 30);
                    
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
                        earth_renderer->getCanvas()->setTextColor(TFT_YELLOW);
                        earth_renderer->getCanvas()->drawString(I18N::getLanguage() == LANG_ZH ? "TLE过期,请连WiFi更新" : "Stale TLE, sync WiFi", 5, 48);
                    }
                } else if (selectedPassIndex >= 0 && selectedPassIndex < (int)localRecommendedPasses.size()) {
                    // Draw Detail View
                    const auto& p = localRecommendedPasses[selectedPassIndex];
                    earth_renderer->getCanvas()->setTextColor(TFT_CYAN);
                    earth_renderer->getCanvas()->drawString(I18N::get(TXT_PASS_NAME), 5, 20);
                    earth_renderer->getCanvas()->setTextColor(TFT_WHITE);
                    earth_renderer->getCanvas()->drawString(p.satName.c_str(), 40, 20);
                    
                    // Score: (y=32)
                    earth_renderer->getCanvas()->setTextColor(TFT_CYAN);
                    earth_renderer->getCanvas()->drawString(I18N::get(TXT_PASS_SCORE), 5, 32);
                    String stars = "";
                    for(int s=0;s<p.score;s++) stars += "*";
                    uint16_t starColor = (p.score==5) ? TFT_GOLD : (p.score>=3 ? TFT_GREEN : TFT_LIGHTGRAY);
                    earth_renderer->getCanvas()->setTextColor(starColor);
                    int scoreX = (I18N::getLanguage() == LANG_ZH) ? 60 : 45;
                    earth_renderer->getCanvas()->drawString(stars.c_str(), scoreX, 32);
                    
                    // Orbit: MM/DD (y=45)
                    earth_renderer->getCanvas()->setTextColor(TFT_CYAN);
                    earth_renderer->getCanvas()->drawString(I18N::get(TXT_RL_ORBIT), 5, 45);
                    
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
                    earth_renderer->getCanvas()->drawString(dateStr, 45, 45);
                    
                    // Time: HH:MM:SS - HH:MM:SS (y=57)
                    char timeStr[64];
                    sprintf(timeStr, "%02d:%02d:%02d-%02d:%02d:%02d", 
                            aos_tm.tm_hour, aos_tm.tm_min, aos_tm.tm_sec, 
                            los_tm.tm_hour, los_tm.tm_min, los_tm.tm_sec);
                    earth_renderer->getCanvas()->setTextColor(TFT_LIGHTGRAY);
                    earth_renderer->getCanvas()->drawString(timeStr, 5, 57);
                    
                    // Mag & Peak (y=70)
                    earth_renderer->getCanvas()->setTextColor(TFT_CYAN);
                    earth_renderer->getCanvas()->drawString(I18N::get(TXT_PASS_MAG), 5, 70);
                    earth_renderer->getCanvas()->setTextColor(TFT_WHITE);
                    char magBuf[16];
                    if (p.maxBrightness < 98.0) {
                        sprintf(magBuf, "%.1f", p.maxBrightness);
                    } else {
                        sprintf(magBuf, "%s", I18N::get(TXT_VIS_NA));
                    }
                    int magX = (I18N::getLanguage() == LANG_ZH) ? 40 : 35;
                    earth_renderer->getCanvas()->drawString(magBuf, magX, 70);
                    
                    earth_renderer->getCanvas()->setTextColor(TFT_CYAN);
                    earth_renderer->getCanvas()->drawString(I18N::get(TXT_PASS_MAX_EL), 65, 70);
                    earth_renderer->getCanvas()->setTextColor(TFT_WHITE);
                    int maxElX = (I18N::getLanguage() == LANG_ZH) ? 120 : 100;
                    earth_renderer->getCanvas()->drawString((String((int)p.maxElevation) + "°").c_str(), maxElX, 70);
                    
                    // Reason: (y=82)
                    earth_renderer->getCanvas()->setTextColor(TFT_CYAN);
                    earth_renderer->getCanvas()->drawString(I18N::get(TXT_PASS_REASON), 5, 82);
                    String reason = I18N::get(TXT_PASS_REASON_DARK);
                    if (p.maxBrightness <= 2.0) reason += I18N::get(TXT_PASS_REASON_BRIGHT);
                    if (p.maxElevation > 60) reason += I18N::get(TXT_PASS_REASON_ZENITH);
                    if (p.visibleDuration > 300) reason += I18N::get(TXT_PASS_REASON_LONG);
                    earth_renderer->getCanvas()->setTextColor(TFT_LIGHTGRAY);
                    int reasonX = (I18N::getLanguage() == LANG_ZH) ? 40 : 50;
                    earth_renderer->getCanvas()->drawString(reason.c_str(), reasonX, 82);
                    
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
                            bool isZh = (I18N::getLanguage() == LANG_ZH);
                            char azaltBuf[32];
                            if (isZh) {
                                sprintf(azaltBuf, "方位:%03d° 仰角:%02d°", (int)az, (int)el);
                            } else {
                                sprintf(azaltBuf, "Az:%03d° El:%02d°", (int)az, (int)el);
                            }
                            earth_renderer->getCanvas()->drawString(azaltBuf, 5, 95);
                            
                            if (satType == SAT_TYPE_SPACE_STATION && noradId == 25544) {
                                double freq_aprs = 145.825;
                                double freq_sstv = 145.800;
                                double shift_aprs = (freq_aprs * -range_rate / 299792.458) * 1000.0;
                                double shift_sstv = (freq_sstv * -range_rate / 299792.458) * 1000.0;
                                
                                char rx1Buf[32];
                                char rx2Buf[32];
                                if (isZh) {
                                    sprintf(rx1Buf, "下行1:%07.3f", freq_aprs + shift_aprs/1000.0);
                                    sprintf(rx2Buf, "下行2:%07.3f", freq_sstv + shift_sstv/1000.0);
                                } else {
                                    sprintf(rx1Buf, "Rx1:%07.3f", freq_aprs + shift_aprs/1000.0);
                                    sprintf(rx2Buf, "Rx2:%07.3f", freq_sstv + shift_sstv/1000.0);
                                }
                                earth_renderer->getCanvas()->drawString(rx1Buf, 5, 108);
                                earth_renderer->getCanvas()->drawString(rx2Buf, 5, 120);
                            }
                            else if (satType == SAT_TYPE_WEATHER || satType == SAT_TYPE_HAM) {
                                if (downlinkFreq.length() > 0) {
                                    double freq_mhz = downlinkFreq.toDouble();
                                    double shift_khz = (freq_mhz * -range_rate / 299792.458) * 1000.0;
                                    char rxBuf[32];
                                    if (isZh) {
                                        sprintf(rxBuf, "下行:%s (%+.1f)", downlinkFreq.c_str(), shift_khz);
                                    } else {
                                        sprintf(rxBuf, "Rx:%s (%+.1f)", downlinkFreq.c_str(), shift_khz);
                                    }
                                    earth_renderer->getCanvas()->drawString(rxBuf, 5, 108);
                                }
                                if (satType == SAT_TYPE_HAM && uplinkFreq.length() > 0) {
                                    earth_renderer->getCanvas()->setTextColor(TFT_ORANGE);
                                    String txStr = isZh ? ("上行:" + uplinkFreq) : ("Tx:" + uplinkFreq);
                                    if (tone.length() > 0) txStr += isZh ? (" 亚音:" + tone) : (" Tone:" + tone);
                                    earth_renderer->getCanvas()->drawString(txStr.c_str(), 5, 120);
                                }
                            }
                        }
                    }
                } else {
                    
                    // Draw Tree View
                    const char* catNames[] = {
                        I18N::get(TXT_CAT_TONIGHT),
                        I18N::get(TXT_CAT_NEXT_7D),
                        I18N::get(TXT_CAT_HIGHLY_REC),
                        I18N::get(TXT_CAT_ALL_PASSES)
                    };
                    int lineH = (I18N::getLanguage() == LANG_ZH) ? 14 : 11;
                    int y = 20;
                    int itemsPerPage = (I18N::getLanguage() == LANG_ZH) ? 6 : 7;
                    int startIndex = (passScrollIndex / itemsPerPage) * itemsPerPage;
                    
                    for (int i = 0; i < itemsPerPage && (startIndex + i) < localDisplayTree.size(); i++) {
                        int idx = startIndex + i;
                        const auto& item = localDisplayTree[idx];
                        
                        if (idx == passScrollIndex) {
                            earth_renderer->getCanvas()->fillRect(2, y-1, 136, lineH, earth_renderer->getCanvas()->color565(0, 120, 255));
                        }
                        
                        if (item.isCategory) {
                            earth_renderer->getCanvas()->setTextColor(idx == passScrollIndex ? TFT_WHITE : TFT_CYAN);
                            String prefix = catExpanded[item.categoryIndex] ? "[-] " : "[+] ";
                            earth_renderer->getCanvas()->drawString((prefix + catNames[item.categoryIndex]).c_str(), 5, y);
                        } else {
                            const auto& p = localRecommendedPasses[item.passIndex];
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
                        y += lineH;
                    }
                    
                    if (localDisplayTree.size() > itemsPerPage) {
                        earth_renderer->getCanvas()->setTextColor(TFT_DARKGREY);
                        earth_renderer->getCanvas()->drawString("[^/v]", 110, 5);
                    }
                }
            }
            
            // Draw GNSS and WiFi Status at the bottom of the panel
            // Divider line shifted up to y=100
            earth_renderer->getCanvas()->drawFastHLine(0, 100, 140, TFT_DARKGREY);
            
            // Draw WiFi Status
            if (HalWifi::isConnected()) {
                earth_renderer->getCanvas()->setTextColor(TFT_GREEN);
                earth_renderer->getCanvas()->drawString("WF:ON", 5, 105);
            } else {
                earth_renderer->getCanvas()->setTextColor(TFT_LIGHTGRAY);
                earth_renderer->getCanvas()->drawString("WF:OFF", 5, 105);
            }
            
            // Draw GNSS Status
            if (gnss && gnss->isModuleInitialized()) {
                if (gnss->getStatus() == GNSS_STATUS_LOCKED) {
                    earth_renderer->getCanvas()->setTextColor(TFT_GREEN);
                    earth_renderer->getCanvas()->drawString("GP:FIX", 52, 105);
                } else if (gnss->isInStandbyMode()) {
                    if (gnssTimedOut) {
                        earth_renderer->getCanvas()->setTextColor(TFT_RED);
                        earth_renderer->getCanvas()->drawString("GP:TMO", 52, 105);
                    } else {
                        earth_renderer->getCanvas()->setTextColor(TFT_LIGHTGRAY);
                        earth_renderer->getCanvas()->drawString("GP:OFF", 52, 105);
                    }
                } else {
                    earth_renderer->getCanvas()->setTextColor(TFT_YELLOW);
                    earth_renderer->getCanvas()->drawString("GP:SCH", 52, 105);
                }
            } else {
                earth_renderer->getCanvas()->setTextColor(TFT_DARKGREY);
                earth_renderer->getCanvas()->drawString("GP:N/A", 52, 105);
            }
            
            // Draw M5Chain Mono Status
            if (isMonoInitialized) {
                earth_renderer->getCanvas()->setTextColor(TFT_GREEN);
                earth_renderer->getCanvas()->drawString("MN:OK", 100, 105);
            } else {
                earth_renderer->getCanvas()->setTextColor(TFT_DARKGREY);
                earth_renderer->getCanvas()->drawString("MN:ND", 100, 105); // Not Detected
            }
            
            // Draw GP Epoch Version
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
                snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, month + 1, doy);
                tleEpoch += buf;
            } else {
                tleEpoch += I18N::get(TXT_VIS_NA);
            }
            earth_renderer->getCanvas()->setTextColor(TFT_LIGHTGRAY);
            earth_renderer->getCanvas()->drawString(tleEpoch.c_str(), 5, 117);
        }
        
        // Draw Time Machine at bottom right
        if ((appState == STATE_MAIN || appState == STATE_LANG_SELECT) && showHud && !showHelp && !showRecommendations) {
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
                    bool isZh = (I18N::getLanguage() == LANG_ZH);
                    earth_renderer->getCanvas()->setTextColor(satColor);
                    earth_renderer->getCanvas()->drawString(isZh ? "视角锁定" : "Sat View", isZh ? 180 : 180, 5);
                    
                    double az = 0, el = 0, dist = 0, range_rate = 0, skew = 0;
                    bool hasValidPos = false;
                    
                    if (currentType == SAT_TYPE_GEO_TV) {
                        String slotStr = (focusSatIndex >= 0 && focusSatIndex < NUM_SATELLITES) ? g_satellites[focusSatIndex].uplinkFreq : "";
                        double slotLon = getGeoSlotLongitude(currentNoradId, slotStr);
                        GeodeticCoord geo;
                        ECEFCoord ecef;
                        TopocentricCoord topo;
                        calculateGeoSatPosition(slotLon, baseUserLat, baseUserLon, baseUserAlt, geo, ecef, topo, skew);
                        az = topo.az; el = topo.el; dist = topo.range; range_rate = 0.0;
                        hasValidPos = true;
                    } else if (currentCalc != nullptr) {
                        double tx, ty, tz;
                        if (currentCalc->getTEME(current_unix + timeMachineOffset, tx, ty, tz)) {
                            double gmst = CoordTransform::getGMST(CoordTransform::unixToJulian(current_unix + timeMachineOffset));
                            ECEFCoord ecef = CoordTransform::temeToECEF(tx, ty, tz, gmst);
                            GeodeticCoord obsGeo = {baseUserLat, baseUserLon, baseUserAlt / 1000.0};
                            TopocentricCoord topo = CoordTransform::ecefToTopocentric(obsGeo, ecef);
                            az = topo.az; el = topo.el; dist = topo.range;
                            
                            double tx_prev, ty_prev, tz_prev;
                            double dist_prev = dist;
                            if (currentCalc->getTEME(current_unix + timeMachineOffset - 1, tx_prev, ty_prev, tz_prev)) {
                                double gmst_prev = CoordTransform::getGMST(CoordTransform::unixToJulian(current_unix + timeMachineOffset - 1));
                                ECEFCoord ecef_prev = CoordTransform::temeToECEF(tx_prev, ty_prev, tz_prev, gmst_prev);
                                TopocentricCoord topo_prev = CoordTransform::ecefToTopocentric(obsGeo, ecef_prev);
                                dist_prev = topo_prev.range;
                            }
                            range_rate = dist - dist_prev;
                            hasValidPos = true;
                        }
                    }
                    
                    if (hasValidPos) {
                        earth_renderer->getCanvas()->setTextColor(satColor);
                        
                        char azBuf[32];
                        char elBuf[32];
                        if (isZh) {
                            sprintf(azBuf, "方位: %03d°", (int)az);
                            sprintf(elBuf, "仰角: %02d°", (int)el);
                        } else {
                            sprintf(azBuf, "Az : %03d°", (int)az);
                            sprintf(elBuf, "El : %02d°", (int)el);
                        }
                        
                        if (currentType == SAT_TYPE_GEO_TV) {
                            earth_renderer->getCanvas()->drawString(azBuf, 5, 84);
                            earth_renderer->getCanvas()->drawString(elBuf, 5, 96);
                            
                            char skewBuf[32];
                            char bandBuf[32];
                            if (isZh) {
                                sprintf(skewBuf, "极化角: %+.1f°", skew);
                                sprintf(bandBuf, "下行: %s", downlinkFreq.length() > 0 ? downlinkFreq.c_str() : "广播");
                            } else {
                                sprintf(skewBuf, "Skew: %+.1f°", skew);
                                sprintf(bandBuf, "Rx  : %s", downlinkFreq.length() > 0 ? downlinkFreq.c_str() : "Bcast");
                            }
                            earth_renderer->getCanvas()->drawString(skewBuf, 5, 108);
                            earth_renderer->getCanvas()->drawString(bandBuf, 5, 120);
                        } else if (currentType == SAT_TYPE_SPACE_STATION && currentNoradId == 25544) {
                            double freq_aprs = 145.825;
                            double freq_sstv = 145.800;
                            double shift_aprs = (freq_aprs * -range_rate / 299792.458) * 1000.0;
                            double shift_sstv = (freq_sstv * -range_rate / 299792.458) * 1000.0;
                            
                            earth_renderer->getCanvas()->drawString(azBuf, 5, 84);
                            earth_renderer->getCanvas()->drawString(elBuf, 5, 96);
                            
                            char rx1Buf[32];
                            char rx2Buf[32];
                            if (isZh) {
                                sprintf(rx1Buf, "下行1: %07.3f", freq_aprs + shift_aprs/1000.0);
                                sprintf(rx2Buf, "下行2: %07.3f", freq_sstv + shift_sstv/1000.0);
                            } else {
                                sprintf(rx1Buf, "Rx1: %07.3f", freq_aprs + shift_aprs/1000.0);
                                sprintf(rx2Buf, "Rx2: %07.3f", freq_sstv + shift_sstv/1000.0);
                            }
                            earth_renderer->getCanvas()->drawString(rx1Buf, 5, 108);
                            earth_renderer->getCanvas()->drawString(rx2Buf, 5, 120);
                        } else {
                            bool hasFreq = ((currentType == SAT_TYPE_HAM || currentType == SAT_TYPE_WEATHER) && downlinkFreq.length() > 0);
                            int startY = hasFreq ? 91 : 104;
                            earth_renderer->getCanvas()->drawString(azBuf, 5, startY);
                            earth_renderer->getCanvas()->drawString(elBuf, 5, startY + 13);
                            
                            if (hasFreq) {
                                double freq_mhz = downlinkFreq.toDouble();
                                double shift_khz = (freq_mhz * -range_rate / 299792.458) * 1000.0;
                                char freqBuf[32];
                                if (isZh) {
                                    sprintf(freqBuf, "下行: %s (%+.1f)", downlinkFreq.c_str(), shift_khz);
                                } else {
                                    sprintf(freqBuf, "Rx : %s (%+.1f)", downlinkFreq.c_str(), shift_khz);
                                }
                                earth_renderer->getCanvas()->drawString(freqBuf, 5, startY + 26);
                            }
                        }
                    }
                }
            }

        if (appState == STATE_LANG_SELECT) {
            drawLangSelectDialog(earth_renderer->getCanvas());
        }
    }
    
    pushCanvasWithFilter();

    // Update Chain Mono Display (dynamic interval: 100ms normally)
    updateChainMonoDisplay();
}
}
