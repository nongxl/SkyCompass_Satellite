#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include <Arduino.h>

/**
 * @brief 日志管理器类
 * 统一管理所有模块的日志输出
 */
class LogManager {
private:
    // 日志控制参数
    bool _enabled;              // 是否启用日志
    
    // 各个模块的日志状态
    struct ModuleLogState {
        uint32_t lastLogTime;    // 模块上次日志时间
        uint32_t logInterval;    // 模块日志间隔
        bool enabled;            // 模块日志是否启用
    };
    
    // 模块日志状态
    ModuleLogState _sunLogState;
    ModuleLogState _moonLogState;
    ModuleLogState _galaxyLogState;
    
    /**
     * @brief 私有构造函数
     */
    LogManager() {
        _enabled = true;
        
        // 初始化模块日志状态
        _sunLogState.lastLogTime = 0;
        _sunLogState.logInterval = 5000; // 太阳5秒
        _sunLogState.enabled = true;
        
        _moonLogState.lastLogTime = 0;
        _moonLogState.logInterval = 5000; // 月亮5秒
        _moonLogState.enabled = true;
        
        _galaxyLogState.lastLogTime = 0;
        _galaxyLogState.logInterval = 5000; // 银河5秒
        _galaxyLogState.enabled = true;
    }
    
public:
    /**
     * @brief 获取单例实例
     * @return 日志管理器实例
     */
    static LogManager& getInstance() {
        static LogManager instance;
        return instance;
    }
    
    /**
     * @brief 启用/禁用所有日志
     * @param enabled 是否启用
     */
    void enableLog(bool enabled) {
        _enabled = enabled;
    }
    
    /**
     * @brief 检查是否应该输出太阳日志
     * @return 是否应该输出
     */
    bool shouldLogSun() {
        if (!_enabled || !_sunLogState.enabled) return false;
        
        uint32_t currentTime = millis();
        if (currentTime - _sunLogState.lastLogTime >= _sunLogState.logInterval) {
            _sunLogState.lastLogTime = currentTime;
            return true;
        }
        return false;
    }
    
    /**
     * @brief 检查是否应该输出月亮日志
     * @return 是否应该输出
     */
    bool shouldLogMoon() {
        if (!_enabled || !_moonLogState.enabled) return false;
        
        uint32_t currentTime = millis();
        if (currentTime - _moonLogState.lastLogTime >= _moonLogState.logInterval) {
            _moonLogState.lastLogTime = currentTime;
            return true;
        }
        return false;
    }
    
    /**
     * @brief 检查是否应该输出银河日志
     * @return 是否应该输出
     */
    bool shouldLogGalaxy() {
        if (!_enabled || !_galaxyLogState.enabled) return false;
        
        uint32_t currentTime = millis();
        if (currentTime - _galaxyLogState.lastLogTime >= _galaxyLogState.logInterval) {
            _galaxyLogState.lastLogTime = currentTime;
            return true;
        }
        return false;
    }
    
    /**
     * @brief 设置太阳日志间隔
     * @param interval 间隔（毫秒）
     */
    void setSunLogInterval(uint32_t interval) {
        _sunLogState.logInterval = interval;
    }
    
    /**
     * @brief 设置月亮日志间隔
     * @param interval 间隔（毫秒）
     */
    void setMoonLogInterval(uint32_t interval) {
        _moonLogState.logInterval = interval;
    }
    
    /**
     * @brief 设置银河日志间隔
     * @param interval 间隔（毫秒）
     */
    void setGalaxyLogInterval(uint32_t interval) {
        _galaxyLogState.logInterval = interval;
    }
    
    /**
     * @brief 启用/禁用太阳日志
     * @param enabled 是否启用
     */
    void enableSunLog(bool enabled) {
        _sunLogState.enabled = enabled;
    }
    
    /**
     * @brief 启用/禁用月亮日志
     * @param enabled 是否启用
     */
    void enableMoonLog(bool enabled) {
        _moonLogState.enabled = enabled;
    }
    
    /**
     * @brief 启用/禁用银河日志
     * @param enabled 是否启用
     */
    void enableGalaxyLog(bool enabled) {
        _galaxyLogState.enabled = enabled;
    }
};

#endif // LOG_MANAGER_H