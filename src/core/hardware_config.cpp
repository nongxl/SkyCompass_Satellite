#include "hardware_config.h"

HardwareConfig::HardwareConfig() : _isConfigured(false) {
    for (int i = 0; i < HW_MOD_COUNT; i++) {
        _selected[i] = false;
    }
}

bool HardwareConfig::isConfigured() {
    Preferences prefs;
    if (prefs.begin("hw_cfg", true)) {
        _isConfigured = prefs.getBool("configured", false);
        prefs.end();
    }
    return _isConfigured;
}

void HardwareConfig::load() {
    Preferences prefs;
    if (prefs.begin("hw_cfg", true)) {
        _isConfigured = prefs.getBool("configured", false);
        if (_isConfigured) {
            uint8_t mask = prefs.getUChar("mask", 0);
            for (int i = 0; i < HW_MOD_COUNT; i++) {
                _selected[i] = (mask & (1 << i)) != 0;
            }
        } else {
            // 首次开机未保存过配置时的默认值（默认勾选 Cap LoRa-1262 与 8Servos 云台）
            _selected[HW_MOD_CAP_LORA1262] = true;
            _selected[HW_MOD_UNIT_GPSV11]  = false;
            _selected[HW_MOD_CHAIN_MONO]   = false;
            _selected[HW_MOD_UNIT_8SERVOS] = true;
        }
        prefs.end();
    }
}

void HardwareConfig::save() {
    Preferences prefs;
    if (prefs.begin("hw_cfg", false)) {
        uint8_t mask = 0;
        for (int i = 0; i < HW_MOD_COUNT; i++) {
            if (_selected[i]) {
                mask |= (1 << i);
            }
        }
        prefs.putUChar("mask", mask);
        prefs.putBool("configured", true);
        prefs.end();
        _isConfigured = true;
    }
}

bool HardwareConfig::validate(String& outError, Language lang) const {
    bool isZh = (lang == LANG_ZH);
    
    // 冲突 1：两个定位模块不可同时启用
    if (_selected[HW_MOD_CAP_LORA1262] && _selected[HW_MOD_UNIT_GPSV11]) {
        outError = isZh ? "冲突: 顶置Cap与Unit GPS定位模块互斥!" : "Conflict: Cap LoRa & Unit GPS conflict!";
        return false;
    }

    // 冲突 2：Unit GPS 占用了机身唯一 Grove 接口，不可同时外接副屏或舵机
    if (_selected[HW_MOD_UNIT_GPSV11]) {
        if (_selected[HW_MOD_CHAIN_MONO] || _selected[HW_MOD_UNIT_8SERVOS]) {
            outError = isZh ? "冲突: 机身Grove口被GPS独占, 无法接副屏/舵机!" : "Conflict: Grove occupied by Unit GPS!";
            return false;
        }
    }

    // 冲突 3：没有 Cap 扩展口时，机身 Grove 接口只能接一个设备（Chain Mono 与 8Servos 冲突）
    if (!_selected[HW_MOD_CAP_LORA1262]) {
        if (_selected[HW_MOD_CHAIN_MONO] && _selected[HW_MOD_UNIT_8SERVOS]) {
            outError = isZh ? "冲突: 无Cap时机身Grove口无法同时接屏幕和舵机!" : "Conflict: Grove port cannot share Screen & Servo!";
            return false;
        }
    }

    return true;
}

void HardwareConfig::getWiringGuide(std::vector<String>& outLines, Language lang) const {
    outLines.clear();
    bool isZh = (lang == LANG_ZH);

    bool hasCap = _selected[HW_MOD_CAP_LORA1262];
    bool hasGps = _selected[HW_MOD_UNIT_GPSV11];
    bool hasMono = _selected[HW_MOD_CHAIN_MONO];
    bool hasServo = _selected[HW_MOD_UNIT_8SERVOS];

    if (!hasCap && !hasGps && !hasMono && !hasServo) {
        outLines.push_back(isZh ? "【方案】纯单机模拟演示" : "[Plan] Standalone Simulator");
        outLines.push_back(isZh ? "● 无需连接任何外接硬件" : "● No external hardware needed");
        outLines.push_back(isZh ? "● 系统使用网络时间与内置/手动坐标" : "● Uses WiFi time & manual/cached pos");
        return;
    }

    // 识别方案名称
    if (hasCap && hasMono && hasServo) {
        outLines.push_back(isZh ? "【方案】★ 浑仪终极全家桶 (全满血)" : "[Plan] Ultimate Gimbal Suite");
    } else if (hasCap && hasServo) {
        outLines.push_back(isZh ? "【方案】浑仪机械云台追踪版" : "[Plan] Gimbal Mechanical Tracker");
    } else if (hasCap && hasMono) {
        outLines.push_back(isZh ? "【方案】便携双频GNSS+像素副屏版" : "[Plan] GNSS + Pixel Screen");
    } else if (hasCap) {
        outLines.push_back(isZh ? "【方案】Cap LoRa-1262 顶置定位版" : "[Plan] Cap LoRa GNSS Standalone");
    } else if (hasGps) {
        outLines.push_back(isZh ? "【方案】Unit GPS v1.1 徒步外置版" : "[Plan] Unit GPS Portable");
    } else if (hasMono) {
        outLines.push_back(isZh ? "【方案】独立像素副屏模式 (无GPS)" : "[Plan] Pixel Screen Only (No GPS)");
    } else if (hasServo) {
        outLines.push_back(isZh ? "【方案】独立浑仪舵机模式 (无GPS)" : "[Plan] Gimbal Servos Only (No GPS)");
    }

    // 各接口接线指引
    if (hasCap) {
        outLines.push_back(isZh ? "● 顶部槽: 扣接 CapLoRa-1262" : "● Top Slot: Attach CapLoRa-1262");
    }

    if (hasGps) {
        outLines.push_back(isZh ? "● 机身Grove口: 插接 Unit GPS v1.1" : "● Body Grove: Plug Unit GPS v1.1");
    }

    if (hasMono) {
        outLines.push_back(isZh ? "● 机身Grove口: 插接 Chain Mono (UART)" : "● Body Grove: Plug Chain Mono (UART)");
    }

    if (hasServo) {
        if (hasCap) {
            outLines.push_back(isZh ? "● Cap拓展口: 插接 Unit 8Servos (I2C)" : "● Cap Ext Port: Plug Unit 8Servos");
        } else {
            outLines.push_back(isZh ? "● 机身Grove口: 插接 Unit 8Servos (I2C)" : "● Body Grove: Plug Unit 8Servos");
        }
    }
}

const char* HardwareConfig::getModuleName(HardwareModule mod, Language lang) const {
    switch (mod) {
        case HW_MOD_CAP_LORA1262:
            return "CapLoRa-1262";
        case HW_MOD_UNIT_GPSV11:
            return "Unit GPS v1.1";
        case HW_MOD_CHAIN_MONO:
            return "Chain Mono";
        case HW_MOD_UNIT_8SERVOS:
            return "Unit 8Servos";
        default:
            return "";
    }
}

const char* HardwareConfig::getModuleSubtitle(HardwareModule mod, Language lang) const {
    bool isZh = (lang == LANG_ZH);
    switch (mod) {
        case HW_MOD_CAP_LORA1262:
            return isZh ? "顶置定位/LoRa通信槽" : "Top Slot GNSS/LoRa Cap";
        case HW_MOD_UNIT_GPSV11:
            return isZh ? "机身侧面外接定位模块" : "Side Grove GNSS Module";
        case HW_MOD_CHAIN_MONO:
            return isZh ? "8x8点阵过境像素副屏" : "8x8 Pixel Screen (UART)";
        case HW_MOD_UNIT_8SERVOS:
            return isZh ? "浑仪三轴机械云台舵机" : "3-Axis Gimbal Servo Driver";
        default:
            return "";
    }
}

const char* HardwareConfig::getModuleDescription(HardwareModule mod, Language lang) const {
    bool isZh = (lang == LANG_ZH);
    switch (mod) {
        case HW_MOD_CAP_LORA1262:
            return isZh ? "顶置槽插接，集成双频高精度GNSS与LoRa长距通信，自带HY2.0扩展口。" : "Top 14-pin slot. Integrated dual-band GNSS & LoRa, with HY2.0 ext port.";
        case HW_MOD_UNIT_GPSV11:
            return isZh ? "机身侧面Grove接口外接GNSS定位模块，独占UART通信与时钟同步。" : "Side Grove port external GNSS module, dedicated UART & clock sync.";
        case HW_MOD_CHAIN_MONO:
            return isZh ? "8x8单色点阵副屏，经Grove口UART通信，实时渲染卫星过境动画。" : "8x8 monochrome pixel display via Grove UART, real-time pass animations.";
        case HW_MOD_UNIT_8SERVOS:
            return isZh ? "I2C总线8路舵机驱动模块，配合浑仪机械结构实时三轴追焦过境卫星。" : "I2C 8-ch servo driver, controls 3-axis gimbal to track satellites.";
        default:
            return "";
    }
}

const uint16_t* HardwareConfig::getModuleImage(HardwareModule mod) const {
    switch (mod) {
        case HW_MOD_CAP_LORA1262:
            return img_hw_cap_lora1262;
        case HW_MOD_UNIT_GPSV11:
            return img_hw_unit_gpsv11;
        case HW_MOD_CHAIN_MONO:
            return img_hw_chain_mono;
        case HW_MOD_UNIT_8SERVOS:
            return img_hw_unit_8servos;
        default:
            return nullptr;
    }
}
