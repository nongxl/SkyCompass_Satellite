#pragma once
#include <Arduino.h>
#include "../hal/hal_radio.h"
#include <vector>

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

private:
    // AX.25 协议解析
    static bool decodeAX25(const uint8_t* data, size_t len, DecodedTelemetry& out);

    // NORBI 专属 LoRa 遥测解析 (NORAD 46494)
    static bool decodeNORBI(const uint8_t* data, size_t len, DecodedTelemetry& out);

    // Vladivostok-1 / Geoscan 系列解析 (NORAD 61751)
    static bool decodeGeoscan(const uint8_t* data, size_t len, DecodedTelemetry& out);

    // FOSSASAT-2E 专属遥测解析 (NORAD 50463)
    static bool decodeFossaSat(const uint8_t* data, size_t len, DecodedTelemetry& out);
};
