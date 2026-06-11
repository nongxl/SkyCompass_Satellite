#include "attitude_estimator.h"
#include <math.h>

/**
 * @brief 构造函数
 * @param imu IMU模块指针
 */
AttitudeEstimator::AttitudeEstimator(HalImu* imu) : _imu(imu), _yawOffset(0.0), _useCompass(false) {
    // 初始化姿态数据
    _attitude.roll = 0.0;
    _attitude.pitch = 0.0;
    _attitude.yaw = 0.0;
    _attitude.heading = 0.0;
    
    // 初始化滤波后的姿态数据
    _filteredAttitude.roll = 0.0;
    _filteredAttitude.pitch = 0.0;
    _filteredAttitude.yaw = 0.0;
    _filteredAttitude.heading = 0.0;
    
    // 初始化滤波系数（0.1-0.3之间，平衡响应速度和噪声抑制）
    _filterAlpha = 0.2;
    
    // 初始化UI映射层参数
    _maxRollAngle = 90.0;    // 最大滚转角90度
    _maxPitchAngle = 90.0;   // 最大俯仰角90度
    _rotationScale = 1.0;    // 旋转缩放因子1.0
    _useNonlinearScaling = false; // 禁用非线性缩放
    
    // 初始化虚拟航向参数
    _yawReference = 0.0;
    _useVirtualHeading = true; // 默认启用虚拟航向
}

/**
 * @brief 初始化姿态估计器
 * @return 初始化是否成功
 */
bool AttitudeEstimator::begin() {
    if (_imu) {
        ImuData imuData = _imu->getData();
        if (imuData.status == IMU_STATUS_READY) {
            Serial.println("[AttitudeEstimator] IMU already initialized");
            initVirtualHeading();
            return true;
        } else {
            Serial.println("[AttitudeEstimator] Initializing IMU...");
            if (_imu->begin()) {
                initVirtualHeading();
                return true;
            }
            Serial.println("[AttitudeEstimator] IMU initialization failed!");
            return false;
        }
    }
    Serial.println("[AttitudeEstimator] No IMU instance!");
    return false;
}

/**
 * @brief 更新姿态数据
 * @return 更新是否成功
 */
bool AttitudeEstimator::update() {
    if (_imu->update()) {
        ImuData imuData = _imu->getData();
        
        // 应用低通滤波器到原始IMU数据
        float rawRoll = imuData.roll;
        float rawPitch = imuData.pitch;
        float rawYaw = imuData.yaw;
        
        // 低通滤波器公式：filtered = alpha * raw + (1 - alpha) * filtered
        _filteredAttitude.roll = _filterAlpha * rawRoll + (1.0 - _filterAlpha) * _filteredAttitude.roll;
        _filteredAttitude.pitch = _filterAlpha * rawPitch + (1.0 - _filterAlpha) * _filteredAttitude.pitch;
        _filteredAttitude.yaw = _filterAlpha * rawYaw + (1.0 - _filterAlpha) * _filteredAttitude.yaw;
        
        // 更新姿态数据
        _attitude.roll = _filteredAttitude.roll;
        _attitude.pitch = _filteredAttitude.pitch;
        _attitude.yaw = _filteredAttitude.yaw;
        _attitude.heading = normalizeAngle(_attitude.yaw + _yawOffset);
        if (_attitude.heading < 0) {
            _attitude.heading += 360.0;
        }



        return true;
    }

    return false;
}

/**
 * @brief 获取当前姿态数据
 * @return 姿态数据结构体
 */
AttitudeData AttitudeEstimator::getAttitude() {
    return _attitude;
}

/**
 * @brief 校准偏航角（设置当前方向为参考方向）
 */
void AttitudeEstimator::calibrateHeading() {
    Serial.println("[Attitude] Calibrating - setting reference orientation...");
    if (_imu) {
        _imu->calibrate();
    }
}

/**
 * @brief 设置偏航角偏移量
 * @param offset 偏航角偏移量（度）
 */
void AttitudeEstimator::setYawOffset(float offset) {
    _yawOffset = offset;
}

/**
 * @brief 获取偏航角偏移量
 * @return 偏航角偏移量（度）
 */
float AttitudeEstimator::getYawOffset() {
    return _yawOffset;
}

/**
 * @brief 启用/禁用指南针
 * @param enable 是否启用指南针
 */
void AttitudeEstimator::enableCompass(bool enable) {
    _useCompass = enable;
}

/**
 * @brief 计算设备朝向与目标方向的夹角
 * @param targetAzimuth 目标方向的方位角（度）
 * @return 夹角（度），正值表示目标在右侧，负值表示目标在左侧
 */
float AttitudeEstimator::calculateDirectionError(float targetAzimuth) {
    // 计算设备朝向与目标方向的夹角
    float error = targetAzimuth - _attitude.heading;
    
    // 限制夹角在-180到180度范围内
    error = normalizeAngle(error);
    
    return error;
}

/**
 * @brief 限制角度范围在-180到180度之间
 * @param angle 角度（度）
 * @return 限制后的角度（度）
 */
float AttitudeEstimator::normalizeAngle(float angle) {
    while (angle > 180.0) {
        angle -= 360.0;
    }
    while (angle < -180.0) {
        angle += 360.0;
    }
    return angle;
}

/**
 * @brief 获取映射到UI的姿态数据（考虑角度限制和缩放）
 * @return 映射后的姿态数据结构体
 */
AttitudeData AttitudeEstimator::getMappedAttitude() {
    AttitudeData mappedAttitude = _attitude;
    
    // 应用旋转缩放
    float roll = _attitude.roll * _rotationScale;
    float pitch = _attitude.pitch * _rotationScale;
    
    // 应用非线性缩放（如果启用）
    if (_useNonlinearScaling) {
        // 使用反正切函数实现非线性缩放，小幅移动更敏感，大幅移动更平缓
        roll = atan(roll * 0.1) * 10.0;
        pitch = atan(pitch * 0.1) * 10.0;
    }
    
    // 限制最大旋转角度
    if (roll > _maxRollAngle) roll = _maxRollAngle;
    if (roll < -_maxRollAngle) roll = -_maxRollAngle;
    if (pitch > _maxPitchAngle) pitch = _maxPitchAngle;
    if (pitch < -_maxPitchAngle) pitch = -_maxPitchAngle;
    
    // 更新映射后的姿态数据
    mappedAttitude.roll = roll;
    mappedAttitude.pitch = pitch;
    // 更新航向为虚拟航向
    mappedAttitude.heading = getVirtualHeading();
    
    return mappedAttitude;
}

/**
 * @brief 设置最大旋转角度
 * @param maxRoll 最大滚转角（度）
 * @param maxPitch 最大俯仰角（度）
 */
void AttitudeEstimator::setMaxRotationAngles(float maxRoll, float maxPitch) {
    _maxRollAngle = maxRoll;
    _maxPitchAngle = maxPitch;
}

/**
 * @brief 设置旋转缩放因子
 * @param scale 缩放因子（0-1，值越大旋转越灵敏）
 */
void AttitudeEstimator::setRotationScale(float scale) {
    if (scale < 0.0) scale = 0.0;
    if (scale > 1.0) scale = 1.0;
    _rotationScale = scale;
}

/**
 * @brief 启用/禁用非线性缩放
 * @param enable 是否启用非线性缩放
 */
void AttitudeEstimator::enableNonlinearScaling(bool enable) {
    _useNonlinearScaling = enable;
}

/**
 * @brief 初始化虚拟航向参考值
 * 在系统启动或用户触发校准时调用
 */
void AttitudeEstimator::initVirtualHeading() {
    Serial.print("[DEBUG] Initializing virtual heading. Current yaw: ");
    Serial.println(_attitude.yaw);
    // 记录当前IMU Yaw作为参考值
    _yawReference = _attitude.yaw;
    Serial.print("[DEBUG] Set yawReference: ");
    Serial.println(_yawReference);
}

/**
 * @brief 启用/禁用虚拟航向
 * @param enable 是否启用虚拟航向
 */
void AttitudeEstimator::enableVirtualHeading(bool enable) {
    _useVirtualHeading = enable;
    // 如果启用虚拟航向，初始化参考值
    if (enable) {
        initVirtualHeading();
    }
}

/**
 * @brief 获取虚拟航向（相对于参考方向的旋转）
 * @return 虚拟航向（度）
 */
float AttitudeEstimator::getVirtualHeading() {
    if (_useVirtualHeading) {
        // 计算相对于参考值的Yaw
        float yawRelative = _attitude.yaw - _yawReference;
        // 归一化到-180到180度
        return normalizeAngle(yawRelative);
    }
    // 如果未启用虚拟航向，返回0
    return 0.0;
}
