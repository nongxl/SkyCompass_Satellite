#include "timezone_manager.h"
#include "core/log_manager.h"
#include <ArduinoJson.h>

#if __has_include("timezone_grid.h")
#include "timezone_grid.h"
#define HAS_TIMEZONE_GRID 1
#else
#define HAS_TIMEZONE_GRID 0
#endif

TimezoneManager::TimezoneManager() : _currentOffset(8 * 3600), _isOnlineOffsetValid(false) {
}

TimezoneManager::~TimezoneManager() {
}

void TimezoneManager::begin() {
    _preferences.begin("timezone", false);
    
    // 从NVS读取上次的时区（默认东八区：28800）
    _currentOffset = _preferences.getInt("offset", 8 * 3600);
    LOG_I("TimezoneManager", "Loaded offset from NVS: %d seconds (UTC%+d)", _currentOffset, _currentOffset / 3600);
}

void TimezoneManager::setTimezoneOffset(int offsetSeconds) {
    _currentOffset = offsetSeconds;
    _isOnlineOffsetValid = true;
    _preferences.putInt("offset", _currentOffset);
    LOG_I("TimezoneManager", "Forced offset to: %d seconds (UTC%+d)", _currentOffset, _currentOffset / 3600);
}

bool TimezoneManager::updateOnlineTimezone() {
    if (WiFi.status() != WL_CONNECTED) {
        LOG_I("TimezoneManager", "WiFi not connected. Cannot fetch online timezone.");
        return false;
    }

    LOG_I("TimezoneManager", "Fetching timezone from ip-api.com...");
    HTTPClient http;
    // 使用 fields 参数仅获取需要的数据，减少解析负担
    http.begin("http://ip-api.com/json/?fields=status,offset");
    http.setTimeout(5000);
    
    int httpCode = http.GET();
    bool success = false;
    
    if (httpCode == 200) {
        String payload = http.getString();
        
        // 解析JSON
        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, payload);
        
        if (!error) {
            const char* status = doc["status"];
            if (status && strcmp(status, "success") == 0) {
                if (doc.containsKey("offset")) {
                    int offset = doc["offset"].as<int>();
                    
                    if (_currentOffset != offset || !_isOnlineOffsetValid) {
                        _currentOffset = offset;
                        _isOnlineOffsetValid = true;
                        
                        // 保存到 NVS
                        _preferences.putInt("offset", _currentOffset);
                        LOG_I("TimezoneManager", "Online timezone updated: %d seconds (UTC%+d)", _currentOffset, _currentOffset / 3600);
                    } else {
                        LOG_I("TimezoneManager", "Online timezone unchanged.");
                    }
                    success = true;
                }
            } else {
                LOG_I("TimezoneManager", "API returned fail status.");
            }
        } else {
//             log_i("[TimezoneManager] JSON parsing failed: %s\n", error.c_str());
        }
    } else {
        LOG_I("TimezoneManager", "HTTP request failed, code: %d", httpCode);
    }
    
    http.end();
    return success;
}

int TimezoneManager::getOfflineOffset(float latitude, float longitude) {
#if HAS_TIMEZONE_GRID
    // 离线网格表维度：[180][360]
    // 行对应纬度：从 89 (索引 0) 到 -90 (索引 179)
    // 列对应经度：从 -180 (索引 0) 到 179 (索引 359)
    
    int row = 89 - (int)floor(latitude);
    int col = (int)floor(longitude) + 180;
    
    // 边界保护
    if (row < 0) row = 0;
    if (row > 179) row = 179;
    if (col < 0) col = 0;
    if (col > 359) col = 359;
    
    int8_t offsetHours = timezone_map[row][col];
    return offsetHours * 3600;
#else
    // 如果没有生成离线网格表，降级使用航海时间计算
    int8_t tz = (int8_t)round(longitude / 15.0);
    return tz * 3600;
#endif
}

int TimezoneManager::getTimezoneOffset(float latitude, float longitude) {
    // 优先级 1：如果之前成功获取过在线时区，直接使用它（最精确，包含夏令时）
    if (_isOnlineOffsetValid) {
        return _currentOffset;
    }
    
    // 优先级 2：如果有有效的定位数据，使用离线网格查询
    if (latitude != 0.0f || longitude != 0.0f) {
        return getOfflineOffset(latitude, longitude);
    }
    
    // 优先级 3：没有网络也没有定位，使用 NVS 缓存的时区
    return _currentOffset;
}
