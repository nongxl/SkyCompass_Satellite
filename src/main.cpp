#include <Arduino.h>
#include <M5Cardputer.h>
#include "core/tle_data.h"
#include "core/sgp4_calc.h"
#include "core/coord_transform.h"
#include "core/earth_renderer.h"
#include "core/observation_predictor.h"
#include "core/tle_updater.h"

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
    TLEData tle;
    SGP4Calc calc;
    OrbitCache cache;
};

SatProfile g_satellites[] = {
    {25544, "ISS", TFT_YELLOW, 2, true},
    {48274, "Tiangong", TFT_GREEN, 1, true},
    {20580, "Hubble", TFT_CYAN, 0, true},
    {33591, "NOAA 19", TFT_ORANGE, 0, true},
    {25994, "Terra", TFT_PINK, 0, false},
    {27424, "Aqua", TFT_MAGENTA, 0, false},
    {57165, "Meteor-M2", TFT_WHITE, 0, false}
};
const int NUM_SATELLITES = sizeof(g_satellites) / sizeof(g_satellites[0]);

// We use a simulated time starting near the TLE epoch for Phase 3 offline testing
uint32_t current_unix = 0; // Will be set in setup()
unsigned long last_update = 0;

// Default GNSS location (Beijing)
double baseUserLat = 39.9042;
double baseUserLon = 116.4074;

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
bool isManualLocationMode = false;
bool predictionsReady = false;
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
        
        // Sort by start time
        std::sort(allPasses.begin(), allPasses.end(), [](const PassEvent& a, const PassEvent& b) {
            return a.aosTime < b.aosTime;
        });
        
        portENTER_CRITICAL(&passMutex);
        recommendedPasses = allPasses;
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
        Serial.println("No WiFi credentials available. Staying offline.");
        vTaskDelete(NULL);
        return;
    }

    // 1. Connect WiFi
    HalWifi::begin(ssid.c_str(), pass.c_str());
    
    if (HalWifi::isConnected() && shouldSave) {
        HalWifi::saveCredentials(ssid, pass);
    }
    
    if (HalWifi::isConnected()) {
        // 2. Fetch NTP
        HalWifi::syncNTPTime();
        
        // 3. Update time
        uint32_t ntpTime = HalWifi::getUnixTime();
        if (ntpTime > 0) {
            current_unix = ntpTime;
            Serial.printf("Time synced to UTC: %u\n", current_unix);
        }

        // 3.5 IP Geolocation
        if (!gnss || gnss->getStatus() != GNSS_STATUS_LOCKED) {
            Serial.println("Fetching IP Geolocation...");
            HTTPClient http;
            http.begin("http://ip-api.com/json/");
            int httpCode = http.GET();
            if (httpCode == HTTP_CODE_OK) {
                String payload = http.getString();
                // Parse lat/lon directly from JSON string to avoid ArduinoJson dependency
                int latIdx = payload.indexOf("\"lat\":");
                int lonIdx = payload.indexOf("\"lon\":");
                if (latIdx > 0 && lonIdx > 0) {
                    int latEnd = payload.indexOf(",", latIdx);
                    int lonEnd = payload.indexOf(",", lonIdx);
                    if (latEnd > 0 && lonEnd > 0) {
                        String latStr = payload.substring(latIdx + 6, latEnd);
                        String lonStr = payload.substring(lonIdx + 6, lonEnd);
                        baseUserLat = latStr.toDouble();
                        baseUserLon = lonStr.toDouble();
                        Serial.printf("IP Geolocation successful: Lat %f, Lon %f\n", baseUserLat, baseUserLon);
                    }
                }
            } else {
                Serial.printf("IP Geolocation failed. HTTP Code: %d\n", httpCode);
            }
            http.end();
        }

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
            Serial.println("TLE Data is ready and models updated!");
            
            // Rerun predictor with new data
            portENTER_CRITICAL(&passMutex);
            predictionsReady = false;
            portEXIT_CRITICAL(&passMutex);
            triggerPrediction = true;
        }
        
        Serial.println("Network tasks complete. Turning off WiFi to save power.");
        HalWifi::disconnect();
    }
    vTaskDelete(NULL);
}

void setup() {
    Serial.begin(115200);
    // Remove the 4 second delay to boot instantly
    Serial.println(F("\n\n--- SkyCompass Satellite: Phase 4 ---"));

    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    
    earth_renderer = new EarthRenderer(&M5Cardputer.Display);
    earth_renderer->begin();

    // Initialize IMU
    if (imu && imu->begin()) {
        attitude = new AttitudeEstimator(imu);
        attitude->begin();
        Serial.println(F("IMU Initialized"));
    }
    
    // Initialize Position & Sun Calculator
    pos_manager = new PositionManager(gnss);
    pos_manager->begin(); 
    
    sun_calc = new SunCalculator(pos_manager);
    sun_calc->begin();
    
    // Setup LittleFS for TLE Cache
    TLEUpdater::begin();
    
    // Offline initialization (use default TLEs)
    g_satellites[0].tle = TLEManager::getISS_TLE();
    g_satellites[1].tle = TLEManager::getTiangong_TLE();
    g_satellites[2].tle = TLEManager::getHubble_TLE();
    
    for (int i = 0; i < 3; i++) {
        g_satellites[i].calc.init(g_satellites[i].tle);
    }
    
    // Set default offline time
    current_unix = TLEManager::getMockTimeAnchor();
    Serial.println("Offline boot: Using Mock Time Anchor.");
    
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
    xTaskCreatePinnedToCore(
        networkTask,
        "NetworkTask",
        8192,
        NULL,
        1,
        NULL,
        0
    );
}

void drawWiFiSetupPage() {
    auto canvas = earth_renderer->getCanvas();
    uint16_t width = canvas->width();
    uint16_t height = canvas->height();
    
    // Background
    canvas->fillRect(0, 0, width, height, canvas->color565(20, 30, 40));
    
    // Top Bar
    canvas->fillRect(0, 0, width, 25, TFT_ORANGE);
    canvas->setTextColor(TFT_BLACK);
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
                    canvas->fillRect(5, yPos - 2, width - 10, 18, canvas->color565(0, 120, 255));
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
    auto canvas = earth_renderer->getCanvas();
    uint16_t width = canvas->width();
    uint16_t height = canvas->height();
    
    // Background
    canvas->fillRect(0, 0, width, height, canvas->color565(20, 30, 40));
    
    // Top Bar
    canvas->fillRect(0, 0, width, 25, canvas->color565(100, 50, 200)); // Purple
    canvas->setTextColor(TFT_WHITE);
    canvas->setTextSize(2);
    canvas->drawString("Satellites", 10, 5);
    
    canvas->setTextSize(1);
    int yPos = 35;
    int itemsPerPage = 4;
    int startIndex = (satSelectedIndex / itemsPerPage) * itemsPerPage;
    
    for (int i = 0; i < itemsPerPage && (startIndex + i) < NUM_SATELLITES; i++) {
        int index = startIndex + i;
        if (index == satSelectedIndex) {
            canvas->fillRect(5, yPos - 2, width - 10, 18, canvas->color565(0, 120, 255));
            canvas->setTextColor(TFT_WHITE);
        } else {
            canvas->setTextColor(TFT_LIGHTGRAY);
        }
        
        String checkBox = g_satellites[index].selected ? "[x] " : "[ ] ";
        String disp = checkBox + g_satellites[index].name;
        canvas->drawString(disp.c_str(), 10, yPos);
        
        yPos += 22;
    }
    
    canvas->setTextColor(TFT_LIGHTGRAY);
    canvas->drawString("[^/v] Sel [Enter] Toggle [ESC] Exit", 5, height - 15);
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
            else if (isManualLocationMode && M5Cardputer.Keyboard.isKeyPressed(';')) currentKey = ';';
            else if (isManualLocationMode && M5Cardputer.Keyboard.isKeyPressed('.')) currentKey = '.';
            
            auto handleContinuousKey = [&](char key) {
                if (isManualLocationMode) {
                    if (key == ';') { baseUserLat += 1.0; if (baseUserLat > 90) baseUserLat = 90; }
                    else if (key == '.') { baseUserLat -= 1.0; if (baseUserLat < -90) baseUserLat = -90; }
                    else if (key == ',') { baseUserLon -= 1.0; if (baseUserLon < -180) baseUserLon += 360; }
                    else if (key == '/') { baseUserLon += 1.0; if (baseUserLon > 180) baseUserLon -= 360; }
                } else {
                    if (key == ',') current_unix -= 60;
                    else if (key == '/') current_unix += 60;
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
                } else if (M5Cardputer.Keyboard.isKeyPressed('w')) {
                    appState = STATE_WIFI_SETUP;
                    wifiIsScanning = true;
                    wifiIsInputtingPassword = false;
                } else if (M5Cardputer.Keyboard.isKeyPressed('s')) {
                    appState = STATE_SAT_SELECT;
                }

            } else if (appState == STATE_WIFI_SETUP) {
                if (M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE) || M5Cardputer.Keyboard.isKeyPressed(27) || M5Cardputer.Keyboard.isKeyPressed('`')) {
                    if (wifiIsInputtingPassword) {
                        wifiIsInputtingPassword = false;
                    } else {
                        appState = STATE_MAIN;
                    }
                } else if (wifiIsInputtingPassword) {
                    if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
                        // Connect
                        appState = STATE_MAIN;
                        NetworkParams* params = new NetworkParams();
                        params->ssid = wifiNetworks[wifiSelectedIndex].ssid;
                        params->pass = String(wifiPasswordBuffer);
                        params->shouldSave = true;
                        
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
                if (M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE) || M5Cardputer.Keyboard.isKeyPressed(27) || M5Cardputer.Keyboard.isKeyPressed('`')) {
                    appState = STATE_MAIN;
                    // Trigger predictor to rerun since selection might have changed
                    portENTER_CRITICAL(&passMutex);
                    predictionsReady = false;
                    portEXIT_CRITICAL(&passMutex);
                    triggerPrediction = true;
                } else if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
                    g_satellites[satSelectedIndex].selected = !g_satellites[satSelectedIndex].selected;
                } else if (M5Cardputer.Keyboard.isKeyPressed(';')) { // UP arrow
                    if (satSelectedIndex > 0) satSelectedIndex--;
                    else satSelectedIndex = NUM_SATELLITES - 1;
                } else if (M5Cardputer.Keyboard.isKeyPressed('.')) { // DOWN arrow
                    satSelectedIndex = (satSelectedIndex + 1) % NUM_SATELLITES;
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
        static unsigned long gnssStartTime = millis();
        if (gnss && !gnss->isInStandbyMode()) {
            if (gnss->getStatus() == GNSS_STATUS_LOCKED) {
                Serial.println("GNSS Locked. Entering standby mode to save power.");
                gnss->enterStandbyMode();
            } else if (millis() - gnssStartTime > 60000) {
                Serial.println("GNSS Timeout (1 min). Entering standby mode to save power.");
                gnss->enterStandbyMode();
            }
        }
        
        // Default to looking at equator and prime meridian if no IMU
        double viewLat = 0.0;
        double viewLon = 0.0;
        
        if (isManualLocationMode) {
            viewLat = baseUserLat;
            viewLon = baseUserLon;
            earth_renderer->setCameraAttitude(0, 0, 0);
        } else if (attitude && imu) {
            AttitudeData att = attitude->getAttitude();
            
            // "平放设备时的观察视角改成在定位点上方" -> 默认对准用户的纬度
            viewLat = baseUserLat - att.pitch; 
            
            // "观察视角应该和定位在同一侧" -> 默认对准用户的经度
            // 左右转动反向
            viewLon = baseUserLon + att.roll; 
            
            // Disable camera Z-rotation because user wants to see the sides, not spin the screen.
            earth_renderer->setCameraAttitude(0, 0, 0);
        }

        // Update Sun Position
        if (sun_calc) {
            SunPositionData sunPos = sun_calc->calculatePosition(current_unix, viewLat, viewLon);
            earth_renderer->setSunPosition(sunPos.subsolarLat, sunPos.subsolarLon);
        }

        std::vector<SatRenderData> sats;

        for (int i = 0; i < NUM_SATELLITES; i++) {
            if (!g_satellites[i].selected) continue;
            
            double tx, ty, tz;
            if (g_satellites[i].calc.getTEME(current_unix, tx, ty, tz)) {
                double gmst = CoordTransform::getGMST(CoordTransform::unixToJulian(current_unix));
                ECEFCoord ecef = CoordTransform::temeToECEF(tx, ty, tz, gmst);
                
                SatRenderData data;
                data.name = g_satellites[i].name;
                data.currentPos = CoordTransform::ecefToGeodetic(ecef);
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
        earth_renderer->render(viewLat, viewLon, renderUserLat, baseUserLon, sats);
        
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
                // Show top 3 recommendations to leave room for WiFi/GNSS status
                for (int i = 0; i < recommendedPasses.size() && i < 3; i++) {
                    const auto& p = recommendedPasses[i];
                    String stars = "";
                    for(int s=0;s<p.score;s++) stars += "*";
                    
                    String line = String(p.satName.c_str()) + " " + stars;
                    earth_renderer->getCanvas()->setTextColor(TFT_GREEN);
                    earth_renderer->getCanvas()->drawString(line.c_str(), 5, y);
                    
                    // Convert seconds to relative hours/mins
                    int timeDiff = p.aosTime - current_unix; // Use actual current time for diff
                    if (timeDiff < 0) timeDiff = 0;
                    int hoursAway = timeDiff / 3600;
                    int minsAway = (timeDiff % 3600) / 60;
                    
                    String info = "In " + String(hoursAway) + "h " + String(minsAway) + "m, El: " + String((int)p.maxElevation);
                    earth_renderer->getCanvas()->setTextColor(TFT_LIGHTGRAY);
                    earth_renderer->getCanvas()->drawString(info.c_str(), 5, y+10);
                    y += 26;
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
                    earth_renderer->getCanvas()->setTextColor(TFT_LIGHTGRAY);
                    earth_renderer->getCanvas()->drawString("GNSS: OFF", 70, 115);
                } else {
                    earth_renderer->getCanvas()->setTextColor(TFT_YELLOW);
                    earth_renderer->getCanvas()->drawString("GNSS: SCH", 70, 115);
                }
            }
        }
        
        // Draw Time Machine at bottom right
        if (appState == STATE_MAIN) {
            char timeStr[32];
            int tzOffset = (int)round(baseUserLon / 15.0);
            time_t local_t = current_unix + tzOffset * 3600;
            struct tm *ptm = gmtime(&local_t);
            snprintf(timeStr, sizeof(timeStr), "%02d-%02d %02d:%02d", ptm->tm_mon+1, ptm->tm_mday, ptm->tm_hour, ptm->tm_min);
            
            earth_renderer->getCanvas()->setTextSize(1);
            earth_renderer->getCanvas()->setTextColor(TFT_WHITE);
            int textWidth = earth_renderer->getCanvas()->textWidth(timeStr);
            earth_renderer->getCanvas()->drawString(timeStr, 238 - textWidth, 125);
        }
        
        earth_renderer->getCanvas()->pushSprite(0, 0);
    }
}