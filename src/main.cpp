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
    bool selected;
    SatIconType iconType;
    const char* description;
    TLEData tle;
    SGP4Calc calc;
    OrbitCache cache;
};

const int MAX_SATELLITES = 50;
int NUM_SATELLITES = 14;

SatProfile g_satellites[MAX_SATELLITES] = {
    {25544, "ISS", TFT_YELLOW, 2, true, ICON_STATION, "International Space Station. The largest human-made structure in space, visible as a very bright moving star."},
    {48274, "Tiangong", TFT_GREEN, 1, true, ICON_STATION, "China's Tiangong Space Station. A permanent modular space station in LEO."},
    {20580, "Hubble", TFT_CYAN, 0, true, ICON_TELESCOPE, "Hubble Space Telescope. A vital observatory that revolutionized our understanding of the universe."},
    {33591, "NOAA 19", TFT_ORANGE, 0, true, ICON_SATELLITE, "NOAA weather satellite. Known for transmitting APT weather images back to Earth."},
    {50463, "JWST", TFT_GOLD, 0, false, ICON_DEEPSPACE, "James Webb Space Telescope. Located at L2 point 1.5 million km away, observing in infrared."},
    {53690, "BlueWalker 3", TFT_WHITE, 0, false, ICON_SATELLITE, "AST SpaceMobile's prototype. Features a massive 64 sqm array, very bright and controversial."},
    {41882, "Fengyun-4A", TFT_BLUE, 0, false, ICON_SATELLITE, "Chinese geostationary meteorological satellite, located 35,786 km above the equator."},
    {43539, "BeiDou-3", TFT_RED, 0, false, ICON_SATELLITE, "Medium Earth Orbit navigation satellite part of the BeiDou system (BDS)."},
    {27386, "Envisat", TFT_LIGHTGRAY, 0, false, ICON_SATELLITE, "A huge 8-ton inactive Earth observation satellite. Now one of the largest pieces of space debris."},
    {4382, "DFH-1", TFT_RED, 0, false, ICON_SATELLITE, "Dong Fang Hong I. China's first satellite launched in 1970, still orbiting today as a silent monument."},
    {25994, "Terra", TFT_PINK, 0, false, ICON_SATELLITE, "NASA's flagship Earth Observing System satellite."},
    {27424, "Aqua", TFT_MAGENTA, 0, false, ICON_SATELLITE, "NASA Earth observation satellite focusing on the water cycle."},
    {43166, "Iridium 127", TFT_WHITE, 0, false, ICON_SATELLITE, "Iridium NEXT network. The original 1st-gen Iridium satellites produced legendary 'flares' up to mag -8."},
    {57165, "Meteor-M2", TFT_WHITE, 0, false, ICON_SATELLITE, "Russian meteorological satellite transmitting LRPT weather images."}
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

// Default GNSS location (Beijing for public release)
// double baseUserLat = 22.85; // Nanning (test location)
// double baseUserLon = 108.33;
double baseUserLat = 39.90; // Beijing
double baseUserLon = 116.40;

// Helper to pre-calculate orbits with caching
void calculateOrbit(SGP4Calc& calc, uint32_t baseTime, OrbitCache& cache, std::vector<GeodeticCoord>& past, std::vector<GeodeticCoord>& future) {
    // Only recalculate orbit path if simulated time has advanced by more than 5 minutes (300 seconds)
    // The ground track changes very slowly, so we don't need to redraw the path every 10 seconds.
    if (cache.lastCalcTime == 0 || abs((int)baseTime - (int)cache.lastCalcTime) > 300) {
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
    
    // Output from cache
    past = cache.past;
    future = cache.future;
}

#include "core/observation_predictor.h"

TaskHandle_t predictorTaskHandle = NULL;
std::vector<PassEvent> recommendedPasses;
bool showRecommendations = false;
int passScrollIndex = 0;

// IMU Lock State
bool isImuLocked = false;
float lockedPitch = 0;
float lockedRoll = 0;
float lockedYaw = 0;

unsigned long bootTime = 0;
bool showHelp = false;
bool isManualLocationMode = false;
bool predictionsReady = false;
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
        
        ObservationPredictor predictor(baseUserLat, baseUserLon, 0);
        std::vector<PassEvent> allPasses;
        
        // Use actual current time for predictions
        uint32_t startTime = current_unix;
        
        for (int i = 0; i < NUM_SATELLITES; i++) {
            if (triggerPrediction) break;
            
            if (g_satellites[i].selected) {
                auto passes = predictor.predictPasses(g_satellites[i].tle, startTime, 7);
                allPasses.insert(allPasses.end(), passes.begin(), passes.end());
                vTaskDelay(pdMS_TO_TICKS(10)); // Yield between satellites
            }
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
        portEXIT_CRITICAL(&passMutex);
    }
}

struct NetworkParams {
    String ssid;
    String pass;
    bool shouldSave;
};

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

void drawWrappedText(LGFX_Sprite* canvas, String text, int x, int y, int w, int lineH) {
    int start = 0;
    while (start < text.length()) {
        int fitChars = w / 6; // roughly 6px per char for setTextSize(1)
        if (start + fitChars >= text.length()) {
            canvas->drawString(text.substring(start).c_str(), x, y);
            break;
        }
        int end = start + fitChars;
        int lastSpace = text.lastIndexOf(' ', end);
        if (lastSpace > start && lastSpace > start + fitChars/2) {
            end = lastSpace;
        }
        canvas->drawString(text.substring(start, end).c_str(), x, y);
        start = end + 1;
        y += lineH;
    }
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
        } else { // SATELLITE
            canvas->fillRect(iconX - 3, iconY - 3, 9, 9, TFT_WHITE);
            canvas->fillRect(iconX - 15, iconY - 3, 9, 9, satColor);
            canvas->fillRect(iconX - 6, iconY - 1, 3, 3, TFT_LIGHTGRAY);
        }
        
        // Draw Name next to icon
        canvas->setTextColor(selSat.color);
        canvas->drawString(selSat.name.c_str(), rightX + 48, descY + 8);
        descY += 32; // Skip past icon and name
        
        canvas->setTextColor(TFT_LIGHTGRAY);
        if (selSat.description) {
            drawWrappedText(canvas, selSat.description, rightX, descY, width - rightX - 5, 10);
        } else {
            canvas->drawString("No description.", rightX, descY);
        }
    } else {
        canvas->setTextColor(downloadErrorMsg.length() > 0 ? TFT_RED : TFT_LIGHTGRAY);
        String msg = downloadErrorMsg.length() > 0 ? downloadErrorMsg : "Enter 5-digit NORAD ID to add custom satellite.";
        drawWrappedText(canvas, msg.c_str(), rightX, descY, width - rightX - 5, 10);
    }
    
    canvas->setTextColor(TFT_DARKGREY);
    canvas->drawString("[^/v] Sel [Enter] Toggle [d] Del Custom [ESC] Exit", 5, height - 12);
    
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

    // CRITICAL: IMU and Attitude filter must update as fast as possible!
    // Otherwise the AHRS filter will diverge and cause freezing/lag.
    if (imu && attitude) {
        imu->update();
        attitude->update();
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
            
            auto handleContinuousKey = [&](char key) {
                if (isSatViewMode || (!isManualLocationMode && !showRecommendations)) {
                    if (key == ',') current_unix -= 60;
                    else if (key == '/') current_unix += 60;
                } else if (isManualLocationMode) {
                    if (key == ';') { baseUserLat += 1.0; if (baseUserLat > 90) baseUserLat = 90; }
                    else if (key == '.') { baseUserLat -= 1.0; if (baseUserLat < -90) baseUserLat = -90; }
                    else if (key == ',') { baseUserLon -= 1.0; if (baseUserLon < -180) baseUserLon += 360; }
                    else if (key == '/') { baseUserLon += 1.0; if (baseUserLon > 180) baseUserLon -= 360; }
                } else if (showRecommendations) {
                    if (key == ';') { if (passScrollIndex > 0) passScrollIndex--; }
                    else if (key == '.') { int maxIndex = (int)recommendedPasses.size() - 3; if (maxIndex < 0) maxIndex = 0; if (passScrollIndex < maxIndex) passScrollIndex++; }
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
            
            if (currentKey != 0) {
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
                lastKey = 0;
            }
        }

        
        // Handle discrete keyboard input
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            if (appState == STATE_MAIN) {
                if (M5Cardputer.Keyboard.isKeyPressed('c')) {
                    isManualLocationMode = !isManualLocationMode;
                    if (!isManualLocationMode) {
                        triggerPrediction = true;
                        portENTER_CRITICAL(&passMutex);
                        predictionsReady = false;
                        portEXIT_CRITICAL(&passMutex);
                    }
                } else if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
                    showRecommendations = !showRecommendations;
                    passScrollIndex = 0;
                    if (showRecommendations) {
                        triggerPrediction = true;
                        portENTER_CRITICAL(&passMutex);
                        predictionsReady = false;
                        portEXIT_CRITICAL(&passMutex);
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
                        if (!found) isSatViewMode = false;
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
                    } else if (showRecommendations) {
                        if (passScrollIndex > 0) passScrollIndex--;
                    } else if (isManualLocationMode) {
                        baseUserLat += 1.0;
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
                    } else if (showRecommendations) {
                        int maxIndex = (int)recommendedPasses.size() - 3;
                        if (maxIndex < 0) maxIndex = 0;
                        if (passScrollIndex < maxIndex) passScrollIndex++;
                    } else if (isManualLocationMode) {
                        baseUserLat -= 1.0;
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
        
        // Default to looking at equator and prime meridian if no IMU
        double viewLat = 0.0;
        double viewLon = 0.0;
        
        earth_renderer->setZoom(currentZoom);
        
        if (isSatViewMode && focusSatIndex >= 0 && focusSatIndex < NUM_SATELLITES && g_satellites[focusSatIndex].selected) {
            double tx, ty, tz;
            if (g_satellites[focusSatIndex].calc.getTEME(current_unix, tx, ty, tz)) {
                double gmst = CoordTransform::getGMST(CoordTransform::unixToJulian(current_unix));
                ECEFCoord ecef = CoordTransform::temeToECEF(tx, ty, tz, gmst);
                GeodeticCoord geo = CoordTransform::ecefToGeodetic(ecef);
                viewLat = geo.lat;
                viewLon = geo.lon;
                earth_renderer->setCameraFocusAlt(geo.alt);
            } else {
                earth_renderer->setCameraFocusAlt(0);
            }
            
            earth_renderer->setCenterOffset(0, 0); // Keep centered
            if (attitude && imu) {
                if (!isImuLocked) {
                    AttitudeData att = attitude->getAttitude();
                    lockedPitch = att.pitch;
                    lockedRoll = att.roll;
                }
                // Option A: Pass real pitch and roll to camera (inverted as requested by user)
                earth_renderer->setCameraAttitude(-lockedPitch, -lockedRoll, 0);
            } else {
                earth_renderer->setCameraAttitude(0, 0, 0);
            }
        } else if (isManualLocationMode) {
            viewLat = baseUserLat;
            viewLon = baseUserLon;
            
            // "站在地面上" effect
            float dynamicPitch = (currentZoom - 1.0f) / 14.0f * 70.0f;
            int offsetY = (int)((currentZoom - 1.0f) / 14.0f * 60.0f);
            earth_renderer->setCenterOffset(0, offsetY);
            earth_renderer->setCameraFocusAlt(0);
            earth_renderer->setCameraAttitude(dynamicPitch, 0, 0);
        } else if (attitude && imu) {
            if (!isImuLocked) {
                AttitudeData att = attitude->getAttitude();
                lockedPitch = att.pitch;
                lockedRoll = att.roll;
            }
            
            // Anchor view to user location exactly
            viewLat = baseUserLat; 
            viewLon = baseUserLon; 
            
            float dynamicPitch = (currentZoom - 1.0f) / 14.0f * 70.0f;
            int offsetY = (int)((currentZoom - 1.0f) / 14.0f * 60.0f);
            earth_renderer->setCenterOffset(0, offsetY);
            earth_renderer->setCameraFocusAlt(0);
            
            // Use IMU to tilt camera around the user pin, just like in Sat View mode
            earth_renderer->setCameraAttitude(dynamicPitch - lockedPitch, -lockedRoll, 0);
        } else {
            earth_renderer->setCenterOffset(0, 0);
            earth_renderer->setCameraFocusAlt(0);
            earth_renderer->setCameraAttitude(0, 0, 0);
        }

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

        std::vector<SatRenderData> sats;

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
                data.name = g_satellites[i].name;
                data.iconType = g_satellites[i].iconType;
                data.currentPos = geo;
                data.color = g_satellites[i].color;
                
                // Skip expensive orbit path recalculation if user is holding the fast-forward button
                if (!isFastForwarding) {
                    calculateOrbit(g_satellites[i].calc, current_unix, g_satellites[i].cache, data.pastOrbit, data.futureOrbit);
                } else {
                    data.pastOrbit = g_satellites[i].cache.past;
                    data.futureOrbit = g_satellites[i].cache.future;
                }
                
                sats.push_back(data);
            }
        }
        
        // Render scene
        double renderUserLat = baseUserLat;
        if (isManualLocationMode && ((millis() / 500) % 2 == 0)) {
            renderUserLat = 999.0; // Blink marker by putting it off-planet
        }
        earth_renderer->setObserverConstrained(!isSatViewMode);
        earth_renderer->render(viewLat, viewLon, renderUserLat, baseUserLon, sats);
        
        // Draw coordinate overlay
        if (!showRecommendations && !showHelp && appState == STATE_MAIN) {
            earth_renderer->getCanvas()->setTextSize(1);
            earth_renderer->getCanvas()->setTextColor(TFT_LIGHTGRAY);
            
            char latDir = baseUserLat >= 0 ? 'N' : 'S';
            char lonDir = baseUserLon >= 0 ? 'E' : 'W';
            double alt = 0.0;
            if (gnss && gnss->getStatus() == GNSS_STATUS_LOCKED) {
                alt = gnss->getData().altitude;
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
            uint16_t w = 200, h = 144;
            int x = (canvas->width() - w) / 2;
            int y = (canvas->height() - h) / 2;
            
            canvas->fillRect(x, y, w, h, canvas->color565(20, 30, 40));
            canvas->drawRect(x, y, w, h, TFT_LIGHTGRAY);
            
            canvas->setTextColor(TFT_WHITE);
            canvas->setTextSize(1);
            canvas->drawString("--- Help & Shortcuts ---", x + 25, y + 5);
            
            canvas->setTextColor(TFT_CYAN);
            int ty = y + 20;
            canvas->drawString("[w]", x + 5, ty); canvas->setTextColor(TFT_LIGHTGRAY); canvas->drawString("WiFi Toggle", x + 30, ty); ty += 12;
            canvas->setTextColor(TFT_CYAN);
            canvas->drawString("[S]", x + 5, ty); canvas->setTextColor(TFT_LIGHTGRAY); canvas->drawString("Satellites", x + 35, ty); ty += 12;
            canvas->setTextColor(TFT_CYAN);
            canvas->drawString("[C]", x + 5, ty); canvas->setTextColor(TFT_LIGHTGRAY); canvas->drawString("Manual Loc (; . , /)", x + 35, ty); ty += 12;
            canvas->setTextColor(TFT_CYAN);
            canvas->drawString("[,][/]", x + 5, ty); canvas->setTextColor(TFT_LIGHTGRAY); canvas->drawString("Time Machine", x + 45, ty); ty += 12;
            canvas->setTextColor(TFT_CYAN);
            canvas->drawString("[Enter]", x + 5, ty); canvas->setTextColor(TFT_LIGHTGRAY); canvas->drawString("Toggle Pass List", x + 50, ty); ty += 12;
            canvas->setTextColor(TFT_CYAN);
            canvas->drawString("[H]", x + 5, ty); canvas->setTextColor(TFT_LIGHTGRAY); canvas->drawString("Toggle Help", x + 35, ty); ty += 12;
            canvas->setTextColor(TFT_CYAN);
            canvas->drawString("[g/G]", x + 5, ty); canvas->setTextColor(TFT_LIGHTGRAY); canvas->drawString("GNSS Toggle", x + 40, ty); ty += 12;
            canvas->setTextColor(TFT_CYAN);
            canvas->drawString("[v/V]", x + 5, ty); canvas->setTextColor(TFT_LIGHTGRAY); canvas->drawString("Sat View (; .)", x + 40, ty); ty += 12;
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
                earth_renderer->getCanvas()->drawString("Calculating...", 5, 30);
            } else {
                int y = 25;
                if (recommendedPasses.empty()) {
                    earth_renderer->getCanvas()->drawString("No passes in 7 days", 5, 30);
                }
                // Show top 3 recommendations based on scroll index
                for (int i = 0; i < 3 && (passScrollIndex + i) < recommendedPasses.size(); i++) {
                    const auto& p = recommendedPasses[passScrollIndex + i];
                    String stars = "";
                    for(int s=0;s<p.score;s++) stars += "*";
                    
                    uint16_t starColor = TFT_LIGHTGRAY;
                    if (p.score == 5) starColor = TFT_GOLD;
                    else if (p.score >= 3) starColor = TFT_GREEN;
                    
                    String nameLine = String(p.satName.c_str()) + " ";
                    earth_renderer->getCanvas()->setTextColor(TFT_WHITE);
                    earth_renderer->getCanvas()->drawString(nameLine.c_str(), 5, y);
                    
                    int textW = earth_renderer->getCanvas()->textWidth(nameLine.c_str());
                    earth_renderer->getCanvas()->setTextColor(starColor);
                    earth_renderer->getCanvas()->drawString(stars.c_str(), 5 + textW, y);
                    
                    // Convert aosTime to local time string
                    int tzOffsetSec = pos_manager ? pos_manager->getTimezoneManager()->getTimezoneOffset(baseUserLat, baseUserLon) : ((int)round(baseUserLon / 15.0) * 3600);
                    time_t aos_t = (time_t)p.aosTime + tzOffsetSec;
                    struct tm * aos_tm = gmtime(&aos_t);
                    char timeStr[32];
                    sprintf(timeStr, "%02d/%02d %02d:%02d", aos_tm->tm_mon + 1, aos_tm->tm_mday, aos_tm->tm_hour, aos_tm->tm_min);
                    
                    // Helper to convert azimuth to compass direction
                    auto getAzStr = [](float az) -> const char* {
                        const char* dirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
                        int idx = (int)round(az / 45.0) % 8;
                        if (idx < 0) idx += 8;
                        return dirs[idx];
                    };
                    
                    String info = String(timeStr) + " " + getAzStr(p.startAz) + "->" + getAzStr(p.maxAz) + " " + String((int)p.maxElevation) + "deg";
                    
                    earth_renderer->getCanvas()->setTextColor(TFT_LIGHTGRAY);
                    earth_renderer->getCanvas()->drawString(info.c_str(), 5, y+10);
                    y += 26;
                }
                
                // Draw scrollbar or scroll indicator if needed
                if (recommendedPasses.size() > 3) {
                    earth_renderer->getCanvas()->setTextColor(TFT_DARKGREY);
                    earth_renderer->getCanvas()->drawString("[^/v]", 105, 5);
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
        if (appState == STATE_MAIN) {
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
            }
        }
        
        earth_renderer->getCanvas()->pushSprite(0, 0);
    }
}