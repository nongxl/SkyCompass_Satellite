/**
 * @file sky_hemisphere.h
 * @brief 天空半球数学模型和2D投影函数的头文件
 * 
 * 按照要求实现：
 * 1. 定义天空半球的数学模型
 * 2. 将(Azimuth, Altitude)转换为3D单位向量
 * 3. 将3D向量投影到2D屏幕坐标（正交投影）
 */

#ifndef SKY_HEMISPHERE_H
#define SKY_HEMISPHERE_H

#include <math.h>

// 常量定义
#ifndef DEG_TO_RAD
#define DEG_TO_RAD (M_PI / 180.0f)
#endif

#ifndef RAD_TO_DEG
#define RAD_TO_DEG (180.0f / M_PI)
#endif

// 避免与ui_manager.h中的结构体定义冲突
#ifndef POINT_STRUCTS_DEFINED
#define POINT_STRUCTS_DEFINED

/**
 * @brief 3D点结构体
 */
typedef struct Point3D {
    float x;
    float y;
    float z;
    
    // 构造函数
    Point3D() : x(0), y(0), z(0) {}
    Point3D(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
} Point3D;

/**
 * @brief 2D点结构体
 */
typedef struct Point2D {
    int16_t x;
    int16_t y;
    
    // 构造函数
    Point2D() : x(0), y(0) {}
    Point2D(int16_t x_, int16_t y_) : x(x_), y(y_) {}
} Point2D;

#endif // POINT_STRUCTS_DEFINED

/**
 * @brief 天空半球类
 * 
 * 实现天空半球的数学模型和2D投影函数
 */
class SkyHemisphere {
public:
    /**
     * @brief 将方位角和高度角转换为3D单位向量
     * 
     * @param azimuth 方位角（度，北=0，顺时针）
     * @param altitude 高度角（度，地平线=0）
     * @return 3D单位向量
     */
    static Point3D azAltToVector(float azimuth, float altitude);
    
    /**
     * @brief 将3D单位向量正交投影到2D屏幕坐标
     * 
     * @param vector 3D单位向量
     * @param screenWidth 屏幕宽度
     * @param screenHeight 屏幕高度
     * @param screenRadius 屏幕上半球的半径
     * @param screenCenterX 屏幕中心点X坐标
     * @param screenCenterY 屏幕中心点Y坐标
     * @return 2D屏幕坐标
     */
    static Point2D projectToScreen(const Point3D& vector, uint16_t screenWidth, uint16_t screenHeight, uint16_t screenRadius, int16_t screenCenterX, int16_t screenCenterY);
    
    /**
     * @brief 直接将方位角和高度角转换为2D屏幕坐标
     * 
     * @param azimuth 方位角（度，北=0，顺时针）
     * @param altitude 高度角（度，地平线=0）
     * @param screenWidth 屏幕宽度
     * @param screenHeight 屏幕高度
     * @param screenRadius 屏幕上半球的半径
     * @param screenCenterX 屏幕中心点X坐标
     * @param screenCenterY 屏幕中心点Y坐标
     * @return 2D屏幕坐标
     */
    static Point2D azAltToScreen(float azimuth, float altitude, uint16_t screenWidth, uint16_t screenHeight, uint16_t screenRadius, int16_t screenCenterX, int16_t screenCenterY);
    
    /**
     * @brief 应用IMU姿态旋转到3D向量
     * 
     * 将观察者坐标系的旋转转换为世界坐标系的反向旋转，应用到天体向量上
     * 
     * @param vector 原始3D向量（世界坐标系）
     * @param roll 滚转角（度，绕X轴）
     * @param pitch 俯仰角（度，绕Y轴）
     * @param yaw 偏航角（度，绕Z轴）
     * @return 旋转后的3D向量
     */
    static Point3D applyIMURotation(const Point3D& vector, float roll, float pitch, float yaw);
    
    /**
     * @brief 直接将方位角和高度角转换为2D屏幕坐标，并应用IMU姿态旋转
     * 
     * @param azimuth 方位角（度，北=0，顺时针）
     * @param altitude 高度角（度，地平线=0）
     * @param roll 滚转角（度，绕X轴）
     * @param pitch 俯仰角（度，绕Y轴）
     * @param yaw 偏航角（度，绕Z轴）
     * @param screenWidth 屏幕宽度
     * @param screenHeight 屏幕高度
     * @param screenRadius 屏幕上半球的半径
     * @param screenCenterX 屏幕中心点X坐标
     * @param screenCenterY 屏幕中心点Y坐标
     * @return 2D屏幕坐标
     */
    static Point2D azAltToScreenWithIMU(float azimuth, float altitude, float roll, float pitch, float yaw, uint16_t screenWidth, uint16_t screenHeight, uint16_t screenRadius, int16_t screenCenterX, int16_t screenCenterY);
};

#endif // SKY_HEMISPHERE_H
