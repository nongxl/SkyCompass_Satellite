#pragma once
#include <Arduino.h>

struct OrbitRecord {
    String name;
    uint32_t catalogNumber = 0;
    char internationalDesignator[16] = "";
    
    // Keplerian Elements
    double inclination = 0.0;       // i (degrees)
    double eccentricity = 0.0;      // e
    double meanMotion = 0.0;        // n (revs per day)
    double meanAnomaly = 0.0;       // M (degrees)
    double argumentOfPerigee = 0.0; // omega (degrees)
    double raan = 0.0;              // Omega (degrees)
    double bstar = 0.0;             // B* drag term
    uint32_t epochUnix = 0;         // Epoch timestamp
    double epochDays = 0.0;         // Epoch day of year fractional
    int epochYr = 0;                // Epoch year (e.g. 2026 or 26)
    
    String getBatchId() const {
        String id = String(internationalDesignator);
        id.trim();
        if (id.length() >= 7) {
            if (id.length() >= 9 && id[4] == '-') {
                // YYYY-NNNAAA -> YYNNN
                return id.substring(2, 4) + id.substring(5, 8);
            } else {
                // YYNNNAAA -> YYNNN
                return id.substring(0, 5);
            }
        }
        if (catalogNumber > 0) {
            char buf[12];
            sprintf(buf, "%05u", (unsigned int)(catalogNumber % 100000));
            return String(buf);
        }
        return "";
    }
};

