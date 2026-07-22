#pragma GCC optimize ("O3")
#pragma GCC optimize ("fast-math")

#include "earth_renderer.h"
#include "earth_data.h"
#include "light_points_data.h"
#include "sgp4_calc.h"
#include "i18n.h"
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
    _canvas->setFont(&fonts::efontCN_12);
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
    
    // Translate pivot center to the visual focus point so that rotation is centered on it
    float z_temp = z - _cameraFocusR;
    
    float z_pitched = y * sinf(pitchRad) + z_temp * cosf(pitchRad);
    float y_pitched = y * cosf(pitchRad) - z_temp * sinf(pitchRad);
    
    float z_actual = z_pitched + _cameraFocusR;
    
    float z_limit = _cameraFocusR * (1.0f - cosf(pitchRad));
    if (z_actual < z_limit) {
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
            
            float z_temp = z - _cameraFocusR;
            float z_pitched = y * sin_pitch + z_temp * cos_pitch;
            float z_actual = z_pitched + _cameraFocusR;
            
            float z_limit = _cameraFocusR * (1.0f - cos_pitch);
            if (z_actual >= z_limit) {
                float y_pitched = y * cos_pitch - z_temp * sin_pitch;
                
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
        float z = -R_sky * cos_c;
        
        // Apply Camera Pitch
        float y_pitched = y * cos_pitch - z * sin_pitch;
        float z_pitched = y * sin_pitch + z * cos_pitch;
        
        // Star must be in front of the camera
        if (z_pitched < 0) {
            float denom = -z_pitched / R_sky;
            if (denom < 0.05f) denom = 0.05f;
            
            float x_proj = x / denom;
            float y_proj = y_pitched / denom;
            
            // Apply Camera Roll
            float rotatedX = x_proj * cos_roll - y_proj * sin_roll;
            float rotatedY = x_proj * sin_roll + y_proj * cos_roll;
            
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
    
    // Calculate the projected center of the Earth sphere under camera 3D rotation
    float pitchRad = _cameraPitch * DEG_TO_RAD;
    float rollRad = -_cameraRoll * DEG_TO_RAD;
    
    float center_y_pitched = _cameraFocusR * sinf(pitchRad);
    float center_rotatedX = -center_y_pitched * sinf(rollRad);
    float center_rotatedY = center_y_pitched * cosf(rollRad);
    
    int circleX = _centerX + _centerOffsetX + (int)center_rotatedX;
    int circleY = _centerY + _centerOffsetY - (int)center_rotatedY;
    
    // Fill earth background
    _canvas->fillCircle(circleX, circleY, _earthRadius, _display->color565(10, 20, 30));
    _canvas->drawCircle(circleX, circleY, _earthRadius, _display->color565(30, 60, 100));
    
    // Draw continents
    drawContinents(centerLat, centerLon);
    
    // Draw city light pollution on the dark side
    drawLightPollution(centerLat, centerLon);
    
    // Draw atmosphere and polar effects (only in Gorgeous mode [2])
    if (_visualMode == 2) {
        drawAirglow(centerLat, centerLon);
        drawAuroras(centerLat, centerLon);
    }
    
    // Draw user location as a map pin 📍
    int ux, uy;
    if (_drawDecorations && userLat <= 90.0 && projectOrthographic(userLat, userLon, 0, centerLat, centerLon, ux, uy)) {
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
                float z_temp = proj_z - _cameraFocusR;
                float z_pitched = proj_y * sinf(pitchRad) + z_temp * cosf(pitchRad);
                float z_actual = z_pitched + _cameraFocusR;
                bool visible = z_actual >= 0;
                
                int x = -1, y = -1;
                if (visible) {
                    float y_pitched = proj_y * cosf(pitchRad) - z_temp * sinf(pitchRad);
                    
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
    if (_drawDecorations) {
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

static GeodeticCoord interpolateOrbit(const std::vector<GeodeticCoord>& orbit, float idx) {
    if (orbit.empty()) return {0.0, 0.0, 0.0};
    if (idx <= 0.0f) return orbit.front();
    if (idx >= orbit.size() - 1.0f) return orbit.back();
    
    int i = (int)idx;
    float t = idx - (float)i;
    const auto& p1 = orbit[i];
    const auto& p2 = orbit[i+1];
    
    GeodeticCoord res;
    res.lat = p1.lat + t * (p2.lat - p1.lat);
    
    float dLon = p2.lon - p1.lon;
    if (dLon > 180.0f) dLon -= 360.0f;
    else if (dLon < -180.0f) dLon += 360.0f;
    res.lon = p1.lon + t * dLon;
    if (res.lon > 180.0f) res.lon -= 360.0f;
    else if (res.lon < -180.0f) res.lon += 360.0f;
    
    res.alt = p1.alt + t * (p2.alt - p1.alt);
    return res;
}

static uint16_t scaleColor(uint16_t color, float factor) {
    if (factor >= 1.0f) return color;
    uint8_t r = ((color >> 11) & 0x1F);
    uint8_t g = ((color >> 5) & 0x3F);
    uint8_t b = (color & 0x1F);
    r = (uint8_t)(r * factor);
    g = (uint8_t)(g * factor);
    b = (uint8_t)(b * factor);
    if (r > 0x1F) r = 0x1F;
    if (g > 0x3F) g = 0x3F;
    if (b > 0x1F) b = 0x1F;
    return (r << 11) | (g << 5) | b;
}

void EarthRenderer::drawSatellite(const SatRenderData& sat, double centerLat, double centerLon, double userLat, double userLon) {
    // Check observer visibility (reused from pre-calculated state)
    bool isVisibleToObserver = sat.isVisible;

    // Draw Orbit if orbit data is available
    if (sat.pastOrbit && sat.futureOrbit && (!sat.pastOrbit->empty() || !sat.futureOrbit->empty())) {

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
    }
        
        // Draw Mission Visualization Layer
        if (sat.isRecentLaunchBatch && sat.pastOrbit && sat.futureOrbit && sat.proxyFormation && !sat.proxyFormation->empty()) {
            double ageDays = 30.0;
            if (sat.launchEpoch > 0 && sat.simTime >= sat.launchEpoch) {
                ageDays = (double)(sat.simTime - sat.launchEpoch) / 86400.0;
            }

            uint16_t baseCol = TFT_WHITE;
            if (ageDays <= 2.0) baseCol = TFT_WHITE;
            else if (ageDays <= 14.0) baseCol = 0x07FF; // TFT_CYAN
            else if (ageDays >= 365.0) baseCol = _display->color565(150, 150, 150);
            else baseCol = 0x07FF;

            float breathe = 0.8f + 0.2f * sinf((float)millis() * 0.003f);
            uint16_t missionColor = scaleColor(baseCol, breathe);

            std::vector<GeodeticCoord> mergedOrbit;
            if (sat.pastOrbit) {
                mergedOrbit.insert(mergedOrbit.end(), sat.pastOrbit->begin(), sat.pastOrbit->end());
            }
            if (sat.futureOrbit) {
                mergedOrbit.insert(mergedOrbit.end(), sat.futureOrbit->begin(), sat.futureOrbit->end());
            }

            if (!mergedOrbit.empty()) {
                float preciseCenterIdx = 0.0f;
                
                // Locate the nearest point in the cached orbit to the representative sat's real-time propagated position
                float minD = 1e9f;
                int bestIdx = 0;
                for (size_t i = 0; i < mergedOrbit.size(); i++) {
                    float dLat = mergedOrbit[i].lat - sat.currentPos.lat;
                    float dLon = mergedOrbit[i].lon - sat.currentPos.lon;
                    if (dLon > 180.0f) dLon -= 360.0f;
                    else if (dLon < -180.0f) dLon += 360.0f;
                    float d = dLat * dLat + dLon * dLon;
                    if (d < minD) {
                        minD = d;
                        bestIdx = i;
                    }
                }
                
                // Compute high-precision sub-pixel float index using 2D vector projection in the neighborhood of bestIdx
                // to achieve absolute smooth, millisecond-level continuous sliding without any discrete coordinate jumping or jitter.
                int N = mergedOrbit.size();
                if (N > 1) {
                    int prevIdx = (bestIdx - 1 + N) % N;
                    int nextIdx = (bestIdx + 1) % N;
                    
                    float pLat = sat.currentPos.lat;
                    float pLon = sat.currentPos.lon;
                    
                    float aLat = mergedOrbit[prevIdx].lat;
                    float aLon = mergedOrbit[prevIdx].lon;
                    if (aLon - pLon > 180.0f) aLon -= 360.0f;
                    else if (aLon - pLon < -180.0f) aLon += 360.0f;
                    
                    float bLat = mergedOrbit[bestIdx].lat;
                    float bLon = mergedOrbit[bestIdx].lon;
                    if (bLon - pLon > 180.0f) bLon -= 360.0f;
                    else if (bLon - pLon < -180.0f) bLon += 360.0f;
                    
                    float cLat = mergedOrbit[nextIdx].lat;
                    float cLon = mergedOrbit[nextIdx].lon;
                    if (cLon - pLon > 180.0f) cLon -= 360.0f;
                    else if (cLon - pLon < -180.0f) cLon += 360.0f;
                    
                    float dPA2 = (pLat - aLat) * (pLat - aLat) + (pLon - aLon) * (pLon - aLon);
                    float dPC2 = (pLat - cLat) * (pLat - cLat) + (pLon - cLon) * (pLon - cLon);
                    
                    if (dPA2 < dPC2) {
                        // Project onto segment AB
                        float vAB_lat = bLat - aLat;
                        float vAB_lon = bLon - aLon;
                        float vAP_lat = pLat - aLat;
                        float vAP_lon = pLon - aLon;
                        float lenSq = vAB_lat * vAB_lat + vAB_lon * vAB_lon;
                        float t = 0.5f;
                        if (lenSq > 1e-6f) {
                            t = (vAP_lat * vAB_lat + vAP_lon * vAB_lon) / lenSq;
                            if (t < 0.0f) t = 0.0f;
                            if (t > 1.0f) t = 1.0f;
                        }
                        preciseCenterIdx = (float)prevIdx + t;
                    } else {
                        // Project onto segment BC
                        float vBC_lat = cLat - bLat;
                        float vBC_lon = cLon - bLon;
                        float vBP_lat = pLat - bLat;
                        float vBP_lon = pLon - bLon;
                        float lenSq = vBC_lat * vBC_lat + vBC_lon * vBC_lon;
                        float t = 0.5f;
                        if (lenSq > 1e-6f) {
                            t = (vBP_lat * vBC_lat + vBP_lon * vBC_lon) / lenSq;
                            if (t < 0.0f) t = 0.0f;
                            if (t > 1.0f) t = 1.0f;
                        }
                        preciseCenterIdx = (float)bestIdx + t;
                    }
                } else {
                    preciseCenterIdx = (float)bestIdx;
                }
                
                float dIndex = (float)(mergedOrbit.size() - 1) / 360.0f;
                float maxIdx = (float)(mergedOrbit.size() - 1);

                // 1. Draw Occupancy Light Band
                if (sat.occupancy > 1.0f) {
                    int numSteps = (int)(sat.occupancy / 5.0f) + 2;
                    if (numSteps > 75) numSteps = 75;
                    if (numSteps < 5) numSteps = 5;
                    
                    int prevLx = -1, prevLy = -1;
                    bool prevLVisible = false;
                    
                    uint16_t lightBandColor = scaleColor(baseCol, 0.4f * breathe);
                    
                    for (int step = 0; step < numSteps; step++) {
                        float ratio = (float)step / (float)(numSteps - 1);
                        float P = sat.occupancyStartPhase + ratio * sat.occupancy;
                        if (P >= 360.0f) P -= 360.0f;
                        
                        float diff = P - sat.repAlongTrackPhase;
                        if (diff > 180.0f) diff -= 360.0f;
                        else if (diff < -180.0f) diff += 360.0f;
                        
                        float idx = preciseCenterIdx + diff * dIndex;
                        if (maxIdx > 0.1f) {
                            idx = fmodf(idx, maxIdx);
                            if (idx < 0.0f) idx += maxIdx;
                        } else {
                            idx = 0.0f;
                        }
                        
                        GeodeticCoord pt = interpolateOrbit(mergedOrbit, idx);
                        int lx, ly;
                        bool lVisible = projectOrthographic(pt.lat, pt.lon, pt.alt, centerLat, centerLon, lx, ly);
                        
                        if (lVisible && prevLVisible) {
                            if (abs(lx - prevLx) < 100 && abs(ly - prevLy) < 100) {
                                _canvas->drawLine(prevLx, prevLy, lx, ly, lightBandColor);
                            }
                        }
                        prevLx = lx;
                        prevLy = ly;
                        prevLVisible = lVisible;
                    }
                }

                // 2. Draw Proxy Satellites
                for (const auto& fp : *(sat.proxyFormation)) {
                    float diff = fp.AlongTrackPhase - sat.repAlongTrackPhase;
                    if (diff > 180.0f) diff -= 360.0f;
                    else if (diff < -180.0f) diff += 360.0f;
                    
                    float idx = preciseCenterIdx + diff * dIndex;
                    if (maxIdx > 0.1f) {
                        idx = fmodf(idx, maxIdx);
                        if (idx < 0.0f) idx += maxIdx;
                    } else {
                        idx = 0.0f;
                    }
                    
                    GeodeticCoord pt = interpolateOrbit(mergedOrbit, idx);
                    int px, py;
                    if (projectOrthographic(pt.lat, pt.lon, pt.alt, centerLat, centerLon, px, py)) {
                        uint16_t proxyColor = scaleColor(baseCol, breathe * fp.brightness);
                        _canvas->fillRect(px - 1, py - 1, 3, 3, proxyColor);
                    }
                }
            }
        }
    
    // Draw Fading Motion Trail for the selected satellite (rolling history of past frames)
    if (sat.isSelected && sat.calc) {
        const size_t MAX_TRAIL_STEPS = 4; // reduced from 8 to make trail collapse faster
        struct TrailHistoryEntry {
            uint32_t simTime = 0;
            String name = "";
        };
        static std::vector<TrailHistoryEntry> history;
        
        static String lastSelectedName = "";
        String currentName = sat.name ? sat.name : "";
        if (currentName != lastSelectedName) {
            history.clear();
            lastSelectedName = currentName;
        }
        
        // Add current frame to rolling history every render frame
        TrailHistoryEntry entry;
        entry.simTime = sat.simTime;
        entry.name = currentName;
        history.insert(history.begin(), entry);
        if (history.size() > MAX_TRAIL_STEPS) {
            history.resize(MAX_TRAIL_STEPS);
        }
        
        // Draw the trail using the historical simulated times of the past N frames
        for (size_t i = 1; i < history.size(); i++) {
            uint32_t t_past = history[i].simTime;
            double tx, ty, tz;
            if (sat.calc->getTEME(t_past, tx, ty, tz)) {
                double gmst = CoordTransform::getGMST(CoordTransform::unixToJulian(t_past));
                ECEFCoord ecef = CoordTransform::temeToECEF(tx, ty, tz, gmst);
                GeodeticCoord geo = CoordTransform::ecefToGeodetic(ecef);
                
                int tx_screen, ty_screen;
                if (projectOrthographic(geo.lat, geo.lon, geo.alt, centerLat, centerLon, tx_screen, ty_screen)) {
                    bool shadow = false;
                    if (!_isFastForwarding) {
                        shadow = isSatelliteInShadow(geo.lat, geo.lon, geo.alt, _subsolarLat, _subsolarLon, _hasSunData);
                    }
                    
                    float fadeFactor = (1.0f - (float)i / (float)MAX_TRAIL_STEPS) * 0.6f;
                    uint16_t trailCol = shadow ? _display->color565(60, 60, 70) : sat.color;
                    trailCol = scaleColor(trailCol, fadeFactor);
                    
                    drawSatelliteIcon(tx_screen, ty_screen, sat.iconType, trailCol, shadow, fadeFactor);
                }
            }
        }
    }

    // Draw Satellite Current Position
    int sx, sy;
    if (projectOrthographic(sat.currentPos.lat, sat.currentPos.lon, sat.currentPos.alt, centerLat, centerLon, sx, sy)) {
        // Render colorful if visible to observer, otherwise render gray
        uint16_t drawColor = isVisibleToObserver ? sat.color : _display->color565(100, 100, 100);
        bool renderDark = !isVisibleToObserver;
        float intensity = 1.0f;
        
        // Apply breathing blink effect matching Mono speed and keeping a dim shadow at minimum brightness
        if (sat.isSelected && isVisibleToObserver) {
            intensity = 0.65f + 0.35f * sinf((float)millis() * 0.0025f);
            drawColor = scaleColor(drawColor, intensity);
        }
        
        drawSatelliteIcon(sx, sy, sat.iconType, drawColor, renderDark, intensity);
        
        _canvas->setTextColor(drawColor);
        _canvas->setTextSize(1);
        const char* displayName = sat.name;
        if (sat.isRecentLaunchBatch && sat.shortName != nullptr) {
            displayName = sat.shortName;
        }
        _canvas->drawString(displayName, sx + 8, sy - 4);
    }
}

inline void blendPixelAlpha(LGFX_Sprite* canvas, int x, int y, uint8_t r, uint8_t g, uint8_t b, float alpha) {
    if (alpha <= 0.001f) return;
    int width = canvas->width();
    int height = canvas->height();
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    
    uint16_t* buf = (uint16_t*)canvas->getBuffer();
    if (!buf) return;
    
    int index = y * width + x;
    uint16_t rawCol = buf[index];
    
    uint16_t oldCol = (rawCol >> 8) | (rawCol << 8);
    
    uint8_t r_o = (oldCol >> 11) & 0x1F;
    uint8_t g_o = (oldCol >> 5) & 0x3F;
    uint8_t b_o = oldCol & 0x1F;
    
    uint8_t r_t = (r * 31) / 255;
    uint8_t g_t = (g * 63) / 255;
    uint8_t b_t = (b * 31) / 255;
    
    int r_new = (int)(r_o * (1.0f - alpha) + r_t * alpha);
    int g_new = (int)(g_o * (1.0f - alpha) + g_t * alpha);
    int b_new = (int)(b_o * (1.0f - alpha) + b_t * alpha);
    
    if (r_new > 31) r_new = 31;
    if (g_new > 63) g_new = 63;
    if (b_new > 31) b_new = 31;
    
    uint16_t newCol = (r_new << 11) | (g_new << 5) | b_new;
    buf[index] = (newCol >> 8) | (newCol << 8);
}

inline void fillTriangleAlpha(LGFX_Sprite* canvas, int x0, int y0, int x1, int y1, int x2, int y2, uint8_t r, uint8_t g, uint8_t b, float alpha) {
    if (alpha <= 0.001f) return;
    int minX = std::min({x0, x1, x2});
    int maxX = std::max({x0, x1, x2});
    int minY = std::min({y0, y1, y2});
    int maxY = std::max({y0, y1, y2});

    auto edge = [](int ax, int ay, int bx, int by, int cx, int cy) {
        return (cx - ax) * (by - ay) - (cy - ay) * (bx - ax);
    };

    for (int y = minY; y <= maxY; y++) {
        for (int x = minX; x <= maxX; x++) {
            int w0 = edge(x1, y1, x2, y2, x, y);
            int w1 = edge(x2, y2, x0, y0, x, y);
            int w2 = edge(x0, y0, x1, y1, x, y);
            if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                blendPixelAlpha(canvas, x, y, r, g, b, alpha);
            }
        }
    }
}

inline void fillCircleAlpha(LGFX_Sprite* canvas, int cx, int cy, int rad, uint8_t r, uint8_t g, uint8_t b, float alpha) {
    if (alpha <= 0.001f) return;
    for (int dy = -rad; dy <= rad; dy++) {
        for (int dx = -rad; dx <= rad; dx++) {
            if (dx * dx + dy * dy <= rad * rad) {
                blendPixelAlpha(canvas, cx + dx, cy + dy, r, g, b, alpha);
            }
        }
    }
}

void EarthRenderer::drawFocusSightLineAndShadow(double centerLat, double centerLon, double userLat, double userLon, const std::vector<SatRenderData>& satellites) {
    if (!_drawDecorations || userLat > 90.0) return;

    // 获取观察者地面坐标点屏幕投影 (ux, uy)
    int ux = -1, uy = -1;
    bool userVisible = projectOrthographic(userLat, userLon, 0, centerLat, centerLon, ux, uy);

    // 1. 绘制太阳日影方位对齐线 (Solar Shadow) —— 纯粹水滴形灰色阴影大头针，尖端直连定位点并随太阳方向围绕旋转
    if (_hasSunData && userVisible) {
        float uLatR = (float)userLat * DEG_TO_RAD;
        float uLonR = (float)userLon * DEG_TO_RAD;
        float subLatR = (float)_subsolarLat * DEG_TO_RAD;
        float subLonR = (float)_subsolarLon * DEG_TO_RAD;

        // 计算观察者处的太阳仰角 (Solar Elevation Angle)
        float sin_sunEl = sinf(uLatR) * sinf(subLatR) + cosf(uLatR) * cosf(subLatR) * cosf(subLonR - uLonR);
        if (sin_sunEl > 1.0f) sin_sunEl = 1.0f;
        if (sin_sunEl < -1.0f) sin_sunEl = -1.0f;
        float sunElDeg = asinf(sin_sunEl) * RAD_TO_DEG;

        // 1. 太阳仰角晨昏过渡渐变因子 (太阳仰角在 -1.0° 到 +9.0° 之间 10° 范围平滑渐变)
        float sunFade = (sunElDeg + 1.0f) / 10.0f;
        if (sunFade > 1.0f) sunFade = 1.0f;
        if (sunFade < 0.0f) sunFade = 0.0f;

        // 2. 地球视角边缘 (Limb) 渐变因子 (转动球体至边缘 70%-100% 区域平滑淡出)
        float dxCenter = (float)(ux - _centerX - _centerOffsetX);
        float dyCenter = (float)(uy - _centerY - _centerOffsetY);
        float distFromCenter = sqrtf(dxCenter * dxCenter + dyCenter * dyCenter);
        float maxR = (float)_cameraFocusR;
        float limbFade = 1.0f;
        if (distFromCenter > maxR * 0.70f) {
            limbFade = (maxR - distFromCenter) / (maxR * 0.30f);
            if (limbFade < 0.0f) limbFade = 0.0f;
            if (limbFade > 1.0f) limbFade = 1.0f;
        }

        float totalFade = sunFade * limbFade;

        // 仅当渐变透明度 > 1% 时绘制日影，实现丝滑渐入渐出效果
        if (totalFade > 0.01f) {
            float dLon = subLonR - uLonR;
            float y = sinf(dLon) * cosf(subLatR);
            float x = cosf(uLatR) * sinf(subLatR) - sinf(uLatR) * cosf(subLatR) * cosf(dLon);
            float sunAzRad = atan2f(y, x);

            // 日影方向 = 太阳反方向 (Sun Azimuth + 180deg)
            float shadowAzRad = sunAzRad + (float)M_PI;

            // 动态调节水滴阴影大头针的伸展长度 (正午短，早晚长)
            float clampEl = sunElDeg < 5.0f ? 5.0f : sunElDeg;
            float d_dist = 0.12f / tanf(clampEl * DEG_TO_RAD);
            if (d_dist < 0.06f) d_dist = 0.06f; // 正午短水滴 (~8-10px)
            if (d_dist > 0.25f) d_dist = 0.25f; // 早晚长水滴 (~25-30px)

            float shLatR = uLatR + d_dist * cosf(shadowAzRad);
            float shLonR = uLonR + (d_dist * sinf(shadowAzRad)) / cosf(uLatR);

            double shLat = shLatR * RAD_TO_DEG;
            double shLon = shLonR * RAD_TO_DEG;

            int sx = -1, sy = -1;
            if (projectOrthographic(shLat, shLon, 0, centerLat, centerLon, sx, sy)) {
                if (abs(sx - ux) < 120 && abs(sy - uy) < 120) {
                    float dx = (float)(sx - ux);
                    float dy = (float)(sy - uy);
                    float len = sqrtf(dx * dx + dy * dy);

                    if (len < 5.0f) {
                        if (len > 0.01f) {
                            dx = (dx / len) * 5.0f;
                            dy = (dy / len) * 5.0f;
                            len = 5.0f;
                        } else {
                            dx = 5.0f;
                            dy = 0.0f;
                            len = 5.0f;
                        }
                    }

                    // 水滴形阴影大头针的法线向量
                    float nx = -dy / len;
                    float ny = dx / len;

                    // 计算水滴头两侧张角顶点
                    int pLeftX = (int)(ux + dx + nx * 3.0f);
                    int pLeftY = (int)(uy + dy + ny * 3.0f);
                    int pRightX = (int)(ux + dx - nx * 3.0f);
                    int pRightY = (int)(uy + dy - ny * 3.0f);
                    int headX = (int)(ux + dx);
                    int headY = (int)(uy + dy);

                    // 使用真实 Alpha 混合与底图地形像素叠加，完全渐变淡出至完全透明，绝无纯黑硬接缝
                    fillTriangleAlpha(_canvas, ux, uy, pLeftX, pLeftY, pRightX, pRightY, 60, 65, 80, totalFade);
                    fillCircleAlpha(_canvas, headX, headY, 3, 60, 65, 80, totalFade);
                    blendPixelAlpha(_canvas, headX, headY, 130, 135, 150, totalFade);
                }
            }
        }
    }

    // 2. 查找是否有处于选中焦点状态的卫星 (isSelected == true)
    const SatRenderData* focusSat = nullptr;
    for (const auto& sat : satellites) {
        if (sat.isSelected) {
            focusSat = &sat;
            break;
        }
    }
    if (!focusSat) return;

    // 3. 绘制 3D 星地视线连线与仰角角标 (3D Sight Line & Ground Angle Badge)
    int sx = -1, sy = -1;
    bool satVisible = projectOrthographic(focusSat->currentPos.lat, focusSat->currentPos.lon, focusSat->currentPos.alt, centerLat, centerLon, sx, sy);

    if (focusSat->isVisible && (userVisible || satVisible)) {
        // 计算观察者与焦点卫星在 Topocentric 坐标系下的实时仰角与方位角
        TopocentricCoord topo = {0, 0, 0};
        if (focusSat->calc) {
            double tx, ty, tz;
            if (focusSat->calc->getTEME(focusSat->simTime, tx, ty, tz)) {
                double gmst = CoordTransform::getGMST(CoordTransform::unixToJulian(focusSat->simTime));
                ECEFCoord ecef = CoordTransform::temeToECEF(tx, ty, tz, gmst);
                GeodeticCoord obsGeo = {userLat, userLon, 0.0};
                topo = CoordTransform::ecefToTopocentric(obsGeo, ecef);
            }
        }
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

    drawFocusSightLineAndShadow(centerLat, centerLon, userLat, userLon, satellites);
    
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
    
    // Draw all light points under all states (including fast-forwarding / time adjustment)
    int step = 1;
    
    for (int i = 0; i < light_points_count; i += step) {
        float sin_lat = light_points[i].sinLat;
        float cos_lat = light_points[i].cosLat;
        float sin_lon = light_points[i].sinLon;
        float cos_lon = light_points[i].cosLon;
        
        // 1. Determine if the point is in darkness (cos_dist <= 0.05f)
        float cos_lon_subLon = cos_lon * cos_subLon + sin_lon * sin_subLon;
        float cos_dist = sin_subLat * sin_lat + cos_subLat * cos_lat * cos_lon_subLon;
        
        if (cos_dist <= 0.05f) {
            // 2. Complete orthographic projection
            float cos_dLon = cos_lon * cos_cLon + sin_lon * sin_cLon;
            float cos_c = sin_cLat * sin_lat + cos_cLat * cos_lat * cos_dLon;
            float sin_dLon = sin_lon * cos_cLon - cos_lon * sin_cLon;
            
            float x = r * cos_lat * sin_dLon;
            float y = r * (cos_cLat * sin_lat - sin_cLat * cos_lat * cos_dLon);
            float z = r * cos_c;
            
            float z_temp = z - _cameraFocusR;
            float z_pitched = y * sin_pitch + z_temp * cos_pitch;
            float z_actual = z_pitched + _cameraFocusR;
            
            float z_limit = _cameraFocusR * (1.0f - cos_pitch);
            if (z_actual >= z_limit) {
                float y_pitched = y * cos_pitch - z_temp * sin_pitch;
                float rotatedX = x * cos_roll - y_pitched * sin_roll;
                float rotatedY = x * sin_roll + y_pitched * cos_roll;
                
                int outX = _centerX + _centerOffsetX + (int)rotatedX;
                int outY = _centerY + _centerOffsetY - (int)rotatedY;
                
                if (outX >= 0 && outX < width && outY >= 0 && outY < height) {
                    float factor = (0.05f - cos_dist) / 0.20f;
                    if (factor > 1.0f) factor = 1.0f;
                    if (factor < 0.0f) factor = 0.0f;
                    
                    int idx = outY * width + outX;
                    uint16_t cur565 = __builtin_bswap16(buffer[idx]);
                    
                    // 提取当前像素 RGB565 (R:5bit, G:6bit, B:5bit)
                    uint16_t r5 = (cur565 >> 11) & 0x1F;
                    uint16_t g6 = (cur565 >> 5) & 0x3F;
                    uint16_t b5 = cur565 & 0x1F;
                    
                    // 璀璨金黄基色：R/G 快速提升形成金黄，B 保持较低防止过早泛白
                    uint16_t dr5 = (uint16_t)(7 * factor);   // 红色快速提升
                    uint16_t dg6 = (uint16_t)(9 * factor);   // 绿色稳步提升
                    uint16_t db5 = (uint16_t)(1.5f * factor); // 蓝色低增量，维持金色基调
                    
                    r5 = (r5 + dr5 > 31) ? 31 : (r5 + dr5);
                    g6 = (g6 + dg6 > 63) ? 63 : (g6 + dg6);
                    b5 = (b5 + db5 > 31) ? 31 : (b5 + db5);
                    
                    // 仅当 R 与 G 均接近饱和（大城市极高密度重叠）时，才提升 B 分量形成“亮白核心”
                    if (r5 >= 28 && g6 >= 56) {
                        uint16_t extraB = (uint16_t)(3 * factor);
                        b5 = (b5 + extraB > 31) ? 31 : (b5 + extraB);
                    }
                    
                    uint16_t new565 = (r5 << 11) | (g6 << 5) | b5;
                    buffer[idx] = __builtin_bswap16(new565);
                }
            }
        }
    }
}

void EarthRenderer::drawSatelliteIcon(int x, int y, SatIconType iconType, uint16_t color, bool renderDark, float intensity) {
    auto getScaled = [&](uint16_t baseCol) -> uint16_t {
        if (intensity >= 0.99f) return baseCol;
        return scaleColor(baseCol, intensity);
    };

    uint16_t darkGrayCol = getScaled(renderDark ? _display->color565(80,80,80) : TFT_WHITE);
    uint16_t lightGrayCol = getScaled(renderDark ? _display->color565(50,50,50) : TFT_LIGHTGRAY);
    uint16_t orangeCol = getScaled(TFT_ORANGE);
    uint16_t hamLightGray = getScaled(TFT_LIGHTGRAY);

    if (iconType == ICON_STATION) {
        // Space Station (Core module + big solar panels)
        _canvas->fillRect(x - 2, y - 1, 5, 3, darkGrayCol);
        _canvas->fillRect(x - 7, y - 3, 4, 7, color);
        _canvas->fillRect(x + 4, y - 3, 4, 7, color);
    } else if (iconType == ICON_ROCKET) {
        // Rocket Debris (Cylinder + nozzle + engine flame)
        _canvas->fillRect(x - 2, y - 4, 5, 8, darkGrayCol);
        _canvas->fillTriangle(x - 2, y - 4, x + 2, y - 4, x, y - 7, color);
        _canvas->fillRect(x - 2, y + 4, 2, 2, orangeCol); // Engine 1
        _canvas->fillRect(x + 1, y + 4, 2, 2, orangeCol); // Engine 2
    } else if (iconType == ICON_TELESCOPE) {
        // Space Telescope (Tube + lens cover + solar panel)
        _canvas->fillRect(x - 2, y - 3, 5, 7, darkGrayCol);
        _canvas->fillRect(x - 3, y - 4, 7, 2, lightGrayCol);
        _canvas->fillRect(x - 6, y, 3, 2, color);
        _canvas->fillRect(x + 4, y, 3, 2, color);
    } else if (iconType == ICON_DEEPSPACE) {
        // Deep Space (Star flare)
        _canvas->drawLine(x, y - 5, x, y + 5, color);
        _canvas->drawLine(x - 5, y, x + 5, y, color);
        _canvas->drawLine(x - 2, y - 2, x + 2, y + 2, darkGrayCol);
        _canvas->drawLine(x - 2, y + 2, x + 2, y - 2, darkGrayCol);
    } else if (iconType == ICON_DEBRIS) {
        // Space Debris (Half regular solar grid panel on left, jagged outline and floating dots on right)
        _canvas->fillRect(x - 6, y - 2, 6, 5, color);
        _canvas->drawLine(x - 6, y - 2, x - 1, y - 2, lightGrayCol);
        _canvas->drawLine(x - 6, y + 2, x - 1, y + 2, lightGrayCol);
        _canvas->drawLine(x - 1, y - 2, x - 1, y + 2, lightGrayCol);
        
        // Jagged right half
        _canvas->drawLine(x, y - 2, x + 3, y - 1, color);
        _canvas->drawLine(x + 3, y - 1, x + 1, y + 1, color);
        _canvas->drawLine(x + 1, y + 1, x, y + 2, color);
        
        // Detached drifting debris fragments
        _canvas->drawPixel(x + 5, y - 3, color);
        _canvas->drawPixel(x + 5, y + 2, color);
    } else if (iconType == ICON_DFH1) {
        // DongFangHong-1 (Spherical body + 4 antennas)
        _canvas->fillCircle(x, y, 3, darkGrayCol);
        _canvas->drawLine(x - 2, y - 2, x - 6, y - 6, color);
        _canvas->drawLine(x + 2, y - 2, x + 6, y - 6, color);
        _canvas->drawLine(x - 2, y + 2, x - 6, y + 6, color);
        _canvas->drawLine(x + 2, y + 2, x + 6, y + 6, color);
    } else if (iconType == ICON_BLUEWALKER3) {
        // BlueWalker 3 (Center array + left/right huge flat solar arrays)
        _canvas->fillRect(x - 1, y - 1, 3, 3, darkGrayCol);
        _canvas->fillRect(x - 7, y - 3, 5, 7, color);
        _canvas->fillRect(x + 3, y - 3, 5, 7, color);
        _canvas->drawFastVLine(x - 5, y - 3, 7, TFT_BLACK);
        _canvas->drawFastVLine(x + 5, y - 3, 7, TFT_BLACK);
        _canvas->drawFastHLine(x - 7, y, 5, TFT_BLACK);
        _canvas->drawFastHLine(x + 3, y, 5, TFT_BLACK);
    } else if (iconType == ICON_WEATHER) {
        // Weather Sat (Body + left panel + right instrument mount)
        _canvas->fillRect(x - 1, y - 2, 3, 5, darkGrayCol);
        _canvas->drawLine(x - 2, y, x - 6, y - 2, color);
        _canvas->fillRect(x - 8, y - 4, 3, 3, color);
        _canvas->drawFastHLine(x + 2, y, 2, color);
        _canvas->drawPixel(x + 3, y - 1, color);
    } else if (iconType == ICON_NAVIGATION) {
        // Navigation Sat (Body + symmetric flat solar wings + lower helical antenna)
        _canvas->fillRect(x - 1, y - 2, 3, 5, darkGrayCol);
        _canvas->drawFastHLine(x - 6, y, 5, hamLightGray);
        _canvas->drawFastHLine(x + 2, y, 5, hamLightGray);
        _canvas->fillRect(x - 8, y - 1, 3, 3, color);
        _canvas->fillRect(x + 6, y - 1, 3, 3, color);
        _canvas->drawFastVLine(x, y + 3, 2, color);
        _canvas->drawPixel(x, y + 5, color);
    } else if (iconType == ICON_COMMUNICATION) {
        // Comm Sat (Spherical core + top V-shape antenna + bottom dish antenna)
        _canvas->fillCircle(x, y, 2, darkGrayCol);
        _canvas->drawLine(x, y - 2, x - 3, y - 6, color);
        _canvas->drawLine(x, y - 2, x + 3, y - 6, color);
        _canvas->drawFastVLine(x, y + 2, 2, color);
        _canvas->drawFastHLine(x - 2, y + 4, 5, color);
    } else if (iconType == ICON_SPACEPLANE) {
        // Spaceplane (X-37B) — top-down view: blunt nose, delta wings, vertical stabiliser
        // Nose
        _canvas->fillRect(x - 1, y - 4, 3, 2, darkGrayCol);
        // Fuselage
        _canvas->fillRect(x - 2, y - 2, 5, 5, darkGrayCol);
        // Delta wings (widest at centre)
        _canvas->fillTriangle(x - 4, y + 1, x - 1, y - 1, x - 1, y + 3, color);
        _canvas->fillTriangle(x + 5, y + 1, x + 2, y - 1, x + 2, y + 3, color);
        // Vertical tail fin (offset slightly right)
        _canvas->drawFastVLine(x + 1, y + 3, 3, color);
    } else if (iconType == ICON_SOLAR_PROBE) {
        // Solar Probe (Parker) — heat shield (wide disc) + instrument boom + two tiny wings
        // Heat shield
        _canvas->fillEllipse(x, y - 1, 4, 3, lightGrayCol);
        _canvas->drawEllipse(x, y - 1, 4, 3, color);
        // Instrument boom below shield
        _canvas->drawFastVLine(x, y + 2, 3, darkGrayCol);
        // Tiny solar panels flanking boom
        _canvas->fillRect(x - 3, y + 3, 2, 1, color);
        _canvas->fillRect(x + 2, y + 3, 2, 1, color);
    } else if (iconType == ICON_CHAIN_MONO) {
        // Chain Mono module — rectangular body with screen and Grove connector nub
        _canvas->fillRect(x - 4, y - 3, 9, 7, darkGrayCol);   // Module body
        _canvas->fillRect(x - 3, y - 2, 7, 5, color);          // Screen area (lighter)
        _canvas->fillRect(x - 2, y - 1, 5, 3, darkGrayCol);    // Screen content (dark pixels)
        _canvas->fillRect(x - 5, y + 1, 1, 2, lightGrayCol);   // Left Grove nub
    } else if (iconType == ICON_LANDER) {
        // Lander — hexagonal body + top antenna + three landing legs
        // Antenna
        _canvas->drawFastVLine(x, y - 4, 2, color);
        // Main body (flat hexagon)
        _canvas->fillRect(x - 2, y - 2, 5, 4, darkGrayCol);
        // Three landing legs (left, centre-right, right)
        _canvas->drawLine(x - 2, y + 2, x - 4, y + 4, color);
        _canvas->drawLine(x,     y + 2, x,     y + 4, color);
        _canvas->drawLine(x + 2, y + 2, x + 4, y + 4, color);
        // Foot pads
        _canvas->drawFastHLine(x - 5, y + 4, 2, color);
        _canvas->drawPixel(x, y + 5, color);
        _canvas->drawFastHLine(x + 4, y + 4, 2, color);
    } else {
        // Generic Sat (Tiny cube + single solar wing)
        _canvas->fillRect(x - 1, y - 1, 3, 3, darkGrayCol);
        _canvas->fillRect(x - 5, y - 1, 3, 3, color);
        _canvas->drawLine(x - 2, y, x - 1, y, hamLightGray);
    }
}

#define PI_F 3.14159265f
#define TWO_PI_F 6.2831853f

inline void blendPixelAdd(LGFX_Sprite* canvas, int x, int y, uint8_t r, uint8_t g, uint8_t b, float alpha) {
    int width = canvas->width();
    int height = canvas->height();
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    
    uint16_t* buf = (uint16_t*)canvas->getBuffer();
    if (!buf) return;
    
    int index = y * width + x;
    uint16_t rawCol = buf[index];
    
    // Byte swap from Big Endian (display format) to Little Endian (CPU format)
    uint16_t oldCol = (rawCol >> 8) | (rawCol << 8);
    
    // Extract RGB 565 components (r: 5 bits, g: 6 bits, b: 5 bits)
    uint8_t r_o = (oldCol >> 11) & 0x1F;
    uint8_t g_o = (oldCol >> 5) & 0x3F;
    uint8_t b_o = oldCol & 0x1F;
    
    // Additive blend (r: 0-31, g: 0-63, b: 0-31)
    // Convert target colors (r, g, b are 0-255) to 565 scale
    int r_add = (int)(((r * 31) / 255.0f) * alpha);
    int g_add = (int)(((g * 63) / 255.0f) * alpha);
    int b_add = (int)(((b * 31) / 255.0f) * alpha);
    
    int r_new = r_o + r_add;
    int g_new = g_o + g_add;
    int b_new = b_o + b_add;
    
    if (r_new > 31) r_new = 31;
    if (g_new > 63) g_new = 63;
    if (b_new > 31) b_new = 31;
    
    uint16_t newCol = (r_new << 11) | (g_new << 5) | b_new;
    
    // Byte swap back to Big Endian (display format)
    buf[index] = (newCol >> 8) | (newCol << 8);
}

inline void drawLineAdd(LGFX_Sprite* canvas, int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b, float alpha) {
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int steps = dx > dy ? dx : dy;
    if (steps == 0) {
        blendPixelAdd(canvas, x0, y0, r, g, b, alpha);
        blendPixelAdd(canvas, x0, y0 - 1, r, g, b, alpha * 0.7f);
        blendPixelAdd(canvas, x0, y0 + 1, r, g, b, alpha * 0.7f);
        blendPixelAdd(canvas, x0, y0 - 2, r, g, b, alpha * 0.35f);
        blendPixelAdd(canvas, x0, y0 + 2, r, g, b, alpha * 0.35f);
        return;
    }
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / steps;
        int x = x0 + (int)(t * (x1 - x0) + 0.5f);
        int y = y0 + (int)(t * (y1 - y0) + 0.5f);
        blendPixelAdd(canvas, x, y, r, g, b, alpha);
        blendPixelAdd(canvas, x, y - 1, r, g, b, alpha * 0.7f);
        blendPixelAdd(canvas, x, y + 1, r, g, b, alpha * 0.7f);
        blendPixelAdd(canvas, x, y - 2, r, g, b, alpha * 0.35f);
        blendPixelAdd(canvas, x, y + 2, r, g, b, alpha * 0.35f);
    }
}

void EarthRenderer::drawAirglow(double centerLat, double centerLon) {
    float pitchRad = _cameraPitch * DEG_TO_RAD;
    float rollRad = -_cameraRoll * DEG_TO_RAD;
    float center_y_pitched = _cameraFocusR * sinf(pitchRad);
    float center_rotatedX = -center_y_pitched * sinf(rollRad);
    float center_rotatedY = center_y_pitched * cosf(rollRad);
    int circleX = _centerX + _centerOffsetX + (int)center_rotatedX;
    int circleY = _centerY + _centerOffsetY - (int)center_rotatedY;

    // Calculate Sun's 2D direction on screen relative to Earth center
    float sx = 1.0f;
    float sy = 0.0f;
    bool hasSun = _hasSunData;
    
    if (hasSun) {
        float subLatR = (float)_subsolarLat * DEG_TO_RAD;
        float subLonR = (float)_subsolarLon * DEG_TO_RAD;
        float S_x = cosf(subLatR) * cosf(subLonR);
        float S_y = cosf(subLatR) * sinf(subLonR);
        float S_z = sinf(subLatR);
        
        float cLatRad = (float)centerLat * DEG_TO_RAD;
        float cLonRad = (float)centerLon * DEG_TO_RAD;
        float cos_cLat = cosf(cLatRad);
        float sin_cLat = sinf(cLatRad);
        float cos_cLon = cosf(cLonRad);
        float sin_cLon = sinf(cLonRad);
        
        float proj_x = S_y * cos_cLon - S_x * sin_cLon;
        float term2 = S_x * cos_cLon + S_y * sin_cLon;
        float proj_y = cos_cLat * S_z - sin_cLat * term2;
        float proj_z = sin_cLat * S_z + cos_cLat * term2;
        
        float pitchRadCam = _cameraPitch * DEG_TO_RAD;
        float y_pitched = proj_y * cosf(pitchRadCam) - proj_z * sinf(pitchRadCam);
        
        float rollRadCam = -_cameraRoll * DEG_TO_RAD;
        float cos_roll = cosf(rollRadCam);
        float sin_roll = sinf(rollRadCam);
        
        float sun_rotatedX = proj_x * cos_roll - y_pitched * sin_roll;
        float sun_rotatedY = proj_x * sin_roll + y_pitched * cos_roll;
        
        float sun_len = sqrtf(sun_rotatedX * sun_rotatedX + sun_rotatedY * sun_rotatedY);
        if (sun_len > 0.01f) {
            sx = sun_rotatedX / sun_len;
            sy = sun_rotatedY / sun_len;
        } else {
            hasSun = false;
        }
    }

    // Perfect concentric circles for a smooth glass cover appearance (no noise or vertical spikes)
    int N = _isFastForwarding ? 3 : 10; // Reduced layers from 4 to 3 during fast forwarding to speed up
    float startHeight = 2.5f;
    float thickness = 6.75f; // total thickness of airglow dome
    float maxAlpha = 0.55f;

    for (int i = 0; i < N; i++) {
        float t = (float)i / N;
        // Asymmetric bell curve for airglow: faint at bottom, peak at 0.7 height, fade to 0 at top
        float alpha = ((t < 0.7f) ? (0.2f + 0.8f * (t / 0.7f)) : (1.0f - (t - 0.7f) / 0.3f)) * maxAlpha;
        
        float baseR = _earthRadius + startHeight + t * thickness;
        
        // stepRad = 1.0/R guarantees exactly one pixel per step along the circumference - NO GAPS!
        // For fast forwarding, use 2.0/R to reduce overhead by 50% (since we are fast-forwarding, minor gaps are invisible)
        float stepRad = (_isFastForwarding ? 2.0f : 1.0f) / baseR;
        
        for (float rad = 0.0f; rad < TWO_PI_F; rad += stepRad) {
            // Micro-optimization: cache sin and cos to avoid calling them twice per step
            float cos_rad = cosf(rad);
            float sin_rad = sinf(rad);
            
            int x = circleX + (int)(baseR * cos_rad + 0.5f);
            int y = circleY - (int)(baseR * sin_rad + 0.5f);
            
            uint8_t r_val = 0;
            uint8_t g_val = 140;
            uint8_t b_val = 220;
            
            if (hasSun) {
                // Dot product of limb point unit direction (cos_rad, -sin_rad) with Sun unit direction (sx, sy)
                float dot = cos_rad * sx - sin_rad * sy;
                
                if (dot > 0.3f) {
                    // Day side: Cyan-blue (Rayleigh scattering)
                    r_val = 0; g_val = 140; b_val = 220;
                } else if (dot >= 0.0f) {
                    // Day-to-Terminator: Sunset Red to Cyan-Blue
                    float t_color = dot / 0.3f;
                    r_val = (uint8_t)(250.0f * (1.0f - t_color));
                    g_val = (uint8_t)(80.0f + 60.0f * t_color);
                    b_val = (uint8_t)(0.0f + 220.0f * t_color);
                } else if (dot >= -0.3f) {
                    // Terminator-to-Night: Night Gold (Sodium/OH airglow) to Sunset Red
                    float t_color = (dot + 0.3f) / 0.3f;
                    r_val = (uint8_t)(180.0f + 70.0f * t_color);
                    g_val = (uint8_t)(130.0f - 50.0f * t_color);
                    b_val = (uint8_t)(10.0f - 10.0f * t_color);
                } else {
                    // Night side: Warm Sodium Gold
                    r_val = 180; g_val = 130; b_val = 10;
                }
            }
            
            blendPixelAdd(_canvas, x, y, r_val, g_val, b_val, alpha);
        }
    }
}

void EarthRenderer::drawAuroras(double centerLat, double centerLon) {
    // 2 Concentric Rings: Outer Ring at ~66.5 deg, Inner Ring at ~72.5 deg
    // 2 Altitude layers per ring (70km and 240km)
    int N_layers = _isFastForwarding ? 1 : 2;
    int step = _isFastForwarding ? 20 : 8; // Faster step 20 during fast forwarding
    float timePhase = millis() * 0.0015f;
    
    float alt_bot = 70.0f;
    float alt_top = 240.0f;
    float maxAlpha = 0.60f;

    float latitudes[] = { 66.5f, 72.5f, -66.5f, -72.5f };
    bool isNorth[] = { true, true, false, false };

    // Draw polar auroras for both hemispheres, each with two rings
    for (int r_idx = 0; r_idx < 4; r_idx++) {
        float baseLat = latitudes[r_idx];
        bool north = isNorth[r_idx];

        uint8_t r_bot = 10, g_bot = 160, b_bot = 40;
        uint8_t r_top, g_top, b_top;
        if (north) {
            // North Pole: Green to Purple
            r_top = 140; g_top = 20; b_top = 140;
        } else {
            // South Pole: Green to Blue
            r_top = 10; g_top = 80; b_top = 180;
        }

        // Draw altitude layers for this ring
        for (int i = 0; i < N_layers; i++) {
            float t = (N_layers == 1) ? 0.0f : (float)i / (N_layers - 1);
            float baseAlpha = powf(1.0f - t, 1.5f) * maxAlpha;
            float alt = alt_bot + t * (alt_top - alt_bot);
            
            // Color at this altitude
            uint8_t r_col = r_bot + t * (r_top - r_bot);
            uint8_t g_col = g_bot + t * (g_top - g_bot);
            uint8_t b_col = b_bot + t * (b_top - b_bot);

            int prevX = -1, prevY = -1;
            bool prevVisible = false;
            
            int firstX = -1, firstY = -1;
            bool firstVisible = false;

            for (int lon = 0; lon <= 360; lon += step) {
                float lonRad = lon * DEG_TO_RAD;
                
                // Noise on latitude
                float latNoise = 1.6f * sinf(lonRad * 3.0f + (north ? 1.0f : -1.0f) * timePhase + r_idx * 1.5f + i * 0.3f) + 
                                 0.8f * cosf(lonRad * 7.0f - timePhase * 1.2f);
                float drawLat = baseLat + (north ? latNoise : -latNoise);
                
                // Noise on height
                float heightNoise = 12.0f * sinf(lonRad * 5.0f + timePhase * 1.5f + r_idx * 0.7f);
                float drawAlt = alt + heightNoise;
                
                int x, y;
                bool visible = projectOrthographic(drawLat, lon, drawAlt, centerLat, centerLon, x, y);
                
                if (visible) {
                    if (prevVisible) {
                        // Boundary check: Prevent huge lag spikes and false lines by limiting segment length (avoiding projection wrap-around)
                        if (abs(x - prevX) < 80 && abs(y - prevY) < 80) {
                            drawLineAdd(_canvas, prevX, prevY, x, y, r_col, g_col, b_col, baseAlpha);
                        }
                    }
                    
                    if (lon == 0) {
                        firstX = x;
                        firstY = y;
                        firstVisible = true;
                    }
                    
                    prevX = x;
                    prevY = y;
                    prevVisible = true;
                } else {
                    prevVisible = false;
                }
            }
            
            // Close the loop
            if (firstVisible && prevVisible) {
                if (abs(prevX - firstX) < 80 && abs(prevY - firstY) < 80) {
                    drawLineAdd(_canvas, prevX, prevY, firstX, firstY, r_col, g_col, b_col, baseAlpha);
                }
            }
        }
    }
}
