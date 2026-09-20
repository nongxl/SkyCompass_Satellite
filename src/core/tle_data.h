#pragma once
#include "orbit_record.h"

struct TLEData {
    String name;
    String line1;
    String line2;
    int baseScore = 0;
};

struct FormationPoint {
    float AlongTrackPhase; // Along-track orbital position (0 ~ 360 degrees)
    float brightness;      // Brightness factor for animation (0.0f ~ 1.0f)
};

class TLEManager {
public:
    static TLEData getISS_TLE();
    static TLEData getTiangong_TLE();
    static TLEData getHubble_TLE();
    static TLEData getJWST_TLE();
    static TLEData getNGRST_TLE();
    static TLEData getHerschel_TLE();
    static TLEData getSO50_TLE();
    static TLEData getAO91_TLE();
    static TLEData getNORBI_TLE();
    static TLEData getFOSSASAT2E_TLE();
    static TLEData getLilacSat2_TLE();
    static TLEData getXW3_TLE();
    static TLEData getSONATE2_TLE();
    static TLEData getVladivostok1_TLE();
    static TLEData getNORBY2_TLE();
    static TLEData getUMKA1_TLE();
    
    // 统一获取出厂预置最新 TLE (覆盖全部 59 颗预置卫星)
    static bool getBuiltinTLE(uint32_t noradId, TLEData& outTle);
    
    // For Phase 3 offline testing, we need a time anchor that matches the TLE epochs.
    // Returns seconds since UNIX epoch for the mock time (June 10, 2024).
    static uint32_t getMockTimeAnchor();
};
