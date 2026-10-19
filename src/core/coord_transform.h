#pragma once

#include <Arduino.h>

struct ECEFCoord {
    double x; // km
    double y; // km
    double z; // km
};

struct GeodeticCoord {
    double lat; // degrees
    double lon; // degrees
    double alt; // km
};

struct TopocentricCoord {
    double az;    // degrees (0 = North, 90 = East)
    double el;    // degrees
    double range; // km
};

class CoordTransform {
public:
    // Convert Unix timestamp to Julian Date
    static double unixToJulian(uint32_t unix_ts);
    static double unixToJulian(double unix_ts);

    // Calculate Greenwich Mean Sidereal Time (GMST) in radians from Julian Date
    static double getGMST(double julian_date);

    // Convert ECI (TEME) coordinates to ECEF coordinates
    // teme_x, teme_y, teme_z are in km. gmst is in radians.
    static ECEFCoord temeToECEF(double teme_x, double teme_y, double teme_z, double gmst);

    // Convert ECEF coordinates to Geodetic coordinates (Lat, Lon, Alt)
    static GeodeticCoord ecefToGeodetic(const ECEFCoord& ecef);

    // Convert Geodetic coordinates to ECEF coordinates
    static ECEFCoord geodeticToECEF(const GeodeticCoord& geo);

    // Convert Target ECEF to Topocentric (Az, El, Range) relative to Observer
    static TopocentricCoord ecefToTopocentric(const GeodeticCoord& observer, const ECEFCoord& targetECEF);
};
