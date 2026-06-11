#ifndef APP_MAIN_H
#define APP_MAIN_H

#include <Arduino.h>
#include "hal/hal_gnss.h"
#include "hal/hal_imu.h"
#include "hal/hal_display.h"
#include "hal/hal_keyboard.h"
#include "core/position_manager.h"
#include "core/sun_calculator.h"
#include "core/moon_calculator.h"
#include "core/celestial_core.h"
#include "core/attitude_estimator.h"
#include "core/ui_manager.h"
#include "core/view_3d_renderer.h"
#include "time_machine.h"
#include "user_input.h"

/**
 * @brief 主应用类
 */
class AppMain {
private:
    bool _initialized;
    HalGnss* _gnss;
    HalImu* _imu;
    HalDisplay* _display;
    HalKeyboard* _keyboard;
    PositionManager* _positionManager;
    SunCalculator* _sunCalculator;
    MoonCalculator* _moonCalculator;
    CelestialCore* _celestialCore;
    AttitudeEstimator* _attitudeEstimator;
    View3DRenderer* _view3DRenderer;
    UIManager* _uiManager;
    TimeMachine* _timeMachine;
    UserInput* _userInput;
    
    // 更新频率控制变量
    unsigned long _lastGnssUpdate;
    unsigned long _lastImuUpdate;
    unsigned long _lastRenderUpdate;
    static const unsigned long GNSS_UPDATE_INTERVAL;
    static const unsigned long GNSS_UPDATE_INTERVAL_FAST;
    static const unsigned long IMU_UPDATE_INTERVAL;
    static const unsigned long RENDER_UPDATE_INTERVAL;

public:
    /**
     * @brief 构造函数
     */
    AppMain();

    /**
     * @brief 析构函数
     */
    ~AppMain();

    /**
     * @brief 初始化应用
     * @return 初始化是否成功
     */
    bool begin();

    /**
     * @brief 运行应用主循环
     */
    void run();

    /**
     * @brief 停止应用
     */
    void stop();

private:
    /**
     * @brief 初始化硬件模块
     * @return 初始化是否成功
     */
    bool initHardware();

    /**
     * @brief 初始化核心模块
     * @return 初始化是否成功
     */
    bool initCoreModules();

    /**
     * @brief 初始化应用模块
     * @return 初始化是否成功
     */
    bool initAppModules();
};

#endif // APP_MAIN_H
