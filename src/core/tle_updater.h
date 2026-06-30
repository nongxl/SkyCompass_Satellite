#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <LittleFS.h>
#include "tle_data.h"

class TLEUpdater {
public:
    static void begin();
    
    // Attempt to fetch TLE. Uses LittleFS cache if available and fresh.
    // If cache is missing or older than maxAgeSeconds, fetches via WiFi and caches it.
    // returns true if successfully populated outTle.
    static bool getTLE(int noradId, TLEData& outTle, uint32_t maxAgeSeconds = 2 * 24 * 3600, WiFiClient* sharedClient = nullptr);
    
private:
    static bool loadFromCache(int noradId, TLEData& outTle, uint32_t& outTimestamp);
    static bool saveToCache(int noradId, const TLEData& tle, uint32_t timestamp);
    static bool fetchFromNetwork(int noradId, TLEData& outTle, WiFiClient* sharedClient = nullptr);
};
