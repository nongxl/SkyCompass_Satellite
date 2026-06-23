#include "observation_predictor.h"
#include "sgp4_calc.h"
#include "sun_calculator.h"
#include <math.h>

#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105

ObservationPredictor::ObservationPredictor(double userLat, double userLon, double userAlt) {
    _userLat = userLat;
    _userLon = userLon;
    _userAlt = userAlt;
}

int ObservationPredictor::calculateScore(float maxElevation, float visibleDuration, float maxBrightness) {
    int score = 1;
    
    // Base score by magnitude
    if (maxBrightness <= -1.5) score += 3;
    else if (maxBrightness <= 1.0) score += 2;
    else if (maxBrightness <= 3.0) score += 1;
    
    // Elevation bonus
    if (maxElevation >= 40) score += 1;
    
    // Penalty for very low or short passes
    if (maxElevation < 20) score -= 2;
    if (visibleDuration < 120) score -= 1;
    
    if (score > 5) score = 5;
    if (score < 1) score = 1;
    
    return score;
}

std::vector<PassEvent> ObservationPredictor::predictPasses(const TLEData& tle, double stdMag, uint32_t startTime, int daysToPredict) {
    std::vector<PassEvent> passes;
    
    // If the standard magnitude is very dim, it will never be visible to the naked eye (limit is 8.5)
    // We bypass calculation to save CPU and avoid Task Watchdog issues on high-altitude/geostationary satellites
    if (stdMag >= 8.5) {
        return passes;
    }
    
    SGP4Calc sgp4;
    sgp4.init(tle);
    
    SunCalculator sunCalc(nullptr);
    
    uint32_t endTime = startTime + daysToPredict * 24 * 3600;
    uint32_t stepSeconds = 120; // Start with 2 minute step
    
    bool inPass = false;
    PassEvent currentPass;
    currentPass.aosTime = 0;
    currentPass.maxBrightness = 99.0;
    
    GeodeticCoord observerPos = {_userLat, _userLon, _userAlt / 1000.0};
    
    extern volatile bool triggerPrediction;
    extern volatile bool cancelPrediction;
    
    int iterations = 0;
    uint32_t t = startTime;
    bool isRewinding = false;
    
    while (t <= endTime) {
        if (triggerPrediction || cancelPrediction) return passes;
        
        iterations++;
        // Reset Watchdog Timer periodically and yield to Idle Task to prevent starvation
        if (iterations % 300 == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        
        double tx, ty, tz;
        if (!sgp4.getTEME(t, tx, ty, tz)) {
            t += stepSeconds;
            continue;
        }
        
        double gmst = CoordTransform::getGMST(CoordTransform::unixToJulian(t));
        ECEFCoord ecef = CoordTransform::temeToECEF(tx, ty, tz, gmst);
        
        TopocentricCoord topo = CoordTransform::ecefToTopocentric(observerPos, ecef);
        double el = topo.el;
        
        // Atmospheric refraction compensation for low elevations
        if (el > -5.0 && el < 15.0) {
            double r = 1.02 / tan((el + 10.3 / (el + 5.11)) * DEG_TO_RAD);
            el += r / 60.0;
        }
        
        if (!inPass) {
            if (el >= 0.0 && stepSeconds > 10) {
                // Satellite rose above horizon, rewind and switch to fine step
                t -= stepSeconds;
                stepSeconds = 10;
                isRewinding = true;
                continue;
            }
            
            if (el >= 10.0 && stepSeconds <= 10) {
                // AOS at 10 degrees threshold
                inPass = true;
                stepSeconds = 10; // Keep fine step for high precision
                currentPass.satName = tle.name;
                currentPass.aosTime = t;
                currentPass.startAz = topo.az;
                currentPass.maxElevTime = t;
                currentPass.maxElevation = el;
                currentPass.maxAz = topo.az;
                currentPass.maxBrightness = 99.0;
                currentPass.isVisible = false;
                currentPass.visibleDuration = 0;
            }
        } else {
            // We are in a pass (el >= 10.0 usually, but can fluctuate)
            // Update max elevation
            if (el > currentPass.maxElevation) {
                currentPass.maxElevation = el;
                currentPass.maxElevTime = t;
                currentPass.maxAz = topo.az;
            }
            
            // 3. Visibility Check
            SunPositionData sunPos = sunCalc.calculatePosition(t, _userLat, _userLon);
            bool isNight = (sunPos.altitude < -6.0);
            
            // Precise Earth Umbra Shadow Check (Using Fast Spherical Approximation instead of WGS84)
            double dist_sat = sqrt(ecef.x*ecef.x + ecef.y*ecef.y + ecef.z*ecef.z);
            double latR = asin(ecef.z / dist_sat);
            double lonR = atan2(ecef.y, ecef.x);
            double subLatR = sunPos.subsolarLat * DEG_TO_RAD;
            double subLonR = sunPos.subsolarLon * DEG_TO_RAD;
            
            double cos_theta = sin(subLatR)*sin(latR) + cos(subLatR)*cos(latR)*cos(lonR - subLonR);
            bool satIlluminated = true;
            if (cos_theta < 0) {
                // Angle between satellite and sun > 90 deg. Check conical shadow approximation.
                double earthRadius = 6378.137;
                double shadow_dist = dist_sat * sqrt(1.0 - cos_theta * cos_theta);
                if (shadow_dist < earthRadius) {
                    satIlluminated = false; // Eclipsed by Earth
                }
            }
            
            if (isNight && satIlluminated && el >= 10.0) {
                if (currentPass.visibleDuration == 0) {
                    // True AOS for optical visibility (exiting shadow or reaching 10 deg)
                    currentPass.aosTime = t;
                    currentPass.startAz = topo.az;
                }
                currentPass.isVisible = true;
                currentPass.visibleDuration += stepSeconds;
                
                // Track true LOS for optical visibility (entering shadow or dropping below 10 deg)
                currentPass.losTime = t;
                currentPass.endAz = topo.az;
                
                // Calculate phase angle and magnitude
                double obsX = observerPos.lat * DEG_TO_RAD;
                double obsY = observerPos.lon * DEG_TO_RAD;
                
                // Vector to sun (approximate unit vector)
                double sunDirX = cos(subLatR) * cos(subLonR);
                double sunDirY = cos(subLatR) * sin(subLonR);
                double sunDirZ = sin(subLatR);
                
                // Vector from satellite to sun is basically sunDir because sun is so far
                
                // Vector from satellite to observer (ECEF)
                // Observer ECEF
                ECEFCoord obsEcef = CoordTransform::geodeticToECEF(observerPos);
                double satToObsX = obsEcef.x - ecef.x;
                double satToObsY = obsEcef.y - ecef.y;
                double satToObsZ = obsEcef.z - ecef.z;
                
                // Normalize satToObs
                double distObs = sqrt(satToObsX*satToObsX + satToObsY*satToObsY + satToObsZ*satToObsZ);
                satToObsX /= distObs;
                satToObsY /= distObs;
                satToObsZ /= distObs;
                
                double dotProd = sunDirX * satToObsX + sunDirY * satToObsY + sunDirZ * satToObsZ;
                if (dotProd > 1.0) dotProd = 1.0;
                if (dotProd < -1.0) dotProd = -1.0;
                double phaseAngle = acos(dotProd);
                
                double term = sin(phaseAngle) + (PI - phaseAngle) * cos(phaseAngle);
                if (term < 0.001) term = 0.001; // prevent log of 0
                
                double mag = stdMag + 5.0 * log10(topo.range / 1000.0) - 2.5 * log10(term);
                
                // Atmospheric extinction correction (kv = 0.18 for typical clear night sky)
                if (el > 0.0) {
                    double sinEl = sin(el * DEG_TO_RAD);
                    double airMass = 1.0 / (sinEl + 0.15 * pow(el + 3.825, -1.253));
                    mag += 0.18 * airMass;
                }
                
                if (mag < currentPass.maxBrightness) {
                    currentPass.maxBrightness = mag;
                }
            }
            
            // LOS (Loss of Signal) threshold: drops below 0 degrees
            if (el < 0.0) {
                inPass = false;
                
                if (currentPass.isVisible && currentPass.visibleDuration > 30 && currentPass.maxBrightness <= 8.5) {
                    currentPass.score = calculateScore(currentPass.maxElevation, currentPass.visibleDuration, currentPass.maxBrightness);
                    passes.push_back(currentPass);
                }
            }
        }
        
        if (el >= 0.0) {
            isRewinding = false; // We successfully crossed the horizon
        }
        
        if (el < 0.0 && !inPass && !isRewinding) {
            // Satellite is below horizon. Switch back to coarse step.
            stepSeconds = 120;
        }
        
        t += stepSeconds;
    }
    
    return passes;
}
