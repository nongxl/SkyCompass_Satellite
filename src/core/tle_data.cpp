#include "tle_data.h"

// TLE data for testing (from recent dates)
// Note: TLEs change frequently, these are for Phase 0 validation.

TLEData TLEManager::getISS_TLE() {
    TLEData iss;
    iss.name = "ISS (ZARYA)";
    // Example recent TLE for ISS (2026)
    iss.line1 = "1 25544U 98067A   26163.80312907  .00008495  00000+0  16106-3 0  9990";
    iss.line2 = "2 25544  51.6335 321.7912 0004900 178.5917 181.5086 15.49196054571074";
    iss.baseScore = 2; // ISS is huge and very bright
    return iss;
}

TLEData TLEManager::getTiangong_TLE() {
    TLEData tiangong;
    tiangong.name = "CSS (TIANGONG)";
    // Example recent TLE for Tiangong (2026)
    tiangong.line1 = "1 48274U 21035A   26163.81770925  .00021976  00000+0  26094-3 0  9991";
    tiangong.line2 = "2 48274  41.4694 348.5894 0007968  41.6006 318.5438 15.60618766292486";
    tiangong.baseScore = 1; // Tiangong is large but smaller than ISS
    return tiangong;
}

TLEData TLEManager::getHubble_TLE() {
    TLEData hubble;
    hubble.name = "HST (HUBBLE)";
    // Real Hubble TLE from mid 2026
    hubble.line1 = "1 20580U 90037B   26163.25344175  .00006001  00000+0  18892-3 0  9990";
    hubble.line2 = "2 20580  28.4709 114.2921 0001952  91.9422 268.1398 15.30693075787818";
    hubble.baseScore = 0; // Standard satellite brightness
    return hubble;
}

TLEData TLEManager::getJWST_TLE() {
    TLEData jwst;
    jwst.name = "JWST";
    // Pseudo-TLE for deep space at L2, gives it a ~1-year orbit roughly on the ecliptic
    jwst.line1 = "1 50463U 21130A   22001.00000000  .00000000  00000-0  00000-0 0  9999";
    jwst.line2 = "2 50463  23.4392   0.0000 0000000   0.0000   0.0000  0.00273700    00";
    jwst.baseScore = 0;
    return jwst;
}

uint32_t TLEManager::getMockTimeAnchor() {
    // 2026-06-14 00:00:00 UTC = 1781395200
    // MUST match the 2026 TLE epoch
    return 1781395200;
}
