#ifndef HAL_DISPLAY_H
#define HAL_DISPLAY_H

#include <Arduino.h>

/**
 * @brief 显示模块状态枚举
 */
enum DisplayStatus {
    DISPLAY_STATUS_IDLE,      // 空闲状态
    DISPLAY_STATUS_READY,     // 就绪状态
    DISPLAY_STATUS_ERROR      // 错误状态
};

/**
 * @brief 颜色结构体
 */
typedef struct {
    uint8_t r; // 红色通道（0-255）
    uint8_t g; // 绿色通道（0-255）
    uint8_t b; // 蓝色通道（0-255）
} Color;

/**
 * @brief 显示模块抽象类
 */
class HalDisplay {
public:
    /**
     * @brief 初始化显示模块
     * @return 初始化是否成功
     */
    virtual bool begin() = 0;

    /**
     * @brief 清除显示内容
     */
    virtual void clear() = 0;

    /**
     * @brief 更新显示内容
     */
    virtual void update() = 0;

    /**
     * @brief 获取显示宽度
     * @return 显示宽度（像素）
     */
    virtual uint16_t getWidth() = 0;

    /**
     * @brief 获取显示高度
     * @return 显示高度（像素）
     */
    virtual uint16_t getHeight() = 0;

    /**
     * @brief 设置绘制颜色
     * @param color 颜色结构体
     */
    virtual void setColor(Color color) = 0;

    /**
     * @brief 设置绘制颜色（RGB值）
     * @param r 红色通道（0-255）
     * @param g 绿色通道（0-255）
     * @param b 蓝色通道（0-255）
     */
    virtual void setColor(uint8_t r, uint8_t g, uint8_t b) = 0;

    /**
     * @brief 设置背景颜色
     * @param color 颜色结构体
     */
    virtual void setBackgroundColor(Color color) = 0;

    /**
     * @brief 设置背景颜色（RGB值）
     * @param r 红色通道（0-255）
     * @param g 绿色通道（0-255）
     * @param b 蓝色通道（0-255）
     */
    virtual void setBackgroundColor(uint8_t r, uint8_t g, uint8_t b) = 0;

    /**
     * @brief 绘制像素点
     * @param x X坐标
     * @param y Y坐标
     */
    virtual void drawPixel(int16_t x, int16_t y) = 0;

    /**
     * @brief 绘制线段
     * @param x1 起点X坐标
     * @param y1 起点Y坐标
     * @param x2 终点X坐标
     * @param y2 终点Y坐标
     */
    virtual void drawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2) = 0;

    /**
     * @brief 绘制矩形
     * @param x 左上角X坐标
     * @param y 左上角Y坐标
     * @param width 宽度
     * @param height 高度
     * @param filled 是否填充
     */
    virtual void drawRect(int16_t x, int16_t y, uint16_t width, uint16_t height, bool filled = false) = 0;

    /**
     * @brief 绘制圆形
     * @param x 圆心X坐标
     * @param y 圆心Y坐标
     * @param radius 半径
     * @param filled 是否填充
     */
    virtual void drawCircle(int16_t x, int16_t y, uint16_t radius, bool filled = false) = 0;

    /**
     * @brief 绘制文本
     * @param x X坐标
     * @param y Y坐标
     * @param text 文本内容
     * @param size 字体大小
     */
    virtual void drawText(int16_t x, int16_t y, const char* text, uint8_t size = 1) = 0;

    /**
     * @brief 绘制带角度的线段（用于指南针）
     * @param x 中心点X坐标
     * @param y 中心点Y坐标
     * @param radius 半径
     * @param angle 角度（度）
     * @param length 线段长度比例（0-1）
     */
    virtual void drawCompassNeedle(int16_t x, int16_t y, uint16_t radius, float angle, float length = 1.0) = 0;

    /**
     * @brief 获取当前显示状态
     * @return 显示状态枚举
     */
    virtual DisplayStatus getStatus() = 0;

    /**
     * @brief 开启显示模块
     */
    virtual void enable() = 0;

    /**
     * @brief 关闭显示模块
     */
    virtual void disable() = 0;
};

#endif // HAL_DISPLAY_H
