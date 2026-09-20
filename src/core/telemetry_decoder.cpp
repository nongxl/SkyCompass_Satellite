#include "telemetry_decoder.h"

static std::vector<SatelliteTlmProfile> s_customProfiles;

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

const std::vector<SatelliteTlmProfile>& TelemetryDecoder::getBuiltinProfiles() {
    static std::vector<SatelliteTlmProfile> s_builtinProfiles;
    static bool s_initialized = false;
    if (!s_initialized) {
        // 1. NORBI (NORAD 46494) 436.700 MHz LoRa 立方星数据字典
        SatelliteTlmProfile norbi;
        norbi.noradId = 46494;
        norbi.satName = "NORBI";
        norbi.frameType = "NORBI LoRa TLM";
        norbi.rules.push_back({"Frame#", 0, FMT_UINT16_LE, 1.0f, 0.0f, "", 0});
        norbi.rules.push_back({"Vbat", 2, FMT_UINT16_LE, 0.001f, 0.0f, "V", 2});
        norbi.rules.push_back({"Temp", 4, FMT_INT8, 1.0f, 0.0f, "C", 0});
        norbi.rules.push_back({"I_bus", 5, FMT_UINT16_LE, 0.1f, 0.0f, "mA", 1});
        s_builtinProfiles.push_back(norbi);

        // 2. Vladivostok-1 / Geoscan 系列 (NORAD 61751)
        SatelliteTlmProfile geoscan;
        geoscan.noradId = 61751;
        geoscan.satName = "Vladivostok-1";
        geoscan.frameType = "Geoscan LoRa";
        geoscan.rules.push_back({"Vbat", 1, FMT_UINT16_LE, 0.001f, 0.0f, "V", 2});
        geoscan.rules.push_back({"Temp", 3, FMT_INT8, 1.0f, 0.0f, "C", 0});
        s_builtinProfiles.push_back(geoscan);

        // 3. FOSSASAT-2E (NORAD 50463)
        SatelliteTlmProfile fossa;
        fossa.noradId = 50463;
        fossa.satName = "FOSSASAT-2E";
        fossa.frameType = "FOSSA LoRa TLM";
        fossa.rules.push_back({"Vbat", 1, FMT_UINT16_LE, 0.001f, 0.0f, "V", 2});
        fossa.rules.push_back({"Temp", 3, FMT_INT8, 1.0f, 0.0f, "C", 0});
        s_builtinProfiles.push_back(fossa);

        // 4. Geoscan-Edelveis (NORAD 53385)
        SatelliteTlmProfile edelveis;
        edelveis.noradId = 53385;
        edelveis.satName = "Edelveis";
        edelveis.frameType = "Geoscan LoRa";
        edelveis.rules.push_back({"Vbat", 1, FMT_UINT16_LE, 0.001f, 0.0f, "V", 2});
        edelveis.rules.push_back({"Temp", 3, FMT_INT8, 1.0f, 0.0f, "C", 0});
        s_builtinProfiles.push_back(edelveis);

        // 5. PROVES-Electra (NORAD 69795) / PySquared 平台
        SatelliteTlmProfile proves;
        proves.noradId = 69795;
        proves.satName = "PROVES-Electra";
        proves.frameType = "PROVES PySquared";
        proves.rules.push_back({"Vbat", 1, FMT_UINT16_LE, 0.001f, 0.0f, "V", 2});
        proves.rules.push_back({"Temp", 3, FMT_INT8, 1.0f, 0.0f, "C", 0});
        proves.rules.push_back({"I_bus", 4, FMT_UINT16_LE, 0.1f, 0.0f, "mA", 1});
        proves.rules.push_back({"Reboot", 6, FMT_UINT16_LE, 1.0f, 0.0f, "", 0});
        s_builtinProfiles.push_back(proves);

        s_initialized = true;
    }
    return s_builtinProfiles;
}

void TelemetryDecoder::registerProfile(const SatelliteTlmProfile& profile) {
    s_customProfiles.push_back(profile);
}

bool TelemetryDecoder::decodeByProfile(const SatelliteTlmProfile& profile, const uint8_t* data, size_t len, DecodedTelemetry& out) {
    if (len < 6) return false;

    out.satName = profile.satName;
    out.noradId = profile.noradId;
    out.frameType = profile.frameType;

    for (const auto& rule : profile.rules) {
        size_t reqLen = 0;
        switch (rule.format) {
            case FMT_UINT8:
            case FMT_INT8:
                reqLen = 1;
                break;
            case FMT_UINT16_LE:
            case FMT_UINT16_BE:
            case FMT_INT16_LE:
            case FMT_INT16_BE:
                reqLen = 2;
                break;
            case FMT_UINT32_LE:
            case FMT_UINT32_BE:
                reqLen = 4;
                break;
        }

        if (rule.byteOffset + reqLen <= len) {
            double rawVal = 0.0;
            const uint8_t* p = data + rule.byteOffset;
            switch (rule.format) {
                case FMT_UINT8:
                    rawVal = *p;
                    break;
                case FMT_INT8:
                    rawVal = (int8_t)*p;
                    break;
                case FMT_UINT16_LE:
                    rawVal = (uint16_t)(p[0] | (p[1] << 8));
                    break;
                case FMT_UINT16_BE:
                    rawVal = (uint16_t)((p[0] << 8) | p[1]);
                    break;
                case FMT_INT16_LE:
                    rawVal = (int16_t)(p[0] | (p[1] << 8));
                    break;
                case FMT_INT16_BE:
                    rawVal = (int16_t)((p[0] << 8) | p[1]);
                    break;
                case FMT_UINT32_LE:
                    rawVal = (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
                    break;
                case FMT_UINT32_BE:
                    rawVal = (uint32_t)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
                    break;
            }

            double physicalVal = rawVal * (double)rule.multiplier + (double)rule.offset;
            String valStr = (rule.decimals == 0) ? String((long)round(physicalVal)) : String((float)physicalVal, (unsigned int)rule.decimals);
            out.fields.push_back({rule.label, valStr, rule.unit});
        }
    }

    out.isValid = true;
    return true;
}

bool TelemetryDecoder::decodeAX25(const uint8_t* data, size_t len, DecodedTelemetry& out) {
    if (len < 16) return false;

    // 检查是否有 AX.25 帧特征 (Dest 7B, Src 7B, Control 1B, PID 1B)
    bool hasRepeaters = (data[13] & 0x01) == 0;
    size_t headerLen = 14;

    if (hasRepeaters) {
        while (headerLen + 7 <= len) {
            bool last = (data[headerLen + 6] & 0x01) != 0;
            headerLen += 7;
            if (last) break;
        }
    }

    if (headerLen + 2 > len) return false;

    uint8_t control = data[headerLen];
    uint8_t pid = data[headerLen + 1];

    if (control == 0x03 && pid == 0xF0) {
        out.destCall = parseCallsign(data);
        out.sourceCall = parseCallsign(data + 7);
        out.frameType = "AX.25 UI";

        const uint8_t* payload = data + headerLen + 2;
        size_t payloadLen = len - (headerLen + 2);

        out.fields.push_back({"Dest", out.destCall, ""});
        out.fields.push_back({"Source", out.sourceCall, ""});
        out.fields.push_back({"PayloadLen", String((int)payloadLen), "bytes"});

        String asciiPayload = toAscii(payload, payloadLen);
        out.fields.push_back({"Text", asciiPayload, ""});

        out.isValid = true;
        return true;
    }

    return false;
}

DecodedTelemetry TelemetryDecoder::decode(const RadioPacket& packet, uint32_t expectedNorad) {
    DecodedTelemetry result;
    result.rawHex = toHex(packet.payload, packet.length);
    result.rawAscii = toAscii(packet.payload, packet.length);

    // 通用基础射频指标
    result.fields.push_back({"Freq", String(packet.freqMHz, 4), "MHz"});
    result.fields.push_back({"RSSI", String(packet.rssi, 1), "dBm"});
    result.fields.push_back({"SNR", String(packet.snr, 1), "dB"});

    // 1. 优先匹配 AX.25 UI-frame
    if (decodeAX25(packet.payload, packet.length, result)) {
        return result;
    }

    // 2. 自定义注册规则匹配
    for (const auto& prof : s_customProfiles) {
        if (expectedNorad > 0 && prof.noradId == expectedNorad) {
            if (decodeByProfile(prof, packet.payload, packet.length, result)) return result;
        }
    }

    // 3. 内置数据字典规则匹配
    const auto& builtin = getBuiltinProfiles();
    for (const auto& prof : builtin) {
        if (expectedNorad > 0 && prof.noradId == expectedNorad) {
            if (decodeByProfile(prof, packet.payload, packet.length, result)) return result;
        }
    }

    // 4. 通用兜底解析
    result.satName = (expectedNorad > 0) ? ("NORAD " + String(expectedNorad)) : "Unknown Sat";
    result.noradId = expectedNorad;
    result.frameType = "Raw Packet";
    result.isValid = true;
    result.fields.push_back({"DataLen", String((int)packet.length), "bytes"});

    return result;
}
