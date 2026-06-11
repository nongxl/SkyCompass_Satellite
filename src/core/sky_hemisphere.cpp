/**
 * @file sky_hemisphere.cpp
 * @brief 天空半球数学模型和2D投影函数
 * 
 * 按照要求实现：
 * 1. 定义天空半球的数学模型
 * 2. 将(Azimuth, Altitude)转换为3D单位向量
 * 3. 将3D向量投影到2D屏幕坐标（正交投影）
 * 
 * 世界模型定义：
 * - 原点：观察者位置
 * - 坐标系：X（正东）、Y（正北）、Z（正上）
 * - 天空：单位半球（Z ≥ 0）
 * - 地平面：Z = 0
 */

#include "sky_hemisphere.h"

/**
 * @brief 将方位角和高度角转换为3D单位向量
 * 
 * 数学推导：
 * 1. 方位角Azimuth：北=0，顺时针
 * 2. 高度角Altitude：地平线=0
 * 3. 转换为球坐标系：
 *    - 方位角θ = Azimuth（转换为弧度）
 *    - 极角φ = 90° - Altitude（转换为弧度）
 * 4. 球坐标系到笛卡尔坐标系的转换：
 *    - x = sin(φ) * sin(θ)
 *    - y = sin(φ) * cos(θ)
 *    - z = cos(φ)
 * 
 * @param azimuth 方位角（度，北=0，顺时针）
 * @param altitude 高度角（度，地平线=0）
 * @return 3D单位向量
 */
Point3D SkyHemisphere::azAltToVector(float azimuth, float altitude) {
    // 将角度转换为弧度
    float azimuthRad = azimuth * DEG_TO_RAD;
    float altitudeRad = altitude * DEG_TO_RAD;
    
    // 计算球坐标系中的极角（从Z轴正方向测量）
    float polarAngle = (90.0f - altitude) * DEG_TO_RAD;
    
    // 球坐标系到笛卡尔坐标系的转换
    // 使用标准映射确保方位角与屏幕位置正确对应：
    // Az=0°(北) -> y=+1(上方), Az=90°(东) -> x=+1(右方)
    // Az=180°(南) -> y=-1(下方), Az=270°(西) -> x=-1(左方)
    float x = sin(polarAngle) * sin(azimuthRad);
    float y = sin(polarAngle) * cos(azimuthRad);
    float z = cos(polarAngle);
    
    // 返回3D单位向量
    return Point3D(x, y, z);
}

/**
 * @brief 将3D单位向量正交投影到2D屏幕坐标
 * 
 * 数学推导：
 * 正交投影是一种平行投影，其中投影线与投影平面垂直。
 * 对于天空半球，我们使用X-Y平面作为投影平面：
 * - x_screen = x * screenRadius + screenCenterX
 * - y_screen = y * screenRadius + screenCenterY
 * 
 * 注意：由于我们只考虑单位半球（Z ≥ 0），所以所有投影点都在屏幕范围内。
 * 
 * @param vector 3D单位向量
 * @param screenWidth 屏幕宽度
 * @param screenHeight 屏幕高度
 * @param screenRadius 屏幕上半球的半径
 * @param screenCenterX 屏幕中心点X坐标
 * @param screenCenterY 屏幕中心点Y坐标
 * @return 2D屏幕坐标
 */
Point2D SkyHemisphere::projectToScreen(const Point3D& vector, uint16_t screenWidth, uint16_t screenHeight, uint16_t screenRadius, int16_t screenCenterX, int16_t screenCenterY) {
    // 使用标准坐标映射：
    // x = r * sin(azimuth)  -> Az=90°(东)在右侧，Az=270°(西)在左侧
    // y = -r * cos(azimuth) -> Az=0°(北)在上方，Az=180°(南)在下方
    // 从3D向量中提取方位角信息：vector.x = sin(polar)*sin(az), vector.y = sin(polar)*cos(az)
    // 因此：x_screen = r * vector.x / sin(polar), y_screen = -r * vector.y / sin(polar)
    // 但由于 vector.x² + vector.y² + vector.z² = 1，且 sin(polar) = sqrt(vector.x² + vector.y²)
    // 我们可以直接使用 vector.x 和 vector.y 进行映射
    
    // 计算水平投影长度（sin(polarAngle)）
    float horizontalProj = sqrt(vector.x * vector.x + vector.y * vector.y);
    
    // 如果天体在天顶（horizontalProj = 0），显示在中心
    if (horizontalProj < 0.0001f) {
        return Point2D(screenCenterX, screenCenterY);
    }
    
    // 计算投影半径（考虑高度角）
    float r = screenRadius * horizontalProj;
    
    // 使用坐标映射公式
    int16_t screenX = (int16_t)(r * vector.x / horizontalProj) + screenCenterX;
    int16_t screenY = (int16_t)(-r * vector.y / horizontalProj) + screenCenterY;
    
    // 确保坐标在屏幕范围内
    if (screenX < 0) screenX = 0;
    if (screenX >= screenWidth) screenX = screenWidth - 1;
    if (screenY < 0) screenY = 0;
    if (screenY >= screenHeight) screenY = screenHeight - 1;
    
    return Point2D(screenX, screenY);
}

/**
 * @brief 直接将方位角和高度角转换为2D屏幕坐标
 * 
 * 这个函数组合了azAltToVector和projectToScreen的功能，
 * 直接将方位角和高度角转换为屏幕坐标。
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
Point2D SkyHemisphere::azAltToScreen(float azimuth, float altitude, uint16_t screenWidth, uint16_t screenHeight, uint16_t screenRadius, int16_t screenCenterX, int16_t screenCenterY) {
    // 将方位角转换为弧度
    float azimuthRad = azimuth * DEG_TO_RAD;
    
    // 计算投影半径（根据高度角调整，高度角越高越靠近中心）
    float altitudeRad = altitude * DEG_TO_RAD;
    float r = screenRadius * cos(altitudeRad);
    
    // 直接使用坐标映射公式：
    // x = r * sin(azimuth)  -> Az=90°(东)在右侧，Az=270°(西)在左侧
    // y = -r * cos(azimuth) -> Az=0°(北)在上方，Az=180°(南)在下方
    int16_t screenX = (int16_t)(r * sin(azimuthRad)) + screenCenterX;
    int16_t screenY = (int16_t)(-r * cos(azimuthRad)) + screenCenterY;
    
    // 确保坐标在屏幕范围内
    if (screenX < 0) screenX = 0;
    if (screenX >= screenWidth) screenX = screenWidth - 1;
    if (screenY < 0) screenY = 0;
    if (screenY >= screenHeight) screenY = screenHeight - 1;
    
    return Point2D(screenX, screenY);
}

/**
 * @brief 应用IMU姿态旋转到3D向量
 * 
 * 将观察者坐标系的旋转转换为世界坐标系的反向旋转，应用到天体向量上
 * 
 * 旋转顺序：先Yaw（绕Z轴），再Pitch（绕Y轴），最后Roll（绕X轴）
 * 这是因为我们需要应用反向旋转，所以顺序与IMU旋转顺序相反
 * 
 * @param vector 原始3D向量（世界坐标系）
 * @param roll 滚转角（度，绕X轴）
 * @param pitch 俯仰角（度，绕Y轴）
 * @param yaw 偏航角（度，绕Z轴）
 * @return 旋转后的3D向量
 */
Point3D SkyHemisphere::applyIMURotation(const Point3D& vector, float roll, float pitch, float yaw) {
    float rollRad = roll * DEG_TO_RAD;
    float pitchRad = pitch * DEG_TO_RAD;
    float yawRad = yaw * DEG_TO_RAD;
    
    float cosR = cos(rollRad);
    float sinR = sin(rollRad);
    float cosP = cos(pitchRad);
    float sinP = sin(pitchRad);
    float cosY = cos(yawRad);
    float sinY = sin(yawRad);
    
    // 调整旋转逻辑，使天球在上面，用户从上面往下看
    // 先绕X轴旋转（Roll）
    float rx = vector.x * cosR + vector.z * sinR;
    float rz = -vector.x * sinR + vector.z * cosR;
    // 再绕Y轴旋转（Pitch）
    float ry = vector.y * cosP + rz * sinP;
    rz = -vector.y * sinP + rz * cosP;
    // 最后绕Z轴旋转（Yaw）
    float tempX = rx * cosY - ry * sinY;
    ry = rx * sinY + ry * cosY;
    rx = tempX;
    
    return Point3D{rx, ry, rz};
}

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
Point2D SkyHemisphere::azAltToScreenWithIMU(float azimuth, float altitude, float roll, float pitch, float yaw, uint16_t screenWidth, uint16_t screenHeight, uint16_t screenRadius, int16_t screenCenterX, int16_t screenCenterY) {
    // 先将方位角和高度角转换为3D单位向量
    Point3D vector = azAltToVector(azimuth, altitude);
    
    // 应用IMU姿态旋转
    Point3D rotatedVector = applyIMURotation(vector, roll, pitch, yaw);
    
    // 再将3D向量投影到2D屏幕坐标
    return projectToScreen(rotatedVector, screenWidth, screenHeight, screenRadius, screenCenterX, screenCenterY);
}
