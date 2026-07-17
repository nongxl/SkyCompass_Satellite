#pragma once
#include <Arduino.h>
#include "earth_renderer.h" // 引入 SatIconType 的定义
#include "recent_launch_item.h" // 包含 SatelliteType 等类型

enum class Category {
    HUMAN_SPACEFLIGHT,
    ASTRONOMY,
    EARTH_OBSERVATION,
    NAVIGATION,
    WEATHER,
    COMMUNICATIONS,
    ROCKET_BODY,
    HISTORIC_EVENT,
    UNKNOWN
};

enum SatFlags {
    FLAG_VISIBLE     = 1 << 0,
    FLAG_CREWED      = 1 << 1,
    FLAG_HISTORIC    = 1 << 2,
    FLAG_ROCKET_BODY = 1 << 3,
    FLAG_DEBRIS      = 1 << 4,
    FLAG_WEATHER     = 1 << 5,
    FLAG_RADIO       = 1 << 6,
    FLAG_NAVIGATION  = 1 << 7,
    FLAG_SCIENCE     = 1 << 8,
    FLAG_EARTH_OBS   = 1 << 9
};

struct EncyclopediaEntry {
    uint32_t norad;
    const char* name;
    Category category;
    SatIconType icon;
    const char* description_zh;
    const char* description_en;
    uint32_t flags;
    uint16_t color;
    int baseScore;
    double stdMag;
    SatelliteType type;
    const char* downlinkFreq;
    const char* radioMode;
    const char* uplinkFreq;
    const char* tone;
    bool defaultSelected; // 是否在首次启动时默认勾选该卫星
};

class Encyclopedia {
public:
    static const EncyclopediaEntry* getEntries();
    static size_t getEntryCount();
    static const EncyclopediaEntry* getEntryByNorad(uint32_t norad);
    static const char* getDescription(uint32_t noradId);
    static String getCategoryName(Category category);
    static String getFlagName(uint32_t flag);
};
