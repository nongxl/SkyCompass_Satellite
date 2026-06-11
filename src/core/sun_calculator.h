#ifndef SUN_CALCULATOR_H
#define SUN_CALCULATOR_H

#include <Arduino.h>
#include "position_manager.h"

/**
 * @brief 太阳位置数据结构体
 */
typedef struct {
    double azimuth;     // 方位角（度），从北开始顺时针计算
    double altitude;    // 高度角（度），从地平线开始向上计算
    double distance;    // 距离（天文单位）
    double ra;          // 赤经（度）
    double dec;         // 赤纬（度）
    uint32_t sunrise;   // 日出时间（UTC秒）
    uint32_t sunset;    // 日落时间（UTC秒）
    uint32_t noon;      // 正午时间（UTC秒）
} SunPositionData;

/**
 * @brief 太阳位置计算类
 */
class SunCalculator {
public:
    /**
     * @brief 构造函数
     * @param positionManager 位置和时间管理器指针
     */
    SunCalculator(PositionManager* positionManager);

    /**
     * @brief 初始化太阳位置计算器
     * @return 初始化是否成功
     */
    bool begin();

    /**
     * @brief 计算当前时间的太阳位置
     * @return 太阳位置数据结构体
     */
    SunPositionData calculateCurrentPosition();

    /**
     * @brief 计算指定时间的太阳位置
     * @param timestamp UTC时间戳（秒）
     * @param latitude 纬度（度）
     * @param longitude 经度（度）
     * @return 太阳位置数据结构体
     */
    SunPositionData calculatePosition(uint32_t timestamp, double latitude, double longitude);

    /**
     * @brief 计算指定日期的日出日落时间
     * @param year 年份
     * @param month 月份（1-12）
     * @param day 日期（1-31）
     * @param latitude 纬度（度）
     * @param longitude 经度（度）
     * @param sunrise 日出时间（UTC秒）
     * @param sunset 日落时间（UTC秒）
     * @param noon 正午时间（UTC秒）
     */
    void calculateSunriseSunset(uint16_t year, uint8_t month, uint8_t day, double latitude, double longitude, uint32_t& sunrise, uint32_t& sunset, uint32_t& noon);

private:
    PositionManager* _positionManager; // 位置和时间管理器指针
    
    // 日志控制变量
    uint32_t _lastLogTimestamp;  // 上次输出日志的时间戳
    static const uint32_t LOG_INTERVAL = 30; // 日志输出间隔（秒）
    
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

    /**
     * @brief 计算太阳的几何平均 longitude
     * @param jc 儒略世纪数
     * @return 太阳的几何平均 longitude（弧度）
     */
    double calculateMeanLongitude(double jc);

    /**
     * @brief 计算太阳的几何平均 anomaly
     * @param jc 儒略世纪数
     * @return 太阳的几何平均 anomaly（弧度）
     */
    double calculateMeanAnomaly(double jc);

    /**
     * @brief 计算太阳的偏心率
     * @param jc 儒略世纪数
     * @return 太阳的偏心率
     */
    double calculateEccentricity(double jc);

    /**
     * @brief 计算太阳的方程 of center
     * @param jc 儒略世纪数
     * @param meanAnomaly 太阳的几何平均 anomaly（弧度）
     * @return 太阳的方程 of center（弧度）
     */
    double calculateEquationOfCenter(double jc, double meanAnomaly);

    /**
     * @brief 计算太阳的真 longitude
     * @param meanLongitude 太阳的几何平均 longitude（弧度）
     * @param equationOfCenter 太阳的方程 of center（弧度）
     * @return 太阳的真 longitude（弧度）
     */
    double calculateTrueLongitude(double meanLongitude, double equationOfCenter);

    /**
     * @brief 计算太阳的真 anomaly
     * @param meanAnomaly 太阳的几何平均 anomaly（弧度）
     * @param equationOfCenter 太阳的方程 of center（弧度）
     * @return 太阳的真 anomaly（弧度）
     */
    double calculateTrueAnomaly(double meanAnomaly, double equationOfCenter);

    /**
     * @brief 计算太阳的距离
     * @param jc 儒略世纪数
     * @param trueAnomaly 太阳的真 anomaly（弧度）
     * @return 太阳的距离（天文单位）
     */
    double calculateDistance(double jc, double trueAnomaly);

    /**
     * @brief 计算太阳的视 longitude
     * @param trueLongitude 太阳的真 longitude（弧度）
     * @param jc 儒略世纪数
     * @return 太阳的视 longitude（弧度）
     */
    double calculateApparentLongitude(double trueLongitude, double jc);

    /**
     * @brief 计算黄赤交角
     * @param jc 儒略世纪数
     * @return 黄赤交角（弧度）
     */
    double calculateObliquity(double jc);

    /**
     * @brief 计算太阳的赤经
     * @param apparentLongitude 太阳的视 longitude（弧度）
     * @param obliquity 黄赤交角（弧度）
     * @return 太阳的赤经（弧度）
     */
    double calculateRightAscension(double apparentLongitude, double obliquity);

    /**
     * @brief 计算太阳的赤纬
     * @param apparentLongitude 太阳的视 longitude（弧度）
     * @param obliquity 黄赤交角（弧度）
     * @return 太阳的赤纬（弧度）
     */
    double calculateDeclination(double apparentLongitude, double obliquity);

    /**
     * @brief 计算春分点的格林威治小时角
     * @param julianDay 儒略日
     * @param jc 儒略世纪数
     * @return 春分点的格林威治小时角（弧度）
     */
    double calculateGreenwichHourAngle(double julianDay, double jc);

    /**
     * @brief 计算当地小时角
     * @param greenwichHourAngle 春分点的格林威治小时角（弧度）
     * @param rightAscension 太阳的赤经（弧度）
     * @param longitude 经度（弧度）
     * @return 当地小时角（弧度）
     */
    double calculateLocalHourAngle(double greenwichHourAngle, double rightAscension, double longitude);

    /**
     * @brief 计算太阳的高度角
     * @param latitude 纬度（弧度）
     * @param declination 太阳的赤纬（弧度）
     * @param localHourAngle 当地小时角（弧度）
     * @return 太阳的高度角（弧度）
     */
    double calculateAltitude(double latitude, double declination, double localHourAngle);

    /**
     * @brief 计算太阳的方位角
     * @param latitude 纬度（弧度）
     * @param declination 太阳的赤纬（弧度）
     * @param localHourAngle 当地小时角（弧度）
     * @param altitude 太阳的高度角（弧度）
     * @return 太阳的方位角（弧度）
     */
    double calculateAzimuth(double latitude, double declination, double localHourAngle, double altitude);
};

#endif // SUN_CALCULATOR_H
