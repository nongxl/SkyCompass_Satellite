#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

#include <Arduino.h>
#include <Preferences.h>
#include <vector>
#include "hardware_module_images.h"
#include "i18n.h"

enum HardwareModule {
    HW_MOD_CAP_LORA1262 = 0, // Cap LoRa-1262 顶置定位模块
    HW_MOD_UNIT_GPSV11  = 1, // Unit GPS v1.1 侧面定位模块
    HW_MOD_CHAIN_MONO   = 2, // Chain Mono 8x8 像素副屏
    HW_MOD_UNIT_8SERVOS = 3, // Unit 8Servos 浑仪舵机云台
    HW_MOD_COUNT        = 4
};

class HardwareConfig {
public:
    static HardwareConfig& getInstance() {
        static HardwareConfig instance;
        return instance;
    }

    // NVS 配置读写与检查
    bool isConfigured();
    void load();
    void save();

    // 模块勾选状态
    bool isEnabled(HardwareModule mod) const {
        if (mod >= 0 && mod < HW_MOD_COUNT) return _selected[mod];
        return false;
    }

    void setEnabled(HardwareModule mod, bool enabled) {
        if (mod >= 0 && mod < HW_MOD_COUNT) _selected[mod] = enabled;
    }

    void toggle(HardwareModule mod) {
        if (mod >= 0 && mod < HW_MOD_COUNT) _selected[mod] = !_selected[mod];
    }

    // 冲突校验：返回当前组合是否合法，若冲突则返回错误原因
    bool validate(String& outError, Language lang = LANG_ZH) const;

    // 动态生成接线指引列表
    void getWiringGuide(std::vector<String>& outLines, Language lang = LANG_ZH) const;

    // 模块元数据获取
    const char* getModuleName(HardwareModule mod, Language lang = LANG_ZH) const;
    const char* getModuleSubtitle(HardwareModule mod, Language lang = LANG_ZH) const;
    const char* getModuleDescription(HardwareModule mod, Language lang = LANG_ZH) const;
    const uint16_t* getModuleImage(HardwareModule mod) const;

private:
    HardwareConfig();
    bool _selected[HW_MOD_COUNT];
    bool _isConfigured;
};

#endif // HARDWARE_CONFIG_H
