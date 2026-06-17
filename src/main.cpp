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
};

const int MAX_SATELLITES = 50;
int NUM_SATELLITES = 18;

SatProfile g_satellites[MAX_SATELLITES] = {
    {25544, "ISS", TFT_YELLOW, 2, -1.8, true, ICON_STATION, "International Space Station. The largest human-made structure in space, visible as a very bright moving star.", "145.800", "FM/SSTV"},
    {48274, "Tiangong", TFT_GREEN, 1, -0.5, true, ICON_STATION, "China's Tiangong Space Station. A permanent modular space station in LEO.", "", ""},
    {20580, "Hubble", TFT_CYAN, 0, 1.5, true, ICON_TELESCOPE, "Hubble Space Telescope. A vital observatory that revolutionized our understanding of the universe.", "", ""},
    {33591, "NOAA 19", TFT_ORANGE, 0, 3.5, false, ICON_SATELLITE, "NOAA weather satellite. Known for transmitting APT weather images back to Earth.", "137.100", "APT"},
    {50463, "JWST", TFT_GOLD, 0, 10.0, false, ICON_DEEPSPACE, "James Webb Space Telescope. Located at L2 point 1.5 million km away, observing in infrared.", "", ""},
    {53807, "BlueWalker 3", TFT_WHITE, 0, 1.0, false, ICON_SATELLITE, "AST SpaceMobile's prototype. Features a massive 64 sqm array, very bright and controversial.", "", ""},
    {118, "Ablestar R/B", TFT_LIGHTGRAY, 0, 4.0, false, ICON_ROCKET, "Ablestar rocket body.", "", ""},
    {25732, "CZ-4B R/B", TFT_ORANGE, 0, 4.0, true, ICON_ROCKET, "Long March 4B rocket body.", "", ""},
    {6155, "Centaur R/B", TFT_LIGHTGRAY, 0, 4.0, false, ICON_ROCKET, "Centaur rocket body.", "", ""},
    {28499, "Ariane 5 R/B", TFT_LIGHTGRAY, 0, 4.0, false, ICON_ROCKET, "Ariane 5 rocket body.", "", ""},
    {41882, "Fengyun-4A", TFT_BLUE, 0, 10.0, false, ICON_SATELLITE, "Chinese geostationary meteorological satellite, located 35,786 km above the equator.", "", ""},
    {43539, "BeiDou-3", TFT_RED, 0, 10.0, false, ICON_SATELLITE, "Medium Earth Orbit navigation satellite part of the BeiDou system (BDS).", "", ""},
    {27386, "Envisat", TFT_LIGHTGRAY, 0, 2.5, false, ICON_SATELLITE, "A huge 8-ton inactive Earth observation satellite. Now one of the largest pieces of space debris.", "", ""},
    {4382, "DFH-1", TFT_RED, 0, 6.0, false, ICON_SATELLITE, "Dong Fang Hong I. China's first satellite launched in 1970, still orbiting today as a silent monument.", "20.009", "Beacon"},
    {25994, "Terra", TFT_PINK, 0, 3.0, false, ICON_SATELLITE, "NASA's flagship Earth Observing System satellite.", "", ""},
    {27424, "Aqua", TFT_MAGENTA, 0, 3.0, false, ICON_SATELLITE, "NASA Earth observation satellite focusing on the water cycle.", "", ""},
    {43166, "Iridium 127", TFT_WHITE, 0, 4.0, false, ICON_SATELLITE, "Iridium NEXT network. The original 1st-gen Iridium satellites produced legendary 'flares' up to mag -8.", "", ""},
    {57165, "Meteor-M2", TFT_WHITE, 0, 3.5, false, ICON_SATELLITE, "Russian meteorological satellite transmitting LRPT weather images.", "137.100", "LRPT"}
};

// We use a simulated time starting near the TLE epoch for Phase 3 offline testing
uint32_t current_unix = 0; // Will be set in setup()
unsigned long last_update = 0;
unsigned long gnssStartTime = 0;
bool gnssManualMode = false;
bool gnssTimedOut = false;
bool isSatViewMode = false;
int focusSatIndex = -1;
float currentZoom = 1.0f;
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
void calculateOrbit(SGP4Calc& calc, uint32_t baseTime, OrbitCache& cache, int& calcCount) {
    // Only recalculate orbit path if simulated time has advanced by more than 5 minutes (300 seconds)
    // The ground track changes very slowly, so we don't need to redraw the path every 10 seconds.
    if (cache.lastCalcTime == 0 || abs((int)baseTime - (int)cache.lastCalcTime) > 300) {
        if (calcCount >= 1) { // Max 1 expensive calculation per frame to prevent lag spikes
            return;
        }
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
        for (int i = 1; i <= 45; i += 3) {
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
bool manualWifiToggle = false;

// Custom Satellite Input State
String noradInput = "";
String downloadErrorMsg = "";
int deleteConfirmIndex = -1;
bool isDownloadingCustom = false;
portMUX_TYPE passMutex = portMUX_INITIALIZER_UNLOCKED;

volatile bool triggerPrediction = true;

void predictorTask(void* parameter) {
    while (true) {
        if (!triggerPrediction) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        triggerPrediction = false;
        
        ObservationPredictor predictor(baseUserLat, baseUserLon, baseUserAlt / 1000.0);
        std::vector<PassEvent> allPasses;
        
        // Use actual current time for predictions
        uint32_t startTime = current_unix;
        
        int totalSelected = 0;
        for (int i = 0; i < NUM_SATELLITES; i++) {
            if (g_satellites[i].selected) totalSelected++;
        }
        
        predictionProgress = 0;
        int completedCount = 0;
        for (int i = 0; i < NUM_SATELLITES; i++) {
            if (triggerPrediction) break;
            
            if (g_satellites[i].selected) {
                auto passes = predictor.predictPasses(g_satellites[i].tle, g_satellites[i].stdMag, startTime, 7);
                allPasses.insert(allPasses.end(), passes.begin(), passes.end());
                vTaskDelay(pdMS_TO_TICKS(10)); // Yield between satellites
                completedCount++;
            }
            predictionProgress = (completedCount * 100) / (totalSelected > 0 ? totalSelected : 1);
        }
        
        if (triggerPrediction) continue;
        
        // Filter out past passes
        std::vector<PassEvent> upcomingPasses;
        for (const auto& pass : allPasses) {
            if (pass.aosTime > current_unix) {
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
        
        // Auto-expand first category on finish
        catExpanded[0] = true;
        catExpanded[1] = false;
        catExpanded[2] = false;
        catExpanded[3] = false;
        
        rebuildTree(current_unix);
        portEXIT_CRITICAL(&passMutex);
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
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (!error) {
            for (int i = 0; i < NUM_SATELLITES; i++) {
                String idStr = String(g_satellites[i].noradId);
                if (doc.containsKey(idStr)) {
                    g_satellites[i].downlinkFreq = doc[idStr]["freq"].as<String>();
                    g_satellites[i].radioMode = doc[idStr]["mode"].as<String>();
                }
            }
        }
    }
    http.end();
    delete client;
}

void networkTask(void* parameter) {
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
        vTaskDelete(NULL);
        return;
    }

    // 1. Connect WiFi
    HalWifi::begin(ssid.c_str(), pass.c_str());
    
    if (!HalWifi::isConnected()) {
        LOG_I("APP", "WiFi connection failed. Falling back to WiFi Setup.");
        appState = STATE_WIFI_SETUP;
        wifiIsScanning = true;
        wifiIsInputtingPassword = false;
        vTaskDelete(NULL);
        return;
    }
    
    if (HalWifi::isConnected() && shouldSave) {
        HalWifi::saveCredentials(ssid, pass);
    }
    
    if (HalWifi::isConnected()) {
        // Online timezone removed per user request (relies on offline grid)
        
        // 2. Fetch NTP
        HalWifi::syncNTPTime();
        
        // 3. Update time
        uint32_t ntpTime = HalWifi::getUnixTime();
        if (ntpTime > 0) {
            current_unix = ntpTime;
            LOG_I("APP", "Time synced to UTC: %u", current_unix);
        }

        // IP Geolocation removed per user request (relies on GNSS/cache instead)

        // 3.5 Fetch Frequencies
        fetchFrequencies();

        // 4. Fetch TLEs
        bool updated = false;
        
        for (int i = 0; i < NUM_SATELLITES; i++) {
            TLEData new_tle;
            if (TLEUpdater::getTLE(g_satellites[i].noradId, new_tle)) {
                new_tle.baseScore = g_satellites[i].baseScore;
                g_satellites[i].tle = new_tle;
                g_satellites[i].calc.init(g_satellites[i].tle);
                updated = true;
            }
        }
        
        if (updated) {
            LOG_I("APP", "TLE Data is ready and models updated!");
            
            // Rerun predictor with new data
            portENTER_CRITICAL(&passMutex);
            predictionsReady = false;
            portEXIT_CRITICAL(&passMutex);
            triggerPrediction = true;
        }
        if (!manualWifiToggle) {
            LOG_I("APP", "Network tasks complete. Turning off WiFi to save power.");
            HalWifi::disconnect();
        } else {
            LOG_I("APP", "Network tasks complete. WiFi remains connected.");
        }
    }
    vTaskDelete(NULL);
}

void saveCustomSatellites() {
    Preferences prefs;
    prefs.begin("satellites", false);
    String idList = "";
    for (int i = 14; i < NUM_SATELLITES; i++) {
        idList += String(g_satellites[i].noradId);
        if (i < NUM_SATELLITES - 1) idList += ",";
    }
    prefs.putString("customIds", idList);
    prefs.end();
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
                    p.description = "Custom added satellite.";
                    p.tle = loaded_tle;
                    p.calc.init(p.tle);
                    if (NUM_SATELLITES < MAX_SATELLITES) {
                        g_satellites[NUM_SATELLITES++] = p;
                    }
                }
            }
        }
    }
    
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
    // Auto connects at boot and auto disconnects when done
    manualWifiToggle = false;
    xTaskCreatePinnedToCore(networkTask, "NetworkTask", 8192, NULL, 1, NULL, 0);
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
        int lastSpace = text.lastIndexOf(' ', end);
        if (lastSpace > start && lastSpace > start + fitChars/2) {
            end = lastSpace;
        }
        if (draw) canvas->drawString(text.substring(start, end).c_str(), x, y);
        start = end + 1;
        y += lineH;
    }
    return lines;
}

void drawSatSelectPage() {
    auto canvas = earth_renderer->getCanvas();
    uint16_t width = canvas->width();
    uint16_t height = canvas->height();
    
    // Background
    canvas->fillRect(0, 0, width, height, canvas->color565(20, 30, 40));
    
    // Top Bar
    canvas->fillRect(0, 0, width, 20, canvas->color565(100, 50, 200)); // Purple
    canvas->setTextColor(TFT_WHITE);
    canvas->setTextSize(1);
    canvas->drawString("Satellites Encyclopedia", 5, 6);
    
    // Left Panel (List)
    int yPos = 25;
    int itemsPerPage = 6;
    int startIndex = (satSelectedIndex / itemsPerPage) * itemsPerPage;
    
    // Total items is NUM_SATELLITES + 1 (the Add row)
    for (int i = 0; i < itemsPerPage && (startIndex + i) <= NUM_SATELLITES; i++) {
        int index = startIndex + i;
        if (index == satSelectedIndex) {
            canvas->fillRect(2, yPos - 2, 82, 15, canvas->color565(0, 120, 255));
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
            // "Add Custom" row
            String text = isDownloadingCustom ? "Downloading..." : ("[+] " + noradInput + "_");
            canvas->drawString(text.c_str(), 4, yPos);
        }
        
        yPos += 16;
    }
    
    // Right Panel (Description)
    canvas->drawFastVLine(85, 20, height-20, TFT_DARKGREY);
    
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
            canvas->fillRect(iconX - 5, iconY + 8, 4, 4, TFT_ORANGE); // Engine 1
            canvas->fillRect(iconX + 2, iconY + 8, 4, 4, TFT_ORANGE); // Engine 2
        } else { // SATELLITE
            canvas->fillRect(iconX - 3, iconY - 3, 9, 9, TFT_WHITE);
            canvas->fillRect(iconX - 15, iconY - 3, 9, 9, satColor);
            canvas->fillRect(iconX - 6, iconY - 1, 3, 3, TFT_LIGHTGRAY);
        }
        
        // Draw Name next to icon
        canvas->setTextColor(selSat.color);
        canvas->drawString(selSat.name.c_str(), rightX + 48, descY + 8);
        descY += 32; // Skip past icon and name
        
        // Determine radio layout beforehand
        double tx, ty, tz;
        bool isTracking = false;
        double az = 0, el = 0, dist = 0;
        
        if (selSat.calc.getTEME(current_unix, tx, ty, tz)) {
            double gmst = CoordTransform::getGMST(CoordTransform::unixToJulian(current_unix));
            ECEFCoord satEcef = CoordTransform::temeToECEF(tx, ty, tz, gmst);
            GeodeticCoord obsGeo = {baseUserLat, baseUserLon, baseUserAlt / 1000.0};
            TopocentricCoord topo = CoordTransform::ecefToTopocentric(obsGeo, satEcef);
            az = topo.az; el = topo.el; dist = topo.range;
            if (el > 0) isTracking = true;
        }
        
        int radioY = isTracking ? 90 : (selSat.downlinkFreq.length() > 0 ? 101 : height);
        
        // Draw Description with Auto-Scroll
        canvas->setTextColor(TFT_LIGHTGRAY);
        if (selSat.description) {
            int descAreaHeight = radioY - descY - 2;
            int totalLines = drawWrappedText(canvas, selSat.description, rightX, descY, width - rightX - 5, 10, false);
            int totalHeight = totalLines * 10;
            
            int yOffset = 0;
            if (totalHeight > descAreaHeight && descAreaHeight > 10) {
                int scrollRange = totalHeight - descAreaHeight + 10; // Extra 10px to show bottom clearly
                int cycleTime = scrollRange * 33 + 2000; // 1000ms pause at each end, 30px/sec speed
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
        
        // Draw Radio Info Block
        if (isTracking) {
            double tx_prev, ty_prev, tz_prev;
            double dist_prev = dist;
            if (selSat.calc.getTEME(current_unix - 1, tx_prev, ty_prev, tz_prev)) {
                double gmst_prev = CoordTransform::getGMST(CoordTransform::unixToJulian(current_unix - 1));
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
            
            canvas->fillRect(rightX - 2, radioY - 2, width - rightX - 2, 47, canvas->color565(30, 40, 50));
            
            canvas->setTextColor(TFT_YELLOW);
            char posBuf[32];
            sprintf(posBuf, "Az:%03.0f El:%02.0f", az, el);
            canvas->drawString(posBuf, rightX, radioY);
            
            if (dlFreq > 0) {
                canvas->setTextColor(TFT_GREEN);
                char freqBuf[32];
                sprintf(freqBuf, "Rx:%s", selSat.downlinkFreq.c_str());
                canvas->drawString(freqBuf, rightX, radioY + 11);
                
                canvas->setTextColor(dopplerShiftHz > 0 ? TFT_CYAN : TFT_ORANGE);
                char dopBuf[32];
                sprintf(dopBuf, "%+dHz", (int)dopplerShiftHz);
                canvas->drawString(dopBuf, rightX + canvas->textWidth(freqBuf) + 2, radioY + 11);
            }
            
            if (selSat.uplinkFreq.length() > 0) {
                canvas->setTextColor(TFT_RED);
                String txStr = "Tx:" + selSat.uplinkFreq;
                if (selSat.tone.length() > 0) txStr += " T:" + selSat.tone;
                canvas->drawString(txStr.c_str(), rightX, radioY + 22);
            }
            
            canvas->setTextColor(TFT_LIGHTGRAY);
            canvas->drawString("Mode: " + selSat.radioMode, rightX, radioY + 33);
        } else if (selSat.downlinkFreq.length() > 0) {
            // Draw a subtle background for the radio info (static)
            canvas->fillRect(rightX - 2, radioY - 2, width - rightX - 2, 36, canvas->color565(30, 40, 50));
            canvas->setTextColor(TFT_GREEN);
            canvas->drawString("Rx: " + selSat.downlinkFreq + " MHz", rightX, radioY);
            if (selSat.uplinkFreq.length() > 0) {
                canvas->setTextColor(TFT_RED);
                String txStr = "Tx:" + selSat.uplinkFreq;
                if (selSat.tone.length() > 0) txStr += " T:" + selSat.tone;
                canvas->drawString(txStr.c_str(), rightX, radioY + 11);
            }
            canvas->setTextColor(TFT_CYAN);
            canvas->drawString("Mode: " + selSat.radioMode, rightX, radioY + 22);
        }
    } else {
        canvas->setTextColor(downloadErrorMsg.length() > 0 ? TFT_RED : TFT_LIGHTGRAY);
        String msg = downloadErrorMsg.length() > 0 ? downloadErrorMsg : "Enter 5-digit NORAD ID to add custom satellite.";
        drawWrappedText(canvas, msg.c_str(), rightX, descY, width - rightX - 5, 10);
    }
    
    // Draw Delete Confirm Popup
    if (deleteConfirmIndex >= 0 && deleteConfirmIndex < NUM_SATELLITES) {
        canvas->fillRect(40, height/2 - 20, width - 80, 40, TFT_RED);
        canvas->drawRect(40, height/2 - 20, width - 80, 40, TFT_WHITE);
        canvas->setTextColor(TFT_WHITE);
        canvas->drawString("Delete Custom Sat?", 45, height/2 - 15);
        canvas->drawString("[y] Yes  [n] No", 45, height/2 + 5);
    }
}

void loop() {
    M5Cardputer.update();

    // BtnG0 (side button): trigger screenshot transfer via serial
    if (M5Cardputer.BtnA.wasPressed()) {
        doScreenshot();
    }

    // CRITICAL: IMU and Attitude filter must update as fast as possible!
    // Otherwise the AHRS filter will diverge and cause freezing/lag.
    if (imu && attitude) {
        imu->update();
        attitude->update();
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
        bool isFastForwarding = false;
        
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
                } else if (isSatViewMode || (!isManualLocationMode)) {
                    if (key == ',') current_unix -= 60;
                    else if (key == '/') current_unix += 60;
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
                    if (key == ';') { baseUserLat += step; if (baseUserLat > 90) baseUserLat = 90; }
                    else if (key == '.') { baseUserLat -= step; if (baseUserLat < -90) baseUserLat = -90; }
                    else if (key == ',') { baseUserLon -= step; if (baseUserLon < -180) baseUserLon += 360; }
                    else if (key == '/') { baseUserLon += step; if (baseUserLon > 180) baseUserLon -= 360; }
                    else if (key == '[') { baseUserAlt -= 10.0; if (baseUserAlt < -500) baseUserAlt = -500; }
                    else if (key == ']') { baseUserAlt += 10.0; if (baseUserAlt > 9000) baseUserAlt = 9000; }
                }
                
                if (key == ' ') {
                    isImuLocked = !isImuLocked;
                }
                
                if (key == '-' || key == '_') {
                    currentZoom -= 0.2f;
                    if (currentZoom < 1.0f) currentZoom = 1.0f;
                } else if (key == '=' || key == '+') {
                    currentZoom += 0.2f;
                    if (currentZoom > 20.0f) currentZoom = 20.0f;
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
                        if (!isManualLocationMode) isFastForwarding = true; // Flag for rendering optimization
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
                        if (!isManualLocationMode) isFastForwarding = true;
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
                        currentZoom = 1.0f;
                        earth_renderer->setZoom(currentZoom);
                    }
                    isManualLocationMode = !isManualLocationMode;
                    if (!isManualLocationMode) {
                        triggerPrediction = true;
                        portENTER_CRITICAL(&passMutex);
                        predictionsReady = false;
                        portEXIT_CRITICAL(&passMutex);
                    }
                } else if (M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE)) {
                    showHud = !showHud;
                } else if (M5Cardputer.Keyboard.isKeyPressed(27) || M5Cardputer.Keyboard.isKeyPressed('`')) {
                    if (showRecommendations) {
                        if (selectedPassIndex != -1) {
                            selectedPassIndex = -1; // Back to tree
                        } else {
                            showRecommendations = false; // Close panel
                        }
                    } else if (showHelp) {
                        showHelp = false;
                    } else if (isManualLocationMode) {
                        isManualLocationMode = false;
                    } else if (isSatViewMode) {
                        isSatViewMode = false;
                    }
                } else if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
                    if (appState == STATE_MAIN && !showRecommendations) {
                        showRecommendations = true;
                        passScrollIndex = 0;
                        triggerPrediction = true;
                        portENTER_CRITICAL(&passMutex);
                        predictionsReady = false;
                        portEXIT_CRITICAL(&passMutex);
                        rebuildTree(current_unix);
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
                    if (!HalWifi::isConnected()) {
                        manualWifiToggle = true;
                        xTaskCreatePinnedToCore(networkTask, "NetworkTask", 8192, NULL, 1, NULL, 0);
                    } else {
                        WiFi.disconnect(true);
                        WiFi.mode(WIFI_OFF);
                    }
                } else if (M5Cardputer.Keyboard.isKeyPressed('s')) {
                    appState = STATE_SAT_SELECT;
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
                        if (focusSatIndex >= 0 && focusSatIndex < NUM_SATELLITES && g_satellites[focusSatIndex].selected) {
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
                            if (attitude && imu) {
                                AttitudeData att = attitude->getAttitude();
                                basePitch = att.pitch;
                                baseRoll = att.roll;
                            }
                        }
                    }
                } else if (M5Cardputer.Keyboard.isKeyPressed(';')) {
                    if (isSatViewMode) {
                        int idx = focusSatIndex - 1;
                        for (int count = 0; count < NUM_SATELLITES; count++) {
                            if (idx < 0) idx = NUM_SATELLITES - 1;
                            if (g_satellites[idx].selected) {
                                focusSatIndex = idx;
                                break;
                            }
                            idx--;
                        }
                    }
                } else if (M5Cardputer.Keyboard.isKeyPressed('.')) {
                    if (isSatViewMode) {
                        int idx = focusSatIndex + 1;
                        for (int count = 0; count < NUM_SATELLITES; count++) {
                            if (idx >= NUM_SATELLITES) idx = 0;
                            if (g_satellites[idx].selected) {
                                focusSatIndex = idx;
                                break;
                            }
                            idx++;
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
                            networkTask, "NetworkTask", 8192, params, 1, NULL, 0
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
                if (deleteConfirmIndex >= 0) {
                    if (M5Cardputer.Keyboard.isKeyPressed('y')) {
                        if (deleteConfirmIndex >= 14 && deleteConfirmIndex < NUM_SATELLITES) {
                            for (int i = deleteConfirmIndex; i < NUM_SATELLITES - 1; i++) {
                                g_satellites[i] = g_satellites[i + 1];
                            }
                            NUM_SATELLITES--;
                            if (focusSatIndex == deleteConfirmIndex) focusSatIndex = -1;
                            else if (focusSatIndex > deleteConfirmIndex) focusSatIndex--;
                            if (satSelectedIndex >= NUM_SATELLITES) satSelectedIndex = NUM_SATELLITES;
                            saveCustomSatellites();
                            
                            // Retrigger prediction
                            triggerPrediction = true;
                            portENTER_CRITICAL(&passMutex);
                            predictionsReady = false;
                            portEXIT_CRITICAL(&passMutex);
                        }
                        deleteConfirmIndex = -1;
                    } else if (M5Cardputer.Keyboard.isKeyPressed('n') || M5Cardputer.Keyboard.isKeyPressed(27)) {
                        deleteConfirmIndex = -1;
                    }
                } else if (satSelectedIndex == NUM_SATELLITES) {
                    // Inputting NORAD ID
                    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE)) {
                        if (noradInput.length() > 0) noradInput.remove(noradInput.length() - 1);
                        downloadErrorMsg = "";
                    } else if (M5Cardputer.Keyboard.isKeyPressed(27) || M5Cardputer.Keyboard.isKeyPressed('`')) {
                        appState = STATE_MAIN;
                    } else if (M5Cardputer.Keyboard.isKeyPressed(';')) {
                        if (satSelectedIndex > 0) satSelectedIndex--;
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
                                p.selected = true; // Auto select newly added custom sat
                                p.iconType = ICON_SATELLITE;
                                p.tle = loaded_tle;
                                p.calc.init(p.tle);
                                if (NUM_SATELLITES < MAX_SATELLITES) {
                                    g_satellites[NUM_SATELLITES++] = p;
                                    saveCustomSatellites();
                                }
                                noradInput = "";
                                
                                // Retrigger prediction
                                triggerPrediction = true;
                                portENTER_CRITICAL(&passMutex);
                                predictionsReady = false;
                                portEXIT_CRITICAL(&passMutex);
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
                        // Trigger predictor to rerun since selection might have changed
                        portENTER_CRITICAL(&passMutex);
                        predictionsReady = false;
                        portEXIT_CRITICAL(&passMutex);
                        triggerPrediction = true;
                    } else if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
                        g_satellites[satSelectedIndex].selected = !g_satellites[satSelectedIndex].selected;
                    } else if (M5Cardputer.Keyboard.isKeyPressed('d') && satSelectedIndex >= 14) {
                        deleteConfirmIndex = satSelectedIndex;
                    } else if (M5Cardputer.Keyboard.isKeyPressed(';')) { // UP arrow
                        if (satSelectedIndex > 0) satSelectedIndex--;
                        else satSelectedIndex = NUM_SATELLITES; // Move to Add row
                    } else if (M5Cardputer.Keyboard.isKeyPressed('.')) { // DOWN arrow
                        satSelectedIndex = (satSelectedIndex + 1) % (NUM_SATELLITES + 1); // +1 for Add row
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
                    baseUserLat = gData.latitude;
                    baseUserLon = gData.longitude;
                }
                
                static bool gnssTimeSynced = false;
                if (gData.timeValid && gData.dateValid && !gnssTimeSynced) {
                    current_unix = convertGNSSDateToUnix(gData.year, gData.month, gData.day, gData.hour, gData.minute, gData.second);
                    gnssTimeSynced = true;
                    LOG_I("APP", "Time synced to GNSS UTC: %u", current_unix);
                    
                    // Trigger predictor again with correct time
                    portENTER_CRITICAL(&passMutex);
                    predictionsReady = false;
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
        double targetFocusAlt = 0.0;
        
        earth_renderer->setZoom(currentZoom);
        
        if (isSatViewMode && focusSatIndex >= 0 && focusSatIndex < NUM_SATELLITES && g_satellites[focusSatIndex].selected) {
            double tx, ty, tz;
            if (g_satellites[focusSatIndex].calc.getTEME(current_unix, tx, ty, tz)) {
                double gmst = CoordTransform::getGMST(CoordTransform::unixToJulian(current_unix));
                ECEFCoord ecef = CoordTransform::temeToECEF(tx, ty, tz, gmst);
                GeodeticCoord geo = CoordTransform::ecefToGeodetic(ecef);
                targetViewLat = geo.lat;
                targetViewLon = geo.lon;
                targetFocusAlt = geo.alt;
            }
            
            targetOffsetX = 0; targetOffsetY = 0; // Keep centered
            if (attitude && imu) {
                if (!isImuLocked) {
                    AttitudeData att = attitude->getAttitude();
                    lockedPitch = att.pitch - basePitch;
                    lockedRoll = att.roll - baseRoll;
                }
                // Pass real pitch and roll to camera, scaled by zoom to prevent flying off screen
                float zoomScale = 1.0f / currentZoom;
                targetPitch = -lockedPitch * zoomScale;
                targetRoll = -lockedRoll * zoomScale;
                targetYaw = 0;
            }
        } else if (isManualLocationMode) {
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
            
            // Use IMU to tilt globe at 1x zoom, and tilt camera at high zoom
            float t = (currentZoom - 1.0f) / 14.0f; // 0.0 to 1.0
            float globeFactor = 1.0f - t;
            float cameraFactor = t * (1.0f / currentZoom);
            
            // Anchor view to user location, but let IMU spin globe at low zoom
            // Note: signs are flipped to match the expected intuitive physical rotation
            targetViewLat = baseUserLat - lockedPitch * globeFactor; 
            targetViewLon = baseUserLon - lockedRoll * globeFactor; 
            
            float dynamicPitch = t * 70.0f;
            int baseOffsetY = (int)(t * 60.0f);
            
            targetPitch = dynamicPitch - lockedPitch * cameraFactor;
            targetRoll = -lockedRoll * cameraFactor;
            targetYaw = 0;
            
            // Fix: Calculate exact offset to counteract the pitch-induced displacement
            // so the User Pin stays exactly at (centerY + baseOffsetY)
            float r = 60.75f * currentZoom; // 135 * 0.45 * zoom
            float pitchRad = targetPitch * DEG_TO_RAD;
            targetOffsetY = baseOffsetY - (int)(r * sinf(pitchRad));
        }
        
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
        
        float dt = isFastForwarding ? 1.0f : 0.15f; // Instant snap when fast forwarding
        
        smoothViewLat += (targetViewLat - smoothViewLat) * dt;
        smoothViewLon += (targetViewLon - smoothViewLon) * dt;
        if (smoothViewLon > 180.0) smoothViewLon -= 360.0;
        if (smoothViewLon < -180.0) smoothViewLon += 360.0;
        
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
            SunPositionData sunPos = sun_calc->calculatePosition(current_unix, viewLat, viewLon);
            earth_renderer->setSunPosition(sunPos.subsolarLat, sunPos.subsolarLon);
        }

        static uint32_t lastLogTime = 0;
        bool shouldLogNow = (current_unix != lastLogTime && (current_unix % 10 == 0));
        if (shouldLogNow && appState == STATE_MAIN) {
            lastLogTime = current_unix;
            int offset = 8; // Nanning uses China Standard Time (UTC+8), while simple geo math gave +7
            time_t local_unix = current_unix + offset * 3600;
            struct tm *ti = gmtime(&local_unix);
            log_i("--- Satellite Positions at Local Time: %04d-%02d-%02d %02d:%02d:%02d ---", 
                  ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday, ti->tm_hour, ti->tm_min, ti->tm_sec);
        }

        static std::vector<SatRenderData> sats;
        sats.clear();
        sats.reserve(NUM_SATELLITES);
        int orbitsCalculatedThisFrame = 0;
        for (int i = 0; i < NUM_SATELLITES; i++) {
            if (!g_satellites[i].selected) continue;
            
            double tx, ty, tz;
            if (g_satellites[i].calc.getTEME(current_unix, tx, ty, tz)) {
                double gmst = CoordTransform::getGMST(CoordTransform::unixToJulian(current_unix));
                ECEFCoord ecef = CoordTransform::temeToECEF(tx, ty, tz, gmst);
                GeodeticCoord geo = CoordTransform::ecefToGeodetic(ecef);
                if (shouldLogNow && appState == STATE_MAIN) {
                    bool inShadow = false;
                    if (sun_calc) {
                        SunPositionData sPos = sun_calc->calculatePosition(current_unix, viewLat, viewLon);
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
                    log_i("[%s] Lat: %.2f, Lon: %.2f, Alt: %.1f km, Shadow: %s", 
                          g_satellites[i].name.c_str(), geo.lat, geo.lon, geo.alt, inShadow ? "YES" : "NO");
                }
                
                SatRenderData data;
                data.name = g_satellites[i].name.c_str();
                data.iconType = g_satellites[i].iconType;
                data.currentPos = geo;
                data.color = g_satellites[i].color;
                
                calculateOrbit(g_satellites[i].calc, current_unix, g_satellites[i].cache, orbitsCalculatedThisFrame);
                
                data.pastOrbit = &(g_satellites[i].cache.past);
                data.futureOrbit = &(g_satellites[i].cache.future);
                
                sats.push_back(data);
            }
        }
        
        // Render scene
        double renderUserLat = baseUserLat;
        if (isManualLocationMode && ((millis() / 500) % 2 == 0)) {
            renderUserLat = 999.0; // Blink marker by putting it off-planet
        }
        earth_renderer->setObserverConstrained(!isSatViewMode);
        earth_renderer->setFastForwarding(isFastForwarding);
        earth_renderer->setUnixTime(current_unix);
        earth_renderer->render(viewLat, viewLon, renderUserLat, baseUserLon, sats);
        
        // Draw coordinate overlay
        if (!showRecommendations && !showHelp && appState == STATE_MAIN && showHud) {
            earth_renderer->getCanvas()->setTextSize(1);
            earth_renderer->getCanvas()->setTextColor(TFT_LIGHTGRAY);
            
            char latDir = baseUserLat >= 0 ? 'N' : 'S';
            char lonDir = baseUserLon >= 0 ? 'E' : 'W';
            double alt = baseUserAlt;
            if (gnss && gnss->getStatus() == GNSS_STATUS_LOCKED) {
                alt = gnss->getData().altitude;
                baseUserAlt = alt; // Keep in sync
            }
            
            char latStr[16], lonStr[16], altStr[16];
            snprintf(latStr, sizeof(latStr), "%c%.2f", latDir, abs(baseUserLat));
            snprintf(lonStr, sizeof(lonStr), "%c%.2f", lonDir, abs(baseUserLon));
            snprintf(altStr, sizeof(altStr), "%.0fm", alt);
            
            earth_renderer->getCanvas()->drawString(latStr, 5, 5);
            earth_renderer->getCanvas()->drawString(lonStr, 5, 17);
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
            
            drawHotKey("Config(Loc&Alt[])", 'c', x + 5, ty); ty += 12;
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
                int y = 25;
                if (recommendedPasses.empty()) {
                    earth_renderer->getCanvas()->drawString("No passes in 7 days", 5, 30);
                }
                                if (selectedPassIndex != -1) {
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
                    if (sIdx != -1) {
                        double tx, ty, tz;
                        if (g_satellites[sIdx].calc.getTEME(current_unix, tx, ty, tz)) {
                            double gmst = CoordTransform::getGMST(CoordTransform::unixToJulian(current_unix));
                            ECEFCoord ecef = CoordTransform::temeToECEF(tx, ty, tz, gmst);
                            GeodeticCoord geo = CoordTransform::ecefToGeodetic(ecef);
                            GeodeticCoord obsGeo = {baseUserLat, baseUserLon, baseUserAlt / 1000.0};
                            TopocentricCoord topo = CoordTransform::ecefToTopocentric(obsGeo, ecef);
                            double az = topo.az;
                            double el = topo.el;
                            double dist = topo.range;
                            
                            double tx_prev, ty_prev, tz_prev;
                            double dist_prev = dist;
                            if (g_satellites[sIdx].calc.getTEME(current_unix - 1, tx_prev, ty_prev, tz_prev)) {
                                double gmst_prev = CoordTransform::getGMST(CoordTransform::unixToJulian(current_unix - 1));
                                ECEFCoord ecef_prev = CoordTransform::temeToECEF(tx_prev, ty_prev, tz_prev, gmst_prev);
                                TopocentricCoord topo_prev = CoordTransform::ecefToTopocentric(obsGeo, ecef_prev);
                                dist_prev = topo_prev.range;
                            }
                            double range_rate = dist - dist_prev;
                            
                            earth_renderer->getCanvas()->setTextColor(TFT_GREEN);
                            char azaltBuf[32];
                            sprintf(azaltBuf, "Az:%03d Alt:%02d", (int)az, (int)el);
                            earth_renderer->getCanvas()->drawString(azaltBuf, 5, 97);
                            
                            if (g_satellites[sIdx].downlinkFreq.length() > 0) {
                                double freq_mhz = g_satellites[sIdx].downlinkFreq.toDouble();
                                double shift_khz = (freq_mhz * -range_rate / 299792.458) * 1000.0;
                                char rxBuf[32];
                                sprintf(rxBuf, "Rx:%s (%+.1f)", g_satellites[sIdx].downlinkFreq.c_str(), shift_khz);
                                earth_renderer->getCanvas()->drawString(rxBuf, 5, 109);
                            }
                            if (g_satellites[sIdx].uplinkFreq.length() > 0) {
                                earth_renderer->getCanvas()->setTextColor(TFT_ORANGE);
                                String txStr = "Tx:" + g_satellites[sIdx].uplinkFreq;
                                if (g_satellites[sIdx].tone.length() > 0) txStr += " T:" + g_satellites[sIdx].tone;
                                earth_renderer->getCanvas()->drawString(txStr.c_str(), 5, 121);
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
            
            if (HalWifi::isConnected()) {
                earth_renderer->getCanvas()->setTextColor(TFT_GREEN);
                earth_renderer->getCanvas()->drawString("WiFi: ON", 5, 115);
            } else {
                earth_renderer->getCanvas()->setTextColor(TFT_LIGHTGRAY);
                earth_renderer->getCanvas()->drawString("WiFi: OFF", 5, 115);
            }
            
            if (gnss) {
                if (gnss->getStatus() == GNSS_STATUS_LOCKED) {
                    earth_renderer->getCanvas()->setTextColor(TFT_GREEN);
                    earth_renderer->getCanvas()->drawString("GNSS: FIX", 70, 115);
                } else if (gnss->isInStandbyMode()) {
                    if (gnssTimedOut) {
                        earth_renderer->getCanvas()->setTextColor(TFT_RED);
                        earth_renderer->getCanvas()->drawString("GNSS: TMOUT", 70, 115);
                    } else {
                        earth_renderer->getCanvas()->setTextColor(TFT_LIGHTGRAY);
                        earth_renderer->getCanvas()->drawString("GNSS: OFF", 70, 115);
                    }
                } else {
                    earth_renderer->getCanvas()->setTextColor(TFT_YELLOW);
                    earth_renderer->getCanvas()->drawString("GNSS: SCH", 70, 115);
                }
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
            time_t local_t = current_unix + tzOffsetSec;
            struct tm *ptm = gmtime(&local_t);
            snprintf(timeStr, sizeof(timeStr), "%02d-%02d %02d:%02d", ptm->tm_mon+1, ptm->tm_mday, ptm->tm_hour, ptm->tm_min);
            
            earth_renderer->getCanvas()->setTextSize(1);
            earth_renderer->getCanvas()->setTextColor(TFT_WHITE);
            int textWidth = earth_renderer->getCanvas()->textWidth(timeStr);
            earth_renderer->getCanvas()->drawString(timeStr, 238 - textWidth, 125);
            
            if (isSatViewMode && focusSatIndex >= 0 && focusSatIndex < NUM_SATELLITES) {
                earth_renderer->getCanvas()->setTextColor(g_satellites[focusSatIndex].color);
                earth_renderer->getCanvas()->drawString("Sat View", 180, 5);
                
                // Add calculation for Az/Alt and Doppler Shift
                double tx, ty, tz;
                if (g_satellites[focusSatIndex].calc.getTEME(current_unix, tx, ty, tz)) {
                    double gmst = CoordTransform::getGMST(CoordTransform::unixToJulian(current_unix));
                    ECEFCoord ecef = CoordTransform::temeToECEF(tx, ty, tz, gmst);
                    GeodeticCoord geo = CoordTransform::ecefToGeodetic(ecef);
                    GeodeticCoord obsGeo = {baseUserLat, baseUserLon, baseUserAlt / 1000.0};
                    TopocentricCoord topo = CoordTransform::ecefToTopocentric(obsGeo, ecef);
                    double az = topo.az;
                    double el = topo.el;
                    double dist = topo.range;
                    
                    double tx_prev, ty_prev, tz_prev;
                    double dist_prev = dist;
                    if (g_satellites[focusSatIndex].calc.getTEME(current_unix - 1, tx_prev, ty_prev, tz_prev)) {
                        double gmst_prev = CoordTransform::getGMST(CoordTransform::unixToJulian(current_unix - 1));
                        ECEFCoord ecef_prev = CoordTransform::temeToECEF(tx_prev, ty_prev, tz_prev, gmst_prev);
                        TopocentricCoord topo_prev = CoordTransform::ecefToTopocentric(obsGeo, ecef_prev);
                        dist_prev = topo_prev.range;
                    }
                    
                    double range_rate = dist - dist_prev;
                    
                    uint16_t satColor = g_satellites[focusSatIndex].color;
                    earth_renderer->getCanvas()->setTextColor(satColor);
                    
                    char azBuf[32];
                    char elBuf[32];
                    sprintf(azBuf, "Az : %03d", (int)az);
                    sprintf(elBuf, "Alt: %02d", (int)el);
                    earth_renderer->getCanvas()->drawString(azBuf, 5, 105);
                    earth_renderer->getCanvas()->drawString(elBuf, 5, 115);
                    
                    String freq = g_satellites[focusSatIndex].downlinkFreq;
                    if (freq.length() > 0) {
                        double freq_mhz = freq.toDouble();
                        double shift_khz = (freq_mhz * -range_rate / 299792.458) * 1000.0;
                        char freqBuf[32];
                        sprintf(freqBuf, "Rx : %s (%+.1f)", freq.c_str(), shift_khz);
                        earth_renderer->getCanvas()->drawString(freqBuf, 5, 125);
                    }
                }
            }
        }
        
        earth_renderer->getCanvas()->pushSprite(0, 0);
    }
}
