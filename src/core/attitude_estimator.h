#ifndef ATTITUDE_ESTIMATOR_H
#define ATTITUDE_ESTIMATOR_H

#include <Arduino.h>
#include "hal/hal_imu.h"

/**
 * @brief 姿态数据结构体
 */
typedef struct {
    float roll;   // 滚转角（度）
    float pitch;  // 俯仰角（度）
    float yaw;    // 偏航角（度）
    float heading; // 航向角（度），从北开始顺时针计算
} AttitudeData;

/**
 * @brief 姿态估计器类
 */
class AttitudeEstimator {
private:
    HalImu* _imu;          // IMU模块指针
    AttitudeData _attitude; // 当前姿态数据
    float _yawOffset;       // 偏航角偏移量（用于校准）
    bool _useCompass;       // 是否使用指南针（如果有）
    
    // UI映射层参数
    float _maxRollAngle;    // 最大滚转角（度）
    float _maxPitchAngle;   // 最大俯仰角（度）
    float _rotationScale;   // 旋转缩放因子（0-1）
    bool _useNonlinearScaling; // 是否使用非线性缩放
    
    // 虚拟航向参数
    float _yawReference;    // 参考Yaw值（用于计算虚拟航向）
    bool _useVirtualHeading; // 是否使用虚拟航向
    
    // 低通滤波器参数
    float _filterAlpha;     // 滤波系数（0-1，值越大响应越快，噪声越大）
    AttitudeData _filteredAttitude; // 滤波后的姿态数据

public:
    /**
     * @brief 构造函数
     * @param imu IMU模块指针
     */
    AttitudeEstimator(HalImu* imu);

    /**
     * @brief 初始化姿态估计器
     * @return 初始化是否成功
     */
    bool begin();

    /**
     * @brief 更新姿态数据
     * @return 更新是否成功
     */
    bool update();

    /**
     * @brief 获取当前姿态数据
     * @return 姿态数据结构体
     */
    AttitudeData getAttitude();

    /**
     * @brief 校准偏航角（设置当前方向为参考方向）
     */
    void calibrateHeading();

    /**
     * @brief 设置偏航角偏移量
     * @param offset 偏航角偏移量（度）
     */
    void setYawOffset(float offset);

    /**
     * @brief 获取偏航角偏移量
     * @return 偏航角偏移量（度）
     */
    float getYawOffset();

    /**
     * @brief 启用/禁用指南针
     * @param enable 是否启用指南针
     */
    void enableCompass(bool enable);

    /**
     * @brief 计算设备朝向与目标方向的夹角
     * @param targetAzimuth 目标方向的方位角（度）
     * @return 夹角（度），正值表示目标在右侧，负值表示目标在左侧
     */
    float calculateDirectionError(float targetAzimuth);
    
    /**
     * @brief 获取映射到UI的姿态数据（考虑角度限制和缩放）
     * @return 映射后的姿态数据结构体
     */
    AttitudeData getMappedAttitude();
    
    /**
     * @brief 设置最大旋转角度
     * @param maxRoll 最大滚转角（度）
     * @param maxPitch 最大俯仰角（度）
     */
    void setMaxRotationAngles(float maxRoll, float maxPitch);
    
    /**
     * @brief 设置旋转缩放因子
     * @param scale 缩放因子（0-1，值越大旋转越灵敏）
     */
    void setRotationScale(float scale);
    
    /**
     * @brief 启用/禁用非线性缩放
     * @param enable 是否启用非线性缩放
     */
    void enableNonlinearScaling(bool enable);
    
    /**
     * @brief 初始化虚拟航向参考值
     * 在系统启动或用户触发校准时调用
     */
    void initVirtualHeading();
    
    /**
     * @brief 启用/禁用虚拟航向
     * @param enable 是否启用虚拟航向
     */
    void enableVirtualHeading(bool enable);
    
    /**
     * @brief 获取虚拟航向（相对于参考方向的旋转）
     * @return 虚拟航向（度）
     */
    float getVirtualHeading();

private:
    /**
     * @brief 限制角度范围在-180到180度之间
     * @param angle 角度（度）
     * @return 限制后的角度（度）
     */
    float normalizeAngle(float angle);
};

#endif // ATTITUDE_ESTIMATOR_H
