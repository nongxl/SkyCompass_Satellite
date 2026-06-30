#pragma once
#include <Arduino.h>
#include <vector>
#include <memory>
#include "coord_transform.h"
#include "sgp4_calc.h"
#include "tle_data.h"
#include "earth_renderer.h"

// Cache structures to avoid running heavy SGP4 math every frame
struct OrbitCache {
    uint32_t lastCalcTime = 0;
    std::vector<GeodeticCoord> past;
    std::vector<GeodeticCoord> future;
};

enum SatelliteType {
    SAT_TYPE_VISUAL,
    SAT_TYPE_HAM,
    SAT_TYPE_WEATHER,
    SAT_TYPE_SPACE_STATION,
    SAT_TYPE_HISTORICAL
};

struct SatProfile {
    int noradId;
    String name;
    uint16_t color;
    int baseScore;
    double stdMag;
    bool selected;
    SatIconType iconType;
    const char* description;
    String downlinkFreq;
    String radioMode;
    String uplinkFreq;
    String tone;
    TLEData tle;
    SGP4Calc calc;
    OrbitCache cache;
    SatelliteType type;
};

struct SatRealtimeCache {
    GeodeticCoord lastGeo;
    bool lastGeoValid = false;
    bool lastInShadow = false;
    bool isVisible = false;
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
    SGP4Calc calc;
    GeodeticCoord lastGeo;
    bool lastGeoValid = false;
    bool isVisible = false;
    OrbitCache cache;
};
