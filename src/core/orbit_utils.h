#ifndef ORBIT_UTILS_H
#define ORBIT_UTILS_H

#include <Arduino.h>
#include <vector>
#include "core/earth_renderer.h"
#include "core/sgp4_calc.h"
#include "core/recent_launch_item.h"

class OrbitUtils {
public:
    static void autoAssignIconAndColor(const String& name, SatIconType& icon, uint16_t& color);
    static double getGeoSlotLongitude(uint32_t noradId, const String& slotStr);
    static void calculateGeoSatPosition(double satLonDeg, double userLatDeg, double userLonDeg, double userAltMeters, 
                                        GeodeticCoord& outGeo, ECEFCoord& outEcef, TopocentricCoord& outTopo, double& outSkewDeg);
    static String getShortNameForDisplay(const String& fullName, uint32_t epoch);
    static void assignShortNameAndIcon(RecentLaunchItem& item);
    static void calculateFormationsForItems(std::vector<RecentLaunchItem>& items, const std::vector<std::vector<float>>* providedPhases = nullptr);
    static void validateSatViewFocusState();
    static void calculateOrbit(SGP4Calc& calc, uint32_t baseTime, OrbitCache& cache, int& calcCount, bool isTimeScrolling, bool forceUpdate = false);
};

#endif // ORBIT_UTILS_H
