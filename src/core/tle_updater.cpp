#include "tle_updater.h"
#include "core/log_manager.h"
#include "../hal/hal_wifi.h"

void TLEUpdater::begin() {
    if (!LittleFS.begin(true)) {
        LOG_I("APP", "LittleFS Mount Failed. Formatting...");
    } else {
        LOG_I("APP", "LittleFS Mounted.");
    }
}

bool TLEUpdater::getTLE(int noradId, TLEData& outTle, uint32_t maxAgeSeconds) {
    uint32_t cacheTime = 0;
    bool hasCache = loadFromCache(noradId, outTle, cacheTime);
    
    uint32_t now = HalWifi::getUnixTime();
    
    // If no time is available from network, just use cache if we have it
    if (now == 0 && hasCache) {
        LOG_I("APP", "No time available, using cached TLE for %d", noradId);
        return true;
    }
    
    // If we have network and cache is old, or no cache exists
    if (HalWifi::isConnected()) {
        if (!hasCache || (now > 0 && (now - cacheTime) > maxAgeSeconds)) {
            LOG_I("APP", "Cache for %d is missing or old. Fetching from network...", noradId);
            TLEData newTle;
            if (fetchFromNetwork(noradId, newTle)) {
                outTle = newTle;
                saveToCache(noradId, newTle, now);
                LOG_I("APP", "Successfully fetched TLE for %d from network!", noradId);
                return true;
            } else if (hasCache) {
                LOG_I("APP", "Network fetch failed, falling back to old cache.");
                return true;
            } else {
                return false;
            }
        } else {
            LOG_I("APP", "Using fresh cached TLE for %d (age %d sec)", noradId, now - cacheTime);
        }
    }
    
    return hasCache;
}

bool TLEUpdater::loadFromCache(int noradId, TLEData& outTle, uint32_t& outTimestamp) {
    String path = "/tle_" + String(noradId) + ".txt";
    if (!LittleFS.exists(path)) return false;
    
    File file = LittleFS.open(path, "r");
    if (!file) return false;
    
    String timeStr = file.readStringUntil('\n');
    timeStr.trim();
    outTimestamp = timeStr.toInt();
    
    outTle.name = file.readStringUntil('\n');
    outTle.name.trim();
    
    outTle.line1 = file.readStringUntil('\n');
    outTle.line1.trim();
    
    outTle.line2 = file.readStringUntil('\n');
    outTle.line2.trim();
    
    file.close();
    return (outTle.line1.length() > 0 && outTle.line2.length() > 0);
}

bool TLEUpdater::saveToCache(int noradId, const TLEData& tle, uint32_t timestamp) {
    String path = "/tle_" + String(noradId) + ".txt";
    File file = LittleFS.open(path, "w");
    if (!file) return false;
    
    file.println(timestamp);
    file.println(tle.name);
    file.println(tle.line1);
    file.println(tle.line2);
    
    file.close();
    return true;
}

bool TLEUpdater::fetchFromNetwork(int noradId, TLEData& outTle) {
    if (noradId == 50463) {
        outTle = TLEManager::getJWST_TLE();
        return true;
    }
    
    WiFiClientSecure *client = new WiFiClientSecure;
    if (!client) {
        LOG_I("APP", "Failed to create WiFiClientSecure");
        return false;
    }
    client->setInsecure(); // Skip certificate verification
    
    HTTPClient http;
    String url = "https://celestrak.org/NORAD/elements/gp.php?CATNR=" + String(noradId) + "&FORMAT=tle";
    
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
        String payload = http.getString();
        
        // Split payload by newline
        int firstNL = payload.indexOf('\n');
        int secondNL = payload.indexOf('\n', firstNL + 1);
        
        if (firstNL > 0 && secondNL > firstNL) {
            outTle.name = payload.substring(0, firstNL);
            outTle.line1 = payload.substring(firstNL + 1, secondNL);
            outTle.line2 = payload.substring(secondNL + 1);
            
            outTle.name.trim();
            outTle.line1.trim();
            outTle.line2.trim();
            
            http.end();
            delete client;
            return true;
        }
    }
    
    LOG_I("APP", "Failed to fetch TLE for %d. HTTP Code: %d", noradId, httpCode);
    http.end();
    delete client;
    return false;
}
