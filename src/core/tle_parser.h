#pragma once
#include "orbit_parser.h"
#include <vector>

class TLEParser : public OrbitParser {
public:
    bool parse(const String& input, OrbitRecord& record) override {
        std::vector<String> lines;
        int start = 0;
        while (start < (int)input.length()) {
            int end = input.indexOf('\n', start);
            if (end == -1) {
                lines.push_back(input.substring(start));
                break;
            }
            lines.push_back(input.substring(start, end));
            start = end + 1;
        }
        
        for (auto& l : lines) {
            l.trim();
        }
        
        String name = "";
        String l1 = "";
        String l2 = "";
        if (lines.size() >= 3) {
            name = lines[0];
            l1 = lines[1];
            l2 = lines[2];
        } else if (lines.size() == 2) {
            l1 = lines[0];
            l2 = lines[1];
            name = "UNKNOWN";
        } else {
            return false;
        }
        
        if (l1.length() < 69 || l2.length() < 69 || l1[0] != '1' || l2[0] != '2') {
            return false;
        }
        
        record.catalogNumber = l1.substring(2, 7).toInt();
        record.name = name;
        
        String intldesg = l1.substring(9, 17);
        intldesg.trim();
        strncpy(record.internationalDesignator, intldesg.c_str(), sizeof(record.internationalDesignator) - 1);
        record.internationalDesignator[sizeof(record.internationalDesignator) - 1] = '\0';
        
        record.inclination = l2.substring(8, 16).toDouble();
        record.raan = l2.substring(17, 25).toDouble();
        record.eccentricity = ("0." + l2.substring(26, 33)).toDouble();
        record.argumentOfPerigee = l2.substring(34, 42).toDouble();
        record.meanAnomaly = l2.substring(43, 51).toDouble();
        record.meanMotion = l2.substring(52, 63).toDouble();
        
        // BStar drag term parsing
        String bstarStr = l1.substring(53, 61);
        bstarStr.trim();
        double bstarVal = 0.0;
        if (bstarStr.length() >= 6) {
            int minusIdx = bstarStr.lastIndexOf('-');
            int plusIdx = bstarStr.lastIndexOf('+');
            int expIdx = (minusIdx > 0) ? minusIdx : plusIdx;
            
            if (expIdx > 0 && expIdx < (int)bstarStr.length() - 1) {
                String mantissaStr = bstarStr.substring(0, expIdx);
                String expStr = bstarStr.substring(expIdx);
                mantissaStr.trim();
                
                double m = mantissaStr.toDouble();
                // TLE format implied decimal point
                if (mantissaStr.startsWith("-")) {
                    m = -abs(m) / 100000.0;
                } else if (mantissaStr.startsWith("+")) {
                    m = abs(m) / 100000.0;
                } else {
                    m = m / 100000.0;
                }
                int e = expStr.toInt();
                bstarVal = m * pow(10, e);
            }
        }
        record.bstar = bstarVal;
        
        record.epochYr = l1.substring(18, 20).toInt();
        record.epochDays = l1.substring(20, 32).toDouble();
        record.epochUnix = parseTleEpoch(l1);
        
        return true;
    }

private:
    static uint32_t parseTleEpoch(const String& line1) {
        if (line1.length() < 32) return 0;
        String yrStr = line1.substring(18, 20);
        String dayStr = line1.substring(20, 32);
        int yr = yrStr.toInt();
        double days = dayStr.toDouble();
        
        int year = (yr < 57) ? (2000 + yr) : (1900 + yr);
        
        auto isLeap = [](int y) {
            return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
        };
        
        uint32_t seconds = 0;
        for (int y = 1970; y < year; ++y) {
            seconds += isLeap(y) ? 366 * 86400 : 365 * 86400;
        }
        seconds += (uint32_t)((days - 1.0) * 86400.0);
        return seconds;
    }
};
