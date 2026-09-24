#pragma once

#include <vector>
#include <Arduino.h>
#include "tle_data.h"
#include "coord_transform.h"

// Define a visible pass event
struct PassEvent {
    String satName;
    uint32_t aosTime;       // Acquisition of Signal (seconds since epoch)
    uint32_t losTime;       // Loss of Signal
    uint32_t maxElevTime;   // Time of Maximum Elevation
    
    float maxElevation;     // Maximum Elevation in degrees
    float maxBrightness;    // Estimated maximum brightness (magnitude, lower is brighter)
    
    float startAz;          // Azimuth at AOS in degrees
    float endAz;            // Azimuth at LOS in degrees
    float maxAz;            // Azimuth at Maximum Elevation in degrees
    
    bool isVisible;         // True if the pass is visually observable (in night + satellite illuminated)
    float visibleDuration;  // Duration of visibility in seconds
    bool isRadioPass = false; // True if this pass meets amateur radio communication criteria
    
    int score;              // 1 to 5 stars
    int baseScore = 0;
    
    // === Event Engine fields ===
    uint8_t eventType = 0;   // 0=NONE, 1=ZENITH_PASS, 2=LONG_PASS, 3=BRIGHT_PASS, 4=CONSTELLATION_TRAIN, 5=CONCURRENT_PASS, 6=MOON_PASS, 7=RECENT_LAUNCH, 8=RADIO_PASS
    int eventBonus = 0;
    String eventTitle = "";
    String eventDesc = "";
    bool satSelected = true;
    int satIndex = -1;
    char launchBatch[8] = "";
    uint32_t epoch = 0;
};

class ObservationPredictor {
public:
    ObservationPredictor(double userLat, double userLon, double userAlt, class PositionManager* pm = nullptr);
    
    // Predict passes for a satellite over a given number of days starting from startTime
    std::vector<PassEvent> predictPasses(const TLEData& tle, double stdMag, uint32_t startTime, int daysToPredict, bool isRadioTarget = false, int maxPasses = 6);
    
    // Post-process predicted passes to check for multi-satellite events (constellation trains, concurrent passes)
    static void postProcessEvents(std::vector<PassEvent>& passes, uint32_t startTime);

private:
    double _userLat;
    double _userLon;
    double _userAlt;
    class PositionManager* _pm;
    
    // Helper to calculate score based on max elevation, visible duration, and max brightness
    int calculateScore(float maxElevation, float visibleDuration, float maxBrightness);
    
    // Helper to calculate score for radio passes based on max elevation and pass duration
    int calculateRadioScore(float maxElevation, float duration);
};
