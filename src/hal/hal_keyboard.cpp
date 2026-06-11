#include "hal_keyboard.h"
#include <M5Cardputer.h>

/**
 * @brief M5 Cardputer ADV的键盘模块实现
 */
class M5Keyboard : public HalKeyboard {
private:
    KeyboardStatus _status;
    bool _enabled;
    Key _lastKey;

public:
    M5Keyboard() : _status(KEYBOARD_STATUS_IDLE), _enabled(false), _lastKey(KEY_NONE) {
    }

    /**
     * @brief 初始化键盘模块
     * @return 初始化是否成功
     */
    bool begin() override {
        // M5Cardputer初始化时会自动初始化键盘模块
        _enabled = true;
        _status = KEYBOARD_STATUS_READY;
        return true;
    }

    /**
     * @brief 检查是否有按键按下
     * @return 是否有按键按下
     */
    bool available() override {
        if (!_enabled) {
            return false;
        }
        
        // 更新键盘状态
        M5Cardputer.Keyboard.updateKeysState();
        
        // 使用M5Cardputer的API检测按键
        return M5Cardputer.Keyboard.isPressed();
    }

    /**
     * @brief 获取按下的按键
     * @return 按键枚举
     */
    Key getKey() override {
        if (!_enabled || !available()) {
            return KEY_NONE;
        }
        
        // 先检测特殊按键
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_TAB)) {
            return KEY_TAB_KEY;
        }
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE)) {
            return KEY_DELETE;
        }
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
            Serial.println("[Keyboard] ENTER key detected");
            return KEY_OK;
        }
        
        // 使用M5Cardputer的API检测按键
        if (M5Cardputer.Keyboard.isKeyPressed('b') || M5Cardputer.Keyboard.isKeyPressed('B')) {
            return KEY_BACK;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('0')) {
            return KEY_0;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('1')) {
            return KEY_1;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('2')) {
            return KEY_2;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('3')) {
            return KEY_3;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('4')) {
            return KEY_4;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('5')) {
            return KEY_5;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('6')) {
            return KEY_6;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('7')) {
            return KEY_7;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('8')) {
            return KEY_8;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('9')) {
            return KEY_9;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('.')) {
            return KEY_PERIOD;
        }
        if (M5Cardputer.Keyboard.isKeyPressed(',')) {
            return KEY_COMMA;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('/')) {
            return KEY_SLASH;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('c') || M5Cardputer.Keyboard.isKeyPressed('C')) {
            return KEY_C;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('s') || M5Cardputer.Keyboard.isKeyPressed('S')) {
            return KEY_S;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('f') || M5Cardputer.Keyboard.isKeyPressed('F')) {
            return KEY_F;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('w') || M5Cardputer.Keyboard.isKeyPressed('W')) {
            return KEY_UP;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('a') || M5Cardputer.Keyboard.isKeyPressed('A')) {
            return KEY_LEFT;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('d') || M5Cardputer.Keyboard.isKeyPressed('D')) {
            return KEY_RIGHT;
        }
        
        // 检测ESC键（反引号键）
        if (M5Cardputer.Keyboard.isKeyPressed('`')) {
            return KEY_ESC;
        }
        
        return KEY_NONE;
    }

    /**
     * @brief 获取按下的按键的ASCII值
     * @return ASCII值
     */
    char getChar() override {
        if (!_enabled || !available()) {
            return 0;
        }
        
        // 使用M5Cardputer的API获取按键
        auto keys = M5Cardputer.Keyboard.keyList();
        if (!keys.empty()) {
            // 获取第一个按键的字符值
            uint8_t c = M5Cardputer.Keyboard.getKey(keys[0]);
            if (c != 0) {
                return static_cast<char>(c);
            }
        }
        
        return 0;
    }

    /**
     * @brief 获取当前键盘状态
     * @return 键盘状态枚举
     */
    KeyboardStatus getStatus() override {
        return _status;
    }

    /**
     * @brief 开启键盘模块
     */
    void enable() override {
        _enabled = true;
        _status = KEYBOARD_STATUS_READY;
    }

    /**
     * @brief 关闭键盘模块
     */
    void disable() override {
        _enabled = false;
        _status = KEYBOARD_STATUS_IDLE;
    }

private:
    /**
     * @brief 将字符转换为按键枚举
     * @param c 字符
     * @return 按键枚举
     */
    Key charToKey(char c) {
        switch (c) {
            case 'w':
            case 'W':
                return KEY_UP;
            case 's':
            case 'S':
                return KEY_S;
            case 'a':
            case 'A':
                return KEY_LEFT;
            case 'd':
            case 'D':
                return KEY_RIGHT;
            case ' ':
                return KEY_OK;
            case 'b':
            case 'B':
                return KEY_BACK;
            case '1':
                return KEY_1;
            case '2':
                return KEY_2;
            case '3':
                return KEY_3;
            case '4':
                return KEY_4;
            case '5':
                return KEY_5;
            case '6':
                return KEY_6;
            case '7':
                return KEY_7;
            case '8':
                return KEY_8;
            case '9':
                return KEY_9;
            case '0':
                return KEY_0;
            case 'c':
            case 'C':
                return KEY_C;
            case 'e':
            case 'E':
                return KEY_E;
            case 'f':
            case 'F':
                return KEY_F;
            case 'g':
            case 'G':
                return KEY_G;
            case 'h':
            case 'H':
                return KEY_H;
            case 'i':
            case 'I':
                return KEY_I;
            case 'j':
            case 'J':
                return KEY_J;
            case 'k':
            case 'K':
                return KEY_K;
            case 'l':
            case 'L':
                return KEY_L;
            case 'm':
            case 'M':
                return KEY_M;
            case 'n':
            case 'N':
                return KEY_N;
            case 'o':
            case 'O':
                return KEY_O;
            case 'p':
            case 'P':
                return KEY_P;
            case 'q':
            case 'Q':
                return KEY_Q;
            case 'r':
            case 'R':
                return KEY_R;
            case 't':
            case 'T':
                return KEY_T;
            case 'u':
            case 'U':
                return KEY_U;
            case 'v':
            case 'V':
                return KEY_V;
            case 'x':
            case 'X':
                return KEY_X;
            case 'y':
            case 'Y':
                return KEY_Y;
            case 'z':
            case 'Z':
                return KEY_Z;
            case ',':
                return KEY_COMMA;
            case '/':
                return KEY_SLASH;
            case '\t':
                return KEY_TAB_KEY; // TAB键
            case '\n':
                return KEY_ENTER_KEY;
            case '\b':
                return KEY_DELETE;
            case 27:
                return KEY_ESC;
            case '`':
                return KEY_ESC; // M5Cardputer上的ESC键是反引号键
            default:
                return KEY_NONE;
        }
    }
};

// 创建全局键盘实例
HalKeyboard* keyboard = new M5Keyboard();
