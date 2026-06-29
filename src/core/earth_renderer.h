#pragma once

#include <M5Cardputer.h>
#include <vector>
#include "coord_transform.h"

enum SatIconType {
    ICON_SATELLITE,
    ICON_STATION,
    ICON_TELESCOPE,
    ICON_DEEPSPACE,
    ICON_ROCKET,
    ICON_DFH1,
    ICON_BLUEWALKER3,
    ICON_WEATHER,
    ICON_NAVIGATION,
    ICON_COMMUNICATION
};

enum TrainState {
    VERY_TIGHT,
    TIGHT,
    EXPANDING,
    OPERATIONAL
};

struct SatRenderData {
    const char* name;
    SatIconType iconType;
    GeodeticCoord currentPos;
    const std::vector<GeodeticCoord>* pastOrbit;
    const std::vector<GeodeticCoord>* futureOrbit;
    uint16_t color;
    bool isVisible;
    bool isRecentLaunchBatch = false;
    int totalSatellitesInBatch = 0;
    uint32_t launchEpoch = 0;
    uint32_t simTime = 0;
    uint32_t lastCalcTime = 0;
};

class EarthRenderer {
public:
    EarthRenderer(M5GFX* display);
    ~EarthRenderer();

    void begin();
    
    // Set sun position (subsolar point) for terminator and shadow calculation
    void setSunPosition(double subsolarLat, double subsolarLon);
    
    // Set camera attitude for AR camera effect
    void setCameraAttitude(float pitch, float roll, float yaw);
    
    // Render the Earth, user, and satellites
    void render(double centerLat, double centerLon, double userLat, double userLon, const std::vector<SatRenderData>& satellites);

    // Set whether satellite color is constrained by ground observer visibility (default: true)
    void setObserverConstrained(bool constrained) { _observerConstrained = constrained; }
    
    // Set whether time machine is fast forwarding to optimize rendering load
    void setFastForwarding(bool ff) { _isFastForwarding = ff; }

    void setZoom(float zoom) {
        _zoom = zoom;
        _earthRadius = (int)(55.0f * _zoom);
        updateFocusR();
    }
    
    // Set offset for the projection center
    void setCenterOffset(int offsetX, int offsetY) {
        _centerOffsetX = offsetX;
        _centerOffsetY = offsetY;
    }
    
    // Set altitude of the focus point (for proper pivot in Sat View)
    void setCameraFocusAlt(double alt) {
        _cameraFocusAlt = alt;
        updateFocusR();
    }
    
    // Set current unix time for sidereal time calculations (like background stars)
    void setUnixTime(uint32_t unixTime) {
        _unixTime = unixTime;
    }

    LGFX_Sprite* getCanvas() { return _canvas; }

private:
    uint32_t _unixTime = 0;
    
    void updateFocusR() {
        _cameraFocusR = _earthRadius;
    }
    M5GFX* _display;
    LGFX_Sprite* _canvas;
    
    // Config
    int _centerX;
    int _centerY;
    int _centerOffsetX = 0;
    int _centerOffsetY = 0;
    int _earthRadius;
    float _zoom = 1.0f;
    float _cameraFocusAlt = 0.0f;
    float _cameraFocusR = 55.0f;
    
    // Camera
    float _cameraPitch = 0;
    float _cameraRoll = 0;
    float _cameraYaw = 0;
    
    // Sun data
    bool _hasSunData = false;
    double _subsolarLat = 0;
    double _subsolarLon = 0;
    
    // Rendering configs
    bool _observerConstrained = true;
    bool _isFastForwarding = false;

    // Helper functions for orthographic projection
    bool projectOrthographic(double lat, double lon, double alt, double centerLat, double centerLon, int& outX, int& outY);
    
    void drawStars(double centerLat, double centerLon);
    void drawEarth(double centerLat, double centerLon, double userLat, double userLon);
    void drawContinents(double centerLat, double centerLon);
    void drawLightPollution(double centerLat, double centerLon);
    void drawSatellite(const SatRenderData& sat, double centerLat, double centerLon, double userLat, double userLon);
};
