#include "tle_updater.h"
#include "sgp4_calc.h"
#include "orbit_data_provider.h"
#include "core/log_manager.h"
#include "../hal/hal_wifi.h"

void TLEUpdater::begin() {
    if (!LittleFS.begin(true)) {
        LOG_I("APP", "LittleFS Mount Failed. Formatting...");
    } else {
        LOG_I("APP", "LittleFS Mounted.");
    }
}

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

bool TLEUpdater::getTLE(int noradId, TLEData& outTle, uint32_t maxAgeSeconds, WiFiClient* sharedClient) {
    uint32_t cacheTime = 0;
    bool hasCache = loadFromCache(noradId, outTle, cacheTime);
    
    uint32_t now = HalWifi::getUnixTime();
    
    // If no time is available from network, just use cache if we have it
    if (now == 0 && hasCache) {
        LOG_I("APP", "No time available, using cached TLE for %d", noradId);
        return true;
    }
    
    // If we have network and cache is old, or no cache exists, OR the TLE epoch itself is stale
    if (HalWifi::isConnected()) {
        uint32_t tleEpoch = 0;
        if (hasCache) {
            tleEpoch = parseTleEpoch(outTle.line1);
        }
        
        bool cacheIsOld = !hasCache || (now > 0 && (now - cacheTime) > maxAgeSeconds);
        bool tleIsStale = hasCache && (now > 0 && tleEpoch > 0 && (now - tleEpoch) > 3 * 24 * 3600 && (now - cacheTime) > 1 * 3600);
        
        if (cacheIsOld || tleIsStale) {
            LOG_I("APP", "Cache for %d is missing, old, or TLE epoch is stale. Fetching from network...", noradId);
            TLEData newTle;
            if (fetchFromNetwork(noradId, newTle, sharedClient)) {
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

bool TLEUpdater::fetchFromNetwork(int noradId, TLEData& outTle, WiFiClient* sharedClient) {
    if (noradId == 50463) {
        outTle = TLEManager::getJWST_TLE();
        return true;
    }
    
    OrbitRecord record;
    if (OrbitDataProvider::loadByCatalogNumber(noradId, record, true)) {
        outTle.name = record.name;
        outTle.baseScore = 0;
        SGP4Calc::buildPseudoTle(record, outTle.line1, outTle.line2);
        return true;
    }
    return false;
}
