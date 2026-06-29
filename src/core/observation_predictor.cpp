#include "observation_predictor.h"
#include "sgp4_calc.h"
#include "sun_calculator.h"
#include "moon_calculator.h"
#include <math.h>
#include <algorithm>

namespace {
uint32_t parseTleEpoch(const String& line1) {
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

int parseTleLaunchYear(const String& line1) {
    if (line1.length() < 11) return 0;
    String yrStr = line1.substring(9, 11);
    int yr = yrStr.toInt();
    return (yr < 57) ? (2000 + yr) : (1900 + yr);
}

int getYearFromUnix(uint32_t unix_ts) {
    int year = 1970;
    while (true) {
        bool isLeap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        uint32_t secondsInYear = isLeap ? 366 * 86400 : 365 * 86400;
        if (unix_ts < secondsInYear) break;
        unix_ts -= secondsInYear;
        year++;
    }
    return year;
}

String getCommonPrefix(const String& name) {
    int idx = name.indexOf('-');
    if (idx == -1) idx = name.indexOf(' ');
    if (idx != -1) {
        return name.substring(0, idx);
    }
    return name;
}
}

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
    passes.reserve(20);
    
    // If the standard magnitude is very dim, it will never be visible to the naked eye (limit is 8.5)
    // We bypass calculation to save CPU and avoid Task Watchdog issues on high-altitude/geostationary satellites
    if (stdMag >= 8.5) {
        return passes;
    }
    
    SGP4Calc sgp4;
    sgp4.init(tle);
    
    SunCalculator sunCalc(nullptr);
    
    uint32_t endTime = startTime + daysToPredict * 24 * 3600;
    uint32_t stepSeconds = 240; // Optimize: Increase coarse step to 240 seconds (4 minutes)
    
    bool inPass = false;
    PassEvent currentPass;
    currentPass.aosTime = 0;
    currentPass.maxBrightness = 99.0;
    
    GeodeticCoord observerPos = {_userLat, _userLon, _userAlt / 1000.0};
    
    // Pre-calculate observer ECEF constants to save CPU time during loops
    const double PI_VAL = 3.14159265358979323846;
    ECEFCoord obsECEF = CoordTransform::geodeticToECEF(observerPos);
    double lat_rad = observerPos.lat * PI_VAL / 180.0;
    double lon_rad = observerPos.lon * PI_VAL / 180.0;
    double sin_lat = sin(lat_rad);
    double cos_lat = cos(lat_rad);
    double sin_lon = sin(lon_rad);
    double cos_lon = cos(lon_rad);
    
    extern volatile bool triggerPrediction;
    extern volatile bool cancelPrediction;
    
    int iterations = 0;
    uint32_t t = startTime;
    bool isRewinding = false;
    uint32_t rewindAnchor = 0;
    
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
        
        // Fast geometric filtering: calculate the Up component of topocentric coordinates directly in ECEF.
        // This is mathematically equivalent to the ENU 'Up' vector without calculating East/North, Range, or Angles.
        double dx = ecef.x - obsECEF.x;
        double dy = ecef.y - obsECEF.y;
        double dz = ecef.z - obsECEF.z;
        double up = cos_lat * cos_lon * dx + cos_lat * sin_lon * dy + sin_lat * dz;
        
        if (!inPass) {
            if (stepSeconds > 10) {
                // If the up component is negative, the satellite is strictly below the horizon (el < 0).
                // We bypass all expensive double-precision calculations (sqrt, asin, atmospheric refraction).
                if (up < 0.0) {
                    t += stepSeconds;
                    continue;
                }
                
                // Potential pass candidate: rewind and start fine calculation (10s intervals)
                rewindAnchor = t;
                t -= stepSeconds;
                stepSeconds = 10;
                isRewinding = true;
                continue;
            }
            
            // Fine-grained step calculation: compute real elevation
            double range = sqrt(dx*dx + dy*dy + dz*dz);
            double el = asin(up / range) * 180.0 / PI_VAL;
            
            // Atmospheric refraction compensation
            if (el > -5.0 && el < 15.0) {
                double r = 1.02 / tan((el + 10.3 / (el + 5.11)) * DEG_TO_RAD);
                el += r / 60.0;
            }
            
            // If elevation falls below 0, handle based on rewind window to prevent lockups.
            if (el < 0.0) {
                if (isRewinding && t < rewindAnchor) {
                    // Still in the rewind window, keep searching at fine step
                    t += stepSeconds;
                    continue;
                }
                stepSeconds = 240;
                isRewinding = false;
                t += stepSeconds;
                continue;
            }
            
            // AOS condition: reaches 10 degrees elevation
            if (el >= 10.0) {
                double east  = -sin_lon * dx + cos_lon * dy;
                double north = -sin_lat * cos_lon * dx - sin_lat * sin_lon * dy + cos_lat * dz;
                double az = atan2(east, north) * 180.0 / PI_VAL;
                if (az < 0.0) az += 360.0;
                
                inPass = true;
                currentPass.satName = tle.name;
                currentPass.aosTime = t;
                currentPass.startAz = az;
                currentPass.maxElevTime = t;
                currentPass.maxElevation = el;
                currentPass.maxAz = az;
                currentPass.maxBrightness = 99.0;
                currentPass.isVisible = false;
                currentPass.visibleDuration = 0;
            }
            
            t += stepSeconds;
        } else {
            // Under pass calculation: stepSeconds is always 10 here
            double range = sqrt(dx*dx + dy*dy + dz*dz);
            double el = asin(up / range) * 180.0 / PI_VAL;
            
            // Atmospheric refraction compensation
            if (el > -5.0 && el < 15.0) {
                double r = 1.02 / tan((el + 10.3 / (el + 5.11)) * DEG_TO_RAD);
                el += r / 60.0;
            }
            
            // LOS condition: drops below 0 degrees elevation
            if (el < 0.0) {
                inPass = false;
                stepSeconds = 240; // Reset back to coarse step
                isRewinding = false;
                
                if (currentPass.isVisible && currentPass.visibleDuration > 30 && currentPass.maxBrightness <= 8.5) {
                    // Populate basic satellite-wide tracking info for post-processors
                    if (tle.line1.length() >= 14) {
                        String batchStr = tle.line1.substring(9, 14);
                        strncpy(currentPass.launchBatch, batchStr.c_str(), sizeof(currentPass.launchBatch) - 1);
                        currentPass.launchBatch[sizeof(currentPass.launchBatch) - 1] = '\0';
                    }
                    currentPass.epoch = parseTleEpoch(tle.line1);
 
                    // 1. Calculate moon separation for MOON_PASS
                    PositionManager pm(nullptr);
                    MoonCalculator moonCalc(&pm);
                    MoonPositionData moonPos = moonCalc.calculatePosition(currentPass.maxElevTime, _userLat, _userLon);
                    
                    double theta_deg = 999.0;
                    if (moonPos.altitude > 0.0) {
                        double el1_rad = currentPass.maxElevation * DEG_TO_RAD;
                        double el2_rad = moonPos.altitude * DEG_TO_RAD;
                        double az1_rad = currentPass.maxAz * DEG_TO_RAD;
                        double az2_rad = moonPos.azimuth * DEG_TO_RAD;
 
                        double cos_theta_val = sin(el1_rad) * sin(el2_rad) + cos(el1_rad) * cos(el2_rad) * cos(az1_rad - az2_rad);
                        if (cos_theta_val > 1.0) cos_theta_val = 1.0;
                        if (cos_theta_val < -1.0) cos_theta_val = -1.0;
                        theta_deg = acos(cos_theta_val) * RAD_TO_DEG;
                    }
 
                    // 2. Identify single satellite events
                    uint8_t evType = 0;
                    int evBonus = 0;
                    String evTitle = "";
                    String evDesc = "";
                    
                    // Check RECENT_LAUNCH
                    int launchYear = parseTleLaunchYear(tle.line1);
                    int currentYear = getYearFromUnix(startTime);
                    
                    if (launchYear == currentYear && startTime >= currentPass.epoch && (startTime - currentPass.epoch) <= 14 * 86400) {
                        int ageDays = (startTime - currentPass.epoch) / 86400;
                        evType = 7;
                        evBonus = 3;
                        evTitle = "Recent Launch";
                        evDesc = "Newly launched satellite (Age: " + String(ageDays) + " days)";
                    }
                    // Check MOON_PASS
                    else if (moonPos.altitude > 0.0 && theta_deg <= 3.0) {
                        evType = 6;
                        evBonus = 2;
                        char buf[64];
                        snprintf(buf, sizeof(buf), "Close pass by the Moon (Separation: %.1f°)", theta_deg);
                        evTitle = "Moon Pass";
                        evDesc = buf;
                    }
                    // Check ZENITH_PASS
                    else if (currentPass.maxElevation >= 70.0) {
                        evType = 1;
                        evBonus = 1; // Optimized: reduce to +1
                        char buf[64];
                        snprintf(buf, sizeof(buf), "Passes nearly directly overhead (Max El: %.1f°)", currentPass.maxElevation);
                        evTitle = "Zenith Pass";
                        evDesc = buf;
                    }
                    // Check BRIGHT_PASS
                    else if (currentPass.maxBrightness <= 1.5) { // Optimized: tighten to <= 1.5
                        evType = 3;
                        evBonus = 2;
                        char buf[64];
                        snprintf(buf, sizeof(buf), "Exceptionally bright in the sky (Mag: %.1f)", currentPass.maxBrightness);
                        evTitle = "Bright Pass";
                        evDesc = buf;
                    }
                    // Check LONG_PASS
                    else if (currentPass.visibleDuration >= 300.0) {
                        evType = 2;
                        evBonus = 1;
                        char buf[64];
                        snprintf(buf, sizeof(buf), "Visible for an extended duration (%.1f min)", currentPass.visibleDuration / 60.0);
                        evTitle = "Long Pass";
                        evDesc = buf;
                    }
                    
                    currentPass.eventType = evType;
                    currentPass.eventBonus = evBonus;
                    currentPass.eventTitle = evTitle;
                    currentPass.eventDesc = evDesc;
                    
                    currentPass.baseScore = calculateScore(currentPass.maxElevation, currentPass.visibleDuration, currentPass.maxBrightness);
                    currentPass.score = currentPass.baseScore + evBonus;
                    
                    // Apply brightness capping to suppress dim satellites to high ratings
                    if (currentPass.maxBrightness > 4.0) {
                        if (currentPass.score > 2) currentPass.score = 2;
                    } else if (currentPass.maxBrightness > 3.0) {
                        if (currentPass.score > 3) currentPass.score = 3;
                    } else if (currentPass.maxBrightness > 1.5) {
                        if (currentPass.score > 4) currentPass.score = 4;
                    }
                    
                    if (currentPass.score > 5) currentPass.score = 5;
                    if (currentPass.score < 1) currentPass.score = 1;
                    
                    passes.push_back(currentPass);
                }
                
                t += stepSeconds;
                continue;
            }
            
            // Update max elevation
            if (el > currentPass.maxElevation) {
                currentPass.maxElevation = el;
                currentPass.maxElevTime = t;
                double east  = -sin_lon * dx + cos_lon * dy;
                double north = -sin_lat * cos_lon * dx - sin_lat * sin_lon * dy + cos_lat * dz;
                double az = atan2(east, north) * 180.0 / PI_VAL;
                if (az < 0.0) az += 360.0;
                currentPass.maxAz = az;
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
            
            double cos_theta_val = sin(subLatR)*sin(latR) + cos(subLatR)*cos(latR)*cos(lonR - subLonR);
            bool satIlluminated = true;
            if (cos_theta_val < 0) {
                // Angle between satellite and sun > 90 deg. Check conical shadow approximation.
                double earthRadius = 6378.137;
                double shadow_dist = dist_sat * sqrt(1.0 - cos_theta_val * cos_theta_val);
                if (shadow_dist < earthRadius) {
                    satIlluminated = false; // Eclipsed by Earth
                }
            }
            
            if (isNight && satIlluminated && el >= 10.0) {
                if (currentPass.visibleDuration == 0) {
                    // True AOS for optical visibility (exiting shadow or reaching 10 deg)
                    currentPass.aosTime = t;
                    double east  = -sin_lon * dx + cos_lon * dy;
                    double north = -sin_lat * cos_lon * dx - sin_lat * sin_lon * dy + cos_lat * dz;
                    double az = atan2(east, north) * 180.0 / PI_VAL;
                    if (az < 0.0) az += 360.0;
                    currentPass.startAz = az;
                }
                currentPass.isVisible = true;
                currentPass.visibleDuration += stepSeconds;
                
                // Track true LOS for optical visibility (entering shadow or dropping below 10 deg)
                currentPass.losTime = t;
                
                double east  = -sin_lon * dx + cos_lon * dy;
                double north = -sin_lat * cos_lon * dx - sin_lat * sin_lon * dy + cos_lat * dz;
                double az = atan2(east, north) * 180.0 / PI_VAL;
                if (az < 0.0) az += 360.0;
                currentPass.endAz = az;
                
                // Calculate phase angle and magnitude
                double sunDirX = cos(subLatR) * cos(subLonR);
                double sunDirY = cos(subLatR) * sin(subLonR);
                double sunDirZ = sin(subLatR);
                
                // Vector from satellite to observer (ECEF)
                double satToObsX = obsECEF.x - ecef.x;
                double satToObsY = obsECEF.y - ecef.y;
                double satToObsZ = obsECEF.z - ecef.z;
                
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
                
                double mag = stdMag + 5.0 * log10(range / 1000.0) - 2.5 * log10(term);
                
                // Atmospheric extinction correction
                if (el > 0.0) {
                    double sinEl = sin(el * DEG_TO_RAD);
                    double airMass = 1.0 / (sinEl + 0.15 * pow(el + 3.825, -1.253));
                    mag += 0.18 * airMass;
                }
                
                if (mag < currentPass.maxBrightness) {
                    currentPass.maxBrightness = mag;
                }
            }
            
            t += stepSeconds;
        }
    }
    
    return passes;
}

void ObservationPredictor::postProcessEvents(std::vector<PassEvent>& passes, uint32_t startTime) {
    if (passes.empty()) return;

    // 1. CONSTELLATION_TRAIN (星座列车)
    struct BatchInfo {
        std::vector<size_t> passIndices;
        std::vector<String> uniqueNames;
    };
    std::vector<std::pair<String, BatchInfo>> batchGroups;

    for (size_t i = 0; i < passes.size(); ++i) {
        const auto& p = passes[i];
        if (strlen(p.launchBatch) > 0 && (startTime >= p.epoch && (startTime - p.epoch) <= 14 * 86400)) {
            String batchStr = String(p.launchBatch);
            bool found = false;
            for (auto& bg : batchGroups) {
                if (bg.first == batchStr) {
                    bg.second.passIndices.push_back(i);
                    if (std::find(bg.second.uniqueNames.begin(), bg.second.uniqueNames.end(), p.satName) == bg.second.uniqueNames.end()) {
                        bg.second.uniqueNames.push_back(p.satName);
                    }
                    found = true;
                    break;
                }
            }
            if (!found) {
                BatchInfo bi;
                bi.passIndices.push_back(i);
                bi.uniqueNames.push_back(p.satName);
                batchGroups.push_back({batchStr, bi});
            }
        }
    }

    for (const auto& bg : batchGroups) {
        if (bg.second.uniqueNames.size() >= 8) {
            String commonPrefix = "Constellation";
            if (!bg.second.uniqueNames.empty()) {
                commonPrefix = getCommonPrefix(bg.second.uniqueNames[0]);
                if (commonPrefix.length() < 3) {
                    commonPrefix = "Constellation";
                }
            }

            // Sort passIndices by their aosTime so we process them in temporal order
            std::vector<size_t> sortedIndices = bg.second.passIndices;
            std::sort(sortedIndices.begin(), sortedIndices.end(), [&](size_t a, size_t b) {
                return passes[a].aosTime < passes[b].aosTime;
            });

            for (size_t i = 0; i < sortedIndices.size(); ++i) {
                size_t idx = sortedIndices[i];
                if (passes[idx].eventType < 4) {
                    passes[idx].eventType = 4;
                    if (i < 2) {
                        passes[idx].eventBonus = 2; // Lead passes get +2
                        passes[idx].eventTitle = commonPrefix + " Train (Lead)";
                        passes[idx].eventDesc = "Cluster of newly launched satellites passing in close succession";
                    } else {
                        passes[idx].eventBonus = 1; // Follower passes get +1
                        passes[idx].eventTitle = commonPrefix + " Train";
                        passes[idx].eventDesc = "Follower satellite in the constellation cluster";
                    }
                }
            }
        }
    }

    // 2. CONCURRENT_PASS (同场双星过境)
    for (size_t i = 0; i < passes.size(); ++i) {
        for (size_t j = i + 1; j < passes.size(); ++j) {
            auto& p_i = passes[i];
            auto& p_j = passes[j];
            
            if (p_i.satName == p_j.satName || p_i.maxBrightness > 2.5 || p_j.maxBrightness > 2.5) {
                continue;
            }

            uint32_t overlapStart = std::max(p_i.aosTime, p_j.aosTime);
            uint32_t overlapEnd = std::min(p_i.losTime, p_j.losTime);

            if (overlapEnd > overlapStart) {
                uint32_t overlapDuration = overlapEnd - overlapStart;
                if (overlapDuration >= 90) {
                    if (p_i.eventType < 5) {
                        p_i.eventType = 5;
                        p_i.eventBonus = 2;
                        p_i.eventTitle = "Concurrent Pass";
                        p_i.eventDesc = p_i.satName + " & " + p_j.satName + " visible together for " + String(overlapDuration) + "s";
                    }
                    if (p_j.eventType < 5) {
                        p_j.eventType = 5;
                        p_j.eventBonus = 2;
                        p_j.eventTitle = "Concurrent Pass";
                        p_j.eventDesc = p_i.satName + " & " + p_j.satName + " visible together for " + String(overlapDuration) + "s";
                    }
                }
            }
        }
    }

    // Recalculate and clamp all scores with brightness capping
    for (auto& p : passes) {
        p.score = p.baseScore + p.eventBonus;
        
        // Apply brightness capping to suppress dim satellites to high ratings
        if (p.maxBrightness > 4.0) {
            if (p.score > 2) p.score = 2;
        } else if (p.maxBrightness > 3.0) {
            if (p.score > 3) p.score = 3;
        } else if (p.maxBrightness > 1.5) {
            if (p.score > 4) p.score = 4;
        }
        
        if (p.score > 5) p.score = 5;
        if (p.score < 1) p.score = 1;
    }
}
