#pragma once
#include "orbit_parser.h"
#include <ArduinoJson.h>
#include <cstdlib>
#include <cstring>

class JSONParser : public OrbitParser {
public:
    bool parse(const String& input, OrbitRecord& record) override {
        return parse(input.c_str(), record);
    }

    bool parse(const char* input, OrbitRecord& record) {
        if (!input) return false;
        while (*input == ' ' || *input == '\t' || *input == '\r' || *input == '\n') {
            ++input;
        }
        if (*input == '\0') return false;

        // 若不以 '{' 开头，按 CelesTrak GP CSV 格式高速零堆内存解析（体积缩小近 3 倍）
        if (*input != '{') {
            return parseCsv(input, record);
        }

        // ArduinoJson document to parse single satellite JSON
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, input);
        if (error) {
            return false;
        }
        
        // CelesTrak OMM / GP JSON standard fields
        record.catalogNumber = doc["NORAD_CAT_ID"] | doc["CATALOG_NUMBER"] | 0;
        if (record.catalogNumber == 0) return false;
        
        String name = doc["OBJECT_NAME"] | "UNKNOWN";
        name.trim();
        record.name = name;
        
        String intlDesig = doc["OBJECT_ID"] | doc["INTERNATIONAL_DESIGNATOR"] | "";
        intlDesig.trim();
        strncpy(record.internationalDesignator, intlDesig.c_str(), sizeof(record.internationalDesignator) - 1);
        record.internationalDesignator[sizeof(record.internationalDesignator) - 1] = '\0';
        
        record.inclination = doc["INCLINATION"] | 0.0;
        record.eccentricity = doc["ECCENTRICITY"] | 0.0;
        record.meanMotion = doc["MEAN_MOTION"] | 0.0;
        record.meanAnomaly = doc["MEAN_ANOMALY"] | 0.0;
        record.argumentOfPerigee = doc["ARG_OF_PERICENTER"] | doc["ARGUMENT_OF_PERIGEE"] | 0.0;
        record.raan = doc["RA_OF_ASC_NODE"] | doc["RAAN"] | 0.0;
        record.bstar = doc["BSTAR"] | 0.0;
        
        const char* epochCStr = doc["EPOCH"] | "";
        applyEpochCStr(epochCStr, strlen(epochCStr), record);
        return true;
    }

private:
    static bool parseCsv(const char* line, OrbitRecord& record) {
        // 跳过 CSV 表头行: OBJECT_NAME,OBJECT_ID,EPOCH,...
        if (strncmp(line, "OBJECT_NAME", 11) == 0) return false;

        const char* colStart[17];
        size_t colLen[17];
        int colIdx = 0;
        const char* p = line;

        while (*p != '\0' && *p != '\r' && *p != '\n' && colIdx < 17) {
            if (*p == '"') {
                ++p;
                colStart[colIdx] = p;
                while (*p != '\0' && *p != '\r' && *p != '\n') {
                    if (*p == '"') {
                        if (*(p + 1) == '"') {
                            p += 2;
                            continue;
                        }
                        break;
                    }
                    ++p;
                }
                colLen[colIdx] = (size_t)(p - colStart[colIdx]);
                if (*p == '"') ++p;
                while (*p != '\0' && *p != ',' && *p != '\r' && *p != '\n') ++p;
            } else {
                colStart[colIdx] = p;
                while (*p != '\0' && *p != ',' && *p != '\r' && *p != '\n') ++p;
                colLen[colIdx] = (size_t)(p - colStart[colIdx]);
            }
            ++colIdx;
            if (*p == ',') {
                ++p;
            } else {
                break;
            }
        }

        if (colIdx < 15) return false;

        // col[11]: NORAD_CAT_ID
        char numBuf[32];
        auto parseDoubleCol = [&](int idx) -> double {
            size_t len = colLen[idx] < sizeof(numBuf) - 1 ? colLen[idx] : sizeof(numBuf) - 1;
            memcpy(numBuf, colStart[idx], len);
            numBuf[len] = '\0';
            return strtod(numBuf, nullptr);
        };
        auto parseUintCol = [&](int idx) -> uint32_t {
            size_t len = colLen[idx] < sizeof(numBuf) - 1 ? colLen[idx] : sizeof(numBuf) - 1;
            memcpy(numBuf, colStart[idx], len);
            numBuf[len] = '\0';
            return (uint32_t)strtoul(numBuf, nullptr, 10);
        };

        record.catalogNumber = parseUintCol(11);
        if (record.catalogNumber == 0) return false;

        // col[0]: OBJECT_NAME (trim spaces)
        const char* ns = colStart[0];
        size_t nl = colLen[0];
        while (nl > 0 && (*ns == ' ' || *ns == '\t')) { ++ns; --nl; }
        while (nl > 0 && (ns[nl - 1] == ' ' || ns[nl - 1] == '\t')) { --nl; }
        if (nl == 0) {
            record.name = "UNKNOWN";
        } else {
            char nameBuf[64];
            size_t copyLen = nl < sizeof(nameBuf) - 1 ? nl : sizeof(nameBuf) - 1;
            memcpy(nameBuf, ns, copyLen);
            nameBuf[copyLen] = '\0';
            record.name = nameBuf;
        }

        // col[1]: OBJECT_ID (International Designator)
        const char* idS = colStart[1];
        size_t idL = colLen[1];
        while (idL > 0 && (*idS == ' ' || *idS == '\t')) { ++idS; --idL; }
        while (idL > 0 && (idS[idL - 1] == ' ' || idS[idL - 1] == '\t')) { --idL; }
        size_t maxId = sizeof(record.internationalDesignator) - 1;
        if (idL > maxId) idL = maxId;
        memcpy(record.internationalDesignator, idS, idL);
        record.internationalDesignator[idL] = '\0';

        // Keplerian Elements
        record.meanMotion        = parseDoubleCol(3);
        record.eccentricity      = parseDoubleCol(4);
        record.inclination       = parseDoubleCol(5);
        record.raan              = parseDoubleCol(6);
        record.argumentOfPerigee = parseDoubleCol(7);
        record.meanAnomaly       = parseDoubleCol(8);
        record.bstar             = parseDoubleCol(14);

        // col[2]: EPOCH
        applyEpochCStr(colStart[2], colLen[2], record);
        return true;
    }

    static void applyEpochCStr(const char* iso, size_t len, OrbitRecord& record) {
        int year = 2000, month = 1, day = 1, hour = 0, minute = 0;
        double second = 0.0;
        if (iso && len >= 19 && parseIsoEpochC(iso, len, year, month, day, hour, minute, second)) {
            record.epochYr = year % 100;
            record.epochDays = calculateEpochDays(year, month, day, hour, minute, second);
            record.epochUnix = toUnixTimestamp(year, month, day, hour, minute, second);
        } else {
            record.epochYr = 26;
            record.epochDays = 1.0;
            record.epochUnix = 1767225600; // 2026-01-01 00:00:00 UTC
        }
    }

    static bool parseIsoEpochC(const char* iso, size_t len, int& year, int& month, int& day, int& hour, int& minute, double& second) {
        if (len < 19) return false;
        auto twoDig = [](const char* p) -> int { return (p[0] - '0') * 10 + (p[1] - '0'); };
        year = (iso[0] - '0') * 1000 + (iso[1] - '0') * 100 + (iso[2] - '0') * 10 + (iso[3] - '0');
        month = twoDig(iso + 5);
        day = twoDig(iso + 8);
        hour = twoDig(iso + 11);
        minute = twoDig(iso + 14);
        char secBuf[16];
        size_t secLen = len - 17;
        if (secLen >= sizeof(secBuf)) secLen = sizeof(secBuf) - 1;
        memcpy(secBuf, iso + 17, secLen);
        secBuf[secLen] = '\0';
        second = strtod(secBuf, nullptr);
        return (year >= 1957 && month >= 1 && month <= 12 && day >= 1 && day <= 31);
    }
    
    static double calculateEpochDays(int year, int month, int day, int hour, int minute, double second) {
        auto isLeap = [](int y) {
            return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
        };
        int daysInMonth[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        if (isLeap(year)) {
            daysInMonth[1] = 29;
        }
        double days = day - 1;
        for (int m = 0; m < month - 1; ++m) {
            days += daysInMonth[m];
        }
        days += (hour / 24.0) + (minute / 1440.0) + (second / 86400.0);
        return days + 1.0;
    }
    
    static uint32_t toUnixTimestamp(int year, int month, int day, int hour, int minute, double second) {
        auto isLeap = [](int y) {
            return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
        };
        uint32_t seconds = 0;
        for (int y = 1970; y < year; ++y) {
            seconds += isLeap(y) ? 366 * 86400 : 365 * 86400;
        }
        int daysInMonth[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        if (isLeap(year)) daysInMonth[1] = 29;
        for (int m = 0; m < month - 1; ++m) {
            seconds += daysInMonth[m] * 86400;
        }
        seconds += (day - 1) * 86400;
        seconds += hour * 3600;
        seconds += minute * 60;
        seconds += (uint32_t)second;
        return seconds;
    }

};
