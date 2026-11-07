#pragma once

#include <M5Cardputer.h>
#include <vector>
#include "coord_transform.h"
#include "tle_data.h"

class SGP4Calc;

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
    ICON_COMMUNICATION,
    ICON_DEBRIS,
    ICON_SPACEPLANE,   // X-37B 航天飞机形状
    ICON_SOLAR_PROBE,  // Parker Solar Probe 等太阳探测器
    ICON_CHAIN_MONO,   // M5Stack Chain Mono 外设模块
    ICON_LANDER        // 月球/行星着陆器
};

enum TrainState {
    VERY_TIGHT,
    TIGHT,
    EXPANDING,
    OPERATIONAL
};

struct InertialOrbitNode {
    float tx;
    float ty;
    float tz;
};

// Inertial-frame compact orbit cache (rotates in real time at 60 FPS with zero SGP4 overhead)
struct OrbitCache {
    uint32_t lastCalcTime = 0;
    float periodSec = 5400.0f;
    float raanRateRadPerSec = 0.0f;
    bool isClosedLoop = true;
    bool isFocusRes = false;
    std::vector<InertialOrbitNode> ring;

    void clearAndShrink() {
        lastCalcTime = 0;
        periodSec = 5400.0f;
        raanRateRadPerSec = 0.0f;
        isClosedLoop = true;
        isFocusRes = false;
        std::vector<InertialOrbitNode>().swap(ring);
    }
};

struct SatRenderData {
    const char* name;
    SatIconType iconType;
    GeodeticCoord currentPos;
    const OrbitCache* orbitCache = nullptr;
    double continuousSimTime = 0.0;
    double gmstRad = 0.0;
    uint16_t color;
    bool isVisible;
    bool isRecentLaunchBatch = false;
    int totalSatellitesInBatch = 0;
    uint32_t launchEpoch = 0;
    uint32_t simTime = 0;
    uint32_t lastCalcTime = 0;
    
    // New fields for Mission Formation Visualization
    const std::vector<FormationPoint>* proxyFormation = nullptr;
    float occupancy = 0.0f;
    float occupancyStartPhase = 0.0f;
    float occupancyEndPhase = 0.0f;
    float repAlongTrackPhase = 0.0f;
    const char* shortName = nullptr;
    
    // Target tracking visual effects
    bool isSelected = false;
    SGP4Calc* calc = nullptr;

    // Radio pass transmitting status (downlink active while in pass)
    bool isRadioTransmitting = false;
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

    void setDrawDecorations(bool draw) { _drawDecorations = draw; }

    void setGnssSearching(bool searching) { _gnssSearching = searching; }

    void setVisualMode(int mode) { _visualMode = mode; }
    int getVisualMode() const { return _visualMode; }

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
    void setUnixTime(double unixTime) {
        _unixTime = unixTime;
    }

    LGFX_Sprite* getCanvas() { return _canvas; }

    static void renderSatelliteIcon(LGFX_Sprite* canvas, int x, int y, SatIconType iconType, uint16_t color, bool renderDark, float intensity = 1.0f);

private:
    double _unixTime = 0.0;
    
    void updateFocusR() {
        _cameraFocusR = _earthRadius;
        _zLimit = _cameraFocusR * (1.0f - _cosPitch);
    }
    void updateFrameConstants(double centerLat, double centerLon);
    bool projectInertial(float tx, float ty, float tz, float cosPsi, float sinPsi, int& outX, int& outY, bool* outInShadow = nullptr) const;

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

    // Precomputed per-frame camera & sun trig constants
    double _cachedCenterLat = 999.0;
    double _cachedCenterLon = 999.0;
    float _cLatRad = 0.0f;
    float _cLonRad = 0.0f;
    float _sinCLat = 0.0f;
    float _cosCLat = 1.0f;
    float _sinCLon = 0.0f;
    float _cosCLon = 1.0f;
    float _sinPitch = 0.0f;
    float _cosPitch = 1.0f;
    float _sinRoll = 0.0f;
    float _cosRoll = 1.0f;
    float _zLimit = 0.0f;
    
    // Sun data
    bool _hasSunData = false;
    double _subsolarLat = 0;
    double _subsolarLon = 0;
    float _subLatRad = 0.0f;
    float _subLonRad = 0.0f;
    float _sinSubLat = 0.0f;
    float _cosSubLat = 1.0f;
    float _sinSubLon = 0.0f;
    float _cosSubLon = 1.0f;
    float _cosSubMinusCLon = 1.0f;
    float _sinSubMinusCLon = 0.0f;
    
    // Rendering configs
    bool _observerConstrained = true;
    bool _isFastForwarding = false;
    int _visualMode = 0; // 0: Normal (With Aurora), 1: Red Night (With Aurora)
    bool _drawDecorations = true;
    bool _gnssSearching = false;

    // Helper functions for orthographic projection
    bool projectOrthographic(double lat, double lon, double alt, double centerLat, double centerLon, int& outX, int& outY);
    
    void drawStars(double centerLat, double centerLon);
    void drawEarth(double centerLat, double centerLon, double userLat, double userLon);
    void drawContinents(double centerLat, double centerLon);
    void drawLightPollution(double centerLat, double centerLon);
    void drawFocusSightLineAndShadow(double centerLat, double centerLon, double userLat, double userLon, const std::vector<SatRenderData>& satellites);
    void drawAuroras(double centerLat, double centerLon);
    void drawSatellite(const SatRenderData& sat, double centerLat, double centerLon, double userLat, double userLon);
    void drawSatelliteIcon(int x, int y, SatIconType iconType, uint16_t color, bool renderDark, float intensity = 1.0f);
};
