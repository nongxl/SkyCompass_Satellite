#include "position_manager.h"

/**
 * @brief 构造函数
 * @param gnss GNSS模块指针
 */
PositionManager::PositionManager(HalGnss* gnss) : 
    _gnss(gnss),
    _useManualTime(false),
    _useManualPosition(false),
    _defaultTimeLogged(false) {
    // 初始化位置数据
    _position.latitude = 0.0;
    _position.longitude = 0.0;
    _position.altitude = 0.0;

    // 初始化时间数据
    _time.year = 0;
    _time.month = 0;
    _time.day = 0;
    _time.hour = 0;
    _time.minute = 0;
    _time.second = 0;

    // 初始化手动时间数据
    _manualTime.year = 0;
    _manualTime.month = 0;
    _manualTime.day = 0;
    _manualTime.hour = 0;
    _manualTime.minute = 0;
    _manualTime.second = 0;

    // 初始化手动位置数据
    _manualPosition.latitude = 0.0;
    _manualPosition.longitude = 0.0;
    _manualPosition.altitude = 0.0;
}

/**
 * @brief 初始化位置和时间管理器
 * @return 初始化是否成功
 */
bool PositionManager::begin() {
    // 初始化GNSS模块（如果存在）
    if (_gnss) {
        return _gnss->begin();
    }
    // 如果没有GNSS，仍然返回成功
    return true;
}

/**
 * @brief 更新位置和时间数据
 * @return 更新是否成功
 */
bool PositionManager::update() {
    // 如果使用手动时间和位置，不需要更新
    if (_useManualTime && _useManualPosition) {
        return true;
    }

    // 更新GNSS数据（如果存在）
    if (_gnss) {
        if (_gnss->update()) {
            // 从GNSS数据更新位置和时间
            GnssData gnssData = _gnss->getData();
            updateFromGnss(gnssData);
            return true;
        }
    }

    return false;
}

/**
 * @brief 获取当前位置数据
 * @return 位置数据结构体
 */
PositionData PositionManager::getPosition() {
    if (_useManualPosition) {
        return _manualPosition;
    }
    
    // 检查位置是否有效，如果无效返回默认位置（北京）
    if (_position.latitude == 0.0 && _position.longitude == 0.0) {
        return getDefaultPosition();
    }
    
    return _position;
}

/**
 * @brief 获取当前时间数据
 * @return 时间数据结构体
 */
TimeData PositionManager::getTime() {
    if (_useManualTime) {
        // 检查手动时间是否有效，如果无效返回默认本地时间（2026年5月10日 22:00）
        if (!hasValidTime()) {
            return getDefaultTime();
        }
        return _manualTime;
    }
    
    // 检查时间是否有效，如果无效返回默认本地时间2026/5/10 22:00
    if (!hasValidTime()) {
        return getDefaultTime();
    }
    
    return _time;
}

/**
 * @brief 获取当前UTC时间的时间戳（秒）
 * @return UTC时间戳
 */
uint32_t PositionManager::getTimestamp() {
    if (_useManualTime) {
        if (hasValidTime()) {
            uint32_t localTs = calculateTimestamp(_manualTime);
            // 关键：时间机器设置的是本地时间，需减去时区偏移量以获得真正的 UTC 时间戳
            int8_t timezone = (int8_t)round(getPosition().longitude / 15.0);
            return localTs - (timezone * 3600);
        }
    } else {
        if (hasValidTime()) {
            // GNSS 原始时间通常就是 UTC，所以直接计算
            return calculateTimestamp(_time);
        }
    }
    
    // 默认时间处理
    TimeData defaultTime = getDefaultTime();
    return calculateTimestamp(defaultTime);
}

/**
 * @brief 将时间戳转换为时间数据结构体
 * @param timestamp UTC时间戳（秒）
 * @return 时间数据结构体
 */
TimeData PositionManager::getTimeData(uint32_t timestamp) {
    TimeData result;
    
    // Unix时间戳是从1970年1月1日开始的
    // 将时间戳转换为年月日时分秒
    result.second = timestamp % 60;
    result.minute = (timestamp / 60) % 60;
    result.hour = (timestamp / 3600) % 24;
    uint32_t days = timestamp / 86400;
    
    // 从1970年1月1日开始计算
    uint16_t year = 1970;
    uint8_t month = 1;
    uint8_t day = 1;
    
    // 计算年月日
    while (days > 0) {
        // 检查是否是闰年
        bool isLeapYear = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        uint16_t daysInYear = isLeapYear ? 366 : 365;
        
        if (days >= daysInYear) {
            days -= daysInYear;
            year++;
        } else {
            // 计算月份
            int monthDays[] = {31, isLeapYear ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            for (uint8_t m = 0; m < 12; m++) {
                if (days >= monthDays[m]) {
                    days -= monthDays[m];
                    month++;
                } else {
                    day += days;
                    days = 0;
                    break;
                }
            }
        }
    }
    
    result.year = (year >= 2000) ? (year - 2000) : 0;
    result.month = month;
    result.day = day;
    
    return result;
}

/**
 * @brief 将时间戳转换为本地时间数据结构体（根据经度计算时区）
 * @param timestamp UTC时间戳（秒）
 * @return 本地时间数据结构体
 */
TimeData PositionManager::getLocalTimeData(uint32_t timestamp) {
    // 获取 UTC 时间
    TimeData utcTime = getTimeData(timestamp);
    
    // 计算时区偏移（简单的整数时区计算）
    PositionData pos = getPosition();
    // 经度每 15 度为一个时区，四舍五入到最接近的整数
    int8_t timezone = (int8_t)round(pos.longitude / 15.0);
    
    // 计算本地时间戳
    int32_t offset = timezone * 3600;
    uint32_t localTimestamp = (uint32_t)((int64_t)timestamp + offset);
    
    // 再将本地时间戳转换为时间数据
    return getTimeData(localTimestamp);
}

/**
 * @brief 设置手动时间（用于时间机器功能）
 * @param time 手动设置的时间数据
 */
void PositionManager::setManualTime(TimeData time) {
    _manualTime = time;
}

/**
 * @brief 启用/禁用手动时间
 * @param enable 是否启用手动时间
 */
void PositionManager::enableManualTime(bool enable) {
    _useManualTime = enable;
}

/**
 * @brief 设置手动位置
 * @param position 手动设置的位置数据
 */
void PositionManager::setManualPosition(PositionData position) {
    _manualPosition = position;
}

/**
 * @brief 启用/禁用手动位置
 * @param enable 是否启用手动位置
 */
void PositionManager::enableManualPosition(bool enable) {
    _useManualPosition = enable;
}

/**
 * @brief 检查是否有有效的位置数据
 * @return 是否有有效的位置数据
 */
bool PositionManager::hasValidPosition() {
    if (_useManualPosition) {
        return (_manualPosition.latitude != 0.0 || _manualPosition.longitude != 0.0);
    }
    return (_position.latitude != 0.0 || _position.longitude != 0.0);
}

/**
 * @brief 检查是否有有效的时间数据
 * @return 是否有有效的时间数据
 */
bool PositionManager::hasValidTime() {
    if (_useManualTime) {
        return (_manualTime.year != 0 && _manualTime.year <= 100 && 
                _manualTime.month >= 1 && _manualTime.month <= 12 && 
                _manualTime.day >= 1 && _manualTime.day <= 31);
    }
    return (_time.year != 0 && _time.year <= 100 && 
            _time.month >= 1 && _time.month <= 12 && 
            _time.day >= 1 && _time.day <= 31);
}

/**
 * @brief 从GNSS数据更新位置和时间
 * @param gnssData GNSS数据结构体
 */
void PositionManager::updateFromGnss(GnssData gnssData) {
    // 检查GNSS是否已经获取到有效定位
    if (!gnssData.isValid || gnssData.status != GNSS_STATUS_LOCKED) {
        Serial.printf("[PositionManager] GNSS not locked yet (status=%d), ignoring position update\n", gnssData.status);
        return;
    }
    
    // 检查位置数据是否有效（经纬度不能为0）
    if (gnssData.latitude == 0.0 && gnssData.longitude == 0.0) {
        Serial.printf("[PositionManager] Invalid position from GNSS (lat=0, lon=0), ignoring\n");
        return;
    }
    
    // 更新位置数据
    _position.latitude = gnssData.latitude;
    _position.longitude = gnssData.longitude;
    _position.altitude = gnssData.altitude;
    Serial.printf("[PositionManager] Updated position from GNSS: lat=%.6f, lon=%.6f, alt=%.1f\n", _position.latitude, _position.longitude, _position.altitude);

    // 更新时间数据
    // 检查GNSS数据是否有效，以及日期是否有效
    Serial.printf("[PositionManager] GNSS date data: year=%d, month=%d, day=%d, dateValid=%d, isValid=%d\n", gnssData.year, gnssData.month, gnssData.day, gnssData.dateValid, gnssData.isValid);
    
    // 更严格的日期验证
    bool monthValid = gnssData.month >= 1 && gnssData.month <= 12;
    bool dayValid = gnssData.day >= 1 && gnssData.day <= 31;
    
    if (!gnssData.isValid || !gnssData.dateValid || !monthValid || !dayValid) {
        Serial.printf("[PositionManager] Date not valid from GNSS (monthValid=%d, dayValid=%d), ignoring time update\n", monthValid, dayValid);
        return;
    }
    
    // GNSS返回的是完整年份（如2026），需要转换为相对于2000年的偏移量（如26）
    // 检查年份是否在合理范围内（2000-2100）
    if (gnssData.year >= 2000 && gnssData.year <= 2100) {
        _time.year = gnssData.year - 2000;
        _time.month = gnssData.month;
        _time.day = gnssData.day;
        _time.hour = gnssData.hour;
        _time.minute = gnssData.minute;
        _time.second = gnssData.second;
        Serial.printf("[PositionManager] Updated time from GNSS: %04d-%02d-%02d %02d:%02d:%02d\n", _time.year + 2000, _time.month, _time.day, _time.hour, _time.minute, _time.second);
    } else if (gnssData.year >= 100 && gnssData.year < 2000) {
        // 处理1900-1999年的情况
        _time.year = gnssData.year - 1900;
        _time.month = gnssData.month;
        _time.day = gnssData.day;
        _time.hour = gnssData.hour;
        _time.minute = gnssData.minute;
        _time.second = gnssData.second;
        Serial.printf("[PositionManager] Updated time from GNSS: %04d-%02d-%02d %02d:%02d:%02d\n", _time.year + 1900, _time.month, _time.day, _time.hour, _time.minute, _time.second);
    } else if (gnssData.year > 0 && gnssData.year < 100) {
        // 已经是偏移量
        _time.year = gnssData.year;
        _time.month = gnssData.month;
        _time.day = gnssData.day;
        _time.hour = gnssData.hour;
        _time.minute = gnssData.minute;
        _time.second = gnssData.second;
        Serial.printf("[PositionManager] Updated time from GNSS: %04d-%02d-%02d %02d:%02d:%02d\n", _time.year + 2000, _time.month, _time.day, _time.hour, _time.minute, _time.second);
    } else {
        // 年份无效，不更新时间数据
        Serial.printf("[PositionManager] Invalid year from GNSS: %d, ignoring time update\n", gnssData.year);
    }
}

/**
 * @brief 计算时间戳
 * @param time 时间数据结构体
 * @return UTC时间戳（秒）
 */
uint32_t PositionManager::calculateTimestamp(TimeData time) {
    // 计算 Unix 时间戳（从 1970 年 1 月 1 日开始的秒数）
    uint16_t fullYear = 2000 + time.year;
    
    // 确保年份在合理范围内
    if (fullYear < 1970) {
        fullYear = 1970;
    } else if (fullYear > 2100) {
        fullYear = 2100;
    }
    
    // 计算从1970年到当前年份的天数
    uint32_t days = 0;
    for (uint16_t y = 1970; y < fullYear; y++) {
        // 闰年判断
        if ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) {
            days += 366;
        } else {
            days += 365;
        }
    }

    // 计算从当年1月1日到当前月份的天数
    uint8_t monthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    // 检查闰年
    if ((fullYear % 4 == 0 && fullYear % 100 != 0) || (fullYear % 400 == 0)) {
        monthDays[1] = 29;
    }
    for (uint8_t m = 0; m < time.month - 1; m++) {
        days += monthDays[m];
    }

    // 加上当前日期
    days += time.day - 1;

    // 计算总秒数（时间数据已经是UTC时间，不需要时区转换）
    uint32_t timestamp = days * 86400UL + time.hour * 3600UL + time.minute * 60UL + time.second;

    return timestamp;
}

/**
 * @brief 获取默认时间数据
 * @return 默认时间数据结构体
 */
TimeData PositionManager::getDefaultTime() {
    TimeData defaultTime;
    defaultTime.year = 26; // 2026年
    defaultTime.month = 5;
    defaultTime.day = 10;
    defaultTime.hour = 14; // 本地时间22:00 = UTC 14:00
    defaultTime.minute = 0;
    defaultTime.second = 0;
    return defaultTime;
}

/**
 * @brief 获取默认位置数据
 * @return 默认位置数据结构体
 */
PositionData PositionManager::getDefaultPosition() {
    PositionData defaultPosition;
    defaultPosition.latitude = 39.9042; // 北京纬度
    defaultPosition.longitude = 116.4074; // 北京经度
    defaultPosition.altitude = 0.0;
    return defaultPosition;
}
