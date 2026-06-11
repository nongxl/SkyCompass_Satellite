#ifndef TIME_MACHINE_H
#define TIME_MACHINE_H

#include <Arduino.h>
#include "core/position_manager.h"

/**
 * @brief 时间机器类
 */
class TimeMachine {
private:
    PositionManager* _positionManager; // 位置和时间管理器指针
    TimeData _targetTime;              // 目标时间
    bool _isActive;                    // 是否激活
    uint8_t _selectedField;            // 当前选中的时间字段（0:年, 1:月, 2:日, 3:时, 4:分, 5:秒）

public:
    /**
     * @brief 构造函数
     * @param positionManager 位置和时间管理器指针
     */
    TimeMachine(PositionManager* positionManager);

    /**
     * @brief 初始化时间机器
     * @return 初始化是否成功
     */
    bool begin();

    /**
     * @brief 激活时间机器
     */
    void activate();

    /**
     * @brief 停用时间机器
     */
    void deactivate();

    /**
     * @brief 检查时间机器是否激活
     * @return 是否激活
     */
    bool isActive();

    /**
     * @brief 获取目标时间
     * @return 目标时间
     */
    TimeData getTargetTime();

    /**
     * @brief 设置目标时间
     * @param time 目标时间
     */
    void setTargetTime(TimeData time);

    /**
     * @brief 增加当前选中字段的值
     * @param step 增加的步长
     */
    void incrementField(uint8_t step = 1);

    /**
     * @brief 减少当前选中字段的值
     * @param step 减少的步长
     */
    void decrementField(uint8_t step = 1);

    /**
     * @brief 选择下一个时间字段
     */
    void selectNextField();

    /**
     * @brief 选择上一个时间字段
     */
    void selectPreviousField();

    /**
     * @brief 获取当前选中的字段索引
     * @return 字段索引
     */
    uint8_t getSelectedField();

    /**
     * @brief 应用目标时间（更新到位置和时间管理器）
     */
    void applyTime();

    /**
     * @brief 重置为当前时间
     */
    void resetToCurrentTime();
    
    /**
     * @brief 调整时间
     * @param seconds 调整的秒数（正数增加，负数减少）
     */
    void adjustTime(int seconds);

private:
    /**
     * @brief 验证时间是否有效
     * @param time 时间数据
     * @return 是否有效
     */
    bool isValidTime(TimeData time);

    /**
     * @brief 获取指定月份的天数
     * @param year 年份
     * @param month 月份（1-12）
     * @return 天数
     */
    uint8_t getDaysInMonth(uint16_t year, uint8_t month);
};

#endif // TIME_MACHINE_H
