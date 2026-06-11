#ifndef HAL_KEYBOARD_H
#define HAL_KEYBOARD_H

#include <Arduino.h>

/**
 * @brief 键盘按键枚举
 */
enum Key {
    KEY_NONE = 0,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_OK,
    KEY_BACK,
    KEY_1,
    KEY_2,
    KEY_3,
    KEY_4,
    KEY_5,
    KEY_6,
    KEY_7,
    KEY_8,
    KEY_9,
    KEY_0,
    KEY_A,
    KEY_B,
    KEY_C,
    KEY_D,
    KEY_E,
    KEY_F,
    KEY_G,
    KEY_H,
    KEY_I,
    KEY_J,
    KEY_K,
    KEY_L,
    KEY_M,
    KEY_N,
    KEY_O,
    KEY_P,
    KEY_Q,
    KEY_R,
    KEY_S,
    KEY_T,
    KEY_U,
    KEY_V,
    KEY_W,
    KEY_X,
    KEY_Y,
    KEY_Z,
    KEY_SPACE,
    KEY_ENTER_KEY,
    KEY_DELETE,
    KEY_ESC,
    KEY_COMMA,
    KEY_SLASH,
    KEY_PERIOD,
    KEY_TAB_KEY
};

/**
 * @brief 键盘模块状态枚举
 */
enum KeyboardStatus {
    KEYBOARD_STATUS_IDLE,      // 空闲状态
    KEYBOARD_STATUS_READY,     // 就绪状态
    KEYBOARD_STATUS_ERROR      // 错误状态
};

/**
 * @brief 键盘模块抽象类
 */
class HalKeyboard {
public:
    /**
     * @brief 初始化键盘模块
     * @return 初始化是否成功
     */
    virtual bool begin() = 0;

    /**
     * @brief 检查是否有按键按下
     * @return 是否有按键按下
     */
    virtual bool available() = 0;

    /**
     * @brief 获取按下的按键
     * @return 按键枚举
     */
    virtual Key getKey() = 0;

    /**
     * @brief 获取按下的按键的ASCII值
     * @return ASCII值
     */
    virtual char getChar() = 0;

    /**
     * @brief 获取当前键盘状态
     * @return 键盘状态枚举
     */
    virtual KeyboardStatus getStatus() = 0;

    /**
     * @brief 开启键盘模块
     */
    virtual void enable() = 0;

    /**
     * @brief 关闭键盘模块
     */
    virtual void disable() = 0;
};

#endif // HAL_KEYBOARD_H
