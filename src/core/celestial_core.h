#ifndef CELESTIAL_CORE_H
#define CELESTIAL_CORE_H

#include "sun_calculator.h"
#include "moon_calculator.h"
#include "position_manager.h"
#include <stdint.h>

/**
 * @brief 天体坐标向量
 */
struct CelestialVector {
    double azimuth;
    double altitude;
    double x, y, z;
};

/**
 * @brief 赤道坐标数据
 */
struct CelestialEquatorial {
    double ra;
    double dec;
};

/**
 * @brief 天体类型枚举
 */
enum CelestialBodyType {
    CELESTIAL_SUN,
    CELESTIAL_MOON,
    CELESTIAL_GALAXY
};

/**
 * @brief 天体核心计算类
 */
class CelestialCore {
public:
    CelestialCore(SunCalculator* sunCalculator, MoonCalculator* moonCalculator, PositionManager* positionManager);
    
    /**
     * @brief 获取天体向量
     * @param time_utc UTC时间戳（秒）
     * @param lat 纬度（度）
     * @param lon 经度（度）
     * @param type 天体类型
     * @return 天体向量数据
     */
    CelestialVector getCelestialVector(uint32_t time_utc, double lat, double lon, CelestialBodyType type);
    
    /**
     * @brief 计算月相
     * @param sunVector 太阳向量
     * @param moonVector 月亮向量
     * @return 月相照明度（0-1）
     */
    double calculateMoonPhase(const CelestialVector& sunVector, const CelestialVector& moonVector);
    
    /**
     * @brief 计算银河中心位置
     * @param time_utc UTC时间戳（秒）
     * @param lat 纬度（度）
     * @param lon 经度（度）
     * @return 银河中心向量
     */
    CelestialVector calculateGalaxyCenter(uint32_t time_utc, double lat, double lon);

    /**
     * @brief 获取银道面指定点的地平位置
     * @param galacticLon 银经 (l)
     * @param galacticLat 银纬 (b)
     * @param time_utc UTC时间戳（秒）
     * @param lat 纬度（度）
     * @param lon 经度（度）
     * @return 该点的地平坐标向量
     */
    CelestialVector getGalacticPoint(double galacticLon, double galacticLat, uint32_t time_utc, double lat, double lon);

    /**
     * @brief 获取银道面指定点的赤道坐标 (RA/Dec)
     * @param galacticLon 银经 (l)
     * @param galacticLat 银纬 (b)
     * @return 赤道坐标
     */
    CelestialEquatorial getGalacticEquatorial(double galacticLon, double galacticLat);

    /**
     * @brief 将赤道坐标转换为地平坐标
     * @param ra 赤经（度）
     * @param dec 赤纬（度）
     * @param time_utc UTC时间戳（秒）
     * @param lat 纬度（度）
     * @param lon 经度（度）
     * @return 地平坐标向量
     */
    CelestialVector raDecToHorizontal(double ra, double dec, uint32_t time_utc, double lat, double lon);
    
    /**
     * @brief 计算本地恒星时（LST）
     * @param time_utc UTC时间戳
     * @param lon 经度
     * @return 本地恒星时（度）
     */
    double calculateLST(uint32_t time_utc, double lon);

    /**
     * @brief [轻量版] 将赤道坐标转换为地平坐标
     * @param ra 赤经（度）
     * @param dec 赤纬（度）
     * @param lst 本地恒星时（度）
     * @param lat 观察者纬度（度）
     * @return 地平坐标向量
     */
    CelestialVector raDecToHorizontal(double ra, double dec, double lst, double lat);

    /**
     * @brief 计算理论潮汐值
     * @param time_utc UTC 时间戳
     * @param lat 纬度
     * @param lon 经度
     * @return 潮汐理论值
     */
    float calculateTideValue(uint32_t time_utc, double lat, double lon);
    
private:
    SunCalculator* _sunCalculator;
    MoonCalculator* _moonCalculator;
    PositionManager* _positionManager;
    
    /**
     * @brief 将方位角和高度角转换为3D单位向量
     * @param azimuth 方位角（弧度）
     * @param altitude 高度角（弧度）
     * @param x 输出X分量
     * @param y 输出Y分量
     * @param z 输出Z分量
     */
    void azAltToVector(double azimuth, double altitude, double& x, double& y, double& z);
};

#endif // CELESTIAL_CORE_H
