#include "celestial_core.h"
#include "log_manager.h"
#include <math.h>

/**
 * @brief 构造函数
 * @param sunCalculator 太阳计算器
 * @param moonCalculator 月亮计算器
 * @param positionManager 位置管理器
 */
CelestialCore::CelestialCore(SunCalculator* sunCalculator, MoonCalculator* moonCalculator, PositionManager* positionManager) 
    : _sunCalculator(sunCalculator), _moonCalculator(moonCalculator),
      _positionManager(positionManager) {
}

/**
 * @brief 获取天体向量
 * @param time_utc UTC时间戳（秒）
 * @param lat 纬度（度）
 * @param lon 经度（度）
 * @param type 天体类型
 * @return 天体向量
 */
CelestialVector CelestialCore::getCelestialVector(uint32_t time_utc, double lat, double lon, CelestialBodyType type) {
    CelestialVector vector;
    
    switch (type) {
        case CELESTIAL_SUN: {
            SunPositionData sunPos = _sunCalculator->calculatePosition(time_utc, lat, lon);
            vector.azimuth = sunPos.azimuth * DEG_TO_RAD;
            vector.altitude = sunPos.altitude * DEG_TO_RAD;
            break;
        }
        case CELESTIAL_MOON: {
            MoonPositionData moonPos = _moonCalculator->calculatePosition(time_utc, lat, lon);
            vector.azimuth = moonPos.azimuth * DEG_TO_RAD;
            vector.altitude = moonPos.altitude * DEG_TO_RAD;
            break;
        }
        case CELESTIAL_GALAXY: {
            return calculateGalaxyCenter(time_utc, lat, lon);
        }
    }
    
    // 转换为3D单位向量
    azAltToVector(vector.azimuth, vector.altitude, vector.x, vector.y, vector.z);
    
    return vector;
}

/**
 * @brief 计算月相
 * @param sunVector 太阳向量
 * @param moonVector 月亮向量
 * @return 月相照明度（0-1）
 */
double CelestialCore::calculateMoonPhase(const CelestialVector& sunVector, const CelestialVector& moonVector) {
    // 计算日月夹角
    double dotProduct = sunVector.x * moonVector.x + sunVector.y * moonVector.y + sunVector.z * moonVector.z;
    double phaseAngle = acos(dotProduct);
    
    // 计算照明度
    double illumination = (1.0 - cos(phaseAngle)) / 2.0;
    
    return illumination;
}

/**
 * @brief 计算银河中心位置
 * @param time_utc UTC时间戳（秒）
 * @param lat 纬度（度）
 * @param lon 经度（度）
 * @return 银河中心向量
 */
CelestialVector CelestialCore::calculateGalaxyCenter(uint32_t time_utc, double lat, double lon) {
    CelestialVector vector;
    
    // 判断是否输出日志
    bool shouldLog = LogManager::getInstance().shouldLogGalaxy();
    if (shouldLog) {
    }
    
    // 银河中心（人马座A*）的赤道坐标（J2000）
    // RA: 17h45m40.04s = 266.4168度
    // Dec: -29°00′28.1″ = -29.0078度
    double ra_galaxy = 266.4168; // 度
    double dec_galaxy = -29.0078; // 度
    
    // Unix时间戳是从1970年1月1日开始的
    // 使用 PositionManager 统一转换时间戳为时间数据
    TimeData timeData = _positionManager->getTimeData(time_utc);
    uint16_t year = timeData.year;
    uint8_t month = timeData.month;
    uint8_t day = timeData.day;
    uint8_t hours = timeData.hour;
    uint8_t minutes = timeData.minute;
    uint8_t seconds = timeData.second;
    
    // 计算儒略日
    double julianDay = 0;
    uint16_t fullYear = year + 2000;
    uint8_t m = month;
    if (m <= 2) {
        fullYear--;
        m += 12;
    }
    int a = fullYear / 100;
    int b = 2 - a + a / 4;
    julianDay = floor(365.25 * (fullYear + 4716)) + floor(30.6001 * (m + 1)) + day + b - 1524.5;
    double timeOfDay = (hours + minutes / 60.0 + seconds / 3600.0) / 24.0;
    julianDay += timeOfDay;
    
    // 计算儒略世纪数
    double jc = (julianDay - 2451545.0) / 36525.0;
    
    CelestialVector res = raDecToHorizontal(ra_galaxy, dec_galaxy, time_utc, lat, lon);
    
    // 只有在需要时才输出详细计算链条
    if (shouldLog) {
        // 注释掉详尽的串口输出，防止长按快进时造成串口阻塞而导致的系统卡顿
        double lst = fmod(280.46061837 + 360.98564736629 * (julianDay - 2451545.0) + 0.000387933 * jc * jc + lon, 360.0);
        if (lst < 0) lst += 360.0;
        Serial.println("[CelestialCore] ====== 银河中心计算链条 ======");
        Serial.print("[CelestialCore] Date (UTC): ");
        Serial.print(year + 2000);
        Serial.print("/"); Serial.print(month); Serial.print("/"); Serial.print(day);
        Serial.print(" "); Serial.print(hours); Serial.print(":"); Serial.print(minutes);
        Serial.print(":"); Serial.println(seconds);
        Serial.print("[CelestialCore] Julian Day: "); Serial.println(julianDay, 4);
        Serial.print("[CelestialCore] LST: "); Serial.print(lst, 2); Serial.println("°");
        Serial.print("[CelestialCore] Azimuth: "); Serial.print(res.azimuth * RAD_TO_DEG, 2);
        Serial.print("°, Altitude: "); Serial.println(res.altitude * RAD_TO_DEG, 2);
        Serial.println("[CelestialCore] ==========================");
    }
    
    return res;
}

/**
 * @brief 将赤道坐标转换为地平坐标
 */
CelestialVector CelestialCore::raDecToHorizontal(double ra, double dec, uint32_t time_utc, double lat, double lon) {
    CelestialVector vector;
    
    double lst = calculateLST(time_utc, lon);
    return raDecToHorizontal(ra, dec, lst, lat);
}

float CelestialCore::calculateTideValue(uint32_t time_utc, double lat, double lon) {
    // 获取太阳和月亮的实时地平坐标
    CelestialVector sun = getCelestialVector(time_utc, lat, lon, CELESTIAL_SUN);
    CelestialVector moon = getCelestialVector(time_utc, lat, lon, CELESTIAL_MOON);
    
    // 计算天顶角 (Z = 90° - Altitude)
    double sunZenith = 90.0 - (sun.altitude * RAD_TO_DEG);
    double moonZenith = 90.0 - (moon.altitude * RAD_TO_DEG);
    
    // 简化物理模型：tide = A_moon * cos²(Z_moon) + A_sun * cos²(Z_sun)
    // 系数 2.2 和 1.0 反映了月球引潮力约是太阳的 2.2 倍
    float moonContrib = 2.2f * pow(cos(moonZenith * DEG_TO_RAD), 2);
    float sunContrib = 1.0f * pow(cos(sunZenith * DEG_TO_RAD), 2);
    
    return moonContrib + sunContrib;
}

double CelestialCore::calculateLST(uint32_t time_utc, double lon) {
    TimeData timeData = _positionManager->getTimeData(time_utc);
    uint16_t year = timeData.year;
    uint8_t month = timeData.month;
    uint8_t day = timeData.day;
    uint8_t hours = timeData.hour;
    uint8_t minutes = timeData.minute;
    uint8_t seconds = timeData.second;
    
    // 计算儒略日
    double julianDay = 0;
    uint16_t fullYear = year + 2000;
    uint8_t m = month;
    if (m <= 2) {
        fullYear--;
        m += 12;
    }
    int a = fullYear / 100;
    int b = 2 - a + a / 4;
    julianDay = floor(365.25 * (fullYear + 4716)) + floor(30.6001 * (m + 1)) + day + b - 1524.5;
    double timeOfDay = (hours + minutes / 60.0 + seconds / 3600.0) / 24.0;
    julianDay += timeOfDay;
    
    // 计算儒略世纪数
    double jc = (julianDay - 2451545.0) / 36525.0;
    
    // 计算格林威治恒星时
    double theta0 = 280.46061837 + 360.98564736629 * (julianDay - 2451545.0) + 0.000387933 * jc * jc - jc * jc * jc / 38710000.0;
    theta0 = fmod(theta0, 360.0);
    
    // 计算本地恒星时（度）
    double lst = theta0 + lon;
    lst = fmod(lst, 360.0);
    if (lst < 0) lst += 360.0;
    return lst;
}

CelestialVector CelestialCore::raDecToHorizontal(double ra, double dec, double lst, double lat) {
    CelestialVector vector;
    
    // 计算时角（度）：H = LST - RA
    double hourAngle = lst - ra;
    
    // 调整时角到-180°到+180°范围
    hourAngle = fmod(hourAngle, 360.0);
    if (hourAngle > 180.0) {
        hourAngle -= 360.0;
    } else if (hourAngle < -180.0) {
        hourAngle += 360.0;
    }
    
    // 将角度转换为弧度
    double hourAngleRad = hourAngle * DEG_TO_RAD;
    double decRad = dec * DEG_TO_RAD;
    double latRad = lat * DEG_TO_RAD;
    
    // 计算高度角（弧度）
    double sinAlt = sin(latRad) * sin(decRad) + cos(latRad) * cos(decRad) * cos(hourAngleRad);
    double altitudeRad = asin(sinAlt);
    
    // 计算方位角（弧度）：Az = atan2(-sin(H), tan(dec)*cos(lat) - sin(lat)*cos(H))
    double y = -sin(hourAngleRad); 
    double x = tan(decRad) * cos(latRad) - sin(latRad) * cos(hourAngleRad);
    double azimuthRad = atan2(y, x);
    
    if (azimuthRad < 0) azimuthRad += 2 * M_PI;
    
    vector.azimuth = azimuthRad;
    vector.altitude = altitudeRad;
    
    // 转换为3D单位向量
    azAltToVector(vector.azimuth, vector.altitude, vector.x, vector.y, vector.z);
    
    return vector;
}

/**
 * @brief 获取银道面指定点的地平位置
 * @param galacticLon 银经 (l)
 * @param galacticLat 银纬 (b)
 */
CelestialVector CelestialCore::getGalacticPoint(double galacticLon, double galacticLat, uint32_t time_utc, double lat, double lon) {
    CelestialEquatorial eq = getGalacticEquatorial(galacticLon, galacticLat);
    return raDecToHorizontal(eq.ra, eq.dec, time_utc, lat, lon);
}

CelestialEquatorial CelestialCore::getGalacticEquatorial(double galacticLon, double galacticLat) {
    // J2000 银道面参数
    const double ra_pole = 192.85948; // 北银极赤经
    const double dec_pole = 27.12825; // 北银极赤纬
    const double lon_node = 32.93192; // 升交点银经
    
    double l_rad = (galacticLon - lon_node) * DEG_TO_RAD;
    double b_rad = galacticLat * DEG_TO_RAD;
    double dec_p_rad = dec_pole * DEG_TO_RAD;
    double ra_p_rad = ra_pole * DEG_TO_RAD;
    
    // Galactic (l, b) -> Equatorial (RA, Dec) 标准二级坐标转换公式
    double sin_dec = sin(dec_p_rad) * sin(b_rad) + cos(dec_p_rad) * cos(b_rad) * sin(l_rad);
    double dec_rad = asin(sin_dec);
    
    double y = cos(b_rad) * cos(l_rad);
    double x = cos(dec_p_rad) * sin(b_rad) - sin(dec_p_rad) * cos(b_rad) * sin(l_rad);
    
    double ra_delta_rad = atan2(y, x);
    double ra_rad = ra_delta_rad + ra_p_rad;
    
    CelestialEquatorial eq;
    eq.ra = ra_rad * RAD_TO_DEG;
    eq.dec = dec_rad * RAD_TO_DEG;
    
    return eq;
}


/**
 * @brief 将方位角和高度角转换为3D单位向量
 * @param azimuth 方位角（弧度）
 * @param altitude 高度角（弧度）
 * @param x 输出X分量
 * @param y 输出Y分量
 * @param z 输出Z分量
 */
void CelestialCore::azAltToVector(double azimuth, double altitude, double& x, double& y, double& z) {
    double cosAlt = cos(altitude);
    // 移除方位角取反，使用标准坐标系
    x = cosAlt * sin(azimuth);
    y = cosAlt * cos(azimuth); // Y 轴指向北
    z = sin(altitude);
}
