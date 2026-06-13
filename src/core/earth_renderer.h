#pragma once

#include <M5Cardputer.h>
#include <vector>
#include "coord_transform.h"

enum SatIconType {
    ICON_SATELLITE,
    ICON_STATION,
    ICON_TELESCOPE,
    ICON_DEEPSPACE
};

struct SatRenderData {
    String name;
    SatIconType iconType;
    GeodeticCoord currentPos;
    std::vector<GeodeticCoord> pastOrbit;
    std::vector<GeodeticCoord> futureOrbit;
    uint16_t color;
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

    LGFX_Sprite* getCanvas() { return _canvas; }

private:
    M5GFX* _display;
    LGFX_Sprite* _canvas;
    
    // Config
    int _centerX;
    int _centerY;
    int _earthRadius;
    
    // Camera
    float _cameraPitch = 0;
    float _cameraRoll = 0;
    float _cameraYaw = 0;
    
    // Sun data
    bool _hasSunData = false;
    double _subsolarLat = 0;
    double _subsolarLon = 0;

    // Helper functions for orthographic projection
    bool projectOrthographic(double lat, double lon, double alt, double centerLat, double centerLon, int& outX, int& outY);
    
    void drawEarth(double centerLat, double centerLon, double userLat, double userLon);
    void drawContinents(double centerLat, double centerLon);
    void drawSatellite(const SatRenderData& sat, double centerLat, double centerLon, double userLat, double userLon);
};
