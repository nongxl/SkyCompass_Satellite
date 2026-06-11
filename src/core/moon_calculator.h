#ifndef MOON_CALCULATOR_H
#define MOON_CALCULATOR_H

#include <Arduino.h>
#include "position_manager.h"

/**
 * @brief 月亮位置数据结构体
 */
typedef struct {
    double azimuth;     // 方位角（度），从北开始顺时针计算
    double altitude;    // 高度角（度），从地平线开始向上计算
    double distance;    // 距离（天文单位）
    double ra;          // 赤经（度）
    double dec;         // 赤纬（度）
} MoonPositionData;

/**
 * @brief 月亮位置计算类
 */
class MoonCalculator {
private:
    PositionManager* _positionManager; // 位置和时间管理器指针
    
    // 日志控制变量
    uint32_t _lastLogTimestamp;  // 上次输出日志的时间戳
    static const uint32_t LOG_INTERVAL = 5; // 日志输出间隔（秒）

public:
    /**
     * @brief 构造函数
     * @param positionManager 位置和时间管理器指针
     */
    MoonCalculator(PositionManager* positionManager);

    /**
     * @brief 初始化月亮位置计算器
     * @return 初始化是否成功
     */
    bool begin();

    /**
     * @brief 计算当前时间的月亮位置
     * @return 月亮位置数据结构体
     */
    MoonPositionData calculateCurrentPosition();

    /**
     * @brief 计算指定时间的月亮位置
     * @param timestamp UTC时间戳（秒）
     * @param latitude 纬度（度）
     * @param longitude 经度（度）
     * @return 月亮位置数据结构体
     */
    MoonPositionData calculatePosition(uint32_t timestamp, double latitude, double longitude);

private:
    /**
     * @brief 计算儒略日
     * @param year 年份
     * @param month 月份（1-12）
     * @param day 日期（1-31）
     * @param hour 小时（0-23）
     * @param minute 分钟（0-59）
     * @param second 秒（0-59）
     * @return 儒略日
     */
    double calculateJulianDay(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second);

    /**
     * @brief 计算儒略世纪数
     * @param julianDay 儒略日
     * @return 儒略世纪数
     */
    double calculateJulianCentury(double julianDay);
};

#endif // MOON_CALCULATOR_H
