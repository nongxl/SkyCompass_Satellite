#pragma once
#include <Arduino.h>
#include <vector>
#include <memory>
#include "coord_transform.h"
#include "sgp4_calc.h"
#include "tle_data.h"
#include "earth_renderer.h"

// Fast two-body f&g state cache to amortize SGP4 across frames
struct TemePropState {
    double sgp4Time = 0.0;
    float tx = 0.0f, ty = 0.0f, tz = 0.0f;
    float vx = 0.0f, vy = 0.0f, vz = 0.0f;
    float ecefX = 0.0f, ecefY = 0.0f, ecefZ = 0.0f;
    float ecefVx = 0.0f, ecefVy = 0.0f, ecefVz = 0.0f;
    bool valid = false;

    void reset() {
        valid = false;
        sgp4Time = 0.0;
    }
};

enum SatelliteType {
    SAT_TYPE_VISUAL,
    SAT_TYPE_HAM,
    SAT_TYPE_WEATHER,
    SAT_TYPE_SPACE_STATION,
    SAT_TYPE_HISTORICAL,
    SAT_TYPE_GEO_TV,
    SAT_TYPE_DEEP_SPACE
};

struct SatProfile {
    int noradId = 0;
    String name;
    uint16_t color = 0xFFFF;
    int baseScore = 0;
    double stdMag = 3.0;
    bool selected = false;
    SatIconType iconType = ICON_SATELLITE;
    const char* description = nullptr;
    String downlinkFreq;
    String radioMode;
    String uplinkFreq;
    String tone;
    TLEData tle;
    std::shared_ptr<SGP4Calc> calc;
    SatelliteType type = SAT_TYPE_VISUAL;
    char opsStatus = '+'; // SATCAT Operational Status: '+' Op, 'D' Decayed, '-' Inactive, 'P' Partial, 'X' Ext, 'B' Backup, 'S' Spare, '?' Unknown
};

struct SatRealtimeCache {
    GeodeticCoord lastGeo;
    bool lastGeoValid = false;
    bool lastInShadow = false;
    bool isVisible = false;
    bool isRadioPassing = false;
    TemePropState prop;
    OrbitCache cache;
};

enum SatSelectTab {
    TAB_ENCYCLOPEDIA = 0,
    TAB_RECENT_LAUNCH = 1
};

struct RecentLaunchRealtimeCache {
    GeodeticCoord lastGeo;
    bool lastGeoValid = false;
    bool lastInShadow = false;
    bool isVisible = false;
    bool isRadioPassing = false;
    TemePropState prop;
    OrbitCache cache;
};

struct RecentLaunchItem {
    String batchId;            // 国际标识符前 5 位 (如 "26042")
    String displayName;        // 智能提取出的星座/卫星公共名称前缀
    int satelliteCount = 0;    // 组内包含的卫星数
    bool isGroup = false;      // 是否为成组任务
    bool selected = false;     // 用户是否勾选观测
    uint32_t epoch = 0;        // TLE 历元时间戳缓存
    float inclination = 0.0f;  // 轨道倾角缓存
    float avgAlt = 0.0f;       // 平均高度缓存
    String repSatName;         // 缓存的代表卫星名称
    TLEData repTLE;            // 代表卫星 TLE 缓存，用于过境预测
    
    std::shared_ptr<SGP4Calc> calc;
    RecentLaunchRealtimeCache cache;

    // New fields for Mission Formation Visualization
    std::vector<FormationPoint> proxyFormation;
    float occupancy = 0.0f;
    float occupancyStartPhase = 0.0f;
    float occupancyEndPhase = 0.0f;
    float repAlongTrackPhase = 0.0f;
    String shortName;
    SatIconType iconType = ICON_SATELLITE;
};

struct LazyObjectItem {
    String name;
    OrbitRecord orbit;
    std::shared_ptr<SGP4Calc> calc;
    GeodeticCoord lastGeo;
    bool lastGeoValid = false;
    bool isVisible = false;
    TemePropState prop;
    OrbitCache cache;

    void release() {
        name = String();
        orbit = OrbitRecord();
        calc.reset();
        lastGeoValid = false;
        isVisible = false;
        prop.reset();
        cache.clearAndShrink();
    }
};

struct Level3ObjectList {
    static const size_t MAX_ITEMS = 5;
    LazyObjectItem items[MAX_ITEMS];
    size_t count = 0;
    
    size_t size() const { return count; }
    bool empty() const { return count == 0; }
    void clear() {
        count = 0;
        for (size_t i = 0; i < MAX_ITEMS; i++) {
            items[i].release();
        }
    }
    void shrink_to_fit() {
        for (size_t i = count; i < MAX_ITEMS; i++) {
            items[i].release();
        }
    }
    LazyObjectItem& operator[](size_t idx) { return items[idx]; }
    const LazyObjectItem& operator[](size_t idx) const { return items[idx]; }
    
    LazyObjectItem* begin() { return &items[0]; }
    const LazyObjectItem* begin() const { return &items[0]; }
    LazyObjectItem* end() { return &items[count]; }
    const LazyObjectItem* end() const { return &items[count]; }
};
