#ifndef USER_INPUT_H
#define USER_INPUT_H

#include <Arduino.h>
#include "hal/hal_keyboard.h"
#include "time_machine.h"
#include "core/ui_manager.h"
#include "core/attitude_estimator.h"
#include "hal/hal_gnss.h"
#include "core/position_manager.h"

/**
 * @brief 用户输入处理器类
 */
class UserInput {
private:
    HalKeyboard* _keyboard;    // 键盘模块指针
    TimeMachine* _timeMachine;  // 时间机器模块指针
    UIManager* _uiManager;      // UI管理器指针
    AttitudeEstimator* _attitudeEstimator; // 姿态估计器指针
    HalGnss* _gnss;             // GNSS模块指针
    PositionManager* _positionManager; // 位置管理器指针
    unsigned long _lastKeyTime; // 上次通用按键时间
    unsigned long _lastSKeyTime; // S键上次触发时间
    unsigned long _lastFKeyTime; // F键上次触发时间
    unsigned long _lastStepTime; // 时间步进上次触发时间
    unsigned long _lastCKeyTime; // C键上次触发时间
    unsigned long _lastTabKeyTime; // Tab键上次触发时间
    unsigned long _lastBKeyTime; // B键上次触发时间
    unsigned long _keyPressStartTime; // 当前按键首次按下的时间
    Key _lastDetectedKey;       // 上次检测到的按键
    
    const unsigned long _debounceDelay = 400;      // 通用按键防抖（毫秒）
    const unsigned long _functionKeyDelay = 600;   // 功能键防抖（毫秒）
    const unsigned long _stepRepeatDelay = 25;    // 长按步进频率控制（毫秒，即 40Hz）

public:
    /**
     * @brief 构造函数
     * @param keyboard 键盘模块指针
     * @param timeMachine 时间机器模块指针
     * @param uiManager UI管理器指针
     * @param attitudeEstimator 姿态估计器指针
     * @param gnss GNSS模块指针
     * @param positionManager 位置管理器指针
     */
    UserInput(HalKeyboard* keyboard, TimeMachine* timeMachine, UIManager* uiManager, AttitudeEstimator* attitudeEstimator, HalGnss* gnss, PositionManager* positionManager);

    /**
     * @brief 初始化用户输入处理器
     * @return 初始化是否成功
     */
    bool begin();

    /**
     * @brief 处理用户输入
     */
    void processInput();

private:
    /**
     * @brief 处理主页面的输入
     * @param key 按键
     */
    void handleMainPageInput(Key key);

    /**
     * @brief 处理太阳数据页面的输入
     * @param key 按键
     */
    void handleSunDataPageInput(Key key);

    /**
     * @brief 处理时间机器页面的输入
     * @param key 按键
     */
    void handleTimeMachinePageInput(Key key);

    /**
     * @brief 处理设置页面的输入
     * @param key 按键
     */
    void handleSettingsPageInput(Key key);

    /**
     * @brief 切换GNSS模块开关状态
     */
    void toggleGnssModule();
    
    /**
     * @brief 处理菜单页面的输入
     * @param key 按键
     */
    void handleMenuPageInput(Key key);
    
    /**
     * @brief 处理位置设置页面的输入
     * @param key 按键
     */
    void handlePositionSettingsPageInput(Key key);
};

#endif // USER_INPUT_H

