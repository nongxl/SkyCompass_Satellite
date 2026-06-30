#pragma once
#include "orbit_parser.h"
#include <ArduinoJson.h>

class JSONParser : public OrbitParser {
public:
    bool parse(const String& input, OrbitRecord& record) override {
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
        
        String epochStr = doc["EPOCH"] | "";
        int year = 2000, month = 1, day = 1, hour = 0, minute = 0;
        double second = 0.0;
        if (epochStr.length() > 0 && parseIsoEpoch(epochStr, year, month, day, hour, minute, second)) {
            record.epochYr = year % 100;
            record.epochDays = calculateEpochDays(year, month, day, hour, minute, second);
            record.epochUnix = toUnixTimestamp(year, month, day, hour, minute, second);
        } else {
            // Fallback epoch
            record.epochYr = 26;
            record.epochDays = 1.0;
            record.epochUnix = 1767225600; // 2026-01-01 00:00:00 UTC
        }
        

        
        return true;
    }

private:
    static bool parseIsoEpoch(const String& iso, int& year, int& month, int& day, int& hour, int& minute, double& second) {
        if (iso.length() < 19) return false;
        year = iso.substring(0, 4).toInt();
        month = iso.substring(5, 7).toInt();
        day = iso.substring(8, 10).toInt();
        hour = iso.substring(11, 13).toInt();
        minute = iso.substring(14, 16).toInt();
        second = iso.substring(17).toDouble();
        return true;
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
