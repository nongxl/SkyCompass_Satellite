#ifndef POSITION_MANAGER_H
#define POSITION_MANAGER_H

#include <Arduino.h>
#include "hal/hal_gnss.h"

/**
 * @brief 位置数据结构体
 */
typedef struct {
    double latitude;  // 纬度（度）
    double longitude; // 经度（度）
    double altitude;  // 高度（米）
} PositionData;

/**
 * @brief 时间数据结构体
 */
typedef struct {
    uint16_t year;   // 年份
    uint8_t month;  // 月份（1-12）
    uint8_t day;    // 日期（1-31）
    uint8_t hour;   // 小时（0-23）
    uint8_t minute; // 分钟（0-59）
    uint8_t second; // 秒（0-59）
} TimeData;

/**
 * @brief 位置和时间管理类
 */
class PositionManager {
private:
    HalGnss* _gnss;         // GNSS模块指针
    PositionData _position;  // 当前位置数据
    TimeData _time;          // 当前时间数据
    bool _useManualTime;     // 是否使用手动设置的时间
    TimeData _manualTime;    // 手动设置的时间
    bool _useManualPosition; // 是否使用手动设置的位置
    PositionData _manualPosition; // 手动设置的位置
    bool _defaultTimeLogged; // 是否已经输出过默认时间日志

public:
    /**
     * @brief 构造函数
     * @param gnss GNSS模块指针
     */
    PositionManager(HalGnss* gnss);

    /**
     * @brief 初始化位置和时间管理器
     * @return 初始化是否成功
     */
    bool begin();

    /**
     * @brief 更新位置和时间数据
     * @return 更新是否成功
     */
    bool update();

    /**
     * @brief 获取当前位置数据
     * @return 位置数据结构体
     */
    PositionData getPosition();

    /**
     * @brief 获取当前时间数据
     * @return 时间数据结构体
     */
    TimeData getTime();

    /**
     * @brief 获取当前UTC时间的时间戳（秒）
     * @return UTC时间戳
     */
    uint32_t getTimestamp();

    /**
     * @brief 设置手动时间（用于时间机器功能）
     * @param time 手动设置的时间数据
     */
    void setManualTime(TimeData time);

    /**
     * @brief 启用/禁用手动时间
     * @param enable 是否启用手动时间
     */
    void enableManualTime(bool enable);

    /**
     * @brief 设置手动位置
     * @param position 手动设置的位置数据
     */
    void setManualPosition(PositionData position);

    /**
     * @brief 启用/禁用手动位置
     * @param enable 是否启用手动位置
     */
    void enableManualPosition(bool enable);

    /**
     * @brief 检查是否有有效的位置数据
     * @return 是否有有效的位置数据
     */
    bool hasValidPosition();

    /**
     * @brief 检查是否有有效的时间数据
     * @return 是否有有效的时间数据
     */
    bool hasValidTime();
    
    /**
     * @brief 将时间戳转换为时间数据结构体
     * @param timestamp UTC时间戳（秒）
     * @return 时间数据结构体
     */
    TimeData getTimeData(uint32_t timestamp);
    
    /**
     * @brief 将时间戳转换为本地时间数据结构体（根据经度计算时区）
     * @param timestamp UTC时间戳（秒）
     * @return 本地时间数据结构体
     */
    TimeData getLocalTimeData(uint32_t timestamp);
    
    /**
     * @brief 获取默认时间数据
     * @return 默认时间数据结构体
     */
    static TimeData getDefaultTime();
    
    /**
     * @brief 获取默认位置数据
     * @return 默认位置数据结构体
     */
    static PositionData getDefaultPosition();

private:
    /**
     * @brief 从GNSS数据更新位置和时间
     * @param gnssData GNSS数据结构体
     */
    void updateFromGnss(GnssData gnssData);

    /**
     * @brief 计算时间戳
     * @param time 时间数据结构体
     * @return UTC时间戳（秒）
     */
    uint32_t calculateTimestamp(TimeData time);
};

#endif // POSITION_MANAGER_H
