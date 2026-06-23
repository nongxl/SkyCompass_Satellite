#pragma once

#include <Arduino.h>

struct TLEData {
    String name;
    String line1;
    String line2;
    int baseScore; // Additional base score for size/brightness
};

class TLEManager {
public:
    static TLEData getISS_TLE();
    static TLEData getTiangong_TLE();
    static TLEData getHubble_TLE();
    static TLEData getJWST_TLE();
    static TLEData getSO50_TLE();
    static TLEData getAO91_TLE();
    
    // For Phase 3 offline testing, we need a time anchor that matches the TLE epochs.
    // Returns seconds since UNIX epoch for the mock time (June 10, 2024).
    static uint32_t getMockTimeAnchor();
};
