#include "telemetry_decoder.h"

static String toHex(const uint8_t* data, size_t len) {
    String res = "";
    char buf[4];
    for (size_t i = 0; i < len; i++) {
        sprintf(buf, "%02X ", data[i]);
        res += buf;
    }
    return res;
}

static String toAscii(const uint8_t* data, size_t len) {
    String res = "";
    for (size_t i = 0; i < len; i++) {
        char c = (char)data[i];
        if (c >= 32 && c <= 126) {
            res += c;
        } else {
            res += '.';
        }
    }
    return res;
}

static String parseCallsign(const uint8_t* raw) {
    String call = "";
    for (int i = 0; i < 6; i++) {
        char c = (char)(raw[i] >> 1);
        if (c > ' ' && c <= 'Z') {
            call += c;
        }
    }
    uint8_t ssid = (raw[6] >> 1) & 0x0F;
    if (ssid > 0) {
        call += "-" + String(ssid);
    }
    return call;
}

bool TelemetryDecoder::decodeAX25(const uint8_t* data, size_t len, DecodedTelemetry& out) {
    if (len < 16) return false;

    // 检查是否有 AX.25 帧特征 (Dest 7B, Src 7B, Control 1B, PID 1B)
    // 最后一字节的最低位判断是否有后续地址扩展
    bool hasRepeaters = (data[13] & 0x01) == 0;
    size_t headerLen = 14;

    if (hasRepeaters) {
        // 最多跳过 2 个中继站 (各 7 字节)
        while (headerLen + 7 <= len) {
            bool last = (data[headerLen + 6] & 0x01) != 0;
            headerLen += 7;
            if (last) break;
        }
    }

    if (headerLen + 2 > len) return false;

    uint8_t control = data[headerLen];
    uint8_t pid = data[headerLen + 1];

    // UI-frame: control 通常为 0x03 (UI-frame unnumbered information)
    if (control == 0x03 && pid == 0xF0) {
        out.destCall = parseCallsign(data);
        out.sourceCall = parseCallsign(data + 7);
        out.frameType = "AX.25 UI";

        const uint8_t* payload = data + headerLen + 2;
        size_t payloadLen = len - (headerLen + 2);

        out.fields.push_back({"Dest", out.destCall, ""});
        out.fields.push_back({"Source", out.sourceCall, ""});
        out.fields.push_back({"PayloadLen", String((int)payloadLen), "bytes"});

        // 尝试检查是否有可读文本
        String asciiPayload = toAscii(payload, payloadLen);
        out.fields.push_back({"Text", asciiPayload, ""});

        out.isValid = true;
        return true;
    }

    return false;
}

bool TelemetryDecoder::decodeNORBI(const uint8_t* data, size_t len, DecodedTelemetry& out) {
    // NORBI (46494) 常用 436.700 MHz LoRa
    // 典型信标包长度为 48~64 字节
    if (len < 16) return false;

    out.satName = "NORBI";
    out.noradId = 46494;
    out.frameType = "NORBI LoRa TLM";

    // 简单解析常见字段 (帧序号, 供电电压, 温度)
    uint16_t packetNum = (data[1] << 8) | data[0];
    uint16_t vbatMv = (data[3] << 8) | data[2];
    int16_t tempC = (int8_t)data[4];

    out.fields.push_back({"Frame#", String(packetNum), ""});
    if (vbatMv > 2000 && vbatMv < 10000) {
        out.fields.push_back({"Vbat", String(vbatMv / 1000.0f, 2), "V"});
    }
    out.fields.push_back({"Temp", String((int)tempC), "C"});

    out.isValid = true;
    return true;
}

bool TelemetryDecoder::decodeGeoscan(const uint8_t* data, size_t len, DecodedTelemetry& out) {
    // Geoscan / Vladivostok-1 (61751)
    out.satName = "Vladivostok-1";
    out.noradId = 61751;
    out.frameType = "Geoscan LoRa";

    // 提取可能包含的信标
    if (len >= 8) {
        uint16_t vbat = (data[2] << 8) | data[1];
        if (vbat > 3000 && vbat < 9000) {
            out.fields.push_back({"Vbat", String(vbat / 1000.0f, 2), "V"});
        }
    }
    out.fields.push_back({"Len", String((int)len), "B"});
    out.isValid = true;
    return true;
}

bool TelemetryDecoder::decodeFossaSat(const uint8_t* data, size_t len, DecodedTelemetry& out) {
    // FOSSASAT-2E (50463)
    out.satName = "FOSSASAT-2E";
    out.noradId = 50463;
    out.frameType = "FOSSA FSK/LoRa";
    out.fields.push_back({"Len", String((int)len), "B"});
    out.isValid = true;
    return true;
}

DecodedTelemetry TelemetryDecoder::decode(const RadioPacket& packet, uint32_t expectedNorad) {
    DecodedTelemetry result;
    result.rawHex = toHex(packet.payload, packet.length);
    result.rawAscii = toAscii(packet.payload, packet.length);

    // 通用基础射频指标
    result.fields.push_back({"Freq", String(packet.freqMHz, 4), "MHz"});
    result.fields.push_back({"RSSI", String(packet.rssi, 1), "dBm"});
    result.fields.push_back({"SNR", String(packet.snr, 1), "dB"});

    // 1. 优先尝试 AX.25 UI-frame
    if (decodeAX25(packet.payload, packet.length, result)) {
        return result;
    }

    // 2. 根据 expectedNorad 定向解析
    if (expectedNorad == 46494) {
        if (decodeNORBI(packet.payload, packet.length, result)) return result;
    } else if (expectedNorad == 61751 || expectedNorad == 57172 || expectedNorad == 57179) {
        if (decodeGeoscan(packet.payload, packet.length, result)) return result;
    } else if (expectedNorad == 50463) {
        if (decodeFossaSat(packet.payload, packet.length, result)) return result;
    }

    // 3. 通用兜底解析
    result.satName = (expectedNorad > 0) ? ("NORAD " + String(expectedNorad)) : "Unknown Sat";
    result.noradId = expectedNorad;
    result.frameType = "Raw Packet";
    result.isValid = true;
    result.fields.push_back({"DataLen", String((int)packet.length), "bytes"});

    return result;
}
