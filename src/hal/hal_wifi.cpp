#include "hal_wifi.h"
#include "core/log_manager.h"
#include <time.h>

void HalWifi::begin(const char* ssid, const char* password) {
    if (ssid == nullptr || strlen(ssid) == 0) {
        LOG_I("APP", "WiFi SSID is empty, skipping WiFi connection.");
        return;
    }
    
    LOG_I("APP", "Connecting to WiFi: %s", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    
    // Wait up to 15 seconds for connection
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 30) {
        delay(500);
        log_i(".");
        retries++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        LOG_I("APP", "\nWiFi Connected!");
        log_i("IP Address: %s", WiFi.localIP().toString().c_str());
    } else {
        LOG_I("APP", "\nWiFi Connection Failed (Timeout).");
    }
}

bool HalWifi::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

bool HalWifi::syncNTPTime(long gmtOffset_sec, int daylightOffset_sec) {
    if (!isConnected()) {
        return false;
    }
    
    LOG_I("APP", "Syncing NTP time...");
    configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org", "time.nist.gov");
    
    struct tm timeinfo;
    int retries = 0;
    while (!getLocalTime(&timeinfo) && retries < 10) {
        delay(500);
        log_i(".");
        retries++;
    }
    
    if (retries < 10) {
        LOG_I("APP", "\nTime synced successfully!");
        log_i("Current time: %s", asctime(&timeinfo));
        return true;
    } else {
        LOG_I("APP", "\nFailed to sync NTP time.");
        return false;
    }
}

uint32_t HalWifi::getUnixTime() {
    time_t now;
    time(&now);
    
    // If year is before 2024, it means time is not synced yet (epoch starts at 1970)
    // 1704067200 is 2024-01-01
    if (now < 1704067200) {
        return 0;
    }
    return (uint32_t)now;
}

std::vector<WiFiNetwork> HalWifi::scanNetworks() {
    std::vector<WiFiNetwork> networks;
    LOG_I("APP", "Scanning WiFi networks...");
    
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    
    int n = WiFi.scanNetworks();
    LOG_I("APP", "Found %d networks", n);
    
    if (n > 0) {
        for (int i = 0; i < n; ++i) {
            WiFiNetwork net;
            net.ssid = WiFi.SSID(i);
            net.rssi = WiFi.RSSI(i);
            net.encryptionType = WiFi.encryptionType(i);
            networks.push_back(net);
        }
    }
    return networks;
}

void HalWifi::saveCredentials(const String& ssid, const String& password) {
    Preferences prefs;
    prefs.begin("wifi", false); // false = read/write
    prefs.putString("ssid", ssid);
    prefs.putString("pass", password);
    prefs.end();
    LOG_I("APP", "WiFi credentials saved to NVS.");
}

bool HalWifi::loadCredentials(String& outSsid, String& outPassword) {
    Preferences prefs;
    prefs.begin("wifi", true); // true = read-only
    outSsid = prefs.getString("ssid", "");
    outPassword = prefs.getString("pass", "");
    prefs.end();
    
    return outSsid.length() > 0;
}

void HalWifi::disconnect() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    LOG_I("APP", "WiFi disconnected and turned off.");
}
