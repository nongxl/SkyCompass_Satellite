#include "moon_calculator.h"
#include "log_manager.h"
#include <math.h>

/**
 * @brief 构造函数
 * @param positionManager 位置和时间管理器指针
 */
MoonCalculator::MoonCalculator(PositionManager* positionManager) : 
    _positionManager(positionManager),
    _lastLogTimestamp(0) {
}

/**
 * @brief 初始化月亮位置计算器
 * @return 初始化是否成功
 */
bool MoonCalculator::begin() {
    // 月亮位置计算器不需要特殊初始化
    return true;
}

/**
 * @brief 计算当前时间的月亮位置
 * @return 月亮位置数据结构体
 */
MoonPositionData MoonCalculator::calculateCurrentPosition() {
    // 获取当前时间戳
    uint32_t timestamp = _positionManager->getTimestamp();
    
    // 获取当前位置
    PositionData position = _positionManager->getPosition();
    double latitude = position.latitude;
    double longitude = position.longitude;
    
    // 计算月亮位置
    return calculatePosition(timestamp, latitude, longitude);
}

/**
 * @brief 计算指定时间的月亮位置
 * @param timestamp UTC时间戳（秒）
 * @param latitude 纬度（度）
 * @param longitude 经度（度）
 * @return 月亮位置数据结构体
 */
MoonPositionData MoonCalculator::calculatePosition(uint32_t timestamp, double latitude, double longitude) {
    MoonPositionData result;
    
    // 判断是否输出日志
    bool shouldLog = LogManager::getInstance().shouldLogMoon();
    if (shouldLog) {
    }
    
    // Unix时间戳是从1970年1月1日开始的
    // 使用 PositionManager 统一转换时间戳为时间数据
    TimeData timeData = _positionManager->getTimeData(timestamp);
    uint16_t year = timeData.year;
    uint8_t month = timeData.month;
    uint8_t day = timeData.day;
    uint8_t hours = timeData.hour;
    uint8_t minutes = timeData.minute;
    uint8_t seconds = timeData.second;
    
    // 计算儒略日（传入完整年份，如 2026）
    double julianDay = calculateJulianDay(year + 2000, month, day, hours, minutes, seconds);
    
    // 计算儒略世纪数
    double jc = calculateJulianCentury(julianDay);
    
    // 计算月亮的几何平均经度
    double L = fmod(218.3164477 + 481267.88123421 * jc - 0.0015786 * jc * jc + jc * jc * jc / 538841 - jc * jc * jc * jc / 65194000, 360.0);
    
    // 计算月亮的平均近点角
    double M = fmod(134.9634114 + 477198.8676313 * jc + 0.0089970 * jc * jc + jc * jc * jc / 1152000 - jc * jc * jc * jc / 14400000, 360.0);
    
    // 计算月亮的升交点经度
    double N = fmod(125.0445550 - 1934.1362619 * jc + 0.0020756 * jc * jc + jc * jc * jc / 467410 - jc * jc * jc * jc / 60616000, 360.0);
    
    // 计算月亮的纬度参数
    double i = 5.1453964;
    
    // 计算月亮的轨道偏心率
    double e = 0.0549000;
    
    // 计算月亮的平近点角（弧度）
    double M_rad = M * DEG_TO_RAD;
    
    // 计算月亮的方程 of center
    double C = (1.2739 * sin((2 * (L - N) - M) * DEG_TO_RAD)) + (0.6583 * sin(2 * (L - N) * DEG_TO_RAD)) + (0.1858 * sin(2 * M_rad)) + (0.0565 * sin(4 * (L - N) * DEG_TO_RAD)) + (0.0282 * sin(2 * (M_rad - (L - N)) * DEG_TO_RAD)) + (0.0260 * sin(2 * (M_rad + (L - N)) * DEG_TO_RAD)) + (0.0172 * sin(4 * M_rad));
    
    // 计算月亮的真经度
    double trueLongitude = L + C;
    
    // 计算月亮的真近点角
    double trueAnomaly = M + C;
    
    // 计算月亮的距离（地球半径单位）
    double distance = (385000.0 / 6378.1) * (1 - e * e) / (1 + e * cos(trueAnomaly * DEG_TO_RAD));
    result.distance = distance / 23455.0; // 转换为天文单位
    
    // 计算月亮的纬度
    double moonLatitude = asin(sin((trueLongitude - N) * DEG_TO_RAD) * sin(i * DEG_TO_RAD)) * RAD_TO_DEG;
    
    // 计算月亮的视经度
    double apparentLongitude = trueLongitude - 0.00569 - 0.00478 * sin((125.04 - 1934.136 * jc) * DEG_TO_RAD);
    
    // 计算黄赤交角，公式中的系数单位是角秒，需要转换为度
    double obliquity = 23.43929111 - jc * (46.8150 / 3600.0 + jc * (0.00059 / 3600.0 - jc * 0.001813 / 3600.0));
    
    // 计算月亮的赤经
    double sinLambda = sin(apparentLongitude * DEG_TO_RAD);
    double cosLambda = cos(apparentLongitude * DEG_TO_RAD);
    double tanAlpha = sinLambda * cos(obliquity * DEG_TO_RAD) / cosLambda;
    double rightAscension = atan(tanAlpha);
    
    // 调整赤经到正确的象限
    if (cosLambda < 0) {
        rightAscension += PI;
    } else if (sinLambda < 0) {
        rightAscension += 2 * PI;
    }
    result.ra = rightAscension * RAD_TO_DEG;
    
    // 计算月亮的赤纬（考虑黄纬）
    double moonLatitudeRad = moonLatitude * DEG_TO_RAD;
    double sinBeta = sin(moonLatitudeRad);
    double cosBeta = cos(moonLatitudeRad);
    double sinEpsilon = sin(obliquity * DEG_TO_RAD);
    double cosEpsilon = cos(obliquity * DEG_TO_RAD);
    
    double declination = asin(sinBeta * cosEpsilon + cosBeta * sinLambda * sinEpsilon);
    result.dec = declination * RAD_TO_DEG;
    
    // 计算春分点的格林威治小时角
    double theta0 = 280.46061837 + 360.98564736629 * (julianDay - 2451545.0) + 0.000387933 * jc * jc - jc * jc * jc / 38710000.0;
    double greenwichHourAngle = fmod(theta0, 360.0) * DEG_TO_RAD;
    
    // 将经度转换为弧度
    double longitudeRad = longitude * DEG_TO_RAD;
    
    // 计算本地恒星时（弧度）
    double localSiderealTime = greenwichHourAngle + longitudeRad;
    
    // 计算当地小时角 - 正确的计算公式：H = LST - RA
    double localHourAngle = localSiderealTime - rightAscension;
    
    // 调整时角到-π到+π范围
    localHourAngle = fmod(localHourAngle, 2 * PI);
    if (localHourAngle > PI) {
        localHourAngle -= 2 * PI;
    } else if (localHourAngle < -PI) {
        localHourAngle += 2 * PI;
    }
    
    // 将观测者纬度转换为弧度
    double latitudeRad = latitude * DEG_TO_RAD;
    
    // 将月亮的赤纬转换为弧度
    double declinationRad = declination; // 已是弧度（asin() 直接返回弧度）
    
    // 计算月亮的高度角
    double sinAlt = sin(latitudeRad) * sin(declinationRad) + cos(latitudeRad) * cos(declinationRad) * cos(localHourAngle);
    double altitudeRad = asin(sinAlt);
    result.altitude = altitudeRad * RAD_TO_DEG;
    
    // 计算月亮的方位角
    // 使用标准地平坐标公式（从正北开始）
    // Az = atan2(-sin(H), tan(dec)*cos(lat) - sin(lat)*cos(H))
    double y = -sin(localHourAngle);
    double x = tan(declinationRad) * cos(latitudeRad) - sin(latitudeRad) * cos(localHourAngle);
    double azimuthRad = atan2(y, x);
    
    // 调整方位角到0-2PI范围
    if (azimuthRad < 0) {
        azimuthRad += 2 * PI;
    }
    
    result.azimuth = azimuthRad * RAD_TO_DEG;
    
    // 只有在需要时才输出详细计算链条
    if (shouldLog) {
        Serial.println("[MoonCalculator] ====== 月亮计算链条 ======");
        Serial.print("[MoonCalculator] Date (UTC): ");
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
        Serial.print("[MoonCalculator] Date (Local): ");
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
        Serial.print("[MoonCalculator] Julian Day: ");
        Serial.println(julianDay, 4);
        Serial.print("[MoonCalculator] Julian Century: ");
        Serial.println(jc, 8);
        Serial.print("[MoonCalculator] Mean Longitude: ");
        Serial.print(L, 2);
        Serial.println("°");
        Serial.print("[MoonCalculator] Mean Anomaly: ");
        Serial.print(M, 2);
        Serial.println("°");
        Serial.print("[MoonCalculator] Right Ascension (RA): ");
        Serial.print(result.ra, 2);
        Serial.println("°");
        Serial.print("[MoonCalculator] Declination (Dec): ");
        Serial.print(result.dec, 2);
        Serial.println("°");
        Serial.print("[MoonCalculator] Latitude: ");
        Serial.print(latitude, 2);
        Serial.print("°, Altitude: ");
        Serial.print(result.altitude, 2);
        Serial.print("°, Azimuth: ");
        Serial.println(result.azimuth, 2);
        Serial.println("[MoonCalculator] ==========================");
    }
    
    return result;
}

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
double MoonCalculator::calculateJulianDay(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second) {
    // 简化的儒略日计算
    if (month <= 2) {
        year--;
        month += 12;
    }
    
    int a = year / 100;
    int b = 2 - a + a / 4;
    
    double jd = floor(365.25 * (year + 4716)) + floor(30.6001 * (month + 1)) + day + b - 1524.5;
    
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
double MoonCalculator::calculateJulianCentury(double julianDay) {
    return (julianDay - 2451545.0) / 36525.0;
}
