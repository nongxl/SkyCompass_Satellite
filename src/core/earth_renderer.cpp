#pragma GCC optimize ("O3")
#pragma GCC optimize ("fast-math")

#include "earth_renderer.h"
#include "earth_data.h"
#include "light_points_data.h"
#include <math.h>

#define DEG_TO_RAD 0.017453292519943295769236907684886

struct BrightStar {
    const char* name;
    float ra; // Degrees
    float dec; // Degrees
    float mag; // Visual magnitude
    uint16_t color; // 16-bit color
};

// Top 25 brightest stars for background reference
const BrightStar BRIGHT_STARS[] = {
    {"Sirius", 101.2871f, -16.7161f, -1.46f, 0xFFFF}, // TFT_WHITE
    {"Canopus", 95.9879f, -52.6956f, -0.74f, 0xFFFF},
    {"Rigil Kentaurus", 219.9021f, -60.8339f, -0.27f, 0xFFE0}, // TFT_YELLOW
    {"Arcturus", 213.9154f, 19.1822f, -0.05f, 0xFD20}, // TFT_ORANGE
    {"Vega", 279.2346f, 38.7836f, 0.03f, 0xFFFF},
    {"Capella", 79.1725f, 45.9981f, 0.08f, 0xFFE0},
    {"Rigel", 78.6346f, -8.2017f, 0.13f, 0x07FF}, // TFT_CYAN
    {"Procyon", 114.8254f, 5.2250f, 0.34f, 0xFFFF},
    {"Achernar", 24.4283f, -57.2367f, 0.46f, 0x07FF},
    {"Betelgeuse", 88.7929f, 7.4069f, 0.5f, 0xF800}, // TFT_RED
    {"Hadar", 210.9558f, -60.3731f, 0.61f, 0x07FF},
    {"Altair", 297.6958f, 8.8683f, 0.76f, 0xFFFF},
    {"Acrux", 186.6496f, -63.0992f, 0.76f, 0x07FF},
    {"Aldebaran", 68.9800f, 16.5092f, 0.86f, 0xFD20},
    {"Antares", 247.3517f, -26.4319f, 0.96f, 0xF800},
    {"Spica", 201.2983f, -11.1614f, 0.97f, 0x07FF},
    {"Pollux", 116.3287f, 28.0261f, 1.14f, 0xFD20},
    {"Fomalhaut", 344.4125f, -29.6222f, 1.16f, 0xFFFF},
    {"Deneb", 310.3579f, 45.2803f, 1.25f, 0xFFFF},
    {"Mimosa", 191.9300f, -59.6886f, 1.25f, 0x07FF},
    {"Regulus", 152.0929f, 11.9672f, 1.35f, 0x07FF},
    {"Adhara", 183.7862f, -58.7489f, 1.5f, 0x07FF},
    {"Castor", 113.6500f, 31.8883f, 1.58f, 0xFFFF},
    {"Gacrux", 187.7913f, -57.1131f, 1.63f, 0xF800},
    {"Shaula", 263.4021f, -37.1036f, 1.62f, 0x07FF},
};
const int NUM_BRIGHT_STARS = 25;

EarthRenderer::EarthRenderer(M5GFX* display) : _display(display) {
    _canvas = new LGFX_Sprite(_display);
    _centerX = 120; // Cardputer width 240 / 2
    _centerY = 67;  // Cardputer height 135 / 2
    _earthRadius = 55; // Slightly smaller to fit orbits
}

EarthRenderer::~EarthRenderer() {
    _canvas->deleteSprite();
    delete _canvas;
}

void EarthRenderer::begin() {
    _canvas->createSprite(_display->width(), _display->height());
}

void EarthRenderer::setSunPosition(double subsolarLat, double subsolarLon) {
    _subsolarLat = subsolarLat;
    _subsolarLon = subsolarLon;
    _hasSunData = true;
}

void EarthRenderer::setCameraAttitude(float pitch, float roll, float yaw) {
    _cameraPitch = pitch;
    _cameraRoll = roll;
    _cameraYaw = yaw;
}

// Check if a point is in the day side (angular distance to sun < 90 deg)
bool isDaylight(double lat, double lon, double subLat, double subLon, bool hasSun) {
    if (!hasSun) return true;
    float latR = (float)lat * DEG_TO_RAD;
    float lonR = (float)lon * DEG_TO_RAD;
    float subLatR = (float)subLat * DEG_TO_RAD;
    float subLonR = (float)subLon * DEG_TO_RAD;
    float cos_dist = sinf(subLatR)*sinf(latR) + cosf(subLatR)*cosf(latR)*cosf(lonR - subLonR);
    return cos_dist > 0;
}

// Check if a satellite is in Earth's shadow (cylindrical shadow model)
bool isSatelliteInShadow(double lat, double lon, double alt, double subLat, double subLon, bool hasSun) {
    if (!hasSun) return false;
    float latR = (float)lat * DEG_TO_RAD;
    float lonR = (float)lon * DEG_TO_RAD;
    float subLatR = (float)subLat * DEG_TO_RAD;
    float subLonR = (float)subLon * DEG_TO_RAD;
    
    float cos_theta = sinf(subLatR)*sinf(latR) + cosf(subLatR)*cosf(latR)*cosf(lonR - subLonR);
    if (cos_theta >= 0) return false; // Day side
    
    float r = 6371.0f + (float)alt; // Actual Earth radius + alt (in km)
    float dist_sq = r * r * (1.0f - cos_theta * cos_theta);
    
    // If distance from axis is less than Earth radius, it's eclipsed
    return dist_sq < (6371.0f * 6371.0f);
}

bool EarthRenderer::projectOrthographic(double lat, double lon, double alt, double centerLat, double centerLon, int& outX, int& outY) {
    float latRad = (float)lat * DEG_TO_RAD;
    float lonRad = (float)lon * DEG_TO_RAD;
    float cLatRad = (float)centerLat * DEG_TO_RAD;
    float cLonRad = (float)centerLon * DEG_TO_RAD;

    // Radius scaling: Earth radius + non-linear altitude scale
    float r = _earthRadius;
    if (alt > 0) {
        float visualAlt = alt;
        if (visualAlt > 20000.0f) visualAlt = 20000.0f;
        r += sqrtf(visualAlt) * 0.4f * _zoom; // scale altitude visual with zoom
    }

    float cos_c = sinf(cLatRad) * sinf(latRad) + cosf(cLatRad) * cosf(latRad) * cosf(lonRad - cLonRad);

    float x = r * cosf(latRad) * sinf(lonRad - cLonRad);
    float y = r * (cosf(cLatRad) * sinf(latRad) - sinf(cLatRad) * cosf(latRad) * cosf(lonRad - cLonRad));
    float z = r * cos_c; // Depth towards camera

    // AR Camera Effect: True 3D Pitch and Roll
    float pitchRad = _cameraPitch * DEG_TO_RAD;
    
    float z_pitched = y * sinf(pitchRad) + z * cosf(pitchRad);
    float y_pitched = y * cosf(pitchRad) - z * sinf(pitchRad);
    
    if (z_pitched < 0) {
        if (alt <= 0) return false; // On or below surface, strictly occluded
        // For altitude > 0, check if it's occluded by the Earth body
        float distSq = x * x + y_pitched * y_pitched;
        if (distSq < _earthRadius * _earthRadius) {
            return false;
        }
    }
    
    // AR Camera Effect: True 3D Roll (rotating the camera around the forward axis)
    float rollRad = -_cameraRoll * DEG_TO_RAD; // Negative to match natural tilt direction
    
    float rotatedX = x * cosf(rollRad) - y_pitched * sinf(rollRad);
    float rotatedY = x * sinf(rollRad) + y_pitched * cosf(rollRad);

    outX = _centerX + _centerOffsetX + (int)rotatedX;
    outY = _centerY + _centerOffsetY - (int)rotatedY;
    return true;
}

void EarthRenderer::drawContinents(double centerLat, double centerLon) {
    // Pre-calculate constants for this frame to save THOUSANDS of CPU cycles
    float cLatRad = (float)centerLat * DEG_TO_RAD;
    float cLonRad = (float)centerLon * DEG_TO_RAD;
    float sin_cLat = sinf(cLatRad);
    float cos_cLat = cosf(cLatRad);
    float sin_cLon = sinf(cLonRad);
    float cos_cLon = cosf(cLonRad);
    
    float rollRad = -_cameraRoll * DEG_TO_RAD;
    float sin_roll = sinf(rollRad);
    float cos_roll = cosf(rollRad);
    
    float pitchRad = _cameraPitch * DEG_TO_RAD;
    float sin_pitch = sinf(pitchRad);
    float cos_pitch = cosf(pitchRad);
    
    float subLatR = (float)_subsolarLat * DEG_TO_RAD;
    float subLonR = (float)_subsolarLon * DEG_TO_RAD;
    float sin_subLat = sinf(subLatR);
    float cos_subLat = cosf(subLatR);
    float sin_subLon = sinf(subLonR);
    float cos_subLon = cosf(subLonR);

    auto drawPath = [&](const MapPoint* pts, int count) {
        int prevX = -1, prevY = -1;
        bool prevVisible = false;
        for (int j = 0; j < count; j++) {
            float sin_lat = pts[j].sinLat;
            float cos_lat = pts[j].cosLat;
            float sin_lon = pts[j].sinLon;
            float cos_lon = pts[j].cosLon;
            float latRad = pts[j].latRad;
            
            // Using identities:
            // cos(lon - cLon) = cos_lon * cos_cLon + sin_lon * sin_cLon
            // sin(lon - cLon) = sin_lon * cos_cLon - cos_lon * sin_cLon
            float cos_dLon = cos_lon * cos_cLon + sin_lon * sin_cLon;
            float sin_dLon = sin_lon * cos_cLon - cos_lon * sin_cLon;
            
            float cos_c = sin_cLat * sin_lat + cos_cLat * cos_lat * cos_dLon;
            
            float r = (float)_earthRadius;
            float x = r * cos_lat * sin_dLon;
            float y = r * (cos_cLat * sin_lat - sin_cLat * cos_lat * cos_dLon);
            float z = r * cos_c;
            
            float z_pitched = y * sin_pitch + z * cos_pitch;
            
            if (z_pitched >= 0) {
                float y_pitched = y * cos_pitch - z * sin_pitch;
                
                float rotatedX = x * cos_roll - y_pitched * sin_roll;
                float rotatedY = x * sin_roll + y_pitched * cos_roll;
                int outX = _centerX + _centerOffsetX + (int)rotatedX;
                int outY = _centerY + _centerOffsetY - (int)rotatedY;
                
                if (prevVisible) {
                    if (abs(outX - prevX) < 100 && abs(outY - prevY) < 100) {
                        uint8_t cr = 50, cg = 150, cb = 50;
                        if (latRad > 0) {
                            float factor = latRad / 1.57079632679f;
                            if (factor > 1.0f) factor = 1.0f;
                            cr = (uint8_t)(50 * (1 - factor));
                            cb = (uint8_t)(50 * (1 - factor) + 150 * factor);
                        } else {
                            float factor = -latRad / 1.57079632679f;
                            if (factor > 1.0f) factor = 1.0f;
                            cg = (uint8_t)(150 * (1 - factor) + 50 * factor);
                            cb = (uint8_t)(50 * (1 - factor) + 150 * factor);
                        }
                        
                        if (_hasSunData) {
                            float cos_lon_subLon = cos_lon * cos_subLon + sin_lon * sin_subLon;
                            float cos_dist = sin_subLat * sin_lat + cos_subLat * cos_lat * cos_lon_subLon;
                            float illum = (cos_dist + 0.2f) / 0.4f;
                            if (illum > 1.0f) illum = 1.0f;
                            if (illum < 0.45f) illum = 0.45f;
                            cr = (uint8_t)(cr * illum);
                            cg = (uint8_t)(cg * illum);
                            cb = (uint8_t)(cb * illum);
                        }
                        
                        uint16_t color = _display->color565(cr, cg, cb);
                        
                        // Actual land line
                        _canvas->drawLine(prevX, prevY, outX, outY, color);
                    }
                }
                prevX = outX;
                prevY = outY;
                prevVisible = true;
            } else {
                prevVisible = false;
            }
        }
    };

    for (int i = 0; i < world_map_count; i++) {
        drawPath(world_map[i].points, world_map[i].length);
    }
}

void EarthRenderer::drawStars(double centerLat, double centerLon) {
    if (_unixTime == 0) return; // Time not set yet
    
    // Calculate Greenwich Mean Sidereal Time (GMST)
    double JD = _unixTime / 86400.0 + 2440587.5;
    double T = (JD - 2451545.0) / 36525.0;
    double GMST_deg = fmod(280.46061837 + 360.98564736629 * (JD - 2451545.0) + 0.000387933 * T * T, 360.0);
    if (GMST_deg < 0) GMST_deg += 360.0;
    
    float GMST_rad = (float)(GMST_deg * DEG_TO_RAD);
    float cLatRad = (float)(centerLat * DEG_TO_RAD);
    float cLonRad = (float)(centerLon * DEG_TO_RAD);
    float sin_cLat = sinf(cLatRad);
    float cos_cLat = cosf(cLatRad);
    
    float pitchRad = _cameraPitch * DEG_TO_RAD;
    float rollRad = -_cameraRoll * DEG_TO_RAD;
    float sin_pitch = sinf(pitchRad);
    float cos_pitch = cosf(pitchRad);
    float sin_roll = sinf(rollRad);
    float cos_roll = cosf(rollRad);
    
    // Radius of the virtual celestial sphere
    float R_sky = 250.0f; 
    
    for (int i = 0; i < NUM_BRIGHT_STARS; i++) {
        const auto& star = BRIGHT_STARS[i];
        
        float ra_rad = star.ra * DEG_TO_RAD;
        float dec_rad = star.dec * DEG_TO_RAD;
        
        // Longitude of the star's projection on Earth is RA - GMST
        float star_lon_rad = ra_rad - GMST_rad;
        
        float sin_lat = sinf(dec_rad);
        float cos_lat = cosf(dec_rad);
        float dLon = star_lon_rad - cLonRad;
        
        // Calculate unpitched coordinates
        float cos_c = sin_cLat * sin_lat + cos_cLat * cos_lat * cosf(dLon);
        float x = R_sky * cos_lat * sinf(dLon);
        float y = R_sky * (cos_cLat * sin_lat - sin_cLat * cos_lat * cosf(dLon));
        float z = R_sky * cos_c;
        
        // Apply Camera Pitch
        float y_pitched = y * cos_pitch - z * sin_pitch;
        
        // Star is visible if it's not blocked by the Earth sphere.
        // Earth is at (0,0).
        float distSq = x * x + y_pitched * y_pitched;
        float earthR = _earthRadius + 1.0f;
        
        if (distSq > earthR * earthR) {
            
            // Apply Camera Roll
            float rotatedX = x * cos_roll - y_pitched * sin_roll;
            float rotatedY = x * sin_roll + y_pitched * cos_roll;
            
            int outX = _centerX + _centerOffsetX + (int)rotatedX;
            int outY = _centerY + _centerOffsetY - (int)rotatedY;
            
            // Avoid drawing over the Earth's body itself
            // Calculate distance from screen center of the Earth projection
            int circleX = _centerX + _centerOffsetX;
            int circleY = _centerY + _centerOffsetY;
            
            float dist_sq = (outX - circleX) * (outX - circleX) + (outY - circleY) * (outY - circleY);
            if (dist_sq > (_earthRadius + 2) * (_earthRadius + 2)) {
                // Dim the star based on its visual magnitude
                uint16_t color = star.color;
                if (star.mag > 0.5f) {
                    // Fake dimming by just using gray (not perfect color accuracy but works for background)
                    color = _display->color565(120, 120, 140);
                }
                
                // Brighter stars are drawn bigger
                if (star.mag < 0.0f) {
                    _canvas->fillRect(outX - 1, outY - 1, 2, 2, color);
                } else {
                    _canvas->drawPixel(outX, outY, color);
                }
            }
        }
    }
}

void EarthRenderer::drawEarth(double centerLat, double centerLon, double userLat, double userLon) {
    // The center of the earth is at (0, 0, 0) relative to projection.
    
    int circleX = _centerX + _centerOffsetX;
    int circleY = _centerY + _centerOffsetY;
    
    // Fill earth background
    _canvas->fillCircle(circleX, circleY, _earthRadius, _display->color565(10, 20, 30));
    _canvas->drawCircle(circleX, circleY, _earthRadius, _display->color565(30, 60, 100));
    
    // Draw continents
    drawContinents(centerLat, centerLon);
    
    // Draw city light pollution on the dark side
    drawLightPollution(centerLat, centerLon);
    
    // Draw user location as a map pin 📍
    int ux, uy;
    if (userLat <= 90.0 && projectOrthographic(userLat, userLon, 0, centerLat, centerLon, ux, uy)) {
        int headX = ux;
        int headY = uy - 6;
        _canvas->fillTriangle(ux, uy, headX - 3, headY + 1, headX + 3, headY + 1, TFT_RED);
        _canvas->fillCircle(headX, headY, 3, TFT_RED);
        _canvas->drawPixel(headX, headY, TFT_WHITE);
    }
    
    // Draw Sun Indicator (Concentric glowing halos mapped to 3D sphere)
    if (_hasSunData) {
        float subLatR = (float)_subsolarLat * DEG_TO_RAD;
        float subLonR = (float)_subsolarLon * DEG_TO_RAD;
        float S_x = cosf(subLatR) * cosf(subLonR);
        float S_y = cosf(subLatR) * sinf(subLonR);
        float S_z = sinf(subLatR);
        
        float U_x = -S_y;
        float U_y = S_x;
        float U_z = 0;
        float U_len = sqrt(U_x*U_x + U_y*U_y);
        if (U_len > 0.001f) {
            U_x /= U_len; U_y /= U_len;
        } else {
            U_x = 1; U_y = 0; U_z = 0;
        }
        
        float V_x = -S_z * U_y;
        float V_y = S_z * U_x;
        float V_z = S_x * U_y - S_y * U_x;
        
        float alphas[] = { 1.5f, 4.0f, 8.0f, 15.0f };
        uint16_t colors[] = { 
            _display->color565(255, 255, 200), 
            _display->color565(200, 200, 50), 
            _display->color565(100, 100, 20), 
            _display->color565(40, 40, 10) 
        };
        
        float cLatRad = (float)centerLat * DEG_TO_RAD;
        float cLonRad = (float)centerLon * DEG_TO_RAD;
        float cos_cLat = cosf(cLatRad);
        float sin_cLat = sinf(cLatRad);
        float cos_cLon = cosf(cLonRad);
        float sin_cLon = sinf(cLonRad);
        float rollRad = -_cameraRoll * DEG_TO_RAD;
        float cos_roll = cosf(rollRad);
        float sin_roll = sinf(rollRad);
        float r = _earthRadius;

        for (int a = 0; a < 4; a++) {
            float alphaR = alphas[a] * DEG_TO_RAD;
            float cos_a = cosf(alphaR);
            float sin_a = sinf(alphaR);
            
            int prevX = -1, prevY = -1;
            bool prevVisible = false;
            
            for (int i = 0; i <= 360; i += 10) {
                float rad = i * DEG_TO_RAD;
                float cos_rad = cosf(rad);
                float sin_rad = sinf(rad);
                
                float P_x = S_x * cos_a + U_x * sin_a * cos_rad + V_x * sin_a * sin_rad;
                float P_y = S_y * cos_a + U_y * sin_a * cos_rad + V_y * sin_a * sin_rad;
                float P_z = S_z * cos_a + U_z * sin_a * cos_rad + V_z * sin_a * sin_rad;
                
                // Direct cartesian projection bypassing expensive asin/atan2
                float term2 = P_x * cos_cLon + P_y * sin_cLon;
                float cos_c = sin_cLat * P_z + cos_cLat * term2;
                
                float proj_x = r * (P_y * cos_cLon - P_x * sin_cLon);
                float proj_y = r * (cos_cLat * P_z - sin_cLat * term2);
                float proj_z = r * cos_c;
                
                float pitchRad = _cameraPitch * DEG_TO_RAD;
                float z_pitched = proj_y * sinf(pitchRad) + proj_z * cosf(pitchRad);
                bool visible = z_pitched >= 0;
                
                int x = -1, y = -1;
                if (visible) {
                    float y_pitched = proj_y * cosf(pitchRad) - proj_z * sinf(pitchRad);
                    
                    float rotatedX = proj_x * cos_roll - y_pitched * sin_roll;
                    float rotatedY = proj_x * sin_roll + y_pitched * cos_roll;
                    x = _centerX + _centerOffsetX + (int)rotatedX;
                    y = _centerY + _centerOffsetY - (int)rotatedY;
                }
                
                if (visible && prevVisible) {
                    if (abs(x - prevX) < 100 && abs(y - prevY) < 100) {
                        _canvas->drawLine(prevX, prevY, x, y, colors[a]);
                    }
                }
                prevX = x;
                prevY = y;
                prevVisible = visible;
            }
        }
    }
    
    // Draw Pole Anchors
    int px, py;
    if (projectOrthographic(90, 0, 0, centerLat, centerLon, px, py)) {
        _canvas->drawLine(px - 2, py, px + 2, py, TFT_CYAN);
        _canvas->drawLine(px, py - 2, px, py + 2, TFT_CYAN);
        int ax, ay;
        if (projectOrthographic(90, 0, 800, centerLat, centerLon, ax, ay)) {
            _canvas->drawLine(px, py, ax, ay, TFT_CYAN);
        }
        _canvas->setTextColor(TFT_CYAN);
        _canvas->drawString("N", px + 4, py - 4);
    }
    if (projectOrthographic(-90, 0, 0, centerLat, centerLon, px, py)) {
        _canvas->drawLine(px - 2, py, px + 2, py, _display->color565(100, 100, 255));
        _canvas->drawLine(px, py - 2, px, py + 2, _display->color565(100, 100, 255));
        int ax, ay;
        if (projectOrthographic(-90, 0, 800, centerLat, centerLon, ax, ay)) {
            _canvas->drawLine(px, py, ax, ay, _display->color565(100, 100, 255));
        }
        _canvas->setTextColor(_display->color565(100, 100, 255));
        _canvas->drawString("S", px + 4, py - 4);
    }
    
    // Draw Dynamic North Arrow
    /*
    {
        int nx, ny;
        double targetLat = centerLat + 1.0;
        double targetLon = centerLon;
        if (targetLat > 90.0) {
            targetLat = 89.0;
            targetLon = centerLon + 180.0;
        }
        
        if (projectOrthographic(targetLat, targetLon, 0, centerLat, centerLon, nx, ny)) {
            float dx = nx - (_centerX + _centerOffsetX);
            float dy = ny - (_centerY + _centerOffsetY);
            float len = sqrt(dx*dx + dy*dy);
            if (len > 0.1f) {
                dx /= len;
                dy /= len;
                
                int cx = _canvas->width() - 25;
                int cy = _canvas->height() / 2;
                
                _canvas->fillCircle(cx, cy, 12, _display->color565(20, 20, 30));
                _canvas->drawCircle(cx, cy, 12, TFT_DARKGRAY);
                
                _canvas->drawLine(cx, cy, cx + (int)(dx * 10), cy + (int)(dy * 10), TFT_RED);
                _canvas->fillTriangle(
                    cx + (int)(dx * 10), cy + (int)(dy * 10),
                    cx + (int)(dx * 4 - dy * 3), cy + (int)(dy * 4 + dx * 3),
                    cx + (int)(dx * 4 + dy * 3), cy + (int)(dy * 4 - dx * 3),
                    TFT_RED
                );
                _canvas->setTextColor(TFT_WHITE);
                _canvas->drawString("N", cx - 3, cy + 14);
            }
        }
    }
    */
}

void EarthRenderer::drawSatellite(const SatRenderData& sat, double centerLat, double centerLon, double userLat, double userLon) {
    if (sat.pastOrbit && sat.futureOrbit) {
        if (sat.pastOrbit->empty() && sat.futureOrbit->empty()) return;
    } else {
        return;
    }
    
    // Check observer visibility
    bool isVisibleToObserver = false;
    bool isEclipsed = false;
    
    if (_hasSunData) {
        float uLatR = userLat * DEG_TO_RAD;
        float uLonR = userLon * DEG_TO_RAD;
        float sLatR = sat.currentPos.lat * DEG_TO_RAD;
        float sLonR = sat.currentPos.lon * DEG_TO_RAD;
        float subLatR = _subsolarLat * DEG_TO_RAD;
        float subLonR = _subsolarLon * DEG_TO_RAD;
        
        // Satellite elevation from observer
        float cos_dist = sinf(uLatR)*sinf(sLatR) + cosf(uLatR)*cosf(sLatR)*cosf(uLonR - sLonR);
        float cos_horizon = 6371.0f / (6371.0f + (float)sat.currentPos.alt);
        bool isAboveHorizon = cos_dist > cos_horizon;
        
        // Observer sun altitude
        float sun_cos_dist = sinf(uLatR)*sinf(subLatR) + cosf(uLatR)*cosf(subLatR)*cosf(uLonR - subLonR);
        float sun_alt = asinf(sun_cos_dist) * RAD_TO_DEG;
        
        // Heavens-Above uses a strict -5.0 degrees sun altitude threshold to cut off visible passes in the morning.
        bool isNight = sun_alt < -5.0f;
        
        isEclipsed = isSatelliteInShadow(sat.currentPos.lat, sat.currentPos.lon, sat.currentPos.alt, _subsolarLat, _subsolarLon, _hasSunData);
        
        if (_observerConstrained) {
            isVisibleToObserver = (isNight && isAboveHorizon && !isEclipsed);
        } else {
            // In Sat View Mode, satellite is bright as long as it is illuminated by the sun
            isVisibleToObserver = !isEclipsed;
        }
    }

    // Draw Orbit
    auto drawOrbit = [&](const std::vector<GeodeticCoord>& orbit, uint16_t baseColor) {
        int prevX = -1, prevY = -1;
        bool prevVisible = false;
        for (const auto& pt : orbit) {
            int x, y;
            bool visible = projectOrthographic(pt.lat, pt.lon, pt.alt, centerLat, centerLon, x, y);
            if (visible && prevVisible) {
                if (abs(x - prevX) < 100 && abs(y - prevY) < 100) {
                    bool shadow = false;
                    if (!_isFastForwarding) {
                        shadow = isSatelliteInShadow(pt.lat, pt.lon, pt.alt, _subsolarLat, _subsolarLon, _hasSunData);
                    }
                    uint16_t color = shadow ? _display->color565(70, 70, 80) : baseColor;
                    _canvas->drawLine(prevX, prevY, x, y, color);
                }
            }
            prevX = x;
            prevY = y;
            prevVisible = visible;
        }
    };
    
    // Dimmed colors for orbit
    uint16_t pastColor = _display->color565(60, 60, 60);
    uint16_t futureColor = _display->color565(120, 120, 120);
    
    if (sat.pastOrbit) drawOrbit(*(sat.pastOrbit), pastColor);
    if (sat.futureOrbit) drawOrbit(*(sat.futureOrbit), futureColor);
    
    // Draw Satellite Current Position
    int sx, sy;
    if (projectOrthographic(sat.currentPos.lat, sat.currentPos.lon, sat.currentPos.alt, centerLat, centerLon, sx, sy)) {
        // Render colorful if visible to observer, otherwise render gray
        uint16_t drawColor = isVisibleToObserver ? sat.color : _display->color565(100, 100, 100);
        bool renderDark = !isVisibleToObserver;
        
        if (sat.iconType == ICON_STATION) {
            // 空间站 (核心舱+大太阳能帆板)
            _canvas->fillRect(sx - 2, sy - 1, 5, 3, renderDark ? _display->color565(80,80,80) : TFT_WHITE);
            _canvas->fillRect(sx - 7, sy - 3, 4, 7, drawColor);
            _canvas->fillRect(sx + 4, sy - 3, 4, 7, drawColor);
        } else if (sat.iconType == ICON_ROCKET) {
            // 火箭残骸 (圆柱体+尾喷口)
            _canvas->fillRect(sx - 2, sy - 4, 5, 8, renderDark ? _display->color565(80,80,80) : TFT_WHITE);
            _canvas->fillTriangle(sx - 2, sy - 4, sx + 2, sy - 4, sx, sy - 7, drawColor);
            _canvas->fillRect(sx - 2, sy + 4, 2, 2, TFT_ORANGE); // Engine 1
            _canvas->fillRect(sx + 1, sy + 4, 2, 2, TFT_ORANGE); // Engine 2
        } else if (sat.iconType == ICON_TELESCOPE) {
            // 望远镜 (长圆筒+镜头盖+小帆板)
            _canvas->fillRect(sx - 2, sy - 3, 5, 7, renderDark ? _display->color565(80,80,80) : TFT_WHITE);
            _canvas->fillRect(sx - 3, sy - 4, 7, 2, renderDark ? _display->color565(50,50,50) : TFT_LIGHTGRAY);
            _canvas->fillRect(sx - 6, sy, 3, 2, drawColor);
            _canvas->fillRect(sx + 4, sy, 3, 2, drawColor);
        } else if (sat.iconType == ICON_DEEPSPACE) {
            // 深空天体 (星芒图标)
            _canvas->drawLine(sx, sy - 5, sx, sy + 5, drawColor);
            _canvas->drawLine(sx - 5, sy, sx + 5, sy, drawColor);
            _canvas->drawLine(sx - 2, sy - 2, sx + 2, sy + 2, renderDark ? _display->color565(80,80,80) : TFT_WHITE);
            _canvas->drawLine(sx - 2, sy + 2, sx + 2, sy - 2, renderDark ? _display->color565(80,80,80) : TFT_WHITE);
        } else {
            // 普通卫星 (小盒子+单侧或不对称帆板)
            _canvas->fillRect(sx - 1, sy - 1, 3, 3, renderDark ? _display->color565(80,80,80) : TFT_WHITE);
            _canvas->fillRect(sx - 5, sy - 1, 3, 3, drawColor);
            _canvas->drawLine(sx - 2, sy, sx - 1, sy, TFT_LIGHTGRAY);
        }
        
        _canvas->setTextColor(drawColor);
        _canvas->setTextSize(1);
        _canvas->drawString(sat.name, sx + 8, sy - 4);
    }
}

void EarthRenderer::render(double centerLat, double centerLon, double userLat, double userLon, const std::vector<SatRenderData>& satellites) {
    static uint32_t lastTime = 0;
    static int frames = 0;
    static int currentFPS = 0;
    
    _canvas->fillSprite(BLACK);
    
    drawStars(centerLat, centerLon);
    drawEarth(centerLat, centerLon, userLat, userLon);
    
    for (const auto& sat : satellites) {
        drawSatellite(sat, centerLat, centerLon, userLat, userLon);
    }
    
    frames++;
    uint32_t now = millis();
    if (now - lastTime >= 1000) {
        currentFPS = frames;
        frames = 0;
        lastTime = now;
    }
}

void EarthRenderer::drawLightPollution(double centerLat, double centerLon) {
    if (!_hasSunData) return;
    
    float subLatR = (float)_subsolarLat * DEG_TO_RAD;
    float subLonR = (float)_subsolarLon * DEG_TO_RAD;
    float sin_subLat = sinf(subLatR);
    float cos_subLat = cosf(subLatR);
    float sin_subLon = sinf(subLonR);
    float cos_subLon = cosf(subLonR);
    
    float cLatRad = (float)centerLat * DEG_TO_RAD;
    float cLonRad = (float)centerLon * DEG_TO_RAD;
    float sin_cLat = sinf(cLatRad);
    float cos_cLat = cosf(cLatRad);
    float sin_cLon = sinf(cLonRad);
    float cos_cLon = cosf(cLonRad);
    
    float rollRad = -_cameraRoll * DEG_TO_RAD;
    float sin_roll = sinf(rollRad);
    float cos_roll = cosf(rollRad);
    
    float pitchRad = _cameraPitch * DEG_TO_RAD;
    float sin_pitch = sinf(pitchRad);
    float cos_pitch = cosf(pitchRad);
    
    float r = (float)_earthRadius;
    
    int width = _canvas->width();
    int height = _canvas->height();
    std::uint16_t* buffer = (std::uint16_t*)_canvas->getBuffer();
    if (!buffer) return;
    
    // Draw 1/4 of the light points during fast-forwarding to maintain 30 FPS, full 3000 points when static.
    int step = _isFastForwarding ? 4 : 1;
    
    for (int i = 0; i < light_points_count; i += step) {
        float sin_lat = light_points[i].sinLat;
        float cos_lat = light_points[i].cosLat;
        float sin_lon = light_points[i].sinLon;
        float cos_lon = light_points[i].cosLon;
        
        // 1. Determine if the point is in darkness (cos_dist <= 0.05f)
        float cos_lon_subLon = cos_lon * cos_subLon + sin_lon * sin_subLon;
        float cos_dist = sin_subLat * sin_lat + cos_subLat * cos_lat * cos_lon_subLon;
        
        if (cos_dist <= 0.05f) {
            // 2. Early occlusion check: check if on front hemisphere (visible to camera)
            float cos_dLon = cos_lon * cos_cLon + sin_lon * sin_cLon;
            float cos_c = sin_cLat * sin_lat + cos_cLat * cos_lat * cos_dLon;
            
            if (cos_c < 0.0f) continue;
            
            // 3. Complete orthographic projection
            float sin_dLon = sin_lon * cos_cLon - cos_lon * sin_cLon;
            
            float x = r * cos_lat * sin_dLon;
            float y = r * (cos_cLat * sin_lat - sin_cLat * cos_lat * cos_dLon);
            float z = r * cos_c;
            
            float z_pitched = y * sin_pitch + z * cos_pitch;
            
            if (z_pitched >= 0.0f) {
                float y_pitched = y * cos_pitch - z * sin_pitch;
                float rotatedX = x * cos_roll - y_pitched * sin_roll;
                float rotatedY = x * sin_roll + y_pitched * cos_roll;
                
                int outX = _centerX + _centerOffsetX + (int)rotatedX;
                int outY = _centerY + _centerOffsetY - (int)rotatedY;
                
                if (outX >= 0 && outX < width && outY >= 0 && outY < height) {
                    float factor = (0.05f - cos_dist) / 0.20f;
                    if (factor > 1.0f) factor = 1.0f;
                    if (factor < 0.0f) factor = 0.0f;
                    
                    uint8_t pr = (uint8_t)(255 * factor);
                    uint8_t pg = (uint8_t)(200 * factor);
                    uint8_t pb = (uint8_t)(90 * factor);
                    
                    std::uint16_t color = _display->color565(pr, pg, pb);
                    buffer[outY * width + outX] = __builtin_bswap16(color);
                }
            }
        }
    }
}

