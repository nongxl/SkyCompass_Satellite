#include "app_main.h"
#include "hal/hal_gnss.h"
#include "hal/hal_imu.h"
#include "hal/hal_display.h"
#include "hal/hal_keyboard.h"
#include "core/position_manager.h"
#include "core/sun_calculator.h"
#include "core/attitude_estimator.h"
#include "core/ui_manager.h"
#include "app/time_machine.h"
#include "app/user_input.h"

// 声明全局gnss实例
extern HalGnss* gnss;

// 声明全局imu实例
extern HalImu* imu;

// 声明全局display实例
extern HalDisplay* display;

// 声明全局keyboard实例
extern HalKeyboard* keyboard;

// 初始化静态常量
const unsigned long AppMain::GNSS_UPDATE_INTERVAL = 1000; // GNSS：1秒更新一次
const unsigned long AppMain::GNSS_UPDATE_INTERVAL_FAST = 500; // GNSS快速模式：500ms更新一次
const unsigned long AppMain::IMU_UPDATE_INTERVAL = 10; // IMU：10ms更新一次（100Hz）
const unsigned long AppMain::RENDER_UPDATE_INTERVAL = 33; // 渲染：33ms更新一次（约30Hz）

/**
 * @brief 构造函数
 */
AppMain::AppMain() :
    _gnss(nullptr),
    _imu(nullptr),
    _display(nullptr),
    _keyboard(nullptr),
    _positionManager(nullptr),
    _sunCalculator(nullptr),
    _moonCalculator(nullptr),
    _celestialCore(nullptr),
    _attitudeEstimator(nullptr),
    _view3DRenderer(nullptr),
    _uiManager(nullptr),
    _timeMachine(nullptr),
    _userInput(nullptr),
    _lastGnssUpdate(0),
    _lastImuUpdate(0),
    _lastRenderUpdate(0),
    _initialized(false) {
}

/**
 * @brief 析构函数
 */
AppMain::~AppMain() {
    // 停止应用
    stop();
    
    // 释放应用模块
    if (_userInput) {
        delete _userInput;
    }
    if (_timeMachine) {
        delete _timeMachine;
    }
    
    // 释放核心模块
    if (_uiManager) {
        delete _uiManager;
    }
    if (_view3DRenderer) {
        delete _view3DRenderer;
    }
    if (_attitudeEstimator) {
        delete _attitudeEstimator;
    }
    if (_celestialCore) {
        delete _celestialCore;
    }
    if (_sunCalculator) {
        delete _sunCalculator;
    }
    if (_moonCalculator) {
        delete _moonCalculator;
    }
    if (_positionManager) {
        delete _positionManager;
    }
    
    // 硬件模块未创建实例，不需要释放
    // 实际项目中需要根据具体情况进行管理
}

/**
 * @brief 初始化应用
 * @return 初始化是否成功
 */
bool AppMain::begin() {
    // 初始化硬件模块
    if (!initHardware()) {
        return false;
    }
    
    // 初始化核心模块
    if (!initCoreModules()) {
        return false;
    }
    
    // 初始化应用模块
    if (!initAppModules()) {
        return false;
    }
    
    _initialized = true;
    return true;
}

/**
 * @brief 运行应用主循环
 */
void AppMain::run() {
    if (!_initialized) {
        return;
    }
    
    unsigned long currentTime = millis();
    
    // 更新GNSS数据（控制更新频率）
    if (_gnss) {
        bool isSearching = _gnss->getStatus() == GNSS_STATUS_SEARCHING;
        unsigned long updateInterval = isSearching ? GNSS_UPDATE_INTERVAL_FAST : GNSS_UPDATE_INTERVAL;
        
        if (currentTime - _lastGnssUpdate >= updateInterval) {
            bool hadNewData = _gnss->update();
            _lastGnssUpdate = currentTime;
            
            // 获取到新数据后自动关闭GNSS模块
            if (hadNewData) {
                Serial.println("[GNSS] New data acquired, auto-disabling GNSS module");
                
                // 更新位置和时间数据
                if (_positionManager) {
                    _positionManager->update();
                }
                
                // 重绘界面显示新数据
                if (_uiManager) {
                    if (_uiManager->getCurrentPage() == PAGE_MAIN) {
                        _uiManager->redrawMainPage();
                    } else if (_uiManager->getCurrentPage() == PAGE_TIDE) {
                        _uiManager->setPage(PAGE_TIDE);
                    }
                }
                
                // 自动关闭GNSS模块以省电
                _gnss->disable();
                Serial.println("[GNSS] GNSS module auto-disabled after new data");
            }
        }
    }
    
    // 固定100Hz更新IMU数据
    if (_imu) {
        if (currentTime - _lastImuUpdate >= IMU_UPDATE_INTERVAL) {
            if (_imu->update()) {
                // 更新姿态估计器
                if (_attitudeEstimator) {
                    _attitudeEstimator->update();
                }
                

            }
            _lastImuUpdate = currentTime;
        }
    }
    
    // 更新位置管理器
    if (_positionManager) {
        _positionManager->update();
    }
    
    // 固定30Hz更新UI显示
    if (_uiManager) {
        if (currentTime - _lastRenderUpdate >= RENDER_UPDATE_INTERVAL) {
            unsigned long renderStartTime = millis();
            _uiManager->update();
            unsigned long renderEndTime = millis();
            
            // 每10次渲染才打印一次渲染耗时，避免覆盖其他调试信息
            //static int renderCount = 0;
            //if (renderCount % 10 == 0) {
            //    Serial.printf("[DEBUG] Render Time | Time: %lu | Delta: %lu ms\n", 
            //                  renderEndTime, renderEndTime - renderStartTime);
            //}
            //renderCount++;
            
            _lastRenderUpdate = currentTime;
        }
    }
    
    // 处理用户输入
    if (_userInput) {
        _userInput->processInput();
    }
    

}

/**
 * @brief 停止应用
 */
void AppMain::stop() {
    // 暂时不执行任何操作，实际项目中需要根据具体情况修改
    // 由于硬件模块和核心模块尚未正确初始化，暂时只设置初始化状态
    
    _initialized = false;
}

/**
 * @brief 初始化硬件模块
 * @return 初始化是否成功
 */
bool AppMain::initHardware() {
    Serial.println(F("[APP] initHardware() starting..."));
    Serial.flush();
    
    _gnss = gnss;
    _imu = imu;
    _display = display;
    _keyboard = keyboard;
    
    bool allSuccess = true;
    
    Serial.println(F("[APP] Initializing GNSS..."));
    Serial.flush();
    if (_gnss) {
        if (!_gnss->begin()) {
            Serial.println(F("[APP] GNSS initialization failed!"));
            allSuccess = false;
        } else {
            Serial.println(F("[APP] GNSS initialized successfully"));
        }
    } else {
        Serial.println(F("[APP] GNSS instance is NULL!"));
    }
    Serial.flush();
    
    Serial.println(F("[APP] Initializing IMU..."));
    Serial.flush();
    if (_imu) {
        if (!_imu->begin()) {
            Serial.println(F("[APP] IMU initialization failed!"));
            allSuccess = false;
        } else {
            Serial.println(F("[APP] IMU initialized successfully"));
        }
    } else {
        Serial.println(F("[APP] IMU instance is NULL!"));
    }
    Serial.flush();
    
    Serial.println(F("[APP] Initializing Display..."));
    Serial.flush();
    if (_display) {
        if (!_display->begin()) {
            Serial.println(F("[APP] Display initialization failed!"));
            allSuccess = false;
        } else {
            Serial.println(F("[APP] Display initialized successfully"));
        }
    } else {
        Serial.println(F("[APP] Display instance is NULL!"));
    }
    Serial.flush();
    
    Serial.println(F("[APP] Initializing Keyboard..."));
    Serial.flush();
    if (_keyboard) {
        if (!_keyboard->begin()) {
            Serial.println(F("[APP] Keyboard initialization failed!"));
            allSuccess = false;
        } else {
            Serial.println(F("[APP] Keyboard initialized successfully"));
        }
    } else {
        Serial.println(F("[APP] Keyboard instance is NULL!"));
    }
    Serial.flush();
    
    Serial.println(F("[APP] initHardware() complete"));
    Serial.flush();
    
    return allSuccess;
}

/**
 * @brief 初始化核心模块
 * @return 初始化是否成功
 */
bool AppMain::initCoreModules() {
    // 初始化位置管理器
    _positionManager = new PositionManager(_gnss);
    if (!_positionManager) {
        return false;
    }
    if (!_positionManager->begin()) {
        delete _positionManager;
        _positionManager = nullptr;
        return false;
    }
    
    // 初始化太阳计算器
    if (_positionManager) {
        _sunCalculator = new SunCalculator(_positionManager);
        if (!_sunCalculator) {
            return false;
        }
        if (!_sunCalculator->begin()) {
            delete _sunCalculator;
            _sunCalculator = nullptr;
            return false;
        }
    }
    
    // 初始化月亮计算器
    if (_positionManager) {
        _moonCalculator = new MoonCalculator(_positionManager);
        if (!_moonCalculator) {
            return false;
        }
        if (!_moonCalculator->begin()) {
            delete _moonCalculator;
            _moonCalculator = nullptr;
            return false;
        }
    }
    
    // 初始化天体核心
    if (_sunCalculator && _moonCalculator) {
        _celestialCore = new CelestialCore(_sunCalculator, _moonCalculator, _positionManager);
        if (!_celestialCore) {
            return false;
        }
    }
    
    // 初始化姿态估计器
    if (_imu) {
        _attitudeEstimator = new AttitudeEstimator(_imu);
        if (!_attitudeEstimator) {
            return false;
        }
        if (!_attitudeEstimator->begin()) {
            delete _attitudeEstimator;
            _attitudeEstimator = nullptr;
            return false;
        }
        // 显式启用虚拟航向
        _attitudeEstimator->enableVirtualHeading(true);
        Serial.println("[DEBUG] Virtual heading enabled");
    }
    
    // 初始化UI管理器
    if (_display && _positionManager) {
        _uiManager = new UIManager(_display, _keyboard, _sunCalculator, _moonCalculator, _celestialCore, _attitudeEstimator, _positionManager);
        if (!_uiManager) {
            return false;
        }
        if (!_uiManager->begin()) {
            delete _uiManager;
            _uiManager = nullptr;
            return false;
        }
    }
    
    // 初始化3D渲染器
    if (_display && _positionManager) {
        _view3DRenderer = new View3DRenderer();
        if (!_view3DRenderer) {
            Serial.println("[APP] View3DRenderer creation failed!");
            return false;
        }
        if (!_view3DRenderer->begin(_display)) {
            Serial.println("[APP] View3DRenderer begin failed!");
            delete _view3DRenderer;
            _view3DRenderer = nullptr;
            return false;
        }
        Serial.println("[APP] View3DRenderer initialized successfully");
        
        // 设置UI管理器的3D渲染器指针
        if (_uiManager) {
            _uiManager->setView3DRenderer(_view3DRenderer);
        }
    } else {
        Serial.println("[APP] View3DRenderer creation skipped - missing dependencies");
    }
    
    return true;
}

/**
 * @brief 初始化应用模块
 * @return 初始化是否成功
 */
bool AppMain::initAppModules() {
    // 初始化时间机器
    if (_positionManager) {
        _timeMachine = new TimeMachine(_positionManager);
        if (!_timeMachine) {
            return false;
        }
        if (!_timeMachine->begin()) {
            delete _timeMachine;
            _timeMachine = nullptr;
            return false;
        }
    }
    
    // 设置UI管理器的时间机器指针
    if (_uiManager && _timeMachine) {
        _uiManager->setTimeMachine(_timeMachine);
    }
    
    // 设置UI管理器的GNSS模块指针
    if (_uiManager && _gnss) {
        _uiManager->setGnss(_gnss);
    }
    
    // 初始化用户输入处理
    _userInput = new UserInput(_keyboard, _timeMachine, _uiManager, _attitudeEstimator, _gnss, _positionManager);
    if (!_userInput) {
        return false;
    }
    if (!_userInput->begin()) {
        delete _userInput;
        _userInput = nullptr;
        return false;
    }
    
    return true;
}
