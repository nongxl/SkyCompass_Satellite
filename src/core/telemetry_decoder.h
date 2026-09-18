#pragma once
#include <Arduino.h>
#include "../hal/hal_radio.h"
#include <vector>

enum FieldFormat {
    FMT_UINT8 = 0,
    FMT_INT8,
    FMT_UINT16_LE,
    FMT_UINT16_BE,
    FMT_INT16_LE,
    FMT_INT16_BE,
    FMT_UINT32_LE,
    FMT_UINT32_BE
};

struct TelemetryFieldRule {
    String label;           // 物理量显示标签 (如 "Vbat", "Temp", "Current")
    uint8_t byteOffset;     // 字节偏移 (从 0 开始)
    FieldFormat format;     // 字段编码格式与字节序
    float multiplier;       // 乘数系数 (如 0.001)
    float offset;           // 偏差偏移量 (如 0.0)
    String unit;            // 单位 (如 "V", "°C", "mA")
    uint8_t decimals;       // 浮点显示小数位数
};

struct SatelliteTlmProfile {
    uint32_t noradId = 0;
    String satName;
    String frameType;
    std::vector<TelemetryFieldRule> rules;
};

struct TelemetryField {
    String key;
    String value;
    String unit;
};

struct DecodedTelemetry {
    bool isValid = false;
    String satName;
    uint32_t noradId = 0;
    String frameType;       // "AX.25 UI", "LoRa Beacon", "Custom FSK", etc.
    String sourceCall;
    String destCall;
    std::vector<TelemetryField> fields;
    String rawHex;
    String rawAscii;
};

class TelemetryDecoder {
public:
    // 解析无线电数据包，并填充 DecodedTelemetry
    static DecodedTelemetry decode(const RadioPacket& packet, uint32_t expectedNorad = 0);

    // 动态注册新卫星的数据字典规则
    static void registerProfile(const SatelliteTlmProfile& profile);

    // 获取已注册的所有卫星遥测规则
    static const std::vector<SatelliteTlmProfile>& getBuiltinProfiles();

private:
    // 通用基于规则的数据字典解析引擎
    static bool decodeByProfile(const SatelliteTlmProfile& profile, const uint8_t* data, size_t len, DecodedTelemetry& out);

    // AX.25 UI-frame 协议基础解包
    static bool decodeAX25(const uint8_t* data, size_t len, DecodedTelemetry& out);
};
