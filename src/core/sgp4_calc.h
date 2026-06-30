#pragma once

#include <Arduino.h>
#include "tle_data.h"
#include "coord_transform.h"
#include <Sgp4.h>

class SGP4Calc {
public:
    SGP4Calc();
    ~SGP4Calc();

    // Copy constructor and assignment operator for deep copying and preventing double-free crashes
    SGP4Calc(const SGP4Calc& other);
    SGP4Calc& operator=(const SGP4Calc& other);

    // Initialize with TLE data (compatible load)
    bool init(const TLEData& tle);

    // Initialize directly with OrbitRecord (OMM elements)
    bool init(const OrbitRecord& record);

    // Get ECI (TEME) coordinates for a given Unix timestamp
    // Returns true on success
    bool getTEME(uint32_t unix_ts, double& x, double& y, double& z);

    // Helper functions for Pseudo TLE generation inside the driver layer
    static void buildPseudoTle(const OrbitRecord& record, String& outL1, String& outL2);
    static String formatBStar(double bstar);
    static String formatIntlDesig(const String& raw);
    static int calculateTleChecksum(const String& line);

private:
    Sgp4* sat;
};
