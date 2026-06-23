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

    // Initialize with TLE data
    bool init(const TLEData& tle);

    // Get ECI (TEME) coordinates for a given Unix timestamp
    // Returns true on success
    bool getTEME(uint32_t unix_ts, double& x, double& y, double& z);

private:
    Sgp4* sat;
};
