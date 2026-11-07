#pragma GCC optimize ("O3")

#include "coord_transform.h"
#include <math.h>

const double WGS84_A = 6378.137;           // Semi-major axis in km
const double WGS84_B = 6356.752314245;     // Semi-minor axis in km
const double WGS84_E2 = 0.00669437999014;  // First eccentricity squared
const double PI_CONST = 3.14159265358979323846;

static constexpr float WGS84_A_F = 6378.137f;
static constexpr float WGS84_B_F = 6356.7523f;
static constexpr float WGS84_E2_F = 0.00669438f;
static constexpr float BOWRING_EP2_B_F = 42.8413115f; // e'^2 * b
static constexpr float BOWRING_E2_A_F  = 42.6976727f; // e^2 * a
static constexpr float RAD_TO_DEG_F = 57.295779513f;
static constexpr float DEG_TO_RAD_F = 0.01745329252f;

double CoordTransform::unixToJulian(uint32_t unix_ts) {
    return (unix_ts / 86400.0) + 2440587.5;
}

double CoordTransform::unixToJulian(double unix_ts) {
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

void CoordTransform::ecefToGeodeticFast(float ex, float ey, float ez, float& outLatDeg, float& outLonDeg, float& outAltKm) {
    float p = sqrtf(ex * ex + ey * ey);
    float lonDeg = atan2f(ey, ex) * RAD_TO_DEG_F;

    float zA = ez * WGS84_A_F;
    float pB = p * WGS84_B_F;
    float h = sqrtf(zA * zA + pB * pB);
    if (h < 1e-3f) {
        outLatDeg = 0.0f;
        outLonDeg = 0.0f;
        outAltKm = -WGS84_A_F;
        return;
    }
    float invH = 1.0f / h;
    float sin_th = zA * invH;
    float cos_th = pB * invH;

    float num = ez + BOWRING_EP2_B_F * (sin_th * sin_th * sin_th);
    float den = p  - BOWRING_E2_A_F  * (cos_th * cos_th * cos_th);
    float latRad = atan2f(num, den);

    float sin_lat = sinf(latRad);
    float cos_lat = cosf(latRad);
    float N = WGS84_A_F / sqrtf(1.0f - WGS84_E2_F * sin_lat * sin_lat);
    float altKm;
    if (fabsf(cos_lat) > 0.1f) {
        altKm = p / cos_lat - N;
    } else {
        altKm = fabsf(ez) / fabsf(sin_lat) - N * (1.0f - WGS84_E2_F);
    }

    if (lonDeg > 180.0f) lonDeg -= 360.0f;
    else if (lonDeg < -180.0f) lonDeg += 360.0f;

    outLatDeg = latRad * RAD_TO_DEG_F;
    outLonDeg = lonDeg;
    outAltKm = altKm;
}

GeodeticCoord CoordTransform::ecefToGeodeticFast(const ECEFCoord& ecef) {
    float lat, lon, alt;
    ecefToGeodeticFast((float)ecef.x, (float)ecef.y, (float)ecef.z, lat, lon, alt);
    return {(double)lat, (double)lon, (double)alt};
}

GeodeticCoord CoordTransform::ecefToGeodetic(const ECEFCoord& ecef) {
    // Bowring closed-form non-iterative WGS84 geodetic conversion (sub-millimeter accuracy, 4x faster than 5-step loop)
    GeodeticCoord geo;
    double p = sqrt(ecef.x * ecef.x + ecef.y * ecef.y);
    double lon = atan2(ecef.y, ecef.x);

    double zA = ecef.z * WGS84_A;
    double pB = p * WGS84_B;
    double h = sqrt(zA * zA + pB * pB);
    if (h < 1e-6) {
        return {0.0, 0.0, -WGS84_A};
    }
    double invH = 1.0 / h;
    double sin_th = zA * invH;
    double cos_th = pB * invH;

    const double ep2_b = 42.84131151331357;
    const double e2_a  = 42.69767270717997;
    double lat = atan2(ecef.z + ep2_b * (sin_th * sin_th * sin_th),
                       p      - e2_a  * (cos_th * cos_th * cos_th));
    double sin_lat = sin(lat);
    double cos_lat = cos(lat);
    double N = WGS84_A / sqrt(1.0 - WGS84_E2 * sin_lat * sin_lat);
    double alt = (fabs(cos_lat) > 0.1) ? (p / cos_lat - N) : (fabs(ecef.z) / fabs(sin_lat) - N * (1.0 - WGS84_E2));

    geo.lat = lat * (180.0 / PI_CONST);
    geo.lon = lon * (180.0 / PI_CONST);
    geo.alt = alt;

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

void CoordTransform::initObserverFrame(double latDeg, double lonDeg, double altKm, ObserverFrame& out) {
    float latRad = (float)latDeg * DEG_TO_RAD_F;
    float lonRad = (float)lonDeg * DEG_TO_RAD_F;
    out.sinLat = sinf(latRad);
    out.cosLat = cosf(latRad);
    out.sinLon = sinf(lonRad);
    out.cosLon = cosf(lonRad);
    float N = WGS84_A_F / sqrtf(1.0f - WGS84_E2_F * out.sinLat * out.sinLat);
    float altF = (float)altKm;
    out.ecefX = (N + altF) * out.cosLat * out.cosLon;
    out.ecefY = (N + altF) * out.cosLat * out.sinLon;
    out.ecefZ = (N * (1.0f - WGS84_E2_F) + altF) * out.sinLat;
}

float CoordTransform::ecefToElevationFast(const ObserverFrame& obs, float ex, float ey, float ez) {
    float dx = ex - obs.ecefX;
    float dy = ey - obs.ecefY;
    float dz = ez - obs.ecefZ;
    float distSq = dx * dx + dy * dy + dz * dz;
    if (distSq < 1e-6f) return 90.0f;
    float up = obs.cosLat * obs.cosLon * dx + obs.cosLat * obs.sinLon * dy + obs.sinLat * dz;
    float sinEl = up / sqrtf(distSq);
    if (sinEl > 1.0f) sinEl = 1.0f;
    else if (sinEl < -1.0f) sinEl = -1.0f;
    return asinf(sinEl) * RAD_TO_DEG_F;
}

void CoordTransform::ecefToTopocentricFast(const ObserverFrame& obs, float ex, float ey, float ez, float& outAz, float& outEl, float& outRange) {
    float dx = ex - obs.ecefX;
    float dy = ey - obs.ecefY;
    float dz = ez - obs.ecefZ;
    float east  = -obs.sinLon * dx + obs.cosLon * dy;
    float north = -obs.sinLat * obs.cosLon * dx - obs.sinLat * obs.sinLon * dy + obs.cosLat * dz;
    float up    =  obs.cosLat * obs.cosLon * dx + obs.cosLat * obs.sinLon * dy + obs.sinLat * dz;
    float range = sqrtf(east * east + north * north + up * up);
    outRange = range;
    if (range < 1e-4f) {
        outAz = 0.0f;
        outEl = 90.0f;
        return;
    }
    float az = atan2f(east, north) * RAD_TO_DEG_F;
    if (az < 0.0f) az += 360.0f;
    float sinEl = up / range;
    if (sinEl > 1.0f) sinEl = 1.0f;
    else if (sinEl < -1.0f) sinEl = -1.0f;
    outAz = az;
    outEl = asinf(sinEl) * RAD_TO_DEG_F;
}
