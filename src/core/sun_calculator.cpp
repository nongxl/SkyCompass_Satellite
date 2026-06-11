#include "sun_calculator.h"
#include "log_manager.h"
#include <math.h>

// 定义PI常量（如果未定义）
#ifndef PI
#define PI 3.14159265358979323846
#endif

/**
 * @brief 构造函数
 * @param positionManager 位置和时间管理器指针
 */
SunCalculator::SunCalculator(PositionManager* positionManager) : 
    _positionManager(positionManager),
    _lastLogTimestamp(0) {
}

/**
 * @brief 初始化太阳位置计算器
 * @return 初始化是否成功
 */
bool SunCalculator::begin() {
    // 太阳位置计算器不需要特殊初始化
    return true;
}

/**
 * @brief 计算当前时间的太阳位置
 * @return 太阳位置数据结构体
 */
SunPositionData SunCalculator::calculateCurrentPosition() {
    // 获取当前时间戳
    uint32_t timestamp = _positionManager->getTimestamp();
    
    // 获取当前位置
    PositionData position = _positionManager->getPosition();
    double latitude = position.latitude;
    double longitude = position.longitude;
    
    // 计算太阳位置
    return calculatePosition(timestamp, latitude, longitude);
}

/**
 * @brief 计算指定时间的太阳位置
 * @param timestamp UTC时间戳（秒）
 * @param latitude 纬度（度）
 * @param longitude 经度（度）
 * @return 太阳位置数据结构体
 */
SunPositionData SunCalculator::calculatePosition(uint32_t timestamp, double latitude, double longitude) {
    SunPositionData result;
    
    // 判断是否输出日志
    bool shouldLog = LogManager::getInstance().shouldLogSun();
    if (shouldLog) {
    }
    
    // 使用 PositionManager 统一转换时间戳为时间数据
    TimeData timeData = _positionManager->getTimeData(timestamp);
    uint16_t year = timeData.year;
    uint8_t month = timeData.month;
    uint8_t day = timeData.day;
    uint8_t hours = timeData.hour;
    uint8_t minutes = timeData.minute;
    uint8_t seconds = timeData.second;
    
    // 计算儒略日（传入完整年份 2026）
    double julianDay = calculateJulianDay(year + 2000, month, day, hours, minutes, seconds);
    
    // 计算儒略世纪数
    double jc = calculateJulianCentury(julianDay);
    
    // 计算太阳的几何平均经度（度）
    double meanLongitude = fmod(280.46646 + jc * (36000.76983 + jc * 0.0003032), 360.0);
    if (meanLongitude < 0) meanLongitude += 360.0;
    
    // 计算太阳的平均近点角（度）
    double meanAnomaly = fmod(357.52911 + jc * (35999.05029 - 0.0001537 * jc), 360.0);
    if (meanAnomaly < 0) meanAnomaly += 360.0;
    
    // 计算太阳的轨道偏心率
    double eccentricity = 0.016708634 - jc * (0.000042037 + 0.0000001267 * jc);
    
    // 计算太阳的方程 of center（度）
    double equationOfCenter = sin(meanAnomaly * DEG_TO_RAD) * (1.914602 - jc * (0.004817 + 0.000014 * jc))
                            + sin(2 * meanAnomaly * DEG_TO_RAD) * (0.019993 - 0.000101 * jc)
                            + sin(3 * meanAnomaly * DEG_TO_RAD) * 0.000289;
    
    // 计算太阳的真经度（度）
    double trueLongitude = meanLongitude + equationOfCenter;
    if (trueLongitude < 0) trueLongitude += 360.0;
    else if (trueLongitude >= 360.0) trueLongitude -= 360.0;
    
    // 计算太阳的视经度（度）
    double apparentLongitude = trueLongitude - 0.00569 - 0.00478 * sin((125.04 - 1934.136 * jc) * DEG_TO_RAD);
    if (apparentLongitude < 0) apparentLongitude += 360.0;
    else if (apparentLongitude >= 360.0) apparentLongitude -= 360.0;
    
    // 计算黄赤交角（度）
    double meanObliquity = 23 + (26 + ((21.448 - jc * (46.815 + jc * (0.00059 - jc * 0.001813)))) / 60) / 60;
    double correctedObliquity = meanObliquity + 0.00256 * cos((125.04 - 1934.136 * jc) * DEG_TO_RAD);
    
    // 计算太阳的赤经（度）
    double rightAscension = atan2(cos(correctedObliquity * DEG_TO_RAD) * sin(apparentLongitude * DEG_TO_RAD),
                                  cos(apparentLongitude * DEG_TO_RAD)) * RAD_TO_DEG;
    if (rightAscension < 0) rightAscension += 360.0;
    
    // 计算太阳的赤纬（度）
    double declination = asin(sin(correctedObliquity * DEG_TO_RAD) * sin(apparentLongitude * DEG_TO_RAD)) * RAD_TO_DEG;
    
    // 计算格林威治恒星时（度）
    double greenwichMeanSiderealTime = fmod(280.46061837 + 360.98564736629 * (julianDay - 2451545.0) +
                                           0.000387933 * jc * jc - jc * jc * jc / 38710000.0, 360.0);
    if (greenwichMeanSiderealTime < 0) greenwichMeanSiderealTime += 360.0;
    
    // 计算本地恒星时（度）
    double localSiderealTime = fmod(greenwichMeanSiderealTime + longitude, 360.0);
    if (localSiderealTime < 0) localSiderealTime += 360.0;
    
    // 计算本地小时角（度）- 正确的计算公式：H = LST - RA
    double localHourAngle = localSiderealTime - rightAscension;
    
    // 调整时角到-180°到+180°范围
    localHourAngle = fmod(localHourAngle, 360.0);
    if (localHourAngle > 180.0) {
        localHourAngle -= 360.0;
    } else if (localHourAngle < -180.0) {
        localHourAngle += 360.0;
    }
    
    // 计算太阳的高度角（度）
    double altitude = asin(sin(latitude * DEG_TO_RAD) * sin(declination * DEG_TO_RAD) +
                          cos(latitude * DEG_TO_RAD) * cos(declination * DEG_TO_RAD) * cos(localHourAngle * DEG_TO_RAD)) * RAD_TO_DEG;
    
    // 计算太阳的方位角（度）
    double azimuth = calculateAzimuth(latitude * DEG_TO_RAD, declination * DEG_TO_RAD, localHourAngle * DEG_TO_RAD, altitude * DEG_TO_RAD) * RAD_TO_DEG;
    
    // 保存结果
    result.azimuth = azimuth;
    result.altitude = altitude;
    result.ra = rightAscension;
    result.dec = declination;
    
    // 只有在需要时才输出详细计算链条
    if (shouldLog) {
        Serial.println("[SunCalculator] ====== 太阳计算链条 ======");
        Serial.print("[SunCalculator] Date (UTC): ");
        Serial.print(year + 2000);
        Serial.print("/");
        Serial.print(month);
        Serial.print("/");
        Serial.print(day);
        Serial.print(" ");
        Serial.print(hours);
        Serial.print(":");
        Serial.print(minutes);
        Serial.print(":");
        Serial.println(seconds);
        
        // 增加本地时间输出
        TimeData localTime = _positionManager->getLocalTimeData(timestamp);
        Serial.print("[SunCalculator] Date (Local): ");
        Serial.print(localTime.year + 2000);
        Serial.print("/");
        Serial.print(localTime.month);
        Serial.print("/");
        Serial.print(localTime.day);
        Serial.print(" ");
        Serial.print(localTime.hour);
        Serial.print(":");
        Serial.print(localTime.minute);
        Serial.print(":");
        Serial.println(localTime.second);
        Serial.print("[SunCalculator] Julian Day: ");
        Serial.println(julianDay, 4);
        Serial.print("[SunCalculator] Julian Century: ");
        Serial.println(jc, 8);
        Serial.print("[SunCalculator] Mean Longitude: ");
        Serial.print(meanLongitude, 2);
        Serial.println("°");
        Serial.print("[SunCalculator] Mean Anomaly: ");
        Serial.print(meanAnomaly, 2);
        Serial.println("°");
        Serial.print("[SunCalculator] Apparent Longitude (λ): ");
        Serial.print(apparentLongitude, 2);
        Serial.println("°");
        Serial.print("[SunCalculator] Obliquity (ε): ");
        Serial.print(correctedObliquity, 2);
        Serial.println("°");
        Serial.print("[SunCalculator] Right Ascension (RA): ");
        Serial.print(rightAscension, 2);
        Serial.println("°");
        Serial.print("[SunCalculator] Declination (Dec): ");
        Serial.print(declination, 2);
        Serial.println("°");
        Serial.print("[SunCalculator] GMST: ");
        Serial.print(greenwichMeanSiderealTime, 2);
        Serial.println("°");
        Serial.print("[SunCalculator] LST: ");
        Serial.print(localSiderealTime, 2);
        Serial.println("°");
        Serial.print("[SunCalculator] Hour Angle (H = LST - RA): ");
        Serial.print(localHourAngle, 2);
        Serial.println("°");
        Serial.print("[SunCalculator] Latitude: ");
        Serial.print(latitude, 2);
        Serial.print("°, Declination: ");
        Serial.print(declination, 2);
        Serial.print("°, LocalHourAngle: ");
        Serial.print(localHourAngle, 2);
        Serial.print("°, Altitude: ");
        Serial.print(altitude, 2);
        Serial.print("°, Azimuth: ");
        Serial.println(azimuth, 2);
        Serial.print("[SunCalculator] Altitude: ");
        Serial.print(altitude, 2);
        Serial.println("°");
        Serial.print("[SunCalculator] Azimuth (0°=N, 90°=E, 180°=S, 270°=W): ");
        Serial.print(azimuth, 2);
        Serial.println("°");
        Serial.println("[SunCalculator] ==========================");
    }
    
    return result;
}

/**
 * @brief 计算太阳的方位角
 * @param latitude 纬度（弧度）
 * @param declination 太阳的赤纬（弧度）
 * @param localHourAngle 当地小时角（弧度）
 * @param altitude 太阳的高度角（弧度）
 * @return 太阳的方位角（弧度）
 */
double SunCalculator::calculateAzimuth(double latitude, double declination, double localHourAngle, double altitude) {
    // 确保输入参数的单位正确
    // latitude: 弧度
    // declination: 弧度
    // localHourAngle: 弧度
    // altitude: 弧度
    
    // 标准地平坐标方位角公式（北=0°，顺时针，即 N→E→S→W）
    // Az = atan2(-sin(H), tan(δ)·cos(φ) - sin(φ)·cos(H))
    // 来源：Meeus《天文算法》第13章
    // H>0 (午后，太阳偏西) → -sin(H)<0 → 方位角落在180°~360°西半侧 ✓
    // H<0 (午前，太阳偏东) → -sin(H)>0 → 方位角落在0°~180°东半侧   ✓
    double y = -sin(localHourAngle);
    double x = tan(declination) * cos(latitude) - sin(latitude) * cos(localHourAngle);
    double azimuth = atan2(y, x);
    
    // 调整方位角到0-2PI范围
    if (azimuth < 0) {
        azimuth += 2 * PI;
    }
    
    return azimuth;
}

/**
 * @brief 计算儒略日
 * @param year 年份（完整年份，如2026）
 * @param month 月份（1-12）
 * @param day 日期（1-31）
 * @param hour 小时（0-23）
 * @param minute 分钟（0-59）
 * @param second 秒（0-59）
 * @return 儒略日
 */
double SunCalculator::calculateJulianDay(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second) {
    // 使用完整年份直接计算
    uint16_t fullYear = year;
    
    // 如果月份小于等于2，将月份和年份调整为上一年
    if (month <= 2) {
        fullYear--;
        month += 12;
    }
    
    // 计算世纪年
    int a = fullYear / 100;
    
    // 计算闰年修正值
    int b = 2 - a + a / 4;
    
    // 计算儒略日
    double jd = floor(365.25 * (fullYear + 4716)) + floor(30.6001 * (month + 1)) + day + b - 1524.5;
    
    // 添加一天中的时间
    double timeOfDay = (hour + minute / 60.0 + second / 3600.0) / 24.0;
    jd += timeOfDay;
    
    return jd;
}

/**
 * @brief 计算儒略世纪数
 * @param julianDay 儒略日
 * @return 儒略世纪数
 */
double SunCalculator::calculateJulianCentury(double julianDay) {
    return (julianDay - 2451545.0) / 36525.0;
}
