#include "user_input.h"
#include "core/log_manager.h"
#include <M5Cardputer.h>

/**
 * @brief 构造函数
 * @param keyboard 键盘模块指针
 * @param timeMachine 时间机器模块指针
 * @param uiManager UI管理器指针
 * @param attitudeEstimator 姿态估计器指针
 * @param gnss GNSS模块指针
 * @param positionManager 位置管理器指针
 */
UserInput::UserInput(HalKeyboard* keyboard, TimeMachine* timeMachine, UIManager* uiManager, AttitudeEstimator* attitudeEstimator, HalGnss* gnss, PositionManager* positionManager) : 
    _keyboard(keyboard),
    _timeMachine(timeMachine),
    _uiManager(uiManager),
    _attitudeEstimator(attitudeEstimator),
    _gnss(gnss),
    _positionManager(positionManager),
    _lastKeyTime(0),
    _lastSKeyTime(0),
    _lastFKeyTime(0),
    _lastStepTime(0),
    _lastCKeyTime(0),
    _lastTabKeyTime(0),
    _lastBKeyTime(0),
    _keyPressStartTime(0),
    _lastDetectedKey(KEY_NONE) {
}

/**
 * @brief 切换GNSS模块开关状态
 */
void UserInput::toggleGnssModule() {
    if (_gnss) {
        bool isEnabled = _gnss->isEnabled();
        
        if (isEnabled) {
            // 禁用GNSS模块
            _gnss->disable();
            LOG_I("GNSS", "GNSS module disabled");
        } else {
            // 启用GNSS模块
            _gnss->enable();
            LOG_I("GNSS", "GNSS module enabled");
        }
        
        // 根据当前页面重绘界面，显示GNSS状态
        if (_uiManager->getCurrentPage() == PAGE_MAIN) {
            _uiManager->redrawMainPage();
        } else if (_uiManager->getCurrentPage() == PAGE_TIDE) {
            _uiManager->setPage(PAGE_TIDE); // 重新设置页面以触发重绘
        }
    } else {
        LOG_I("GNSS", "GNSS module is null");
    }
}

/**
 * @brief 初始化用户输入处理器
 * @return 初始化是否成功
 */
bool UserInput::begin() {
    // 用户输入处理器不需要特殊初始化
    return true;
}

/**
 * @brief 处理用户输入
 */
void UserInput::processInput() {
    if (_keyboard->available()) {
        // 按键防抖
        unsigned long currentTime = millis();
        // 获取按键
        Key key = _keyboard->getKey();
        
        // 特殊处理：时间调节键（逗号和斜杠）绕过通用防抖，以支持极速连发
        bool isTimeKey = (key == KEY_COMMA || key == KEY_SLASH);
        
        // 检测按键状态切换（从无到有，或更换了按键）
        if (key != _lastDetectedKey) {
            _keyPressStartTime = currentTime;
            _lastDetectedKey = key;
        }

        if (!isTimeKey && (currentTime - _lastKeyTime < _debounceDelay)) {
            return; // 忽略抖动的普通按键
        }
        if (!isTimeKey) _lastKeyTime = currentTime;
        
        // 根据当前页面调用不同的处理函数
        UIPage currentPage = _uiManager->getCurrentPage();
        switch (currentPage) {
            case PAGE_MAIN:
                handleMainPageInput(key);
                break;
            case PAGE_MENU:
                handleMenuPageInput(key);
                break;
            case PAGE_SUN_DATA:
                handleSunDataPageInput(key);
                break;
            case PAGE_TIME_MACHINE:
                handleTimeMachinePageInput(key);
                break;
            case PAGE_TIDE:
                handleMainPageInput(key);
                break;
            case PAGE_SETTINGS:
                handleSettingsPageInput(key);
                break;
        }
    }
}

/**
 * @brief 处理主页面的输入
 * @param key 按键
 */
void UserInput::handleMainPageInput(Key key) {
    unsigned long currentTime = millis();
    
    switch (key) {
        case KEY_TAB_KEY: // TAB键在Sky Mode和Tide Mode之间切换
            if (currentTime - _lastTabKeyTime >= _functionKeyDelay) {
                _lastTabKeyTime = currentTime;
                if (_uiManager->getCurrentPage() == PAGE_MAIN) {
                    _uiManager->setPage(PAGE_TIDE);
                } else if (_uiManager->getCurrentPage() == PAGE_TIDE) {
                    _uiManager->setPage(PAGE_MAIN);
                }
            }
            break;
        case KEY_B: // B键切换到时间机器页面
            if (currentTime - _lastBKeyTime >= _functionKeyDelay) {
                _lastBKeyTime = currentTime;
                _uiManager->setPage(PAGE_TIME_MACHINE);
                _timeMachine->activate();
                _uiManager->setTimeMachineActive(true);
            }
            break;
        case KEY_S: // S键控制GNSS模块开关 (增加独立长防抖)
            if (currentTime - _lastSKeyTime >= _functionKeyDelay) {
                _lastSKeyTime = currentTime;
                LOG_I("UserInput", "S key pressed, toggling GNSS module");
                toggleGnssModule();
            } else {
                LOG_I("UserInput", "S key ignored, time since last press: %lu ms (need %lu ms)", currentTime - _lastSKeyTime, _functionKeyDelay);
            }
            break;
        case KEY_F: // F键校准方向 (增加独立长防抖)
            if (_attitudeEstimator && (currentTime - _lastFKeyTime >= _functionKeyDelay)) {
                _lastFKeyTime = currentTime;
                _attitudeEstimator->calibrateHeading();
                _uiManager->redrawMainPage();
            }
            break;
        case KEY_C: // C键打开设置页面
            if (currentTime - _lastCKeyTime >= _functionKeyDelay) {
                _lastCKeyTime = currentTime;
                _uiManager->setPage(PAGE_SETTINGS);
            }
            break;
        case KEY_COMMA: // 逗号键（左）减少时间
            if (currentTime - _lastStepTime >= _stepRepeatDelay) {
                // 首发延迟逻辑：第一次按下立即触发，后续触发需满足长按时间阈值
                bool isFirstPress = (currentTime - _lastStepTime > 400);
                bool isLongPress = (currentTime - _keyPressStartTime > 400);
                
                if (!isFirstPress && !isLongPress) break; // 处于首发后的等待期，防止短按误触发

                int32_t step = isLongPress ? -900 : -1800; // 长按 15min，短按 30min
                _lastStepTime = currentTime;
                _timeMachine->adjustTime(step);
                
                if (_uiManager->getCurrentPage() == PAGE_MAIN) {
                    _uiManager->redrawMainPage();
                } else if (_uiManager->getCurrentPage() == PAGE_TIDE) {
                    _uiManager->redrawTidePage();
                }
            }
            break;
        case KEY_SLASH: // 斜杠键（右）增加时间
            if (currentTime - _lastStepTime >= _stepRepeatDelay) {
                // 首发延迟逻辑
                bool isFirstPress = (currentTime - _lastStepTime > 400);
                bool isLongPress = (currentTime - _keyPressStartTime > 400);
                
                if (!isFirstPress && !isLongPress) break;

                int32_t step = isLongPress ? 900 : 1800;
                _lastStepTime = currentTime;
                _timeMachine->adjustTime(step);
                
                if (_uiManager->getCurrentPage() == PAGE_MAIN) {
                    _uiManager->redrawMainPage();
                } else if (_uiManager->getCurrentPage() == PAGE_TIDE) {
                    _uiManager->redrawTidePage();
                }
            }
            break;
        default:
            break;
    }
}

/**
 * @brief 处理太阳数据页面的输入
 * @param key 按键
 */
void UserInput::handleSunDataPageInput(Key key) {
    switch (key) {
        case KEY_BACK:
        case KEY_ESC: // 后退键返回主页面
            _uiManager->setPage(PAGE_MAIN);
            break;
    }
}

/**
 * @brief 处理时间机器页面的输入
 * @param key 按键
 */
void UserInput::handleTimeMachinePageInput(Key key) {
    switch (key) {
        case KEY_UP: // 上箭头增加当前字段
            _timeMachine->incrementField();
            break;
        case KEY_DOWN: // 下箭头减少当前字段
            _timeMachine->decrementField();
            break;
        case KEY_LEFT: // 左箭头选择上一个字段
            _timeMachine->selectPreviousField();
            break;
        case KEY_RIGHT: // 右箭头选择下一个字段
            _timeMachine->selectNextField();
            break;
        case KEY_OK:
        case KEY_SPACE: // OK键确认并返回主页面
            _timeMachine->applyTime();
            _uiManager->setPage(PAGE_MAIN);
            break;
        case KEY_BACK:
        case KEY_ESC: // 后退键取消并返回主页面
            _timeMachine->deactivate();
            _uiManager->setTimeMachineActive(false);
            _uiManager->setPage(PAGE_MAIN);
            break;
    }
}

/**
 * @brief 处理设置页面的输入
 * @param key 按键
 */
void UserInput::handleSettingsPageInput(Key key) {
    // 处理设置页面的输入逻辑
    if (key == KEY_BACK || key == KEY_ESC || key == KEY_C) {
        // ESC键、BACK键或C键退出设置菜单
        _uiManager->setPage(PAGE_MAIN);
    } else if (key == KEY_TAB_KEY) {
        // 切换到下一个输入字段
        _uiManager->selectNextSettingsField();
    } else if (key == KEY_DELETE) {
        // 删除当前输入字段的最后一个字符
        _uiManager->deleteSettingsChar();
    } else if (key == KEY_0) {
        // 输入数字0
        _uiManager->addSettingsChar('0');
    } else if (key == KEY_1) {
        // 输入数字1
        _uiManager->addSettingsChar('1');
    } else if (key == KEY_2) {
        // 输入数字2
        _uiManager->addSettingsChar('2');
    } else if (key == KEY_3) {
        // 输入数字3
        _uiManager->addSettingsChar('3');
    } else if (key == KEY_4) {
        // 输入数字4
        _uiManager->addSettingsChar('4');
    } else if (key == KEY_5) {
        // 输入数字5
        _uiManager->addSettingsChar('5');
    } else if (key == KEY_6) {
        // 输入数字6
        _uiManager->addSettingsChar('6');
    } else if (key == KEY_7) {
        // 输入数字7
        _uiManager->addSettingsChar('7');
    } else if (key == KEY_8) {
        // 输入数字8
        _uiManager->addSettingsChar('8');
    } else if (key == KEY_9) {
        // 输入数字9
        _uiManager->addSettingsChar('9');
    } else if (key == KEY_PERIOD) {
        // 输入小数点（使用句号键）
        _uiManager->addSettingsChar('.');
    } else if (key == KEY_OK) {
        // 确认设置，应用位置数据
        LOG_I("UserInput", "OK key pressed, applying settings");
        _uiManager->applySettings();
        _uiManager->setPage(PAGE_MAIN);
    }
}

/**
 * @brief 处理菜单页面的输入
 * @param key 按键
 */
void UserInput::handleMenuPageInput(Key key) {
    switch (key) {
        case KEY_BACK:
        case KEY_ESC: // 后退键返回主页面
            _uiManager->setPage(PAGE_MAIN);
            break;
        case KEY_C: // C键进入设置页面
            _uiManager->setPage(PAGE_SETTINGS);
            break;
    }
}
