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

// Public wrappers — allow main.cpp batch logic to call internal helpers
bool TLEUpdater::loadFromCachePublic(int noradId, TLEData& outTle, uint32_t& outTimestamp) {
    return loadFromCache(noradId, outTle, outTimestamp);
}

uint32_t TLEUpdater::parseTleEpochPublic(const String& line1) {
    return parseTleEpoch(line1);
}

bool TLEUpdater::getTLE(int noradId, TLEData& outTle, uint32_t maxAgeSeconds, WiFiClient* sharedClient, String* outError) {
    if (outError) *outError = "";
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
        
        // cacheTime == 0 means this was saved during offline boot with no valid timestamp.
        // In that case the effective age is calculated from the TLE epoch instead.
        // If even tleEpoch is unknown, treat the cache as brand-new to avoid a pointless
        // network round-trip immediately after the first offline boot.
        uint32_t effectiveCacheTime = cacheTime;
        if (effectiveCacheTime == 0) {
            effectiveCacheTime = (tleEpoch > 0) ? tleEpoch : now; // treat as just-fetched
        }
        
        bool cacheIsOld = !hasCache || (now > 0 && (now - effectiveCacheTime) > maxAgeSeconds);
        bool tleIsStale = hasCache && (now > 0 && tleEpoch > 0 && (now - tleEpoch) > 3 * 24 * 3600 && (now - effectiveCacheTime) > 1 * 3600);
        
        if (cacheIsOld || tleIsStale) {
            LOG_I("APP", "Cache for %d is missing, old, or TLE epoch is stale. Fetching from network... (cacheTime=%u, effectiveCacheTime=%u, now=%u, maxAge=%u)", noradId, cacheTime, effectiveCacheTime, now, maxAgeSeconds);
            TLEData newTle;
            if (fetchFromNetwork(noradId, newTle, sharedClient, outError)) {
                outTle = newTle;
                saveToCache(noradId, newTle, now);
                LOG_I("APP", "Successfully fetched TLE for %d from network!", noradId);
                return true;
            } else if (hasCache) {
                LOG_I("APP", "Network fetch failed, falling back to old cache. Refreshing timestamp to suppress retry.");
                // Refresh the cached timestamp to now so that this satellite is not
                // considered stale again on the very next boot.  Without this, a TLS
                // memory-allocation failure (SSL -32512) causes the device to hammer
                // CelesTrak on every boot, triggering connection refusals.
                saveToCache(noradId, outTle, now);
                return true;
            } else {
                return false;
            }
        } else {
            LOG_I("APP", "Using fresh cached TLE for %d (cacheAge %d sec, effectiveCacheTime=%u)", noradId, now - effectiveCacheTime, effectiveCacheTime);
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
    File file = LittleFS.open(path, "w", true);
    if (!file) return false;
    
    // If timestamp is 0 (offline boot), fall back to the TLE's own epoch so that
    // subsequent online sessions have a meaningful age reference and don't always
    // treat the cache as expired.
    uint32_t effectiveTimestamp = timestamp;
    if (effectiveTimestamp == 0 && tle.line1.length() >= 32) {
        effectiveTimestamp = parseTleEpoch(tle.line1);
    }
    
    file.println(effectiveTimestamp);
    file.println(tle.name);
    file.println(tle.line1);
    file.println(tle.line2);
    
    file.close();
    return true;
}

bool TLEUpdater::fetchFromNetwork(int noradId, TLEData& outTle, WiFiClient* sharedClient, String* outError) {
    if (noradId == 50463) {
        outTle = TLEManager::getJWST_TLE();
        return true;
    }
    
    OrbitRecord record;
    int httpCode = 0;
    if (OrbitDataProvider::loadByCatalogNumber(noradId, record, true, sharedClient, &httpCode)) {
        outTle.name = record.name;
        outTle.baseScore = 0;
        SGP4Calc::buildPseudoTle(record, outTle.line1, outTle.line2);
        return true;
    }
    
    if (outError) {
        if (httpCode < 0) {
            *outError = "Connection Refused";
        } else if (httpCode == 404) {
            *outError = "ID Not Found";
        } else if (httpCode == 200) {
            *outError = "No GP Data";
        } else {
            *outError = "HTTP Error " + String(httpCode);
        }
    }
    return false;
}
