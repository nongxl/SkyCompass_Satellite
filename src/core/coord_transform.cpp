#include "coord_transform.h"
#include <math.h>

const double WGS84_A = 6378.137;           // Semi-major axis in km
const double WGS84_E2 = 0.00669437999014;  // First eccentricity squared
const double PI_CONST = 3.14159265358979323846;

double CoordTransform::unixToJulian(uint32_t unix_ts) {
    return (unix_ts / 86400.0) + 2440587.5;
}

double CoordTransform::getGMST(double julian_date) {
    double jd0 = floor(julian_date + 0.5) - 0.5;
    double T0 = (jd0 - 2451545.0) / 36525.0;
    double gmst_0h_sec = 24110.54841 + 8640184.812866 * T0 + 0.093104 * T0 * T0 - 6.2e-6 * T0 * T0 * T0;
    double time_of_day_sec = (julian_date - jd0) * 86400.0;
    double gmst_sec = gmst_0h_sec + time_of_day_sec * 1.002737909350795;
    double gmst_rad = fmod(gmst_sec, 86400.0) * 2.0 * PI_CONST / 86400.0;
    if (gmst_rad < 0.0) {
        gmst_rad += 2.0 * PI_CONST;
    }
    return gmst_rad;
}

ECEFCoord CoordTransform::temeToECEF(double teme_x, double teme_y, double teme_z, double gmst) {
    ECEFCoord ecef;
    ecef.x = teme_x * cos(gmst) + teme_y * sin(gmst);
    ecef.y = teme_x * -sin(gmst) + teme_y * cos(gmst);
    ecef.z = teme_z;
    return ecef;
}

GeodeticCoord CoordTransform::ecefToGeodetic(const ECEFCoord& ecef) {
    GeodeticCoord geo;
    double r_delta = sqrt(ecef.x * ecef.x + ecef.y * ecef.y);
    double lon = atan2(ecef.y, ecef.x);
    
    // Iterative calculation for latitude and altitude
    double lat = atan2(ecef.z, r_delta * (1.0 - WGS84_E2));
    double N = WGS84_A;
    double alt = 0.0;
    
    for (int i = 0; i < 5; i++) {
        double sin_lat = sin(lat);
        N = WGS84_A / sqrt(1.0 - WGS84_E2 * sin_lat * sin_lat);
        alt = r_delta / cos(lat) - N;
        lat = atan2(ecef.z, r_delta * (1.0 - WGS84_E2 * (N / (N + alt))));
    }
    
    geo.lat = lat * 180.0 / PI_CONST;
    geo.lon = lon * 180.0 / PI_CONST;
    geo.alt = alt;

    // Normalize longitude to -180 to 180
    while (geo.lon > 180.0) geo.lon -= 360.0;
    while (geo.lon < -180.0) geo.lon += 360.0;
    
    return geo;
}

ECEFCoord CoordTransform::geodeticToECEF(const GeodeticCoord& geo) {
    double lat_rad = geo.lat * PI_CONST / 180.0;
    double lon_rad = geo.lon * PI_CONST / 180.0;
    
    double sin_lat = sin(lat_rad);
    double cos_lat = cos(lat_rad);
    double sin_lon = sin(lon_rad);
    double cos_lon = cos(lon_rad);
    
    double N = WGS84_A / sqrt(1.0 - WGS84_E2 * sin_lat * sin_lat);
    
    ECEFCoord ecef;
    ecef.x = (N + geo.alt) * cos_lat * cos_lon;
    ecef.y = (N + geo.alt) * cos_lat * sin_lon;
    ecef.z = (N * (1.0 - WGS84_E2) + geo.alt) * sin_lat;
    
    return ecef;
}

TopocentricCoord CoordTransform::ecefToTopocentric(const GeodeticCoord& observer, const ECEFCoord& targetECEF) {
    ECEFCoord obsECEF = geodeticToECEF(observer);
    
    double dx = targetECEF.x - obsECEF.x;
    double dy = targetECEF.y - obsECEF.y;
    double dz = targetECEF.z - obsECEF.z;
    
    double lat_rad = observer.lat * PI_CONST / 180.0;
    double lon_rad = observer.lon * PI_CONST / 180.0;
    
    double sin_lat = sin(lat_rad);
    double cos_lat = cos(lat_rad);
    double sin_lon = sin(lon_rad);
    double cos_lon = cos(lon_rad);
    
    // Topocentric ENU (East, North, Up)
    double east  = -sin_lon * dx + cos_lon * dy;
    double north = -sin_lat * cos_lon * dx - sin_lat * sin_lon * dy + cos_lat * dz;
    double up    =  cos_lat * cos_lon * dx + cos_lat * sin_lon * dy + sin_lat * dz;
    
    TopocentricCoord topo;
    topo.range = sqrt(east*east + north*north + up*up);
    topo.az = atan2(east, north) * 180.0 / PI_CONST;
    if (topo.az < 0.0) topo.az += 360.0;
    topo.el = asin(up / topo.range) * 180.0 / PI_CONST;
    
    return topo;
}
