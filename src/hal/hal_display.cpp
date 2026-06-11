#include "hal_display.h"
#include <M5Cardputer.h>
#include <M5GFX.h>
#include <math.h>

/**
 * @brief M5 Cardputer ADV的显示模块实现
 */
class M5Display : public HalDisplay {
private:
    DisplayStatus _status;
    bool _enabled;
    Color _currentColor;
    Color _backgroundColor;
    LGFX_Sprite _canvas; // 离屏渲染画布

public:
    M5Display() : _status(DISPLAY_STATUS_IDLE), _enabled(false), _canvas(&M5.Lcd) {
        // 初始化颜色
        _currentColor.r = 255;
        _currentColor.g = 255;
        _currentColor.b = 255;
        _backgroundColor.r = 0;
        _backgroundColor.g = 0;
        _backgroundColor.b = 0;
    }

    /**
     * @brief 初始化显示模块
     * @return 初始化是否成功
     */
    bool begin() override {
        // M5Cardputer初始化时会自动初始化显示模块
        _enabled = true;
        _status = DISPLAY_STATUS_READY;
        
        // 初始化离屏渲染画布
        _canvas.createSprite(M5.Lcd.width(), M5.Lcd.height());
        
        // 设置默认背景颜色
        _canvas.fillScreen(BLACK);
        _canvas.pushSprite(0, 0);
        
        return true;
    }

    /**
     * @brief 清除显示内容
     */
    void clear() override {
        if (!_enabled) {
            return;
        }
        
        _canvas.fillScreen(COLOR565(_backgroundColor.r, _backgroundColor.g, _backgroundColor.b));
    }

    /**
     * @brief 更新显示内容
     */
    void update() override {
        if (!_enabled) {
            return;
        }
        
        // 将离屏渲染的内容一次性推送到屏幕上
        _canvas.pushSprite(0, 0);
    }

    /**
     * @brief 获取显示宽度
     * @return 显示宽度（像素）
     */
    uint16_t getWidth() override {
        return M5.Lcd.width();
    }

    /**
     * @brief 获取显示高度
     * @return 显示高度（像素）
     */
    uint16_t getHeight() override {
        return M5.Lcd.height();
    }

    /**
     * @brief 设置绘制颜色
     * @param color 颜色结构体
     */
    void setColor(Color color) override {
        _currentColor = color;
        _canvas.setTextColor(COLOR565(color.r, color.g, color.b));
    }

    /**
     * @brief 设置绘制颜色（RGB值）
     * @param r 红色通道（0-255）
     * @param g 绿色通道（0-255）
     * @param b 蓝色通道（0-255）
     */
    void setColor(uint8_t r, uint8_t g, uint8_t b) override {
        Color color = {r, g, b};
        setColor(color);
    }

    /**
     * @brief 设置背景颜色
     * @param color 颜色结构体
     */
    void setBackgroundColor(Color color) override {
        _backgroundColor = color;
    }

    /**
     * @brief 设置背景颜色（RGB值）
     * @param r 红色通道（0-255）
     * @param g 绿色通道（0-255）
     * @param b 蓝色通道（0-255）
     */
    void setBackgroundColor(uint8_t r, uint8_t g, uint8_t b) override {
        Color color = {r, g, b};
        setBackgroundColor(color);
    }

    /**
     * @brief 绘制像素点
     * @param x X坐标
     * @param y Y坐标
     */
    void drawPixel(int16_t x, int16_t y) override {
        if (!_enabled) {
            return;
        }
        
        _canvas.drawPixel(x, y, COLOR565(_currentColor.r, _currentColor.g, _currentColor.b));
    }

    /**
     * @brief 绘制线段
     * @param x1 起点X坐标
     * @param y1 起点Y坐标
     * @param x2 终点X坐标
     * @param y2 终点Y坐标
     */
    void drawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2) override {
        if (!_enabled) {
            return;
        }
        
        _canvas.drawLine(x1, y1, x2, y2, COLOR565(_currentColor.r, _currentColor.g, _currentColor.b));
    }

    /**
     * @brief 绘制矩形
     * @param x 左上角X坐标
     * @param y 左上角Y坐标
     * @param width 宽度
     * @param height 高度
     * @param filled 是否填充
     */
    void drawRect(int16_t x, int16_t y, uint16_t width, uint16_t height, bool filled = false) override {
        if (!_enabled) {
            return;
        }
        
        uint16_t color = COLOR565(_currentColor.r, _currentColor.g, _currentColor.b);
        
        if (filled) {
            _canvas.fillRect(x, y, width, height, color);
        } else {
            _canvas.drawRect(x, y, width, height, color);
        }
    }

    /**
     * @brief 绘制圆形
     * @param x 圆心X坐标
     * @param y 圆心Y坐标
     * @param radius 半径
     * @param filled 是否填充
     */
    void drawCircle(int16_t x, int16_t y, uint16_t radius, bool filled = false) override {
        if (!_enabled) {
            return;
        }
        
        uint16_t color = COLOR565(_currentColor.r, _currentColor.g, _currentColor.b);
        
        if (filled) {
            _canvas.fillCircle(x, y, radius, color);
        } else {
            _canvas.drawCircle(x, y, radius, color);
        }
    }

    /**
     * @brief 绘制文本
     * @param x X坐标
     * @param y Y坐标
     * @param text 文本内容
     * @param size 字体大小
     */
    void drawText(int16_t x, int16_t y, const char* text, uint8_t size = 1) override {
        if (!_enabled) {
            return;
        }
        
        _canvas.setTextSize(size);
        _canvas.setCursor(x, y);
        _canvas.print(text);
    }

    /**
     * @brief 绘制带角度的线段（用于指南针）
     * @param x 中心点X坐标
     * @param y 中心点Y坐标
     * @param radius 半径
     * @param angle 角度（度）
     * @param length 线段长度比例（0-1）
     */
    void drawCompassNeedle(int16_t x, int16_t y, uint16_t radius, float angle, float length = 1.0) override {
        if (!_enabled) {
            return;
        }
        
        // 将角度转换为弧度
        float rad = (90 - angle) * DEG_TO_RAD;
        
        // 计算线段终点坐标
        int16_t x2 = x + cos(rad) * radius * length;
        int16_t y2 = y - sin(rad) * radius * length;
        
        // 绘制线段
        drawLine(x, y, x2, y2);
    }

    /**
     * @brief 获取当前显示状态
     * @return 显示状态枚举
     */
    DisplayStatus getStatus() override {
        return _status;
    }

    /**
     * @brief 开启显示模块
     */
    void enable() override {
        _enabled = true;
        _status = DISPLAY_STATUS_READY;
    }

    /**
     * @brief 关闭显示模块
     */
    void disable() override {
        _enabled = false;
        _status = DISPLAY_STATUS_IDLE;
    }

private:
    /**
     * @brief 将RGB颜色转换为565格式
     * @param r 红色通道（0-255）
     * @param g 绿色通道（0-255）
     * @param b 蓝色通道（0-255）
     * @return 565格式颜色值
     */
    uint16_t COLOR565(uint8_t r, uint8_t g, uint8_t b) {
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3);
    }
};

// 创建全局显示实例
HalDisplay* display = new M5Display();
