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
#include "core/image_utils.h"

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

// MonoState 移至 ChainMonoView

#include "core/mono_animator.h"
#include "core/hardware_config.h"
#include "ui/hardware_wizard_view.h"
#include "core/radio_manager.h"
#include "ui/rf_console_view.h"
#include "ui/dialog_views.h"
#include "ui/startup_view.h"
#include "hardware/chain_mono_view.h"

// 硬件外设由 HardwareConfig 动态管理并根据 NVS 配置定向初始化
bool isMonoInitialized = false;
uint8_t mono_id = 0;
uint8_t operation_status = 0;

HardwareWizardView hardware_wizard;

#include "gimbal/gimbal_controller.h"
GimbalController gimbal;
#include "ui/servo_test_view.h"
ServoTestView servo_test_view(gimbal);
#include "ui/wifi_setup_view.h"
WifiSetupView wifi_setup_view;
#include "ui/sat_select_view.h"
#include "ui/recommendation_view.h"
#include "core/orbit_utils.h"
#include "core/radio_tracking_pipeline.h"
#include "gimbal/gimbal_tracking_pipeline.h"
SatSelectView sat_select_view;
RecommendationView recommendation_view;
#include "core/text_utils.h"

#include "core/mono_icons.h"

void drawCortanaCircle(uint8_t* buffer) {
    drawMonoVisualAnimation(buffer);
}



extern HalImu* imu;
extern HalGnss* gnss;

EarthRenderer* earth_renderer = nullptr;

inline void applyNightVisionFilter(LGFX_Sprite* canvas) {
    ImageUtils::applyNightVisionFilter(canvas);
}

inline void pushCanvasWithFilter() {
    ImageUtils::pushCanvasWithFilter(earth_renderer);
}

enum AppState {
    STATE_MAIN,
    STATE_WIFI_SETUP,
    STATE_SAT_SELECT,
    STATE_LANG_SELECT,
    STATE_SERVO_TEST,
    STATE_HW_WIZARD
};
AppState appState = STATE_MAIN;
int langSelectedIndex = 0;
void saveCustomSatellites();



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

// 卫星百科分类筛选状态已移至 SatSelectView


bool g_showCategoryFilterDialog = false;
uint16_t g_selectedCategoryMask = 0; // 0 表示全选（无过滤）
uint16_t g_tempCategoryMask = 0;
int g_categoryFocusIndex = 0;
std::vector<int> g_encyclopediaFilteredIndices;

void updateEncyclopediaFilteredList();

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

// 内存安全检查阈值：确保有足够内部 RAM 分配任务栈 (8-10KB) 与 Wi-Fi 驱动 RX buffer
// 任务栈需要连续 8-10KB (MaxBlock >= 12KB)，总可用堆至少保持在 38KB 以上
static const size_t MIN_SAFE_HEAP_FOR_NETWORK = 18000;
static const size_t MIN_SAFE_BLOCK_FOR_NETWORK = 6000;

inline bool isSystemMemorySafeForNetwork() {
    size_t freeH = ESP.getFreeHeap();
    size_t maxB = ESP.getMaxAllocHeap();
    if (freeH < MIN_SAFE_HEAP_FOR_NETWORK || maxB < MIN_SAFE_BLOCK_FOR_NETWORK) {
        LOG_W("APP", "Insufficient memory for network ops! Free: %u, MaxBlock: %u", (unsigned int)freeH, (unsigned int)maxB);
        return false;
    }
    return true;
}

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
    wifi_setup_view.reset();
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


Level3ObjectList g_level3Objects;

inline void autoAssignIconAndColor(const String& name, SatIconType& icon, uint16_t& color) {
    OrbitUtils::autoAssignIconAndColor(name, icon, color);
}
inline double getGeoSlotLongitude(uint32_t noradId, const String& slotStr) {
    return OrbitUtils::getGeoSlotLongitude(noradId, slotStr);
}
inline void calculateGeoSatPosition(double satLonDeg, double userLatDeg, double userLonDeg, double userAltMeters, 
                                    GeodeticCoord& outGeo, ECEFCoord& outEcef, TopocentricCoord& outTopo, double& outSkewDeg) {
    OrbitUtils::calculateGeoSatPosition(satLonDeg, userLatDeg, userLonDeg, userAltMeters, outGeo, outEcef, outTopo, outSkewDeg);
}
inline String getShortNameForDisplay(const String& fullName, uint32_t epoch) {
    return OrbitUtils::getShortNameForDisplay(fullName, epoch);
}
inline void assignShortNameAndIcon(RecentLaunchItem& item) {
    OrbitUtils::assignShortNameAndIcon(item);
}
inline void calculateFormationsForItems(std::vector<RecentLaunchItem>& items, const std::vector<std::vector<float>>* providedPhases = nullptr) {
    OrbitUtils::calculateFormationsForItems(items, providedPhases);
}

void initRecentLaunchCalcs(RecentLaunchItem& item) {
    if (!item.selected) {
        item.calc.reset();
        return;
    }
    
    // 极速内存路径：若已有代表星 TLE，直接在内存瞬时初始化 SGP4 运算器（0 毫秒、0 磁盘 I/O）
    if (item.repTLE.line1.length() >= 14 && item.repTLE.line2.length() >= 14) {
        if (!item.calc) {
            item.calc = std::make_shared<SGP4Calc>();
            item.calc->init(item.repTLE);
        }
        item.cache.lastGeoValid = false;
        item.cache.isVisible = false;
        if (item.batchId == recentLaunchActiveBatchId) {
            g_repSatTLE = item.repTLE;
            if (item.calc) {
                g_repSatCalc = *(item.calc);
            }
            g_repSatName = item.repSatName.length() > 0 ? item.repSatName : item.displayName;
            g_repSatInitialized = true;
            g_repSatCache = item.cache;
        }
        return;
    }

    // 兜底慢速路径：仅在缺少 TLE 且无后台下载时才读取原始文件，加看门狗重置与让出 CPU
    if (recentLaunchDownloading) return;
    if (!LittleFS.exists("/json_recent_raw.jsonl")) return;

    File f = LittleFS.open("/json_recent_raw.jsonl", "r");
    if (f) {
        JSONParser parser;
        int lineCnt = 0;
        while (f.available()) {
            lineCnt++;
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
                    item.repTLE.name = record.name;
                    item.repTLE.baseScore = 0;
                    SGP4Calc::buildPseudoTle(record, item.repTLE.line1, item.repTLE.line2);
                    
                    if (item.batchId == recentLaunchActiveBatchId) {
                        g_repSatTLE = item.repTLE;
                        g_repSatCalc = *(item.calc);
                        g_repSatName = record.name;
                        g_repSatInitialized = true;
                        g_repSatCache = item.cache;
                    }
                    break;
                }
            }
            if (lineCnt % 50 == 0) {
                esp_task_wdt_reset();
                taskYIELD();
            }
        }
        f.close();
    }
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
// Set to 72 (59 curated builtin + 13 custom) to ensure all entries fit while keeping ample internal RAM
const int MAX_SATELLITES = 72;
SatRealtimeCache g_satCaches[MAX_SATELLITES];
int NUM_BUILTIN_SATELLITES = 0;
int NUM_SATELLITES = 0;

SatProfile g_satellites[MAX_SATELLITES];

// We use a simulated time starting near the TLE epoch for Phase 3 offline testing
volatile uint32_t current_unix = 0; // Will be set in setup()
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
inline void validateSatViewFocusState() {
    OrbitUtils::validateSatViewFocusState();
}

// double baseUserLat = 22.85; // Nanning (test location)
// double baseUserLon = 108.33;
double baseUserLat = 39.90; // Beijing
double baseUserLon = 116.40;
double baseUserAlt = 0.0; // Altitude in meters

inline void doScreenshot() {
    ImageUtils::captureScreenshot(earth_renderer);
}
// Helper to pre-calculate orbits with caching
inline void calculateOrbit(SGP4Calc& calc, uint32_t baseTime, OrbitCache& cache, int& calcCount, bool isTimeScrolling, bool forceUpdate = false) {
    OrbitUtils::calculateOrbit(calc, baseTime, cache, calcCount, isTimeScrolling, forceUpdate);
}

#include "core/observation_predictor.h"

TaskHandle_t predictorTaskHandle = NULL;
TaskHandle_t imuTaskHandle = NULL; // IMU task handle, used to pause IMU during Grove bus probing
SemaphoreHandle_t g_i2cBusMutex = NULL; // 全局 I2C 总线互斥锁，防止 Cap HY2.0 (8/9) 与 IMU 冲突
std::vector<PassEvent> recommendedPasses;
bool showRecommendations = false;
int passScrollIndex = 0;

bool catExpanded[4] = {false, false, false, false};
std::vector<TreeItem> displayTree;
int selectedPassIndex = -1; // For detail view

void rebuildTree(uint32_t current_unix) {
    lockPassMutex();
    rebuildTreeLocal(displayTree, recommendedPasses, current_unix);
    unlockPassMutex();
}

void updateChainMonoDisplay() {
    ChainMonoView::update();
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
bool entryRecentLaunchFocusMode = false;
String entryRecentLaunchActiveBatchId = "";

// Custom Satellite Input State
String noradInput = "";
String downloadErrorMsg = "";
int deleteConfirmIndex = -1;
bool isDownloadingCustom = false;


volatile bool triggerPrediction = true;
uint32_t lastTimeAdjustMillis = 0;
volatile uint32_t g_currentPredictingBaseTime = 0;

int getTotalSelectedSatelliteCount() {
    int count = 0;
    lockSatMutex();
    for (int i = 0; i < NUM_SATELLITES; i++) {
        if (g_satellites[i].selected) {
            count++;
        }
    }
    for (const auto& item : g_recentLaunches) {
        if (item.selected) {
            count++;
        }
    }
    unlockSatMutex();
    return count;
}

bool isCandidateForPassPrediction(int satIndex) {
    if (satIndex < 0 || satIndex >= NUM_SATELLITES) return false;
    
    bool isSel = false;
    SatelliteType type = SAT_TYPE_VISUAL;
    TLEData tle;
    float stdMag = 3.0f;
    uint32_t noradId = 0;
    
    lockSatMutex();
    isSel = g_satellites[satIndex].selected;
    if (isSel) {
        type = g_satellites[satIndex].type;
        tle = g_satellites[satIndex].tle;
        stdMag = g_satellites[satIndex].stdMag;
        noradId = g_satellites[satIndex].noradId;
    }
    unlockSatMutex();
    
    if (!isSel) return false;
    
    // 1. 严格排除非近地/无过境预测意义的目标：静止电视星、深空探测器、历史无轨道目标
    if (type == SAT_TYPE_GEO_TV || type == SAT_TYPE_DEEP_SPACE || type == SAT_TYPE_HISTORICAL) {
        return false;
    }
    
    // 2. TLE 完整性检查
    if (tle.line1.length() < 14 || tle.line2.length() < 63) return false;
    
    // 3. 轨道高度纳秒级快检：必须是近地轨道（LEO），每日运行圈数 mm >= 11.5（对应高度 < 1500km）
    // 瞬间排除中高轨（MEO 如 GPS/北斗、GEO/HEO 如静止卫星），避免无意义的 SGP4 积分运算开销
    double meanMotion = tle.line2.substring(52, 63).toDouble();
    if (meanMotion < 11.5) {
        return false;
    }
    
    // 4. 百科收录卫星检查：如果是业余无线电卫星，直接放行（支持全天候无线电通联）；若是纯目视卫星，则要求目视可见标签与高亮度
    bool isRadioSat = (type == SAT_TYPE_HAM);
    if (satIndex < NUM_BUILTIN_SATELLITES) {
        const EncyclopediaEntry* entry = Encyclopedia::getEntryByNorad(noradId);
        if (entry) {
            if ((entry->flags & FLAG_RADIO) != 0) {
                isRadioSat = true;
            }
            if (!isRadioSat) {
                bool isVisualVisible = (entry->flags & FLAG_VISIBLE) != 0;
                bool isHighBrightness = (entry->stdMag <= 4.2f);
                if (!isVisualVisible || !isHighBrightness) {
                    return false; // 排除暗弱且非无线电的目标
                }
            }
        }
    } else {
        // 自定义添加卫星：非无线电卫星若标准星等暗于 4.2 则排除
        if (!isRadioSat && stdMag > 4.2f) return false;
    }
    
    return true;
}

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
        
        // Heap Protection: 检查剩余总内存和最大连续内存块，防碎片化
        if (ESP.getFreeHeap() < 16000 || ESP.getMaxAllocHeap() < 5000) {
            LOG_I("APP", "Predictor task deferred: low heap safety guard triggered (free: %u, maxBlock: %u)", 
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        
        triggerPrediction = false;
        cancelPrediction = false; // 重置取消状态
        g_orbitCalculating = true;
        
        try {
            std::unique_ptr<ObservationPredictor> predictor(new ObservationPredictor(baseUserLat, baseUserLon, baseUserAlt / 1000.0, pos_manager));
            
            // Use simulated time for predictions
            uint32_t startTime = current_unix + timeMachineOffset;
            g_currentPredictingBaseTime = startTime;
            
            // 1. 收集满足预测条件的候选卫星与近期发射项
            std::vector<int> candidateSatIndices;
            for (int i = 0; i < NUM_SATELLITES; i++) {
                if (isCandidateForPassPrediction(i)) {
                    candidateSatIndices.push_back(i);
                }
            }
            
            std::vector<int> candidateRLIndices;
            lockSatMutex();
            for (int r = 0; r < (int)g_recentLaunches.size(); r++) {
                if (g_recentLaunches[r].selected) {
                    candidateRLIndices.push_back(r);
                }
            }
            unlockSatMutex();
            
            int totalCandidates = candidateSatIndices.size() + candidateRLIndices.size();
            
            if (totalCandidates == 0) {
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
            
            // === PHASE 1: Fast 24-Hour (Tonight) Pass Calculation (< 300ms) ===
            std::vector<PassEvent> phase1Passes;
            phase1Passes.reserve(24);
            
            // Phase 1 - 候选高亮度目视与业余无线电卫星
            for (int satIdx : candidateSatIndices) {
                vTaskDelay(1);
                if (triggerPrediction || cancelPrediction || g_networkActive) break;
                
                if (phase1Passes.size() >= 24 || ESP.getFreeHeap() < 24000 || ESP.getMaxAllocHeap() < 3500) {
                    LOG_I("APP", "Predictor task Phase 1 safely limited: heap protection or max passes reached (%u bytes free, %d passes)", 
                          (unsigned int)ESP.getFreeHeap(), (int)phase1Passes.size());
                    break;
                }
                
                TLEData tle;
                float stdMag = 3.0f;
                bool isRadioTarget = false;
                lockSatMutex();
                tle = g_satellites[satIdx].tle;
                stdMag = g_satellites[satIdx].stdMag;
                if (g_satellites[satIdx].type == SAT_TYPE_HAM) {
                    isRadioTarget = true;
                }
                unlockSatMutex();
                
                if (satIdx < NUM_BUILTIN_SATELLITES) {
                    const EncyclopediaEntry* entry = Encyclopedia::getEntryByNorad(g_satellites[satIdx].noradId);
                    if (entry && (entry->flags & FLAG_RADIO)) {
                        isRadioTarget = true;
                    }
                }
                
                auto passes1 = predictor->predictPasses(tle, stdMag, startTime, 1, isRadioTarget);
                if (passes1.size() > 2) {
                    std::sort(passes1.begin(), passes1.end(), [](const PassEvent& a, const PassEvent& b) {
                        return a.score > b.score;
                    });
                    passes1.resize(2);
                }
                for (auto& p : passes1) {
                    p.satSelected = true;
                    p.satIndex = satIdx;
                }
                phase1Passes.insert(phase1Passes.end(), passes1.begin(), passes1.end());
                completedCount++;
                predictionProgress = (completedCount * 50) / (totalCandidates > 0 ? totalCandidates : 1);
            }
            
            // Phase 1 - 近期发射已勾选项（只要勾选就参与计算）
            for (int rlIdx : candidateRLIndices) {
                vTaskDelay(1);
                if (triggerPrediction || cancelPrediction || g_networkActive) break;
                
                if (phase1Passes.size() >= 24 || ESP.getFreeHeap() < 24000 || ESP.getMaxAllocHeap() < 3500) {
                    break;
                }
                
                TLEData rlTle;
                lockSatMutex();
                if (rlIdx >= 0 && rlIdx < (int)g_recentLaunches.size()) {
                    rlTle = g_recentLaunches[rlIdx].repTLE;
                }
                unlockSatMutex();
                
                if (rlTle.line1.length() >= 14 && rlTle.line2.length() >= 14) {
                    auto passes1 = predictor->predictPasses(rlTle, 3.0, startTime, 1);
                    if (passes1.size() > 2) {
                        std::sort(passes1.begin(), passes1.end(), [](const PassEvent& a, const PassEvent& b) {
                            return a.score > b.score;
                        });
                        passes1.resize(2);
                    }
                    for (auto& p : passes1) {
                        p.satSelected = true;
                        p.satIndex = -100;
                    }
                    phase1Passes.insert(phase1Passes.end(), passes1.begin(), passes1.end());
                }
                completedCount++;
                predictionProgress = (completedCount * 50) / (totalCandidates > 0 ? totalCandidates : 1);
            }
            
            if (triggerPrediction || cancelPrediction || g_networkActive) {
                if (cancelPrediction) {
                    cancelPrediction = false;
                    g_orbitCalculating = false;
                    g_currentPredictingBaseTime = 0;
                }
                continue;
            }
            
            // 立即发布 Phase 1 今夜过境至 UI！
            std::vector<PassEvent> upcomingPhase1;
            upcomingPhase1.reserve(phase1Passes.size());
            for (const auto& pass : phase1Passes) {
                if (pass.losTime >= current_unix + timeMachineOffset) {
                    upcomingPhase1.push_back(pass);
                }
            }
            std::sort(upcomingPhase1.begin(), upcomingPhase1.end(), [](const PassEvent& a, const PassEvent& b) {
                return a.aosTime < b.aosTime;
            });
            if (upcomingPhase1.size() > 24) {
                upcomingPhase1.resize(24);
            }
            
            std::vector<TreeItem> tempDisplayTree1;
            rebuildTreeLocal(tempDisplayTree1, upcomingPhase1, current_unix + timeMachineOffset);
            
            // 立即以 swap 零拷贝安全发布至 UI，绝不执行 operator= 避免 bad_alloc 崩溃
            lockPassMutex();
            recommendedPasses = upcomingPhase1;
            displayTree.swap(tempDisplayTree1);
            predictionsReady = true;
            lastPredictionBaseTime = startTime;
            unlockPassMutex();
            
            // 释放临时树，但完整保留 phase1Passes 作为 Phase 2 坚实基础
            tempDisplayTree1.clear();
            tempDisplayTree1.shrink_to_fit();
            upcomingPhase1.clear();
            upcomingPhase1.shrink_to_fit();
            
            // === PHASE 2: Background 7-Day Full Pass Calculation ===
            completedCount = 0;
            std::vector<PassEvent> allPasses = phase1Passes; // 继承今晚所有有效事件，杜绝今晚事件被未来挤掉！
            phase1Passes.clear();
            phase1Passes.shrink_to_fit();
            
            auto isAlreadyInAllPasses = [&](const PassEvent& p) -> bool {
                for (const auto& exist : allPasses) {
                    if (exist.satName == p.satName && abs((long)(exist.aosTime - p.aosTime)) < 120) {
                        return true;
                    }
                }
                return false;
            };

            // Phase 2 - 候选高亮度目视收录卫星
            for (int satIdx : candidateSatIndices) {
                vTaskDelay(1);
                if (triggerPrediction || cancelPrediction || g_networkActive) break;
                
                if (ESP.getFreeHeap() < 24000 || allPasses.size() >= 48) {
                    LOG_I("APP", "Predictor task Phase 2 safely limited: heap protection or max passes reached (%u bytes free, %d passes)", 
                          ESP.getFreeHeap(), (int)allPasses.size());
                    break;
                }
                
                TLEData tle;
                float stdMag = 3.0f;
                bool isRadioTarget = false;
                lockSatMutex();
                tle = g_satellites[satIdx].tle;
                stdMag = g_satellites[satIdx].stdMag;
                if (g_satellites[satIdx].type == SAT_TYPE_HAM) {
                    isRadioTarget = true;
                }
                unlockSatMutex();
                
                if (satIdx < NUM_BUILTIN_SATELLITES) {
                    const EncyclopediaEntry* entry = Encyclopedia::getEntryByNorad(g_satellites[satIdx].noradId);
                    if (entry && (entry->flags & FLAG_RADIO)) {
                        isRadioTarget = true;
                    }
                }
                
                auto passes = predictor->predictPasses(tle, stdMag, startTime, 7, isRadioTarget);
                // 质量优先排序
                std::sort(passes.begin(), passes.end(), [](const PassEvent& a, const PassEvent& b) {
                    if (a.score != b.score) return a.score > b.score;
                    return a.aosTime < b.aosTime;
                });
                int added = 0;
                for (auto& p : passes) {
                    if (added >= 4) break;
                    if (!isAlreadyInAllPasses(p)) {
                        p.satSelected = true;
                        p.satIndex = satIdx;
                        allPasses.push_back(p);
                        added++;
                    }
                }
                completedCount++;
                predictionProgress = 50 + (completedCount * 50) / (totalCandidates > 0 ? totalCandidates : 1);
            }
            
            // Phase 2 - 近期发射已勾选项
            for (int rlIdx : candidateRLIndices) {
                vTaskDelay(1);
                if (triggerPrediction || cancelPrediction || g_networkActive) break;
                
                if (ESP.getFreeHeap() < 24000 || allPasses.size() >= 48) {
                    LOG_I("APP", "Predictor task Phase 2 safely limited: heap protection or max passes reached (%u bytes free, %d passes)", 
                          ESP.getFreeHeap(), (int)allPasses.size());
                    break;
                }
                
                TLEData rlTle;
                lockSatMutex();
                if (rlIdx >= 0 && rlIdx < (int)g_recentLaunches.size()) {
                    rlTle = g_recentLaunches[rlIdx].repTLE;
                }
                unlockSatMutex();
                
                if (rlTle.line1.length() >= 14 && rlTle.line2.length() >= 14) {
                    auto passes = predictor->predictPasses(rlTle, 3.0, startTime, 7);
                    std::sort(passes.begin(), passes.end(), [](const PassEvent& a, const PassEvent& b) {
                        if (a.score != b.score) return a.score > b.score;
                        return a.aosTime < b.aosTime;
                    });
                    int added = 0;
                    for (auto& p : passes) {
                        if (added >= 4) break;
                        if (!isAlreadyInAllPasses(p)) {
                            p.satSelected = true;
                            p.satIndex = -100;
                            allPasses.push_back(p);
                            added++;
                        }
                    }
                }
                completedCount++;
                predictionProgress = 50 + (completedCount * 50) / (totalCandidates > 0 ? totalCandidates : 1);
            }
            
            predictionProgress = 100;

        
        if (triggerPrediction || cancelPrediction || g_networkActive) {
            if (cancelPrediction) {
                cancelPrediction = false;
                g_orbitCalculating = false; // 强行熄灭 Chain Mono 的计算动画
                g_currentPredictingBaseTime = 0;
            }
            continue;
        }
        
        // Filter out past passes relative to the simulated time
        std::vector<PassEvent> upcomingPasses;
        upcomingPasses.reserve(allPasses.size());
        for (const auto& pass : allPasses) {
            if (pass.losTime >= current_unix + timeMachineOffset) {
                upcomingPasses.push_back(pass);
            }
        }
        
        // 核心保护机制：确保“今晚（24小时内）”的所有有效事件完整保留，绝不被未来事件挤掉
        uint32_t tonightLimit = current_unix + timeMachineOffset + 24 * 3600;
        std::vector<PassEvent> tonightList;
        std::vector<PassEvent> futureList;
        tonightList.reserve(upcomingPasses.size());
        futureList.reserve(upcomingPasses.size());

        for (const auto& p : upcomingPasses) {
            if (p.aosTime < tonightLimit) {
                tonightList.push_back(p);
            } else {
                futureList.push_back(p);
            }
        }

        // 未来事件按分数优先（高质量优先），其次按时间先后排列
        std::sort(futureList.begin(), futureList.end(), [](const PassEvent& a, const PassEvent& b) {
            if (a.score != b.score) return a.score > b.score;
            return a.aosTime < b.aosTime;
        });

        // 总常驻事件容量放宽至 36 个（满足用户对丰富过境事件的需求）
        const size_t TOTAL_MAX_PASSES = 36;
        size_t allowedFuture = (TOTAL_MAX_PASSES > tonightList.size()) ? (TOTAL_MAX_PASSES - tonightList.size()) : 0;
        if (futureList.size() > allowedFuture) {
            futureList.resize(allowedFuture);
        }

        // 合并今晚与未来事件
        upcomingPasses = std::move(tonightList);
        upcomingPasses.insert(upcomingPasses.end(), futureList.begin(), futureList.end());

        // 最终列表统一按时间先后升序排列，使 UI 各分类展开均呈现清晰的时间流
        std::sort(upcomingPasses.begin(), upcomingPasses.end(), [](const PassEvent& a, const PassEvent& b) {
            return a.aosTime < b.aosTime;
        });

        // Compute local temporary variables outside the critical section to prevent malloc/OOM within spinlocks
        std::vector<TreeItem> tempDisplayTree;
        rebuildTreeLocal(tempDisplayTree, upcomingPasses, current_unix + timeMachineOffset);
        
        lockPassMutex();
        recommendedPasses.swap(upcomingPasses);
        displayTree.swap(tempDisplayTree);
        predictionsReady = true;
        lastPredictionBaseTime = startTime; // 写入本次成功的基准时间缓存
        g_currentPredictingBaseTime = 0;
        unlockPassMutex();
        
        allPasses.clear();
        allPasses.shrink_to_fit();
        upcomingPasses.clear();
        upcomingPasses.shrink_to_fit();
        tempDisplayTree.clear();
        tempDisplayTree.shrink_to_fit();
        
        if (g_orbitCalculating) {
            g_orbitCalculating = false;
            g_readyStartTime = millis(); // Trigger 2-second READY effect
        }
        } catch (const std::bad_alloc& e) {
            LOG_W("APP", "Predictor task caught std::bad_alloc (OOM prevented). Deferring calculation.");
            g_orbitCalculating = false;
            g_currentPredictingBaseTime = 0;
            vTaskDelay(pdMS_TO_TICKS(2000));
        } catch (...) {
            LOG_W("APP", "Predictor task caught unknown exception. Safely recovering.");
            g_orbitCalculating = false;
            g_currentPredictingBaseTime = 0;
            vTaskDelay(pdMS_TO_TICKS(1000));
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
        client->stop();
        
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
        client->stop();
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

inline void wrapTextIntoLines(LGFX_Sprite* canvas, const String& text, int w, std::vector<String>& outLines) {
    TextUtils::wrapTextIntoLines(canvas, text, w, outLines);
}
inline String truncateUtf8Chars(const String& str, size_t maxChars) {
    return TextUtils::truncateUtf8(str, maxChars);
}
inline int drawWrappedText(LGFX_Sprite* canvas, String text, int x, int y, int w, int lineH, bool draw = true) {
    return TextUtils::drawWrappedText(canvas, text, x, y, w, lineH, draw);
}
inline void drawScrollingText(LGFX_Sprite* canvas, const char* text, int x, int y, int maxWidth, uint16_t color) {
    TextUtils::drawScrollingText(canvas, text, x, y, maxWidth, color);
}

struct WiFiDisconnectGuard {
    ~WiFiDisconnectGuard() {
        LOG_I("APP", "Network task complete. Turning off WiFi to reclaim memory.");
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
            recentLaunchErrorMsg = (I18N::getLanguage() == LANG_ZH) ? "未找到已知WiFi，请配置" : "WiFi not found, please configure";
            recentLaunchDownloading = false;
            recentLaunchDownloadFinishedMs = millis();
            g_wifiSetupReturnState = STATE_SAT_SELECT;
            appState = STATE_WIFI_SETUP;
            wifi_setup_view.startScan();
            return;
        }
    }
    
    // 2. Sync Time (NTP)
    // Pass gmtOffset_sec=0 to sync to UTC. getUnixTime() returns time() which is
    // affected by configTime()'s timezone offset. We always work in UTC internally.
    recentLaunchErrorMsg = "Syncing NTP time...";
    HalWifi::syncNTPTime(0);
    uint32_t ntpTime = HalWifi::getUnixTime();
    if (ntpTime > 0) {
        current_unix = ntpTime;
        g_timeSynced = true;
        LOG_I("RECENT_LAUNCH", "Time synced to UTC: %u", current_unix);
        lockPassMutex();
        lastPredictionBaseTime = 0;
        predictionsReady = false;
        unlockPassMutex();
        triggerPrediction = true;
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
                if (httpCode == -100) {
                    recentLaunchErrorMsg = "Storage Error";
                } else if (httpCode == -11) {
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
                
                bool hasFreq = false;
                for (int i = 0; i < NUM_SATELLITES; i++) {
                    if (g_satellites[i].noradId == id && g_satellites[i].downlinkFreq.length() > 0) {
                        hasFreq = true;
                        break;
                    }
                }
                downloadErrorMsg = "Download Success!";
                noradInput = "";
                updateEncyclopediaFilteredList();
                
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
            wifi_setup_view.startScan();
        }
    }
    
    isDownloadingCustom = false;
    vTaskDelete(NULL);
}

void networkTaskImpl(void* parameter) {
    NetworkActiveGuard guard;
    PredictorTaskSuspendGuard predGuard;
    WiFiDisconnectGuard wifiGuard;
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
        if (manualWifiToggle || appState == STATE_MAIN || appState == STATE_SAT_SELECT) {
            g_wifiSetupReturnState = appState;
            appState = STATE_WIFI_SETUP;
            wifi_setup_view.startScan();
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
        LOG_I("APP", "WiFi connection failed. Entering setup or offline mode.");
        if (appState == STATE_SAT_SELECT) {
            downloadErrorMsg = (I18N::getLanguage() == LANG_ZH) ? "未找到已知WiFi，请配置" : "WiFi not found, please configure";
            downloadFinishedMs = millis();
        }
        // 当旧凭据在新网络环境中无法连接时，自动弹出 WiFi 扫描配置供用户选择当前网络
        g_wifiSetupReturnState = appState;
        appState = STATE_WIFI_SETUP;
        wifi_setup_view.startScan();
        
        g_wifiConnecting = false;
        g_dataUpdating = false;
        g_timeSynced = true;
        triggerPrediction = true;
        HalWifi::disconnect(); // 失败后关闭驱动，让后续扫描重新初始化
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
        
        // 2. Fetch NTP (UTC, gmtOffset_sec=0 ensures time() returns UTC)
        HalWifi::syncNTPTime(0);
        
        // 3. Update time
        uint32_t ntpTime = HalWifi::getUnixTime();
        if (ntpTime > 0) {
            current_unix = ntpTime;
            g_timeSynced = true;
            LOG_I("APP", "Time synced to UTC: %u", current_unix);
            lockPassMutex();
            lastPredictionBaseTime = 0;
            predictionsReady = false;
            unlockPassMutex();
            triggerPrediction = true;
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
            if (noradId == 100532) {
                // NGRST (Roman) uses hardcoded TLE — no network needed
                TLEData rTle = TLEManager::getNGRST_TLE();
                lockSatMutex();
                g_satellites[i].tle  = rTle;
                g_satellites[i].calc.init(rTle);
                unlockSatMutex();
                updated = true;
                continue;
            }
            if (noradId == 34937) {
                // Herschel uses hardcoded TLE — no network needed
                TLEData hTle = TLEManager::getHerschel_TLE();
                lockSatMutex();
                g_satellites[i].tle  = hTle;
                g_satellites[i].calc.init(hTle);
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
            LOG_I("APP", "Fetching %d stale satellites via HTTP (300ms throttled, shared socket)...", totalStale);
            WiFiClient sharedClient;
            
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
                if (OrbitDataProvider::loadByCatalogNumber(noradId, rec, true, &sharedClient, &httpCode)) {
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

                    // 核心熔断机制：一旦发生网络断开、拒连或被 CelesTrak 限流封控，立即中止后续所有请求！
                    if (httpCode < 0 || httpCode == 429 || httpCode == 403) {
                        LOG_W("APP", "Aborting CelesTrak queries early (Error: %d). Suppressed remaining %d queries.", 
                              httpCode, totalStale - k - 1);
                        break;
                    }
                }
                vTaskDelay(pdMS_TO_TICKS(300)); // 300ms delay to prevent CelesTrak WAF/rate-limiting
            }
            sharedClient.stop();
        }

        // 3.5 仅在有卫星数据更新且未发生拒连时，才同步频率数据；若原本新鲜则跳过网络请求
        if (updated || !LittleFS.exists("/frequencies.json")) {
            if (appState == STATE_SAT_SELECT) {
                downloadErrorMsg = "Syncing frequencies...";
            }
            fetchFrequencies();
        }
        
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
        // 无论何种模式，数据同步任务执行完毕后均关闭 WiFi，彻底释放硬件驱动与内存给系统堆
        LOG_I("APP", "Network tasks complete. Turning off WiFi to save power and free memory.");
        HalWifi::disconnect();
    }
    
    vTaskDelay(pdMS_TO_TICKS(250)); // Allow LwIP sockets and TCP buffers to be fully reclaimed by ESP32 heap
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
    std::vector<RecentLaunchItem> tempLaunches;
    
    // 1. 方案一：优先从极速二进制快照恢复（耗时 < 5ms，开机极速秒过）
    if (OrbitDataProvider::loadRecentLaunchesMeta(tempLaunches) && !tempLaunches.empty()) {
        bool needsTleUpgrade = false;
        if (tempLaunches[0].repTLE.line1.length() < 14 && LittleFS.exists("/json_recent_raw.jsonl")) {
            needsTleUpgrade = true;
        }
        if (!needsTleUpgrade) {
            lockSatMutex();
            g_recentLaunches = std::move(tempLaunches);
            unlockSatMutex();
            recentLaunchDownloadSuccess = false;
            recentLaunchSelectedIndex = 0;
            recentLaunchErrorMsg = "";
            LOG_I("RECENT_LAUNCH", "Fast boot: Loaded %d launches from meta snapshot!", (int)g_recentLaunches.size());
            return;
        }
        LOG_I("RECENT_LAUNCH", "Meta snapshot lacks repTLE. Upgrading to v2 from local raw JSONL once...");
    }
    
    // 2. 若快照未命中，回退到从原始 JSONL 缓存文件执行单趟流式极速解析
    if (!LittleFS.exists("/json_recent_raw.jsonl")) {
        LOG_I("RECENT_LAUNCH", "No local cache JSONL file found.");
        return;
    }
    
    std::vector<std::vector<float>> rawPhases;
    if (OrbitDataProvider::loadRecentLaunchesFromCache(tempLaunches, &rawPhases) && !tempLaunches.empty()) {
        // 单趟解析完成：直接传入各批次相位数据计算编队与聚类，无需重复打开或二次解析文件
        calculateFormationsForItems(tempLaunches, &rawPhases);
        
        // 按照发射批次年份和序号降序排序
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
        
        // 保存排好序的高速快照，供下次开机毫秒级秒开
        OrbitDataProvider::saveRecentLaunchesMeta(tempLaunches);
        
        lockSatMutex();
        g_recentLaunches = std::move(tempLaunches);
        unlockSatMutex();
        recentLaunchDownloadSuccess = false;
        recentLaunchSelectedIndex = 0;
        recentLaunchErrorMsg = "";
        LOG_I("RECENT_LAUNCH", "Loaded %d launches from local cache and created meta snapshot.", (int)g_recentLaunches.size());
    } else {
        LOG_I("RECENT_LAUNCH", "Failed to parse local cache JSONL.");
    }
}

void saveSatelliteRadioConfig(int noradId, const String& freq, const String& mode) {
    Preferences prefs;
    if (prefs.begin("sat_radio", false)) {
        prefs.putString(("f_" + String(noradId)).c_str(), freq);
        prefs.putString(("m_" + String(noradId)).c_str(), mode);
        prefs.end();
    }
}

void loadSatelliteRadioConfig(int noradId, String& outFreq, String& outMode) {
    Preferences prefs;
    if (prefs.begin("sat_radio", true)) {
        outFreq = prefs.getString(("f_" + String(noradId)).c_str(), "");
        outMode = prefs.getString(("m_" + String(noradId)).c_str(), "");
        prefs.end();
    }
}

void saveCustomSatellites() {
    Preferences prefs;
    prefs.begin("satellites", false);
    String idList = "";
    for (int i = NUM_BUILTIN_SATELLITES; i < NUM_SATELLITES; i++) {
        idList += String(g_satellites[i].noradId);
        if (i < NUM_SATELLITES - 1) idList += ",";
        if (g_satellites[i].downlinkFreq.length() > 0) {
            saveSatelliteRadioConfig(g_satellites[i].noradId, g_satellites[i].downlinkFreq, g_satellites[i].radioMode);
        }
    }
    prefs.putString("customIds", idList);
    prefs.end();
}

volatile bool g_isFastForwarding = false;
volatile bool g_imuSamplingEnabled = true; // 控制 IMU 是否采样，替代危险的 vTaskSuspend

void imuTask(void* pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    while (true) {
        TickType_t xFrequency = pdMS_TO_TICKS(10); // 恒定 100Hz 高速采样，保证极佳的跟手性
        if (g_imuSamplingEnabled && attitude) {
            attitude->update(); // attitude->update() 内部已包含 _imu->update()
        }
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

volatile bool g_loadingFinished = false;
volatile int g_loadingProgress = 18;
String g_loadingStatusText = "";

void drawStartupScreen(int progressPercentage, bool showLangSelect = false, int selectedLangIndex = 0) {
    StartupView::draw(progressPercentage, showLangSelect, selectedLangIndex);
}

void drawLangSelectDialog(LGFX_Sprite* canvas) {
    DialogViews::drawLangSelectDialog(canvas, langSelectedIndex);
}

void setup() {
    if (!g_satMutex) {
        g_satMutex = xSemaphoreCreateMutex();
    }
    if (!g_passMutex) {
        g_passMutex = xSemaphoreCreateMutex();
    }
    if (!g_i2cBusMutex) {
        g_i2cBusMutex = xSemaphoreCreateMutex();
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

    // 检查是否为首次开机（无硬件配置记录）
    HardwareConfig::getInstance().load();
    if (!HardwareConfig::getInstance().isConfigured()) {
        LOG_I("APP", "[HW] First boot detected (unconfigured). Launching Hardware Setup Wizard...");
        appState = STATE_HW_WIZARD;
        hardware_wizard.reset();
    }

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
            if (NUM_BUILTIN_SATELLITES > MAX_SATELLITES - 10) {
                NUM_BUILTIN_SATELLITES = MAX_SATELLITES - 10;
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

            // -----------------------------------------------------------------
            // 精准定向硬件外设初始化（由 HardwareConfig 动态驱动，彻底消除引脚争抢）
            // -----------------------------------------------------------------
            auto& hw = HardwareConfig::getInstance();
            hw.load();

            // 若配置了任何机身 Grove 或外部扩展外设，显式开启 Cardputer 外部 5V 升压供电总线
            if (hw.isEnabled(HW_MOD_CAP_LORA1262) || hw.isEnabled(HW_MOD_UNIT_GPSV11) || 
                hw.isEnabled(HW_MOD_CHAIN_MONO) || hw.isEnabled(HW_MOD_UNIT_8SERVOS)) {
                LOG_I("APP", "[HW] Enabling external 5V power bus for Grove/Cap peripherals...");
                M5Cardputer.Power.setExtOutput(true);
                delay(60); // 留出外部外设上电启动及电压稳定时间
            }

            // 1. GNSS 模块定向点火
            if (hw.isEnabled(HW_MOD_CAP_LORA1262)) {
                LOG_I("APP", "[HW] Cap LoRa-1262 GNSS selected. Starting GNSS on RX=15, TX=13...");
                gnss->begin(15, 13, 115200);
                pos_manager = new PositionManager(gnss);
                pos_manager->begin();

                // 1.1 Cap LoRa-1262 射频芯片 (SX1262) 初始化
                LOG_I("APP", "[HW] Cap LoRa-1262 RF selected. Initializing RadioManager...");
                RadioManager::getInstance().init();
            } else if (hw.isEnabled(HW_MOD_UNIT_GPSV11)) {
                LOG_I("APP", "[HW] Unit GPS v1.1 selected. Starting GNSS on Grove port (RX=1, TX=2)...");
                gnss->begin(1, 2, 115200);
                pos_manager = new PositionManager(gnss);
                pos_manager->begin();
            } else {
                LOG_I("APP", "[HW] No GNSS module configured. Running in standalone cached/manual position mode.");
                pos_manager = new PositionManager(nullptr);
                pos_manager->begin();
                if (gnss) gnss->disable();
            }

            // 2. Unit 8Servos 浑仪云台舵机初始化
            if (hw.isEnabled(HW_MOD_UNIT_8SERVOS)) {
                g_imuSamplingEnabled = false;
                delay(15);
                
                if (hw.isEnabled(HW_MOD_CAP_LORA1262)) {
                    // 确保 Cap 上的 IO 扩展芯片使能供电 (PI4IOE5V6408)
                    uint8_t addrs[] = {0x43, 0x44};
                    for (uint8_t addr : addrs) {
                        if (M5.In_I2C.writeRegister8(addr, 0x07, 0x00, 100000)) {
                            M5.In_I2C.writeRegister8(addr, 0x03, 0xFF, 100000);
                            M5.In_I2C.writeRegister8(addr, 0x05, 0xFF, 100000);
                        }
                    }
                    delay(30);
                    // 按用户硬件配置：有 Cap 时，严格分配至 Cap 上的 HY2.0-4P 扩展口 (M5.In_I2C: SDA=8, SCL=9)
                    LOG_I("APP", "[HW] Initializing 3-axis Gimbal on Cap HY2.0 port (SDA=8, SCL=9, 100kHz)...");
                    gimbal.begin(&Wire, 8, 9, 100000);
                } else {
                    // 按用户硬件配置：无 Cap 时，严格分配至机身侧面 Grove 接口 (Wire: SDA=2, SCL=1)
                    LOG_I("APP", "[HW] Initializing 3-axis Gimbal on Body Grove port (Wire: SDA=2, SCL=1, 100kHz)...");
                    gimbal.begin(&Wire, 2, 1, 100000);
                }
                
                g_imuSamplingEnabled = true;
            } else {
                LOG_I("APP", "[HW] Unit 8Servos is disabled.");
            }

            // 3. Chain Mono 8x8 像素副屏初始化
            if (hw.isEnabled(HW_MOD_CHAIN_MONO)) {
                LOG_I("APP", "[HW] Initializing Chain Mono on Body Grove port (115200)...");
                
                g_imuSamplingEnabled = false;
                delay(15);
                
                uint16_t device_nums = 0;
                // Cardputer Grove 口标准 UART 线序: Pin1(G1)=外设TX -> RX=1, Pin2(G2)=外设RX -> TX=2
                // 备用线序: RX=2, TX=1 (应对交叉线或特殊固件)
                const int pinPairs[2][2] = {{1, 2}, {2, 1}};
                bool detected = false;
                
                for (int p = 0; p < 2; p++) {
                    int rxPin = pinPairs[p][0];
                    int txPin = pinPairs[p][1];
                    M5Chain.begin(&Serial2, 115200, rxPin, txPin);
                    delay(30);
                    
                    if (M5Chain.getDeviceNum(&device_nums, 80) == CHAIN_OK && device_nums > 0) {
                        LOG_I("APP", "[HW] Chain Mono detected on Grove (RX=%d, TX=%d)! Devices: %d", rxPin, txPin, device_nums);
                        detected = true;
                        break;
                    }
                    Serial2.end();
                }
                
                if (detected) {
                    device_info_t *infos = (device_info_t *)malloc(sizeof(device_info_t) * device_nums);
                    if (infos != nullptr) {
                        memset(infos, 0, sizeof(device_info_t) * device_nums);
                        device_list_t devices;
                        devices.count = device_nums;
                        devices.devices = infos;
                        if (M5Chain.getDeviceList(&devices, 100)) {
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
                    if (isMonoInitialized) {
                        M5Chain.setMonoMode(mono_id, MONO_PIXEL_MODE, &operation_status);
                        M5Chain.setMonoRotation(mono_id, MONO_ROTATION_0, &operation_status);
                        M5Chain.setMonoBrightness(mono_id, MONO_BRIGHTNESS_LEVEL_7, &operation_status);
                        M5Chain.setMonoClear(mono_id, &operation_status);
                        LOG_I("APP", "[HW] Chain Mono configured successfully (ID=%d).", mono_id);
                    }
                } else {
                    LOG_W("APP", "[HW] Chain Mono not responding on Grove port. Make sure cable is in 'IN' port (NOT 'OUT') and firmly seated.");
                    Serial2.end();
                }
                
                g_imuSamplingEnabled = true;
            } else {
                isMonoInitialized = false;
            }  
            
            Language currL = I18N::getLanguage();
            g_loadingStatusText = (currL == LANG_ZH) ? "初始化传感器与外设..." : ((currL == LANG_JA) ? "センサー・外来機器の初期化中..." : ((currL == LANG_ES) ? "Inicializando sensores..." : "Initializing Hardware..."));
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
            
            Language currL_pos = I18N::getLanguage();
            g_loadingStatusText = (currL_pos == LANG_ZH) ? "载入观测坐标与太阳模型..." : ((currL_pos == LANG_JA) ? "観測座標・太陽モデルの読み込み中..." : ((currL_pos == LANG_ES) ? "Cargando ubicación y Sol..." : "Loading Location & Sun Data..."));
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
            Language currL_parse = I18N::getLanguage();
            for (int i = 0; i < NUM_SATELLITES; i++) {
                if (g_satellites[i].type == SAT_TYPE_GEO_TV || g_satellites[i].type == SAT_TYPE_DEEP_SPACE) {
                    continue;
                }
                g_loadingStatusText = (currL_parse == LANG_ZH) ? ("解析轨道: " + g_satellites[i].name) : ((currL_parse == LANG_JA) ? ("軌道解析中: " + g_satellites[i].name) : ((currL_parse == LANG_ES) ? ("Analizando órbita: " + g_satellites[i].name) : ("Parsing Orbit: " + g_satellites[i].name)));
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
                    else if (norad == 100532) g_satellites[i].tle = TLEManager::getNGRST_TLE();
                    else if (norad == 34937) g_satellites[i].tle = TLEManager::getHerschel_TLE();
                    else if (norad == 27607) g_satellites[i].tle = TLEManager::getSO50_TLE();
                    else if (norad == 43017) g_satellites[i].tle = TLEManager::getAO91_TLE();
                    else if (norad == 46494) g_satellites[i].tle = TLEManager::getNORBI_TLE();
                    else if (norad == 62676) g_satellites[i].tle = TLEManager::getFOSSASAT2E_TLE();
                    else if (norad == 40908) g_satellites[i].tle = TLEManager::getLilacSat2_TLE();
                    else if (norad == 50466) g_satellites[i].tle = TLEManager::getXW3_TLE();
                    else if (norad == 59112) g_satellites[i].tle = TLEManager::getSONATE2_TLE();
                    else if (norad == 61751) g_satellites[i].tle = TLEManager::getVladivostok1_TLE();
                    else if (norad == 57179) g_satellites[i].tle = TLEManager::getNORBY2_TLE();
                    else if (norad == 57172) g_satellites[i].tle = TLEManager::getUMKA1_TLE();
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
            
            g_loadingStatusText = (currL_parse == LANG_ZH) ? "解算自定义目标与频段数据..." : ((currL_parse == LANG_JA) ? "カスタム目標・周波数の計算中..." : ((currL_parse == LANG_ES) ? "Cargando satélites personalizados..." : "Loading Custom Satellites..."));
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
                            } else {
                                String savedFreq = "", savedMode = "";
                                loadSatelliteRadioConfig(p.noradId, savedFreq, savedMode);
                                if (savedFreq.length() > 0) {
                                    p.downlinkFreq = savedFreq;
                                    if (savedMode.length() > 0) p.radioMode = savedMode;
                                    p.type = SAT_TYPE_HAM;
                                }
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
            updateEncyclopediaFilteredList();
            
            Language currL_boot = I18N::getLanguage();
            g_loadingStatusText = (currL_boot == LANG_ZH) ? "构建火箭与群编队数据..." : ((currL_boot == LANG_JA) ? "ロケット・編隊データの構築中..." : ((currL_boot == LANG_ES) ? "Construyendo formaciones..." : "Building Launch Formations..."));
            g_loadingProgress = 85;
            tryLoadRecentLaunchCache();
            
            g_loadingStatusText = (currL_boot == LANG_ZH) ? "启动核心推算引擎..." : ((currL_boot == LANG_JA) ? "推算エンジンの起動中..." : ((currL_boot == LANG_ES) ? "Iniciando motor de predicción..." : "Starting Predictor Engine..."));
            g_loadingProgress = 95;
            
            // Start predictor task on Core 0 for offline data (UI runs on Core 1)
            xTaskCreatePinnedToCore(
                predictorTask,
                "PredictorTask",
                6144,
                NULL,
                1,
                &predictorTaskHandle,
                0
            );
            
            // Start network task on Core 0 to handle WiFi and TLE fetching in background
            manualWifiToggle = false; // 开机默认自动模式：数据新鲜则跳过更新，完成同步后自动关闭 WiFi
            xTaskCreatePinnedToCore(networkTask, "NetworkTask", 5120, NULL, 1, NULL, 0);

            g_loadingStatusText = (currL_boot == LANG_ZH) ? "加载完成，准备就绪！" : ((currL_boot == LANG_JA) ? "ロード完了、準備完了！" : ((currL_boot == LANG_ES) ? "¡Listo!" : "Ready!"));
            g_loadingProgress = 100;
            delay(100);
            g_loadingFinished = true;
            vTaskDelete(NULL);
        },
        "SetupLoader",
        7168,
        NULL,
        2, // Slightly lower than IMU but higher than predictor
        NULL,
        0
    );
    
    // Smooth rendering loop on Core 1 (main setup thread)
    bool needsLangSelect = I18N::isFirstStart();
    int selectedLangIdx = (int)I18N::getLanguage();
    if (selectedLangIdx < 0 || selectedLangIdx > 3) selectedLangIdx = 1; // 默认简体中文

    while (!g_loadingFinished || needsLangSelect) {
        M5Cardputer.update();
        if (needsLangSelect) {
            static bool lastUp = false, lastDown = false, lastEnter = false;
            bool currUp = M5Cardputer.Keyboard.isKeyPressed(';');
            bool currDown = M5Cardputer.Keyboard.isKeyPressed('.');
            bool currEnter = M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER);

            if (currUp && !lastUp) {
                selectedLangIdx = (selectedLangIdx - 1 + 4) % 4;
            }
            if (currDown && !lastDown) {
                selectedLangIdx = (selectedLangIdx + 1) % 4;
            }
            if (currEnter && !lastEnter) {
                I18N::setLanguage((Language)selectedLangIdx);
                I18N::setFirstStartDone();
                needsLangSelect = false;
            }
            lastUp = currUp;
            lastDown = currDown;
            lastEnter = currEnter;
        }
        drawStartupScreen(g_loadingProgress, needsLangSelect, selectedLangIdx);
        updateChainMonoDisplay();
        delay(25);
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




void updateEncyclopediaFilteredList() {
    sat_select_view.updateFilteredList();
}

void drawSatSelectPage() {
    sat_select_view.draw(earth_renderer->getCanvas());
}

void updateRadioTrackingPipeline(uint32_t currentSimTime, int32_t tmOffset) {
    if (appState != STATE_MAIN && !RfConsoleView::getInstance().isActive()) {
        return;
    }
    RadioTrackingPipeline::update(currentSimTime, tmOffset);
}

void loop() {
    gimbal.tick();
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
                    // Serial.printf("[Debug] Time Machine resumed but cache invalid (day crossed). Resetting prediction. baseTime=%u, targetTime=%u\n", baseTime, targetTime);
                    lockPassMutex();
                    predictionsReady = false;
                    lastPredictionBaseTime = 0; // Invalid cache
                    g_currentPredictingBaseTime = 0;
                    cancelPrediction = true; // 跨天时必须打断当前进行的计算并重算
                    unlockPassMutex();
                    triggerPrediction = true;
                } else {
                    // Serial.printf("[Debug] Time Machine resumed, cache is valid (same day). Continuing calculation or keeping cache. baseTime=%u\n", baseTime);
                }
            } else {
                // Serial.println("[Debug] Time Machine resumed. Panel closed, skipping cross-day recalculation checks.");
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
            // Serial.printf("[Debug] Cache reset due to main loop coords change: oldLat=%f, newLat=%f, oldLon=%f, newLon=%f, oldAlt=%f, newAlt=%f\n", 
            //               oldLat, baseUserLat, oldLon, baseUserLon, oldAlt, baseUserAlt);
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
        std::vector<std::vector<float>> rawPhases;
        if (tempLaunches && OrbitDataProvider::loadRecentLaunchesFromCache(*tempLaunches, &rawPhases) && !tempLaunches->empty()) {
            calculateFormationsForItems(*tempLaunches, &rawPhases);
            
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
            
            // 后台下载后同步写入最新快照，供下次开机毫秒级启动
            OrbitDataProvider::saveRecentLaunchesMeta(*tempLaunches);
            
            lockSatMutex();
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
            unlockSatMutex();
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

    // 仅在用户按键调整时光机快进期间短暂暂停 GNSS 串口解析，推荐列表等正常模式下保持 GNSS 实时解析
    if (gnss && (lastTimeAdjustMillis == 0)) {
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
        static bool lastF = false;
        static bool lastA = false;
        static bool lastTab = false;
        static bool lastShift = false;
        static bool lastL = false;
        static bool lastSpace = false;
        static bool lastM = false;
        static bool lastCtrl = false;
        static bool lastT = false;

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
        bool currF = M5Cardputer.Keyboard.isKeyPressed('f') || M5Cardputer.Keyboard.isKeyPressed('F');
        bool currA = M5Cardputer.Keyboard.isKeyPressed('a') || M5Cardputer.Keyboard.isKeyPressed('A');
        bool currTab = M5Cardputer.Keyboard.isKeyPressed(KEY_TAB);
        bool currShift = M5Cardputer.Keyboard.isKeyPressed(KEY_LEFT_SHIFT) || M5Cardputer.Keyboard.keysState().shift;
        bool currL = M5Cardputer.Keyboard.isKeyPressed('l') || M5Cardputer.Keyboard.isKeyPressed('L');
        bool currSpace = M5Cardputer.Keyboard.isKeyPressed(' ');
        bool currM = M5Cardputer.Keyboard.isKeyPressed('m') || M5Cardputer.Keyboard.isKeyPressed('M');
        bool currCtrl = M5Cardputer.Keyboard.isKeyPressed(KEY_LEFT_CTRL) || M5Cardputer.Keyboard.keysState().ctrl;
        bool currT = M5Cardputer.Keyboard.isKeyPressed('t') || M5Cardputer.Keyboard.isKeyPressed('T');

        static bool s_bootKeyFlushed = false;
        if (!s_bootKeyFlushed) {
            lastSemi = currSemi; lastDot = currDot; lastComma = currComma; lastSlash = currSlash;
            lastO = currO; lastV = currV; lastEnter = currEnter; lastBack = currBack;
            lastEsc = currEsc; lastTick = currTick; lastBracketL = currBracketL; lastBracketR = currBracketR;
            lastC = currC; lastR = currR; lastW = currW; lastS = currS;
            lastH = currH; lastG = currG; lastY = currY; lastN = currN;
            lastD = currD; lastF = currF; lastA = currA; lastTab = currTab; lastShift = currShift; lastL = currL;
            lastSpace = currSpace; lastM = currM; lastCtrl = currCtrl;
            s_bootKeyFlushed = true;
            currSemi = currDot = currComma = currSlash = currO = currV = false;
            currEnter = currBack = currEsc = currTick = currBracketL = currBracketR = false;
            currC = currR = currW = currS = currH = currG = currY = currN = currD = currF = currA = false;
            currTab = currShift = currL = currSpace = currM = currCtrl = false;
        }

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
        bool justF = currF && !lastF;
        bool justA = currA && !lastA;
        bool justTab = currTab && !lastTab;
        bool justShift = currShift && !lastShift;
        bool justL = currL && !lastL;
        bool justSpace = currSpace && !lastSpace;
        bool justM = currM && !lastM;
        bool justCtrl = currCtrl && !lastCtrl;
        bool justT = currT && !lastT;
        bool hasAnyKeyJustPressed = justSemi || justDot || justComma || justSlash || justO || justV || justEnter || justBack || justEsc || justTick || justBracketL || justBracketR || justC || justR || justW || justS || justH || justG || justY || justN || justD || justF || justA || justTab || justShift || justL || justSpace || justM || justCtrl || justT;

        // RF Console 全屏终端模式 (仅在主界面下按 Ctrl 开启，退出统一按 Esc 键)
        if (justCtrl && appState == STATE_MAIN && !RfConsoleView::getInstance().isActive()) {
            RfConsoleView::getInstance().setActive(true);
        }

        if (RfConsoleView::getInstance().isActive()) {
            if (justTab) {
                int nextMode = (earth_renderer->getVisualMode() + 1) % 2;
                earth_renderer->setVisualMode(nextMode);
            }
            bool justZero = (M5Cardputer.Keyboard.keysState().word.size() > 0 && M5Cardputer.Keyboard.keysState().word[0] == '0');
            RfConsoleView::getInstance().handleKeys(justSemi, justDot, justEnter, justD, (justEsc || justTick), justT, justComma, justSlash, (justZero || justR), justY, justN, timeMachineOffset);
            if (!RfConsoleView::getInstance().isActive()) {
                // 用户按 Esc 退出了 RF 控制台，立即复位 Canvas 全局状态，防止污染主界面文字排版
                auto c = earth_renderer->getCanvas();
                if (c) {
                    c->setTextDatum(top_left);
                    c->clearClipRect();
                }
            } else {
                // 实时解算当前无线电跟踪状态与多普勒频移，支持时光机快进/倒退
                updateRadioTrackingPipeline(current_unix + timeMachineOffset, timeMachineOffset);
                auto c = earth_renderer->getCanvas();
                if (c) {
                    RfConsoleView::getInstance().draw(c, 240, 135);
                    pushCanvasWithFilter();
                }
                lastSemi = currSemi; lastDot = currDot; lastComma = currComma; lastSlash = currSlash;
                lastO = currO; lastV = currV; lastEnter = currEnter; lastBack = currBack;
                lastEsc = currEsc; lastTick = currTick; lastBracketL = currBracketL; lastBracketR = currBracketR;
                lastC = currC; lastR = currR; lastW = currW; lastS = currS;
                lastH = currH; lastG = currG; lastY = currY; lastN = currN;
                lastD = currD; lastF = currF; lastA = currA; lastTab = currTab; lastShift = currShift;
                lastL = currL; lastSpace = currSpace; lastM = currM; lastCtrl = currCtrl; lastT = currT;
                return;
            }
        }

        if (showHelp) {
            if (millis() < 3000) {
                showHelp = false;
            } else if (hasAnyKeyJustPressed) {
                showHelp = false;
                currSemi = currDot = currComma = currSlash = currO = currV = currEnter = currBack = currEsc = currTick = currBracketL = currBracketR = currC = currR = currW = currS = currH = currG = currY = currN = currD = currTab = currShift = currL = currSpace = false;
                justSemi = justDot = justComma = justSlash = justO = justV = justEnter = justBack = justEsc = justTick = justBracketL = justBracketR = justC = justR = justW = justS = justH = justG = justY = justN = justD = justTab = justShift = false;
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
            else if (M5Cardputer.Keyboard.isKeyPressed(',') && !showRecommendations) currentKey = ',';
            else if (M5Cardputer.Keyboard.isKeyPressed('/') && !showRecommendations) currentKey = '/';
            else if (M5Cardputer.Keyboard.isKeyPressed(';') && !showRecommendations) currentKey = ';';
            else if (M5Cardputer.Keyboard.isKeyPressed('.') && !showRecommendations) currentKey = '.';
            else if (M5Cardputer.Keyboard.isKeyPressed('-') || M5Cardputer.Keyboard.isKeyPressed('_')) currentKey = '-';
            else if (M5Cardputer.Keyboard.isKeyPressed('=') || M5Cardputer.Keyboard.isKeyPressed('+')) currentKey = '=';
            else if (M5Cardputer.Keyboard.isKeyPressed(' ')) currentKey = ' ';
            else if (M5Cardputer.Keyboard.isKeyPressed('[')) currentKey = '[';
            else if (M5Cardputer.Keyboard.isKeyPressed(']')) currentKey = ']';
            
            auto handleContinuousKey = [&](char key) {
                if ((isSatViewMode || (!isManualLocationMode)) && !showRecommendations) {
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
        } else if (appState == STATE_SERVO_TEST) {
            servo_test_view.handleContinuousInput();
        }

        // 推荐过境事件列表专属平滑导航控制 (防止点按误触发长按，舒适匀速平滑滚动)
        if (appState == STATE_MAIN && showRecommendations && selectedPassIndex == -1) {
            static unsigned long s_passHoldStart = 0;
            static unsigned long s_passLastRepeat = 0;
            static char s_passActiveKey = 0;

            bool isSemiDown = M5Cardputer.Keyboard.isKeyPressed(';');
            bool isDotDown = M5Cardputer.Keyboard.isKeyPressed('.');

            if (justSemi) {
                if (passScrollIndex > 0) passScrollIndex--;
                s_passActiveKey = ';';
                s_passHoldStart = millis();
                s_passLastRepeat = millis();
            } else if (justDot) {
                if (passScrollIndex < (int)displayTree.size() - 1) passScrollIndex++;
                s_passActiveKey = '.';
                s_passHoldStart = millis();
                s_passLastRepeat = millis();
            } else if (s_passActiveKey == ';' && isSemiDown) {
                // 长按判定：按住 450ms 以上才触发连发，连发间隔 160ms (每秒约 6 行，清晰匀速，绝无飞窜)
                if (millis() - s_passHoldStart > 450 && millis() - s_passLastRepeat >= 160) {
                    s_passLastRepeat = millis();
                    if (passScrollIndex > 0) passScrollIndex--;
                }
            } else if (s_passActiveKey == '.' && isDotDown) {
                if (millis() - s_passHoldStart > 450 && millis() - s_passLastRepeat >= 160) {
                    s_passLastRepeat = millis();
                    if (passScrollIndex < (int)displayTree.size() - 1) passScrollIndex++;
                }
            } else if (!isSemiDown && !isDotDown) {
                s_passActiveKey = 0;
            }
        }
        
        // Handle discrete keyboard input
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            if (appState == STATE_MAIN) {
                if (justShift) {
                    appState = STATE_SERVO_TEST;
                    servo_test_view.reset();
                    g_imuSamplingEnabled = false;
                    delay(15);
                    gimbal.enterManualTest();
                } else if (justTab) {
                    int nextMode = (earth_renderer->getVisualMode() + 1) % 2;
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
                            // Serial.printf("[Debug] Cache reset on justR: timeCrossedDay=%d, locShifted=%d\n", timeCrossedDay, locShifted);
                            lockPassMutex();
                            lastPredictionBaseTime = 0; // 缓存失效
                            predictionsReady = false;
                            unlockPassMutex();
                            triggerPrediction = true;
                        } else {
                            // Serial.println("[Debug] justR reset applied silently. Coords/Time shift within thresholds.");
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
                        
                        // 首次打开面板时，默认展开“今晚”分类，并折叠其它分类
                        catExpanded[0] = true;
                        catExpanded[1] = false;
                        catExpanded[2] = false;
                        catExpanded[3] = false;
                        
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
                        
                        // Serial.printf("[Debug] Enter Panel: predictionsReady=%d, lastPredictionBaseTime=%u, targetTime=%u, isCacheValid=%d, g_orbitCalculating=%d, triggerPrediction=%d\n", 
                        //               predictionsReady, lastPredictionBaseTime, targetTime, isCacheValid, g_orbitCalculating, triggerPrediction);

                        bool needTrigger = !isCacheValid || (!predictionsReady && !g_orbitCalculating);
                        if (needTrigger && !g_orbitCalculating && !triggerPrediction) {
                            // Serial.printf("[Debug] Triggering calculation. needTrigger=%d, isCacheValid=%d, predictionsReady=%d\n", needTrigger, isCacheValid, predictionsReady);
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
                            if (!isSystemMemorySafeForNetwork()) {
                                LOG_W("APP", "Cannot start NetworkTask from main view: insufficient memory");
                            } else {
                                manualWifiToggle = true;
                                BaseType_t res = xTaskCreatePinnedToCore(networkTask, "NetworkTask", 6144, NULL, 1, NULL, 0);
                                if (res != pdPASS) {
                                    LOG_I("APP", "Failed to create NetworkTask! Free Heap: %u", (unsigned int)ESP.getFreeHeap());
                                    downloadErrorMsg = I18N::get(TXT_LOW_MEMORY);
                                    recentLaunchErrorMsg = I18N::get(TXT_LOW_MEMORY);
                                    downloadFinishedMs = millis();
                                    recentLaunchDownloadFinishedMs = millis();
                                }
                            }
                        } else {
                            HalWifi::disconnect();
                        }
                    }
                } else if (justS) {
                    appState = STATE_SAT_SELECT;
                    currentSatTab = TAB_ENCYCLOPEDIA;
                    g_selectedCategoryMask = 0;
                    g_showCategoryFilterDialog = false;
                    updateEncyclopediaFilteredList();
                    entrySelectedSatellites.clear();
                    for (int i = 0; i < NUM_SATELLITES; i++) {
                        if (g_satellites[i].selected) {
                            entrySelectedSatellites.push_back(g_satellites[i].noradId);
                        }
                    }
                    entryRecentLaunchFocusMode = g_recentLaunchFocusMode;
                    entryRecentLaunchActiveBatchId = recentLaunchActiveBatchId;
                } else if (justL) {
                    appState = STATE_LANG_SELECT;
                    langSelectedIndex = (int)I18N::getLanguage();
                } else if (justM) {
                    appState = STATE_HW_WIZARD;
                    hardware_wizard.reset();
                } else if (justH) {
                    showHelp = !showHelp;
                } else if (justG) {
                    auto& hw = HardwareConfig::getInstance();
                    bool gnssConfigured = hw.isEnabled(HW_MOD_CAP_LORA1262) || hw.isEnabled(HW_MOD_UNIT_GPSV11);
                    if (gnssConfigured && gnss) {
                        if (!gnss->isModuleInitialized()) {
                            if (hw.isEnabled(HW_MOD_CAP_LORA1262)) gnss->begin(15, 13, 115200);
                            else if (hw.isEnabled(HW_MOD_UNIT_GPSV11)) gnss->begin(1, 2, 115200);
                            gnssManualMode = true;
                            gnssTimedOut = false;
                            gnssStartTime = millis();
                        } else if (gnss->isInStandbyMode()) {
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
                WifiConnectRequest req;
                bool shouldExit = false;
                if (wifi_setup_view.handleInput(justEsc, justTick, justBack, justEnter,
                                                justR, justSemi, justDot,
                                                M5Cardputer.Keyboard.keysState(),
                                                req, shouldExit)) {
                    if (shouldExit) {
                        exitWiFiSetupScreen();
                    } else if (req.triggered) {
                        appState = g_wifiSetupReturnState;
                        manualWifiToggle = true; // Stay connected since user explicitly set it up
                        NetworkParams* params = new NetworkParams();
                        params->ssid = req.ssid;
                        params->pass = req.pass;
                        params->shouldSave = true;
                        
                        if (currentSatTab == TAB_RECENT_LAUNCH) {
                            HalWifi::saveCredentials(params->ssid, params->pass);
                            delete params;
                            recentLaunchDownloading = true;
                            recentLaunchErrorMsg = I18N::get(TXT_CONNECTING_WIFI);
                            drawSatSelectPage();
                            pushCanvasWithFilter();
                            BaseType_t res = xTaskCreatePinnedToCore(recentLaunchNetworkTask, "RecentLaunchNetworkTask", 6144, NULL, 1, NULL, 0);
                            if (res != pdPASS) {
                                LOG_I("APP", "Failed to create RecentLaunchNetworkTask! Free Heap: %u", (unsigned int)ESP.getFreeHeap());
                                recentLaunchDownloading = false;
                                recentLaunchErrorMsg = I18N::get(TXT_LOW_MEMORY);
                                recentLaunchDownloadFinishedMs = millis();
                                if (!HalWifi::isConnected()) {
                                    HalWifi::disconnect();
                                }
                            }
                        } else {
                            BaseType_t res = xTaskCreatePinnedToCore(
                                networkTask, "NetworkTask", 6144, params, 1, NULL, 0
                            );
                            if (res != pdPASS) {
                                LOG_I("APP", "Failed to create NetworkTask! Free Heap: %u", (unsigned int)ESP.getFreeHeap());
                                delete params;
                                downloadErrorMsg = I18N::get(TXT_LOW_MEMORY);
                                downloadFinishedMs = millis();
                                if (!HalWifi::isConnected()) {
                                    HalWifi::disconnect();
                                }
                            }
                        }
                    }
                }
            } else if (appState == STATE_SAT_SELECT) {
                if (showListHelp) {
                    if (justH || justEsc || justBack || justEnter || justTick) {
                        showListHelp = false;
                    }
                } else if (g_showCategoryFilterDialog) {
                    int r = g_categoryFocusIndex / 3;
                    int c = g_categoryFocusIndex % 3;
                    if (justEnter || justSpace) {
                        if (g_categoryFocusIndex == 11) {
                            // 第 11 项为“重置全部”，清空所有筛选（恢复显示全部）
                            g_tempCategoryMask = 0;
                        } else {
                            // 使用enter选中/取消对应分类
                            g_tempCategoryMask ^= (1 << g_categoryFocusIndex);
                        }
                    } else if (justEsc || justTick || justBack || justF) {
                        // 使用esc生效并关闭（Cardputer物理Esc键产生tick/27）
                        g_selectedCategoryMask = g_tempCategoryMask;
                        g_showCategoryFilterDialog = false;
                        updateEncyclopediaFilteredList();
                    } else if (justSemi || justW) { // 上 (UP)
                        r = (r - 1 + 4) % 4;
                        g_categoryFocusIndex = r * 3 + c;
                    } else if (justDot || justS) { // 下 (DOWN)
                        r = (r + 1) % 4;
                        g_categoryFocusIndex = r * 3 + c;
                    } else if (justComma || justA) { // 左 (LEFT)
                        c = (c - 1 + 3) % 3;
                        g_categoryFocusIndex = r * 3 + c;
                    } else if (justSlash || justD) { // 右 (RIGHT)
                        c = (c + 1) % 3;
                        g_categoryFocusIndex = r * 3 + c;
                    }
                } else if (justF && currentSatTab == TAB_ENCYCLOPEDIA) {
                    g_showCategoryFilterDialog = true;
                    g_tempCategoryMask = g_selectedCategoryMask;
                    g_categoryFocusIndex = 0;
                } else if (justTab) {
                    int nextMode = (earth_renderer->getVisualMode() + 1) % 2;
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
                            updateEncyclopediaFilteredList();
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
                        LOG_I("APP", "[KEY] Pressed %s in Recent Launch. NetworkActive: %d, FreeHeap: %u, Safe: %d",
                              justC ? "C (Force Refresh)" : "W (Refresh)", g_networkActive, (unsigned int)ESP.getFreeHeap(), isSystemMemorySafeForNetwork());
                        if (g_networkActive) {
                            recentLaunchErrorMsg = I18N::get(TXT_SYS_BUSY);
                            recentLaunchDownloadSuccess = false;
                            recentLaunchDownloadFinishedMs = millis();
                            drawSatSelectPage();
                            pushCanvasWithFilter();
                        } else if (!isSystemMemorySafeForNetwork()) {
                            recentLaunchErrorMsg = I18N::get(TXT_LOW_MEMORY);
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
                            BaseType_t res = xTaskCreatePinnedToCore(recentLaunchNetworkTask, "RecentLaunchNetworkTask", 5120, NULL, 1, NULL, 0);
                            if (res != pdPASS) {
                                recentLaunchDownloading = false;
                                recentLaunchErrorMsg = I18N::get(TXT_TASK_INIT_FAILED);
                                drawSatSelectPage();
                                pushCanvasWithFilter();
                            }
                        }
                    } else {
                        int filteredCount = g_encyclopediaFilteredIndices.size();
                        int realIdx = (satSelectedIndex >= 0 && satSelectedIndex < filteredCount) ? g_encyclopediaFilteredIndices[satSelectedIndex] : -1;
                        if (justC && currentSatTab == TAB_ENCYCLOPEDIA && realIdx >= 0 && realIdx < NUM_SATELLITES) {
                            if (g_networkActive) {
                                downloadErrorMsg = I18N::get(TXT_SYS_BUSY);
                                downloadFinishedMs = millis();
                                drawSatSelectPage();
                                pushCanvasWithFilter();
                            } else if (!isSystemMemorySafeForNetwork()) {
                                downloadErrorMsg = I18N::get(TXT_LOW_MEMORY);
                                downloadFinishedMs = millis();
                                drawSatSelectPage();
                                pushCanvasWithFilter();
                            } else {
                                downloadErrorMsg = I18N::get(TXT_REFRESHING_GP);
                                drawSatSelectPage();
                                pushCanvasWithFilter();
                                BaseType_t res = xTaskCreatePinnedToCore(forceRefreshSingleSatTask, "ForceRefreshSingleSatTask", 6144, (void*)(intptr_t)realIdx, 1, NULL, 0);
                                if (res != pdPASS) {
                                    downloadErrorMsg = I18N::get(TXT_TASK_INIT_FAILED);
                                    downloadFinishedMs = millis();
                                    drawSatSelectPage();
                                    pushCanvasWithFilter();
                                }
                            }
                        } else if (!justC) { // W key: Refresh GP & Frequencies in Encyclopedia
                            if (g_networkActive) {
                                downloadErrorMsg = I18N::get(TXT_SYS_BUSY);
                                downloadFinishedMs = millis();
                                drawSatSelectPage();
                                pushCanvasWithFilter();
                            } else if (!isSystemMemorySafeForNetwork()) {
                                downloadErrorMsg = I18N::get(TXT_LOW_MEMORY);
                                downloadFinishedMs = millis();
                                drawSatSelectPage();
                                pushCanvasWithFilter();
                            } else {
                                manualWifiToggle = true;
                                downloadErrorMsg = I18N::get(TXT_CONNECTING_WIFI);
                                drawSatSelectPage();
                                pushCanvasWithFilter();
                                BaseType_t res = xTaskCreatePinnedToCore(networkTask, "NetworkTask", 5120, NULL, 1, NULL, 0);
                                if (res != pdPASS) {
                                    downloadErrorMsg = I18N::get(TXT_TASK_INIT_FAILED);
                                    downloadFinishedMs = millis();
                                    drawSatSelectPage();
                                    pushCanvasWithFilter();
                                }
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
                            g_selectedCategoryMask = 0;
                            g_showCategoryFilterDialog = false;
                            updateEncyclopediaFilteredList();
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
                            if (!targetItem.selected) {
                                if (getTotalSelectedSatelliteCount() >= 30) {
                                    recentLaunchErrorMsg = (I18N::getLanguage() == LANG_ZH) ? "已达上限: 两列表最多共勾选30颗" : "Limit reached: Max 30 sats total";
                                } else {
                                    targetItem.selected = true;
                                    recentLaunchErrorMsg = "";
                                    g_recentLaunchFocusMode = true;
                                    recentLaunchActiveBatchId = targetItem.batchId;
                                    initRecentLaunchCalcs(targetItem);
                                }
                            } else {
                                targetItem.selected = false;
                                recentLaunchErrorMsg = "";
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
                    int filteredCount = g_encyclopediaFilteredIndices.size();
                    int totalItems = filteredCount + 1;
                    if (satSelectedIndex == filteredCount) {
                        // Inputting NORAD ID
                        if (justBack) {
                            if (noradInput.length() > 0) noradInput.remove(noradInput.length() - 1);
                            downloadErrorMsg = "";
                        } else if (justEsc || justTick) {
                            g_selectedCategoryMask = 0;
                            g_showCategoryFilterDialog = false;
                            updateEncyclopediaFilteredList();
                            appState = STATE_MAIN;
                            validateSatViewFocusState();
                        } else if (justSemi) {
                            if (satSelectedIndex > 0) satSelectedIndex--;
                            else satSelectedIndex = totalItems - 1;
                        } else if (justDot) {
                            satSelectedIndex = 0;
                        } else if (justEnter) {
                            if ((noradInput.length() == 5 || noradInput.length() == 6) && !isDownloadingCustom) {
                                if (g_networkActive) {
                                    downloadErrorMsg = I18N::get(TXT_SYS_BUSY);
                                    downloadFinishedMs = millis();
                                    drawSatSelectPage();
                                    pushCanvasWithFilter();
                                } else if (!isSystemMemorySafeForNetwork()) {
                                    downloadErrorMsg = I18N::get(TXT_LOW_MEMORY);
                                    downloadFinishedMs = millis();
                                    drawSatSelectPage();
                                    pushCanvasWithFilter();
                                } else {
                                    isDownloadingCustom = true;
                                    downloadErrorMsg = "";
                                    drawSatSelectPage();
                                    pushCanvasWithFilter();
                                    
                                    int id = noradInput.toInt();
                                    BaseType_t res = xTaskCreatePinnedToCore(downloadCustomSatTask, "DownloadCustomSatTask", 6144, (void*)(intptr_t)id, 1, NULL, 0);
                                    if (res != pdPASS) {
                                        isDownloadingCustom = false;
                                        downloadErrorMsg = I18N::get(TXT_TASK_INIT_FAILED);
                                        downloadFinishedMs = millis();
                                        drawSatSelectPage();
                                        pushCanvasWithFilter();
                                    }
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
                        int realIdx = (satSelectedIndex >= 0 && satSelectedIndex < filteredCount) ? g_encyclopediaFilteredIndices[satSelectedIndex] : -1;
                        if (justBack || justEsc || justTick) {
                            g_selectedCategoryMask = 0;
                            g_showCategoryFilterDialog = false;
                            updateEncyclopediaFilteredList();
                            appState = STATE_MAIN;
                            validateSatViewFocusState();
                            bool selectionChanged = false;
                            std::vector<int> currentSelected;
                            for (int i = 0; i < NUM_SATELLITES; i++) {
                                if (g_satellites[i].selected) {
                                    currentSelected.push_back(g_satellites[i].noradId);
                                }
                            }
                            if (g_recentLaunchFocusMode != entryRecentLaunchFocusMode ||
                                recentLaunchActiveBatchId != entryRecentLaunchActiveBatchId) {
                                selectionChanged = true;
                            } else {
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
                            }
                            if (selectionChanged) {
                                lockPassMutex();
                                predictionsReady = false;
                                lastPredictionBaseTime = 0;
                                unlockPassMutex();
                                triggerPrediction = true;
                            }
                        } else if (justEnter) {
                            if (realIdx >= 0 && realIdx < NUM_SATELLITES) {
                                if (!g_satellites[realIdx].selected) {
                                    if (getTotalSelectedSatelliteCount() >= 30) {
                                        downloadErrorMsg = (I18N::getLanguage() == LANG_ZH) ? "已达上限: 两列表最多共勾选30颗" : "Limit reached: Max 30 sats total";
                                    } else {
                                        g_satellites[realIdx].selected = true;
                                        downloadErrorMsg = "";
                                    }
                                } else {
                                    g_satellites[realIdx].selected = false;
                                    downloadErrorMsg = "";
                                }
                            }
                        } else if (justD && realIdx >= NUM_BUILTIN_SATELLITES && realIdx < NUM_SATELLITES) {
                            deleteConfirmIndex = realIdx;
                        } else if (justSemi) {
                            if (satSelectedIndex > 0) satSelectedIndex--;
                            else satSelectedIndex = totalItems - 1;
                        } else if (justDot) {
                            satSelectedIndex = (satSelectedIndex + 1) % totalItems;
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
                    I18N::setLanguage((Language)langSelectedIndex);
                    appState = STATE_MAIN;
                } else if (justSemi) { // UP
                    langSelectedIndex = (langSelectedIndex - 1 + 4) % 4;
                } else if (justDot) { // DOWN
                    langSelectedIndex = (langSelectedIndex + 1) % 4;
                }
            } else if (appState == STATE_SERVO_TEST) {
                if (servo_test_view.handleDiscreteInput(justShift, justEsc, justTick, justBack,
                                                       justSemi, justDot, justC, justS)) {
                    g_imuSamplingEnabled = true;
                    appState = STATE_MAIN;
                }
            } else if (appState == STATE_HW_WIZARD) {
                char keyChar = 0;
                if (justEsc || justTick || justBack) keyChar = 27;
                else if (justEnter) keyChar = '\n';
                else if (justBracketL) keyChar = '[';
                else if (justBracketR) keyChar = ']';
                else if (justSpace) keyChar = ' ';
                else if (M5Cardputer.Keyboard.keysState().word.size() > 0) {
                    keyChar = M5Cardputer.Keyboard.keysState().word[0];
                }

                if (hardware_wizard.handleKey(M5Cardputer.Keyboard.keysState(), keyChar)) {
                    appState = STATE_MAIN;
                    earth_renderer->getCanvas()->setTextDatum(top_left);
                    earth_renderer->getCanvas()->clearClipRect();
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
        lastF = currF;
        lastA = currA;
        lastTab = currTab;
        lastShift = currShift;
        lastL = currL;
        lastSpace = currSpace;
        lastM = currM;
        lastCtrl = currCtrl;
        lastT = currT;
        
        if (appState == STATE_WIFI_SETUP) {
            wifi_setup_view.draw(earth_renderer->getCanvas());
            pushCanvasWithFilter();
            updateChainMonoDisplay();
            
            if (wifi_setup_view.isScanning()) {
                wifi_setup_view.performScan();
            }
            return;
        } else if (appState == STATE_SAT_SELECT) {
            drawSatSelectPage();
            pushCanvasWithFilter();
            updateChainMonoDisplay();
            return;
        } else if (appState == STATE_SERVO_TEST) {
            servo_test_view.draw(earth_renderer->getCanvas());
            pushCanvasWithFilter();
            updateChainMonoDisplay();
            return;
        } else if (appState == STATE_HW_WIZARD) {
            hardware_wizard.draw(earth_renderer->getCanvas());
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
                        // Serial.printf("[Debug] GNSS sync cache reset: oldLat=%f, newLat=%f, oldLon=%f, newLon=%f, oldAlt=%f, newAlt=%f\n", 
                        //               oldLat, baseUserLat, oldLon, baseUserLon, oldAlt, baseUserAlt);
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
        
        static bool s_firstMainLoopFrame = true;
        if (s_firstMainLoopFrame) {
            s_firstMainLoopFrame = false;
            lockedPitch = 0.0f;
            lockedRoll = 0.0f;
            smoothViewLat = baseUserLat;
            smoothViewLon = baseUserLon;
            targetViewLat = baseUserLat;
            targetViewLon = baseUserLon;
            smoothPitch = 0.0f;
            smoothRoll = 0.0f;
            smoothYaw = 0.0f;
            targetPitch = 0.0f;
            targetRoll = 0.0f;
            targetYaw = 0.0f;
            if (attitude) {
                attitude->calibrateHeading();
            }
        }
        
        // Handle longitude wrap-around for interpolation
        double lonDiff = targetViewLon - smoothViewLon;
        if (lonDiff > 180.0) targetViewLon -= 360.0;
        else if (lonDiff < -180.0) targetViewLon += 360.0;
        
        float dt = 0.20f;
        if (isSatViewMode && !isCameraTransitioning) {
            dt = 1.0f; // 锁定特定卫星视角时直接跟随机位
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
                                calculateOrbit(g_repSatCalc, simTime, g_repSatCache.cache, orbitsCalculatedThisFrame, lastTimeAdjustMillis != 0, (isSatViewMode && g_recentLaunchFocusMode));
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
                                calculateOrbit(*(item.calc), simTime, item.cache.cache, orbitsCalculatedThisFrame, lastTimeAdjustMillis != 0, false);
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
                            calculateOrbit(obj.calc, simTime, obj.cache, orbitsCalculatedThisFrame, lastTimeAdjustMillis != 0, false);
                            data.pastOrbit = &(obj.cache.past);
                            data.futureOrbit = &(obj.cache.future);
                        } else {
                            data.pastOrbit = nullptr;
                            data.futureOrbit = nullptr;
                        }
                        
                        // Uniform display names for all launch target objects with its launch epoch dates
                        static String sNameCache[8];
                        if (i < 8) {
                            if (recentLaunchSelectedIndex < (int)g_recentLaunches.size()) {
                                sNameCache[i] = getShortNameForDisplay(obj.name, g_recentLaunches[recentLaunchSelectedIndex].epoch);
                            } else {
                                sNameCache[i] = obj.name;
                            }
                            data.shortName = sNameCache[i].c_str();
                        } else {
                            data.shortName = obj.name.c_str();
                        }
                        
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
                            calculateOrbit(calcCopy, simTime, g_satCaches[i].cache, orbitsCalculatedThisFrame, lastTimeAdjustMillis != 0, (isSatViewMode && (focusSatIndex == i) && !g_recentLaunchFocusMode));
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
        
        // =======================================================================
        // 自动过境无线电监听与调度 (RadioManager Autonomous Satellite Radio Patrol)
        // 无论是否接入舵机云台，系统均持续监测过境卫星并驱动 Cap LoRa-1262 射频监听
        // =======================================================================
        updateRadioTrackingPipeline(current_unix + timeMachineOffset, timeMachineOffset);

        // Update 3-axis Gimbal Targets based on active sat tracking (地表全天自主巡天跟踪站 Autonomous Sky Patrol)
        if (gimbal.isOnline() && appState != STATE_SERVO_TEST) {
            GimbalTrackingPipeline::update(current_unix + timeMachineOffset);
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
        
        // Ensure canvas font and datum matches default state for HUD rendering
        earth_renderer->getCanvas()->setFont(I18N::getFont());
        earth_renderer->getCanvas()->setTextDatum(top_left);
        earth_renderer->getCanvas()->clearClipRect();
        
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
            DialogViews::drawHelpDialog(earth_renderer->getCanvas());
        }
        
        if (showRecommendations) {
            recommendation_view.draw(earth_renderer->getCanvas());
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
                    lockSatMutex();
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
                    unlockSatMutex();
                    
                    uint16_t baseCol = TFT_WHITE;
                    if (ageDays <= 2.0) baseCol = TFT_WHITE;
                    else if (ageDays <= 14.0) baseCol = 0x07FF; // TFT_CYAN
                    else if (ageDays >= 365.0) baseCol = earth_renderer->getCanvas()->color565(150, 150, 150);
                    
                    satColor = baseCol;
                    currentCalc = &g_repSatCalc;
                    hasSatInfo = true;
                }
                
                if (hasSatInfo && currentCalc != nullptr) {
                    Language currL = I18N::getLanguage();
                    bool isZh = (currL == LANG_ZH);
                    bool isCjk = (currL == LANG_ZH || currL == LANG_JA);
                    earth_renderer->getCanvas()->setFont(I18N::getFont());
                    earth_renderer->getCanvas()->setTextColor(satColor);
                    const char* satViewStr = (currL == LANG_ZH) ? "视角锁定" : ((currL == LANG_JA) ? "視点固定" : ((currL == LANG_ES) ? "Vista sat" : "Sat View"));
                    earth_renderer->getCanvas()->drawString(satViewStr, 180, 5);

                    // 绘制正在过境监听的动态 WiFi 弧形辐射波纹图标
                    if (RadioManager::getInstance().isEmittingWaves()) {
                        int wx = 168;
                        int wy = 11;
                        float phase = RadioManager::getInstance().getWavePhase();
                        for (int arc = 1; arc <= 3; arc++) {
                            int r = arc * 3 + (int)(phase * 2.5f);
                            uint16_t col = (arc == 1) ? 0x07E0 : ((arc == 2) ? 0x07FF : 0x2965);
                            earth_renderer->getCanvas()->drawCircle(wx, wy, r, col);
                        }
                        earth_renderer->getCanvas()->fillCircle(wx, wy, 2, 0x07E0);
                    }
                    
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

        // 居中顶部 Toast 弹窗渲染 (收到数据包时弹出，5秒自动淡出)
        if (RadioManager::getInstance().hasActiveToast()) {
            auto c = earth_renderer->getCanvas();
            if (c) {
                String toast = RadioManager::getInstance().getToastText();
                float alpha = RadioManager::getInstance().getToastAlpha();
                if (alpha > 0.05f) {
                    c->setFont(&fonts::Font0);
                    c->setTextSize(1);
                    int tw = c->textWidth(toast.c_str()) + 16;
                    int th = 16;
                    int tx = (240 - tw) / 2;
                    int ty = 4;
                    uint16_t boxBg = (alpha > 0.5f) ? 0x0841 : 0x0000;
                    uint16_t borderCol = (alpha > 0.5f) ? 0x07FF : 0x2965;
                    uint16_t textCol = (alpha > 0.5f) ? 0x07E0 : 0x03E0;
                    c->fillRoundRect(tx, ty, tw, th, 4, boxBg);
                    c->drawRoundRect(tx, ty, tw, th, 4, borderCol);
                    c->setTextDatum(MC_DATUM);
                    c->setTextColor(textCol, boxBg);
                    c->drawString(toast.c_str(), tx + tw / 2, ty + th / 2);
                    c->setTextDatum(top_left);
                }
            }
        }
    }
    
    pushCanvasWithFilter();

    // Update Chain Mono Display (dynamic interval: 100ms normally)
    updateChainMonoDisplay();
}
}
