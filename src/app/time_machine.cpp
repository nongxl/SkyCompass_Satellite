#include "time_machine.h"
#include "core/log_manager.h"

/**
 * @brief 构造函数
 * @param positionManager 位置和时间管理器指针
 */
TimeMachine::TimeMachine(PositionManager* positionManager) : 
    _positionManager(positionManager),
    _isActive(false),
    _selectedField(0) {
    // 初始化目标时间为当前时间
    _targetTime = _positionManager->getTime();
}

/**
 * @brief 初始化时间机器
 * @return 初始化是否成功
 */
bool TimeMachine::begin() {
    // 时间机器不需要特殊初始化
    return true;
}

/**
 * @brief 激活时间机器
 */
void TimeMachine::activate() {
    _isActive = true;
    // 保存当前时间作为目标时间
    _targetTime = _positionManager->getTime();
    
    // 检查时间是否有效，如果无效则使用默认本地时间（2026年5月10日 22:00）
    if (_targetTime.year == 0 || _targetTime.month == 0 || _targetTime.day == 0 || _targetTime.year > 100) {
        _targetTime = PositionManager::getDefaultTime();
        
        PositionData pos = _positionManager->getPosition();
        int32_t timezoneOffset = _positionManager->getTimezoneManager()->getTimezoneOffset(pos.latitude, pos.longitude);
        
        // 将本地时间转换为UTC时间
        int64_t localTimestamp = (int64_t)_targetTime.hour * 3600LL + _targetTime.minute * 60LL + _targetTime.second - timezoneOffset;
        
        // 处理跨天情况
        int8_t utcHour = (localTimestamp / 3600) % 24;
        if (utcHour < 0) utcHour += 24;
        int8_t utcMinute = (localTimestamp % 3600) / 60;
        if (utcMinute < 0) utcMinute += 60;
        
        _targetTime.hour = utcHour;
        _targetTime.minute = utcMinute;
        
        LOG_I("DEBUG", "TimeMachine: Using default local time (2026-05-10 22:00:00)");
    }
    
    // 设置手动时间
    _positionManager->setManualTime(_targetTime);
    // 启用手动时间
    _positionManager->enableManualTime(true);
    LOG_I("DEBUG", "TimeMachine activated");
    LOG_I("DEBUG", "Target time: %04d-%02d-%02d %02d:%02d:%02d", _targetTime.year + 2000, _targetTime.month, _targetTime.day,
                  _targetTime.hour, _targetTime.minute, _targetTime.second);
    Serial.flush();
}

/**
 * @brief 停用时间机器
 */
void TimeMachine::deactivate() {
    _isActive = false;
    // 禁用手动时间
    _positionManager->enableManualTime(false);
}

/**
 * @brief 检查时间机器是否激活
 * @return 是否激活
 */
bool TimeMachine::isActive() {
    return _isActive;
}

/**
 * @brief 获取目标时间
 * @return 目标时间
 */
TimeData TimeMachine::getTargetTime() {
    return _targetTime;
}

/**
 * @brief 设置目标时间
 * @param time 目标时间
 */
void TimeMachine::setTargetTime(TimeData time) {
    if (isValidTime(time)) {
        _targetTime = time;
        // 应用目标时间
        _positionManager->setManualTime(_targetTime);
    }
}

/**
 * @brief 增加当前选中字段的值
 * @param step 增加的步长
 */
void TimeMachine::incrementField(uint8_t step) {
    switch (_selectedField) {
        case 0: // 年
            _targetTime.year += step;
            // 确保年份不会溢出
            if (_targetTime.year > 100) {
                _targetTime.year = 100; // 限制到 2100 年
            }
            break;
        case 1: // 月
            _targetTime.month += step;
            if (_targetTime.month > 12) {
                _targetTime.month = 1;
                _targetTime.year++;
                // 确保年份不会溢出
                if (_targetTime.year > 100) {
                    _targetTime.year = 100; // 限制到 2100 年
                }
            }
            break;
        case 2: // 日
            {
                uint16_t fullYear = 2000 + _targetTime.year;
                uint8_t daysInMonth = getDaysInMonth(fullYear, _targetTime.month);
                _targetTime.day += step;
                if (_targetTime.day > daysInMonth) {
                    _targetTime.day = 1;
                    _targetTime.month++;
                    if (_targetTime.month > 12) {
                        _targetTime.month = 1;
                        _targetTime.year++;
                        // 确保年份不会溢出
                        if (_targetTime.year > 100) {
                            _targetTime.year = 100; // 限制到 2100 年
                        }
                    }
                }
            }
            break;
        case 3: // 时
            _targetTime.hour += step;
            if (_targetTime.hour >= 24) {
                _targetTime.hour -= 24;
                _targetTime.day++;
                uint16_t fullYear = 2000 + _targetTime.year;
                uint8_t daysInMonth = getDaysInMonth(fullYear, _targetTime.month);
                if (_targetTime.day > daysInMonth) {
                    _targetTime.day = 1;
                    _targetTime.month++;
                    if (_targetTime.month > 12) {
                        _targetTime.month = 1;
                        _targetTime.year++;
                        // 确保年份不会溢出
                        if (_targetTime.year > 100) {
                            _targetTime.year = 100; // 限制到 2100 年
                        }
                    }
                }
            }
            break;
        case 4: // 分
            _targetTime.minute += step;
            if (_targetTime.minute >= 60) {
                _targetTime.minute -= 60;
                _targetTime.hour++;
                if (_targetTime.hour >= 24) {
                    _targetTime.hour -= 24;
                    _targetTime.day++;
                    uint16_t fullYear = 2000 + _targetTime.year;
                    uint8_t daysInMonth = getDaysInMonth(fullYear, _targetTime.month);
                    if (_targetTime.day > daysInMonth) {
                        _targetTime.day = 1;
                        _targetTime.month++;
                        if (_targetTime.month > 12) {
                            _targetTime.month = 1;
                            _targetTime.year++;
                        }
                    }
                }
            }
            break;
        case 5: // 秒
            _targetTime.second += step;
            if (_targetTime.second >= 60) {
                _targetTime.second -= 60;
                _targetTime.minute++;
                if (_targetTime.minute >= 60) {
                    _targetTime.minute -= 60;
                    _targetTime.hour++;
                    if (_targetTime.hour >= 24) {
                        _targetTime.hour -= 24;
                        _targetTime.day++;
                        uint16_t fullYear = 2000 + _targetTime.year;
                        uint8_t daysInMonth = getDaysInMonth(fullYear, _targetTime.month);
                        if (_targetTime.day > daysInMonth) {
                            _targetTime.day = 1;
                            _targetTime.month++;
                            if (_targetTime.month > 12) {
                                _targetTime.month = 1;
                                _targetTime.year++;
                            }
                        }
                    }
                }
            }
            break;
    }
    
    // 应用目标时间
    _positionManager->setManualTime(_targetTime);
}

/**
 * @brief 减少当前选中字段的值
 * @param step 减少的步长
 */
void TimeMachine::decrementField(uint8_t step) {
    switch (_selectedField) {
        case 0: // 年
            if (_targetTime.year >= step) {
                _targetTime.year -= step;
            }
            break;
        case 1: // 月
            _targetTime.month -= step;
            if (_targetTime.month < 1) {
                _targetTime.month = 12;
                if (_targetTime.year >= step) {
                    _targetTime.year -= step;
                }
            }
            break;
        case 2: // 日
            _targetTime.day -= step;
            if (_targetTime.day < 1) {
                _targetTime.month--;
                if (_targetTime.month < 1) {
                    _targetTime.month = 12;
                    if (_targetTime.year >= step) {
                        _targetTime.year -= step;
                    }
                }
                uint16_t fullYear = 2000 + _targetTime.year;
                _targetTime.day = getDaysInMonth(fullYear, _targetTime.month);
            }
            break;
        case 3: // 时
            if (_targetTime.hour >= step) {
                _targetTime.hour -= step;
            } else {
                _targetTime.hour += 24 - step;
                _targetTime.day--;
                if (_targetTime.day < 1) {
                    _targetTime.month--;
                    if (_targetTime.month < 1) {
                        _targetTime.month = 12;
                        if (_targetTime.year >= step) {
                            _targetTime.year -= step;
                        }
                    }
                    uint16_t fullYear = 2000 + _targetTime.year;
                    _targetTime.day = getDaysInMonth(fullYear, _targetTime.month);
                }
            }
            break;
        case 4: // 分
            if (_targetTime.minute >= step) {
                _targetTime.minute -= step;
            } else {
                _targetTime.minute += 60 - step;
                if (_targetTime.hour >= step) {
                    _targetTime.hour -= step;
                } else {
                    _targetTime.hour += 24 - step;
                    _targetTime.day--;
                    if (_targetTime.day < 1) {
                        _targetTime.month--;
                        if (_targetTime.month < 1) {
                            _targetTime.month = 12;
                            if (_targetTime.year >= step) {
                                _targetTime.year -= step;
                            }
                        }
                        uint16_t fullYear = 2000 + _targetTime.year;
                        _targetTime.day = getDaysInMonth(fullYear, _targetTime.month);
                    }
                }
            }
            break;
        case 5: // 秒
            if (_targetTime.second >= step) {
                _targetTime.second -= step;
            } else {
                _targetTime.second += 60 - step;
                if (_targetTime.minute >= step) {
                    _targetTime.minute -= step;
                } else {
                    _targetTime.minute += 60 - step;
                    if (_targetTime.hour >= step) {
                        _targetTime.hour -= step;
                    } else {
                        _targetTime.hour += 24 - step;
                        _targetTime.day--;
                        if (_targetTime.day < 1) {
                            _targetTime.month--;
                            if (_targetTime.month < 1) {
                                _targetTime.month = 12;
                                if (_targetTime.year >= step) {
                                    _targetTime.year -= step;
                                }
                            }
                            uint16_t fullYear = 2000 + _targetTime.year;
                            _targetTime.day = getDaysInMonth(fullYear, _targetTime.month);
                        }
                    }
                }
            }
            break;
    }
    
    // 应用目标时间
    _positionManager->setManualTime(_targetTime);
}

/**
 * @brief 选择下一个时间字段
 */
void TimeMachine::selectNextField() {
    _selectedField = (_selectedField + 1) % 6;
}

/**
 * @brief 选择上一个时间字段
 */
void TimeMachine::selectPreviousField() {
    _selectedField = (_selectedField - 1 + 6) % 6;
}

/**
 * @brief 获取当前选中的字段索引
 * @return 字段索引
 */
uint8_t TimeMachine::getSelectedField() {
    return _selectedField;
}

/**
 * @brief 应用目标时间（更新到位置和时间管理器）
 */
void TimeMachine::applyTime() {
    _positionManager->setManualTime(_targetTime);
}

/**
 * @brief 重置为当前时间
 */
void TimeMachine::resetToCurrentTime() {
    // 获取当前时间
    TimeData currentTime = _positionManager->getTime();
    // 禁用手动时间以获取真实的当前时间
    _positionManager->enableManualTime(false);
    currentTime = _positionManager->getTime();
    // 重新启用手动时间
    _positionManager->enableManualTime(true);
    // 设置目标时间为当前时间
    _targetTime = currentTime;
    // 应用目标时间
    _positionManager->setManualTime(_targetTime);
}

/**
 * @brief 调整时间
 * @param seconds 调整的秒数（正数增加，负数减少）
 */
void TimeMachine::adjustTime(int seconds) {
    // 确保时间机器已激活
    if (!_isActive) {
        activate();
    }
    
    // 计算总秒数并调整时间
    int totalSeconds = _targetTime.hour * 3600 + _targetTime.minute * 60 + _targetTime.second;
    totalSeconds += seconds;
    
    // 处理总秒数，确保在合理范围内
    int daysToAdd = 0;
    while (totalSeconds < 0) {
        totalSeconds += 86400; // 加上一天的秒数
        daysToAdd--; // 减去一天
    }
    while (totalSeconds >= 86400) {
        totalSeconds -= 86400; // 减去一天的秒数
        daysToAdd++; // 加上一天
    }
    
    // 转换为时分秒
    int hours = totalSeconds / 3600;
    int minutes = (totalSeconds % 3600) / 60;
    int secs = totalSeconds % 60;
    
    // 设置时分秒
    _targetTime.hour = hours;
    _targetTime.minute = minutes;
    _targetTime.second = secs;
    
    // 处理日期溢出
    if (daysToAdd != 0) {
        _targetTime.day += daysToAdd;
        // 处理日期溢出
        uint16_t fullYear = 2000 + _targetTime.year;
        uint8_t daysInMonth = getDaysInMonth(fullYear, _targetTime.month);
        
        while (_targetTime.day > daysInMonth) {
            _targetTime.day -= daysInMonth;
            _targetTime.month++;
            if (_targetTime.month > 12) {
                _targetTime.month = 1;
                _targetTime.year++;
            }
            fullYear = 2000 + _targetTime.year;
            daysInMonth = getDaysInMonth(fullYear, _targetTime.month);
        }
        
        while (_targetTime.day < 1) {
            _targetTime.month--;
            if (_targetTime.month < 1) {
                _targetTime.month = 12;
                _targetTime.year--;
                if (_targetTime.year < 0) {
                    _targetTime.year = 0;
                }
            }
            fullYear = 2000 + _targetTime.year;
            daysInMonth = getDaysInMonth(fullYear, _targetTime.month);
            _targetTime.day += daysInMonth;
        }
    }
    
    // 应用目标时间
    _positionManager->setManualTime(_targetTime);
}

/**
 * @brief 验证时间是否有效
 * @param time 时间数据
 * @return 是否有效
 */
bool TimeMachine::isValidTime(TimeData time) {
    // 验证月份
    if (time.month < 1 || time.month > 12) {
        return false;
    }
    
    // 验证日期
    uint16_t fullYear = 2000 + time.year;
    uint8_t daysInMonth = getDaysInMonth(fullYear, time.month);
    if (time.day < 1 || time.day > daysInMonth) {
        return false;
    }
    
    // 验证小时
    if (time.hour >= 24) {
        return false;
    }
    
    // 验证分钟
    if (time.minute >= 60) {
        return false;
    }
    
    // 验证秒
    if (time.second >= 60) {
        return false;
    }
    
    return true;
}

/**
 * @brief 获取指定月份的天数
 * @param year 年份
 * @param month 月份（1-12）
 * @return 天数
 */
uint8_t TimeMachine::getDaysInMonth(uint16_t year, uint8_t month) {
    uint8_t daysInMonth[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    
    // 检查是否是闰年
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))) {
        return 29;
    }
    
    return daysInMonth[month];
}
