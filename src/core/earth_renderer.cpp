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
    updateFocusR();
}

EarthRenderer::~EarthRenderer() {
    _canvas->deleteSprite();
    delete _canvas;
}

void EarthRenderer::begin() {
    _canvas->createSprite(_display->width(), _display->height());
    _canvas->setFont(I18N::getFont());
}

void EarthRenderer::setSunPosition(double subsolarLat, double subsolarLon) {
    _subsolarLat = subsolarLat;
    _subsolarLon = subsolarLon;
    _subLatRad = (float)subsolarLat * (float)DEG_TO_RAD;
    _subLonRad = (float)subsolarLon * (float)DEG_TO_RAD;
    _sinSubLat = sinf(_subLatRad);
    _cosSubLat = cosf(_subLatRad);
    _sinSubLon = sinf(_subLonRad);
    _cosSubLon = cosf(_subLonRad);
    float dSubC = _subLonRad - _cLonRad;
    _cosSubMinusCLon = cosf(dSubC);
    _sinSubMinusCLon = sinf(dSubC);
    _hasSunData = true;
}

void EarthRenderer::setCameraAttitude(float pitch, float roll, float yaw) {
    _cameraPitch = pitch;
    _cameraRoll = roll;
    _cameraYaw = yaw;
    float pitchRad = _cameraPitch * (float)DEG_TO_RAD;
    _sinPitch = sinf(pitchRad);
    _cosPitch = cosf(pitchRad);
    float rollRad = -_cameraRoll * (float)DEG_TO_RAD;
    _sinRoll = sinf(rollRad);
    _cosRoll = cosf(rollRad);
    _zLimit = _cameraFocusR * (1.0f - _cosPitch);
}

void EarthRenderer::updateFrameConstants(double centerLat, double centerLon) {
    if (centerLat != _cachedCenterLat || centerLon != _cachedCenterLon) {
        _cachedCenterLat = centerLat;
        _cachedCenterLon = centerLon;
        _cLatRad = (float)centerLat * (float)DEG_TO_RAD;
        _cLonRad = (float)centerLon * (float)DEG_TO_RAD;
        _sinCLat = sinf(_cLatRad);
        _cosCLat = cosf(_cLatRad);
        _sinCLon = sinf(_cLonRad);
        _cosCLon = cosf(_cLonRad);
    }
    if (_hasSunData) {
        float dSubC = _subLonRad - _cLonRad;
        _cosSubMinusCLon = cosf(dSubC);
        _sinSubMinusCLon = sinf(dSubC);
    }
    _zLimit = _cameraFocusR * (1.0f - _cosPitch);
}

// Check if a point is in the day side (angular distance to sun < 90 deg)
bool isDaylight(double lat, double lon, double subLat, double subLon, bool hasSun) {
    if (!hasSun) return true;
    float latR = (float)lat * (float)DEG_TO_RAD;
    float lonR = (float)lon * (float)DEG_TO_RAD;
    float subLatR = (float)subLat * (float)DEG_TO_RAD;
    float subLonR = (float)subLon * (float)DEG_TO_RAD;
    float cos_dist = sinf(subLatR)*sinf(latR) + cosf(subLatR)*cosf(latR)*cosf(lonR - subLonR);
    return cos_dist > 0;
}

// Check if a satellite is in Earth's shadow (cylindrical shadow model)
bool isSatelliteInShadow(double lat, double lon, double alt, double subLat, double subLon, bool hasSun) {
    if (!hasSun) return false;
    if (!(lat >= -90.5 && lat <= 90.5 && lon >= -720.5 && lon <= 720.5 && alt > -6000.0 && alt < 1e9)) {
        return false;
    }
    float latR = (float)lat * (float)DEG_TO_RAD;
    float lonR = (float)lon * (float)DEG_TO_RAD;
    float subLatR = (float)subLat * (float)DEG_TO_RAD;
    float subLonR = (float)subLon * (float)DEG_TO_RAD;
    
    float cos_theta = sinf(subLatR)*sinf(latR) + cosf(subLatR)*cosf(latR)*cosf(lonR - subLonR);
    if (cos_theta >= 0) return false; // Day side
    
    float r = 6371.0f + (float)alt; // Actual Earth radius + alt (in km)
    float dist_sq = r * r * (1.0f - cos_theta * cos_theta);
    
    // If distance from axis is less than Earth radius, it's eclipsed
    return dist_sq < (6371.0f * 6371.0f);
}

bool EarthRenderer::projectOrthographic(double lat, double lon, double alt, double centerLat, double centerLon, int& outX, int& outY) {
    if (!(lat >= -90.5 && lat <= 90.5 && lon >= -1080.0 && lon <= 1080.0 && alt > -6000.0 && alt < 1e9)) {
        return false;
    }
    if (centerLat != _cachedCenterLat || centerLon != _cachedCenterLon) {
        updateFrameConstants(centerLat, centerLon);
    }

    float latRad = (float)lat * (float)DEG_TO_RAD;
    float dLonRad = (float)lon * (float)DEG_TO_RAD - _cLonRad;
    float sinLat = sinf(latRad);
    float cosLat = cosf(latRad);
    float sinDLon = sinf(dLonRad);
    float cosDLon = cosf(dLonRad);

    // Radius scaling: Earth radius + non-linear altitude scale
    float r = (float)_earthRadius;
    if (alt > 0.0) {
        float visualAlt = (float)alt;
        if (visualAlt > 20000.0f) visualAlt = 20000.0f;
        r += sqrtf(visualAlt) * 0.4f * _zoom;
    }

    float cos_c = _sinCLat * sinLat + _cosCLat * cosLat * cosDLon;

    // Fast back-hemisphere cull when surface point and unpitched camera
    if (alt <= 0.0 && _cameraPitch == 0.0f && cos_c < 0.0f) {
        return false;
    }

    float x = r * cosLat * sinDLon;
    float y = r * (_cosCLat * sinLat - _sinCLat * cosLat * cosDLon);
    float z = r * cos_c;

    float z_temp = z - _cameraFocusR;
    float z_pitched = y * _sinPitch + z_temp * _cosPitch;
    float y_pitched = y * _cosPitch - z_temp * _sinPitch;
    float z_actual = z_pitched + _cameraFocusR;

    if (z_actual < _zLimit) {
        if (alt <= 0.0) return false;
        float distSq = x * x + y_pitched * y_pitched;
        if (distSq < (float)(_earthRadius * _earthRadius)) {
            return false;
        }
    }

    float rotatedX = x * _cosRoll - y_pitched * _sinRoll;
    float rotatedY = x * _sinRoll + y_pitched * _cosRoll;

    outX = _centerX + _centerOffsetX + (int)rotatedX;
    outY = _centerY + _centerOffsetY - (int)rotatedY;
    return true;
}

bool EarthRenderer::projectInertial(float tx, float ty, float tz, float cosPsi, float sinPsi, int& outX, int& outY, bool* outInShadow) const {
    float rSq = tx * tx + ty * ty + tz * tz;
    if (!(rSq > 1000.0f && rSq < 1e18f)) return false;

    float invR = 1.0f / sqrtf(rSq);
    float rKm = rSq * invR;
    float ux = tx * invR;
    float uy = ty * invR;
    float uz = tz * invR;

    float earthR = 6378.137f - 21.385f * uz * uz;
    float alt = rKm - earthR;

    float r = (float)_earthRadius;
    if (alt > 0.0f) {
        float vAlt = (alt > 20000.0f) ? 20000.0f : alt;
        r += sqrtf(vAlt) * 0.4f * _zoom;
    }

    float term2 = ux * cosPsi + uy * sinPsi;
    float side  = uy * cosPsi - ux * sinPsi;
    float cos_c = _sinCLat * uz + _cosCLat * term2;

    float x = r * side;
    float y = r * (_cosCLat * uz - _sinCLat * term2);
    float z = r * cos_c;

    float z_temp = z - _cameraFocusR;
    float z_pitched = y * _sinPitch + z_temp * _cosPitch;
    float y_pitched = y * _cosPitch - z_temp * _sinPitch;
    float z_actual = z_pitched + _cameraFocusR;

    if (z_actual < _zLimit) {
        if (alt <= 0.0f) return false;
        if (x * x + y_pitched * y_pitched < (float)(_earthRadius * _earthRadius)) {
            return false;
        }
    }

    if (outInShadow) {
        bool inShadow = false;
        if (_hasSunData) {
            float cosTheta = _sinSubLat * uz + _cosSubLat * (term2 * _cosSubMinusCLon + side * _sinSubMinusCLon);
            if (cosTheta < 0.0f) {
                float distSq = rKm * rKm * (1.0f - cosTheta * cosTheta);
                inShadow = (distSq < (6371.0f * 6371.0f));
            }
        }
        *outInShadow = inShadow;
    }

    float rotatedX = x * _cosRoll - y_pitched * _sinRoll;
    float rotatedY = x * _sinRoll + y_pitched * _cosRoll;
    outX = _centerX + _centerOffsetX + (int)rotatedX;
    outY = _centerY + _centerOffsetY - (int)rotatedY;
    return true;
}

void EarthRenderer::drawContinents(double centerLat, double centerLon) {
    const float sin_cLat = _sinCLat;
    const float cos_cLat = _cosCLat;
    const float sin_cLon = _sinCLon;
    const float cos_cLon = _cosCLon;
    const float sin_roll = _sinRoll;
    const float cos_roll = _cosRoll;
    const float sin_pitch = _sinPitch;
    const float cos_pitch = _cosPitch;
    const float z_limit = _zLimit;
    const bool zeroPitch = (_cameraPitch == 0.0f);
    const float sin_subLat = _sinSubLat;
    const float cos_subLat = _cosSubLat;
    const float sin_subLon = _sinSubLon;
    const float cos_subLon = _cosSubLon;
    const float r = (float)_earthRadius;

    auto drawPath = [&](const MapPoint* pts, int count) {
        int prevX = -1, prevY = -1;
        bool prevVisible = false;
        for (int j = 0; j < count; j++) {
            float sin_lat = pts[j].sinLat;
            float cos_lat = pts[j].cosLat;
            float sin_lon = pts[j].sinLon;
            float cos_lon = pts[j].cosLon;
            
            float cos_dLon = cos_lon * cos_cLon + sin_lon * sin_cLon;
            float cos_c = sin_cLat * sin_lat + cos_cLat * cos_lat * cos_dLon;
            if (zeroPitch && cos_c < 0.0f) {
                prevVisible = false;
                continue;
            }

            float sin_dLon = sin_lon * cos_cLon - cos_lon * sin_cLon;
            float x = r * cos_lat * sin_dLon;
            float y = r * (cos_cLat * sin_lat - sin_cLat * cos_lat * cos_dLon);
            float z = r * cos_c;
            
            float z_temp = z - _cameraFocusR;
            float z_pitched = y * sin_pitch + z_temp * cos_pitch;
            float z_actual = z_pitched + _cameraFocusR;
            
            if (z_actual >= z_limit) {
                float y_pitched = y * cos_pitch - z_temp * sin_pitch;
                
                float rotatedX = x * cos_roll - y_pitched * sin_roll;
                float rotatedY = x * sin_roll + y_pitched * cos_roll;
                int outX = _centerX + _centerOffsetX + (int)rotatedX;
                int outY = _centerY + _centerOffsetY - (int)rotatedY;
                
                if (prevVisible) {
                    if (abs(outX - prevX) < 100 && abs(outY - prevY) < 100) {
                        float latRad = pts[j].latRad;
                        uint8_t cr = 50, cg = 150, cb = 50;
                        if (latRad > 0) {
                            float factor = latRad * 0.63661977f;
                            if (factor > 1.0f) factor = 1.0f;
                            cr = (uint8_t)(50 * (1 - factor));
                            cb = (uint8_t)(50 * (1 - factor) + 150 * factor);
                        } else {
                            float factor = -latRad * 0.63661977f;
                            if (factor > 1.0f) factor = 1.0f;
                            cg = (uint8_t)(150 * (1 - factor) + 50 * factor);
                            cb = (uint8_t)(50 * (1 - factor) + 150 * factor);
                        }
                        
                        if (_hasSunData) {
                            float cos_lon_subLon = cos_lon * cos_subLon + sin_lon * sin_subLon;
                            float cos_dist = sin_subLat * sin_lat + cos_subLat * cos_lat * cos_lon_subLon;
                            float illum = (cos_dist + 0.2f) * 2.5f;
                            if (illum > 1.0f) illum = 1.0f;
                            if (illum < 0.45f) illum = 0.45f;
                            cr = (uint8_t)(cr * illum);
                            cg = (uint8_t)(cg * illum);
                            cb = (uint8_t)(cb * illum);
                        }
                        
                        uint16_t color = _display->color565(cr, cg, cb);
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
    
    // Draw polar auroras for both modes to increase polar visibility (both Normal [0] and Red [1])
    drawAuroras(centerLat, centerLon);
    
    // Draw Point Nemo (Spacecraft Cemetery & Mir Wreckage Easter Egg)
    drawPointNemo(centerLat, centerLon);
    
    // Draw user location as a map pin 📍
    int ux, uy;
    if (_drawDecorations && userLat <= 90.0 && projectOrthographic(userLat, userLon, 0, centerLat, centerLon, ux, uy)) {
        int headX = ux;
        int headY = uy - 6;
        if (_gnssSearching) {
            // 飞行器防撞灯/信标灯节奏的缓慢呼吸闪烁与扩散光环 (Aircraft beacon breathing pulse)
            uint32_t t = millis() % 1400; // 1.4 秒周期的柔和呼吸
            float pulse = sinf(t * ((float)M_PI / 1400.0f));
            float breathingFactor = pulse * pulse; // 0.0 -> 1.0 -> 0.0 平滑指数呼吸
            
            // 1. 动态扩散光环 (3.5px 到 11px 平滑扩展淡出)
            float ringR = 3.5f + breathingFactor * 7.5f;
            float ringAlpha = (1.0f - breathingFactor) * 0.45f;
            if (ringAlpha > 0.02f) {
                fillCircleAlpha(_canvas, ux, uy - 3, (int)ringR, 255, 70, 70, ringAlpha);
            }
            
            // 2. 主针体渐变颜色 (暗红 RGB 80,15,15 -> 鲜红 RGB 255,35,35)
            uint8_t r = (uint8_t)(80 + 175 * breathingFactor);
            uint8_t g = (uint8_t)(15 + 20 * breathingFactor);
            uint8_t b = (uint8_t)(15 + 20 * breathingFactor);
            uint16_t pinColor = _display->color565(r, g, b);
            
            _canvas->fillTriangle(ux, uy, headX - 3, headY + 1, headX + 3, headY + 1, pinColor);
            _canvas->fillCircle(headX, headY, 3, pinColor);
            
            // 3. 核心辉光点 (随呼吸渐亮)
            uint8_t coreV = (uint8_t)(120 + 135 * breathingFactor);
            _canvas->drawPixel(headX, headY, _display->color565(coreV, coreV, coreV));
        } else {
            _canvas->fillTriangle(ux, uy, headX - 3, headY + 1, headX + 3, headY + 1, TFT_RED);
            _canvas->fillCircle(headX, headY, 3, TFT_RED);
            _canvas->drawPixel(headX, headY, TFT_WHITE);
        }
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

static inline InertialOrbitNode interpInertialRing(const std::vector<InertialOrbitNode>& ring, bool closed, float u) {
    const int M = (int)ring.size();
    if (M <= 0) return {0.0f, 0.0f, 0.0f};
    if (M == 1) return ring[0];
    const int L = M - 1;

    if (closed) {
        u = fmodf(u, (float)L);
        if (u < 0.0f) u += (float)L;
    } else {
        if (u <= 0.0f) return ring[0];
        if (u >= (float)L) return ring[L];
    }

    int idx = (int)u;
    if (idx >= L) idx = L - 1;
    float t = u - (float)idx;

    const InertialOrbitNode& p1 = ring[idx];
    const InertialOrbitNode& p2 = ring[idx + 1];
    float dx = p2.tx - p1.tx;
    float dy = p2.ty - p1.ty;
    float dz = p2.tz - p1.tz;

    float rx = p1.tx + t * dx;
    float ry = p1.ty + t * dy;
    float rz = p1.tz + t * dz;

    // 二阶弦矢补偿：消除稀疏采样弦中点向地心内凹导致的高度下沉，零 sqrt/trig 开销
    float r1Sq = p1.tx * p1.tx + p1.ty * p1.ty + p1.tz * p1.tz;
    if (r1Sq > 1000.0f) {
        float dSq = dx * dx + dy * dy + dz * dz;
        float sag = 1.0f + 0.5f * t * (1.0f - t) * (dSq / r1Sq);
        if (sag > 1.12f) sag = 1.12f;
        rx *= sag;
        ry *= sag;
        rz *= sag;
    }
    return {rx, ry, rz};
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

static void drawRadioTransmissionWave(LGFX_Sprite* canvas, int sx, int sy, uint16_t satColor) {
    if (!canvas) return;

    // 发射源微天线基准点：位于卫星本体正下方 (sx, sy + 6)，生动展现向地球/地面交互发射下行射频波
    int tx = sx;
    int ty = sy + 6;

    // 动态射频发射脉冲 (每 200ms 一帧，三级循环向下辐射扩散)
    int phase = ((millis() / 200) % 3);

    // 1. 发射源核心信标天线点
    uint16_t centerCol = (phase == 0) ? TFT_WHITE : 0x07E0;
    canvas->drawPixel(tx, ty, centerCol);

    // 2. 第一层发射微弧波 (向下方地面扩散，凸面向下，半径 2~3px)
    uint16_t c1 = (phase == 0) ? 0x07E0 : ((phase == 1) ? TFT_WHITE : 0x04A0);
    canvas->drawPixel(tx - 2, ty + 1, c1);
    canvas->drawPixel(tx - 1, ty + 2, c1);
    canvas->drawPixel(tx,     ty + 2, c1);
    canvas->drawPixel(tx + 1, ty + 2, c1);
    canvas->drawPixel(tx + 2, ty + 1, c1);

    // 3. 第二层扩散发射大弧波 (进一步向下方地面扩散，半径 4~5px)
    if (phase >= 1) {
        uint16_t c2 = (phase == 1) ? 0x07FF : 0x03E0;
        canvas->drawPixel(tx - 4, ty + 1, c2);
        canvas->drawPixel(tx - 3, ty + 3, c2);
        canvas->drawPixel(tx - 1, ty + 4, c2);
        canvas->drawPixel(tx,     ty + 4, c2);
        canvas->drawPixel(tx + 1, ty + 4, c2);
        canvas->drawPixel(tx + 3, ty + 3, c2);
        canvas->drawPixel(tx + 4, ty + 1, c2);
    }
}

void EarthRenderer::drawSatellite(const SatRenderData& sat, double centerLat, double centerLon, double userLat, double userLon) {
    bool isVisibleToObserver = sat.isVisible;

    const OrbitCache* cache = sat.orbitCache;
    if (cache && cache->ring.size() >= 3) {
        const auto& ring = cache->ring;
        const int M = (int)ring.size();
        const int L = M - 1;
        const bool closed = cache->isClosedLoop;

        double simT = (sat.continuousSimTime > 0.0) ? sat.continuousSimTime : (double)sat.simTime;
        double gmst = sat.gmstRad;
        if (gmst == 0.0 && simT > 0.0) {
            gmst = CoordTransform::getGMST(CoordTransform::unixToJulian(simT));
        }

        double dtFromCache = simT - (double)cache->lastCalcTime;
        double dOmega = (double)cache->raanRateRadPerSec * dtFromCache;
        double alpha0 = gmst - dOmega;

        // 将当前卫星精确地固位置反映射至惯性环参考架 pExact，确保轨道线与卫星图标 0.0 像素严丝合缝
        float latRad = (float)sat.currentPos.lat * (float)DEG_TO_RAD;
        float lonRing = (float) fmod((double)sat.currentPos.lon * DEG_TO_RAD + alpha0, 2.0 * M_PI);
        float sinLat0 = sinf(latRad);
        float cosLat0 = cosf(latRad);
        float ux0 = cosLat0 * cosf(lonRing);
        float uy0 = cosLat0 * sinf(lonRing);
        float uz0 = sinLat0;
        float rKm0 = (6378.137f - 21.385f * uz0 * uz0) + (float)sat.currentPos.alt;
        float pExX = rKm0 * ux0;
        float pExY = rKm0 * uy0;
        float pExZ = rKm0 * uz0;

        // 在 3D 惯性环中定位当前精确连续相位索引 uCenter
        int searchCount = closed ? L : M;
        int bestIdx = 0;
        float minDistSq = 1e30f;
        for (int j = 0; j < searchCount; j++) {
            float dx = ring[j].tx - pExX;
            float dy = ring[j].ty - pExY;
            float dz = ring[j].tz - pExZ;
            float dSq = dx * dx + dy * dy + dz * dz;
            if (dSq < minDistSq) {
                minDistSq = dSq;
                bestIdx = j;
            }
        }

        auto projSeg = [&](int iA, int iB, float& outT, float& outDistSq) {
            const InertialOrbitNode& A = ring[iA];
            const InertialOrbitNode& B = ring[iB];
            float abx = B.tx - A.tx, aby = B.ty - A.ty, abz = B.tz - A.tz;
            float apx = pExX - A.tx, apy = pExY - A.ty, apz = pExZ - A.tz;
            float lenSq = abx * abx + aby * aby + abz * abz;
            float t = 0.0f;
            if (lenSq > 1e-4f) {
                t = (apx * abx + apy * aby + apz * abz) / lenSq;
                if (t < 0.0f) t = 0.0f;
                else if (t > 1.0f) t = 1.0f;
            }
            float qx = A.tx + t * abx - pExX;
            float qy = A.ty + t * aby - pExY;
            float qz = A.tz + t * abz - pExZ;
            outT = t;
            outDistSq = qx * qx + qy * qy + qz * qz;
        };

        int prevIdx = closed ? ((bestIdx - 1 + L) % L) : ((bestIdx > 0) ? (bestIdx - 1) : 0);
        int nextIdx = closed ? ((bestIdx + 1) % L) : ((bestIdx < L) ? (bestIdx + 1) : L);

        float uCenter = (float)bestIdx;
        if (prevIdx != bestIdx || nextIdx != bestIdx) {
            float tL = 0.0f, dSqL = 1e30f;
            float tR = 0.0f, dSqR = 1e30f;
            if (prevIdx != bestIdx) {
                int segA = prevIdx;
                int segB = (closed && prevIdx == L - 1) ? L : bestIdx;
                projSeg(segA, segB, tL, dSqL);
            }
            if (nextIdx != bestIdx) {
                int segA = bestIdx;
                int segB = (closed && bestIdx == L - 1) ? L : nextIdx;
                projSeg(segA, segB, tR, dSqR);
            }
            if (dSqL < dSqR) {
                uCenter = (float)prevIdx + tL;
            } else {
                uCenter = (float)bestIdx + tR;
            }
            if (closed && uCenter >= (float)L) uCenter -= (float)L;
        }

        InertialOrbitNode pRing0 = interpInertialRing(ring, closed, uCenter);
        float errX = pExX - pRing0.tx;
        float errY = pExY - pRing0.ty;
        float errZ = pExZ - pRing0.tz;

        int halfSteps = L / 2;
        if (halfSteps < 1) halfSteps = 1;
        float dtStep = cache->periodSec / (float)L;
        // 地球恒星自转角速度 7.2921159e-5 rad/s 减去 J2 升交点进动率
        float dPsiStep = (7.2921159e-5f - cache->raanRateRadPerSec) * dtStep;
        float cosDPsi = cosf(dPsiStep);
        float sinDPsi = sinf(dPsiStep);

        float psi0 = (float) fmod(alpha0 + (double)_cLonRad, 2.0 * M_PI);
        float cosPsi0 = cosf(psi0);
        float sinPsi0 = sinf(psi0);

        float wAngleStep = (float)M_PI / (float)halfSteps;
        float cosWStep = cosf(wAngleStep);
        float sinWStep = sinf(wAngleStep);

        uint16_t shadowColor = _display->color565(70, 70, 80);
        uint16_t pastColor = _display->color565(60, 60, 60);
        uint16_t futureColor = _display->color565(120, 120, 120);

        // dir = -1 绘制过去半圈轨迹，dir = +1 绘制未来半圈轨迹（均从卫星当前精确原点平滑向外延伸）
        auto drawHalfArc = [&](int dir, uint16_t baseColor) {
            float cosPsi = cosPsi0;
            float sinPsi = sinPsi0;
            float stepCos = cosDPsi;
            float stepSin = (dir > 0) ? sinDPsi : -sinDPsi;

            float cosW = 1.0f;
            float sinW = 0.0f;

            int prevX = -1, prevY = -1;
            bool prevVis = false;

            for (int m = 0; m <= halfSteps; m++) {
                float u = uCenter + (float)(dir * m);
                bool clampedEnd = false;
                if (!closed) {
                    if (u < 0.0f) { u = 0.0f; clampedEnd = true; }
                    else if (u > (float)L) { u = (float)L; clampedEnd = true; }
                }

                InertialOrbitNode node = interpInertialRing(ring, closed, u);
                float w = 0.5f * (1.0f + cosW);
                float tx = node.tx + w * errX;
                float ty = node.ty + w * errY;
                float tz = node.tz + w * errZ;

                int x = -1, y = -1;
                bool inShadow = false;
                bool vis = projectInertial(tx, ty, tz, cosPsi, sinPsi, x, y, &inShadow);
                if (vis && prevVis) {
                    if (abs(x - prevX) < 100 && abs(y - prevY) < 100) {
                        _canvas->drawLine(prevX, prevY, x, y, inShadow ? shadowColor : baseColor);
                    }
                }
                prevX = x;
                prevY = y;
                prevVis = vis;

                if (clampedEnd) break;

                float nextCosPsi = cosPsi * stepCos - sinPsi * stepSin;
                float nextSinPsi = sinPsi * stepCos + cosPsi * stepSin;
                cosPsi = nextCosPsi;
                sinPsi = nextSinPsi;

                float nextCosW = cosW * cosWStep - sinW * sinWStep;
                float nextSinW = sinW * cosWStep + cosW * sinWStep;
                cosW = nextCosW;
                sinW = nextSinW;
            }
        };

        drawHalfArc(-1, pastColor);
        drawHalfArc(+1, futureColor);

        // Draw Mission Visualization Layer (Starlink / Constellation Train) directly on the seamless 3D inertial ring
        if (sat.isRecentLaunchBatch && sat.proxyFormation && !sat.proxyFormation->empty()) {
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
            float dIndexPerDeg = (float)L / 360.0f;

            // 1. Draw Occupancy Light Band (instantaneous orbital plane at simT -> constant psi0!)
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

                    float u = uCenter + diff * dIndexPerDeg;
                    InertialOrbitNode pt = interpInertialRing(ring, true, u);
                    float w = 0.5f * (1.0f + cosf(diff * (float)DEG_TO_RAD));
                    int lx, ly;
                    bool lVisible = projectInertial(pt.tx + w * errX, pt.ty + w * errY, pt.tz + w * errZ, cosPsi0, sinPsi0, lx, ly, nullptr);

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

                float u = uCenter + diff * dIndexPerDeg;
                InertialOrbitNode pt = interpInertialRing(ring, true, u);
                float w = 0.5f * (1.0f + cosf(diff * (float)DEG_TO_RAD));
                int px, py;
                if (projectInertial(pt.tx + w * errX, pt.ty + w * errY, pt.tz + w * errZ, cosPsi0, sinPsi0, px, py, nullptr)) {
                    uint16_t proxyColor = scaleColor(baseCol, breathe * fp.brightness);
                    _canvas->fillRect(px - 1, py - 1, 3, 3, proxyColor);
                }
            }
        }
    }

    // Draw Fading Motion Trail for the selected satellite (Photon Streamline / Motion Ribbon)
    if (sat.isSelected) {
        static const int MAX_TRAIL_STEPS = 6;
        struct TrailSample {
            float lat;
            float lon;
            float alt;
            double simT;
            int screenX;
            int screenY;
        };
        static TrailSample s_trail[MAX_TRAIL_STEPS];
        static int s_trailCount = 0;
        static uint32_t s_lastNameHash = 0;
        static uint32_t s_lastMoveMs = 0;

        uint32_t h = 2166136261u;
        if (sat.name) {
            for (const char* p = sat.name; *p; ++p) {
                h ^= (uint8_t)(*p);
                h *= 16777619u;
            }
        }

        double curT = (sat.continuousSimTime > 0.0) ? sat.continuousSimTime : (double)sat.simTime;
        if (h != s_lastNameHash || (s_trailCount > 0 && fabs(curT - s_trail[0].simT) > 14400.0)) {
            s_trailCount = 0;
            s_lastNameHash = h;
            s_lastMoveMs = 0;
        }

        float curLat = (float)sat.currentPos.lat;
        float curLon = (float)sat.currentPos.lon;
        float curAlt = (float)sat.currentPos.alt;

        int curSx = -1, curSy = -1;
        bool curOnScreen = projectOrthographic(curLat, curLon, curAlt, centerLat, centerLon, curSx, curSy);

        if (curOnScreen) {
            bool hasMoved = false;
            if (s_trailCount == 0) {
                hasMoved = true;
            } else {
                int dx = curSx - s_trail[0].screenX;
                int dy = curSy - s_trail[0].screenY;
                // 当屏幕位移 >= 2px 时记录新航迹点
                if (dx * dx + dy * dy >= 4) {
                    hasMoved = true;
                }
            }

            uint32_t nowMs = millis();
            if (hasMoved) {
                int limit = (s_trailCount < MAX_TRAIL_STEPS) ? s_trailCount : (MAX_TRAIL_STEPS - 1);
                for (int k = limit; k > 0; k--) {
                    s_trail[k] = s_trail[k - 1];
                }
                s_trail[0] = {curLat, curLon, curAlt, curT, curSx, curSy};
                if (s_trailCount < MAX_TRAIL_STEPS) s_trailCount++;
                s_lastMoveMs = nowMs;
            }

            // 若卫星停止位移超过 350ms，拖影迅速收拢清空，彻底杜绝静止残留
            if (nowMs - s_lastMoveMs > 350 && s_trailCount > 0) {
                s_trailCount--;
            }

            // 绘制平滑流光轨迹线条（线宽由近及远平滑变细融入夜空，决不重复画深黑矩形图标分身）
            if (s_trailCount >= 2 && (nowMs - s_lastMoveMs <= 800)) {
                int p1x = curSx, p1y = curSy;
                for (int i = 0; i < s_trailCount; i++) {
                    int p2x, p2y;
                    if (projectOrthographic(s_trail[i].lat, s_trail[i].lon, s_trail[i].alt, centerLat, centerLon, p2x, p2y)) {
                        int segDistSq = (p2x - p1x) * (p2x - p1x) + (p2y - p1y) * (p2y - p1y);
                        // 避免跨地球投影背面的破线
                        if (segDistSq < 2000 && segDistSq >= 1) {
                            float alpha = 1.0f - (float)i / (float)s_trailCount;
                            uint16_t lineCol;
                            if (sat.opsStatus == 'D') {
                                // 再入等离子体流光尾迹 (橙红火光渐隐)
                                lineCol = (i <= 1) ? TFT_YELLOW : ((i <= 3) ? TFT_ORANGE : _display->color565(190, 45, 15));
                            } else {
                                // 正常航天器流光线条
                                uint8_t r = (uint8_t)(((sat.color >> 11) & 0x1F) * 8 * alpha);
                                uint8_t g = (uint8_t)(((sat.color >> 5) & 0x3F) * 4 * alpha);
                                uint8_t b = (uint8_t)((sat.color & 0x1F) * 8 * alpha);
                                if (r < 25 && g < 25 && b < 25) {
                                    r = 25; g = 35; b = 50; // 平滑融入暗夜深海底色
                                }
                                lineCol = _display->color565(r, g, b);
                            }
                            _canvas->drawLine(p1x, p1y, p2x, p2y, lineCol);
                            if (i == 0) {
                                _canvas->drawLine(p1x, p1y + 1, p2x, p2y + 1, lineCol);
                            }
                        }
                        p1x = p2x; p1y = p2y;
                    }
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
        
        // 1. 空间碎片再入大气层烧毁动效 (若航天器处于脱轨坠毁状态 opsStatus == 'D'，如 Swift)
        if (sat.opsStatus == 'D') {
            uint32_t t = millis();
            bool hasReachedGround = (sat.currentPos.alt <= 18.0);

            if (!hasReachedGround) {
                // (1) 高空等离子体激波火球与气化微粒
                _canvas->fillCircle(sx, sy, 2, TFT_YELLOW);
                _canvas->drawCircle(sx, sy, 3, TFT_ORANGE);
                _canvas->drawCircle(sx, sy, 4, TFT_RED);
                _canvas->drawPixel(sx, sy, TFT_WHITE);

                int sPhase = (t / 80) % 4;
                _canvas->drawPixel(sx - 4 + (sPhase & 1), sy - 2 + (sPhase >> 1), TFT_ORANGE);
                _canvas->drawPixel(sx - 6, sy + ((sPhase & 2) ? 1 : -1), _display->color565(255, 130, 0));
            } else {
                // (2) 航天器坠入海面时刻：激发声波水波光环与急冷水花 (Splashdown in Point Nemo Ocean)
                uint32_t splashT = t % 2200;
                float spProg1 = (float)splashT / 2200.0f;
                int sr1 = 2 + (int)(spProg1 * 18.0f); // 2px ~ 20px
                uint8_t a1 = (uint8_t)(255 * (1.0f - spProg1));
                if (a1 > 10) {
                    _canvas->drawCircle(sx, sy, sr1, _display->color565(0, (uint8_t)(a1 * 0.85f), a1));
                    _canvas->drawCircle(sx, sy, sr1 + 1, _display->color565((uint8_t)(a1 * 0.4f), a1, 255));
                }

                uint32_t splashT2 = (t + 1100) % 2200;
                float spProg2 = (float)splashT2 / 2200.0f;
                int sr2 = 2 + (int)(spProg2 * 18.0f);
                uint8_t a2 = (uint8_t)(255 * (1.0f - spProg2));
                if (a2 > 10) {
                    _canvas->drawCircle(sx, sy, sr2, _display->color565(0, (uint8_t)(a2 * 0.6f), (uint8_t)(a2 * 0.9f)));
                }

                // 核心坠落残骸光点与水汽白光
                _canvas->fillCircle(sx, sy, 2, _display->color565(120, 240, 255));
                _canvas->drawPixel(sx, sy, TFT_WHITE);
                int steamPhase = (t / 90) % 4;
                _canvas->drawPixel(sx - 2 + steamPhase, sy - 3, _display->color565(200, 230, 255));
                _canvas->drawPixel(sx + 2 - steamPhase, sy - 2, _display->color565(180, 220, 255));
            }

            // 南太平洋航天器公墓 (尼莫点 Point Nemo) 再入等离子体激波与冲击波环
            if (sat.isSelected) {
                int pnx, pny;
                if (projectOrthographic(-48.8767, -123.3933, 0.0, centerLat, centerLon, pnx, pny)) {
                    uint32_t shockT = t % 1600;
                    int sr = 3 + (int)(shockT * 0.012f);
                    _canvas->drawCircle(pnx, pny, sr, _display->color565(255, 120, 20));
                    _canvas->drawCircle(pnx, pny, sr + 1, _display->color565(180, 50, 10));
                    _canvas->setTextColor(_display->color565(0, 230, 255));
                    if (hasReachedGround) {
                        _canvas->drawString("[POINT NEMO · DECAY COMPLETE]", pnx + 7, pny + 9);
                    } else {
                        _canvas->drawString("[ATMOSPHERE RE-ENTRY]", pnx + 7, pny + 9);
                    }
                }
            }
        } else {
            drawSatelliteIcon(sx, sy, sat.iconType, drawColor, renderDark, intensity);
        }

        // 2. 空间站专属太空行走 (EVA Spacewalk) 彩蛋动效
        // 透视放大：宇航员向视线近景漂浮，尺寸放大至 3.5 倍以上并展现头盔面罩探照灯、胸前 DCM 面板、生命背包与招手细节
        if (_evaActive && sat.isSelected && sat.iconType == ICON_STATION) {
            float p = _evaProgress;
            float zoomProg = 0.0f;
            float distRatio = 0.0f;
            int waveY = 0;

            if (p < 0.25f) {
                // 阶段 1：气闸舱开启，安全系绳延伸，宇航员向屏幕近景漂移 (约 6.5s)
                distRatio = p / 0.25f;
                zoomProg = distRatio;
            } else if (p < 0.75f) {
                // 阶段 2：在轨近景特写漫步，微重力浮动，向屏幕用户招手互动 (约 13s)
                distRatio = 1.0f;
                zoomProg = 1.0f;
                waveY = (int)(sinf((float)millis() * 0.0035f) * 2.0f);
            } else {
                // 阶段 3：宇航员顺着安全系绳拉回舱口，透视缩小回舱 (约 6.5s)
                distRatio = 1.0f - (p - 0.75f) / 0.25f;
                zoomProg = distRatio;
            }

            // 屏幕坐标：宇航员漂浮到空间站右下方近景
            int ax_eva = sx + (int)(distRatio * 26.0f);
            int ay_eva = sy + 4 + (int)(distRatio * 10.0f) + waveY;

            // 空间站气闸舱门 (高亮舱口)
            _canvas->fillRect(sx + 3, sy - 1, 3, 3, _display->color565(160, 170, 190));
            _canvas->drawPixel(sx + 4, sy, _display->color565(240, 250, 255));

            // 金黄色悬链线安全系绳 (Gold Safety Tether)
            if (distRatio > 0.05f) {
                int midX = (sx + 4 + ax_eva) / 2 + 2;
                int midY = (sy + ay_eva) / 2 + 5; // 失重微弱悬链自然垂弯
                _canvas->drawLine(sx + 4, sy, midX, midY, _display->color565(210, 175, 10));
                _canvas->drawLine(midX, midY, ax_eva - 4, ay_eva + 1, _display->color565(255, 225, 40));
                _canvas->drawLine(midX, midY + 1, ax_eva - 4, ay_eva + 2, _display->color565(255, 205, 30));
            }

            // 宇航员近景精细特写 (当完全出舱时，呈现高细节白色 EMU 宇航服)
            if (distRatio > 0.15f) {
                // (1) PLSS 生命保障背包 (位于宇航员左后侧)
                _canvas->fillRect(ax_eva - 6, ay_eva - 4, 3, 8, _display->color565(160, 165, 175));
                _canvas->drawFastVLine(ax_eva - 7, ay_eva - 3, 6, _display->color565(110, 115, 125));
                _canvas->drawPixel(ax_eva - 6, ay_eva + 4, _display->color565(255, 130, 20)); // SAFER 辅助喷嘴微光

                // (2) 宇航服主躯干 (Hard Upper Torso, 洁白加压防护层)
                _canvas->fillRect(ax_eva - 3, ay_eva - 2, 7, 7, TFT_WHITE);
                _canvas->drawFastHLine(ax_eva - 3, ay_eva + 4, 7, _display->color565(190, 195, 205)); // 腰部接环阴影

                // (3) 胸前 DCM 生命维持显示控制板 (带状态指示灯)
                _canvas->fillRect(ax_eva - 1, ay_eva - 1, 3, 3, _display->color565(70, 85, 100));
                _canvas->drawPixel(ax_eva - 1, ay_eva, _display->color565(255, 60, 60));  // 红色氧气状态灯
                _canvas->drawPixel(ax_eva + 1, ay_eva, _display->color565(40, 220, 255)); // 青色通信指示灯

                // (4) 航天头盔 (纯白圆弧罩 + 镀金面罩 + 双侧探照灯)
                _canvas->fillCircle(ax_eva, ay_eva - 6, 3, TFT_WHITE);
                _canvas->fillRect(ax_eva - 2, ay_eva - 7, 5, 3, _display->color565(255, 210, 30)); // 纯金镀膜遮阳面罩
                _canvas->drawPixel(ax_eva - 1, ay_eva - 7, _display->color565(255, 255, 210));     // 面罩高光反光
                _canvas->drawPixel(ax_eva - 3, ay_eva - 8, _display->color565(240, 250, 255));     // 左侧头盔 LED 探照灯
                _canvas->drawPixel(ax_eva + 3, ay_eva - 8, _display->color565(240, 250, 255));     // 右侧头盔 LED 探照灯

                // (5) 左臂 (微重力自然把持)
                _canvas->drawLine(ax_eva - 3, ay_eva, ax_eva - 5, ay_eva + 2, TFT_WHITE);
                _canvas->drawPixel(ax_eva - 6, ay_eva + 3, _display->color565(200, 205, 215)); // 灰色加压手套

                // (6) 右臂与招手动画 (在阶段 2 中向屏幕用户招手挥动)
                if (p >= 0.25f && p < 0.75f) {
                    int waveFrame = (millis() / 250) % 4; // 0, 1, 2, 3
                    if (waveFrame == 0) {
                        _canvas->drawLine(ax_eva + 3, ay_eva, ax_eva + 6, ay_eva - 4, TFT_WHITE);
                        _canvas->drawPixel(ax_eva + 7, ay_eva - 5, _display->color565(255, 255, 255));
                    } else if (waveFrame == 1 || waveFrame == 3) {
                        _canvas->drawLine(ax_eva + 3, ay_eva, ax_eva + 5, ay_eva - 6, TFT_WHITE);
                        _canvas->drawPixel(ax_eva + 5, ay_eva - 7, _display->color565(255, 255, 255));
                    } else {
                        _canvas->drawLine(ax_eva + 3, ay_eva, ax_eva + 7, ay_eva - 2, TFT_WHITE);
                        _canvas->drawPixel(ax_eva + 8, ay_eva - 2, _display->color565(255, 255, 255));
                    }
                } else {
                    _canvas->drawLine(ax_eva + 3, ay_eva, ax_eva + 5, ay_eva + 2, TFT_WHITE);
                    _canvas->drawPixel(ax_eva + 6, ay_eva + 3, _display->color565(200, 205, 215));
                }

                // (7) 加压双腿 (失重微屈状态)
                _canvas->drawLine(ax_eva - 2, ay_eva + 5, ax_eva - 3, ay_eva + 9, TFT_WHITE);
                _canvas->drawPixel(ax_eva - 4, ay_eva + 9, _display->color565(170, 175, 185)); // 左靴
                _canvas->drawLine(ax_eva + 2, ay_eva + 5, ax_eva + 3, ay_eva + 8, TFT_WHITE);
                _canvas->drawPixel(ax_eva + 4, ay_eva + 8, _display->color565(170, 175, 185)); // 右靴
            }

            // HUD 提示文字
            _canvas->setTextColor(_display->color565(0, 230, 255));
            _canvas->drawString("EVA IN PROGRESS · ASTRONAUT SPACEWALK", sx - 40, sy + 18);
        }

        // 若无线电正在过境发射，叠加动态射频数据发射图标（即使未光学可见亦可发射）
        if (sat.isRadioTransmitting) {
            drawRadioTransmissionWave(_canvas, sx, sy, sat.color);
        }
        
        _canvas->setTextColor(drawColor);
        _canvas->setTextSize(1);
        const char* displayName = sat.name;
        if (sat.isRecentLaunchBatch && sat.shortName != nullptr) {
            displayName = sat.shortName;
        }
        _canvas->drawString(displayName, sx + 8, sy - 4);
    }
}

void EarthRenderer::drawFocusSightLineAndShadow(double centerLat, double centerLon, double userLat, double userLon, const std::vector<SatRenderData>& satellites) {
    if (!_drawDecorations || userLat > 90.0) return;

    // 获取观察者地面坐标点屏幕投影 (ux, uy)
    int ux = -1, uy = -1;
    bool userVisible = projectOrthographic(userLat, userLon, 0, centerLat, centerLon, ux, uy);

    // 绘制太阳日影方位对齐线 (Solar Shadow) —— 纯粹水滴形灰色阴影大头针，尖端直连定位点并随太阳方向围绕旋转
    if (_hasSunData && userVisible) {
        float uLatR = (float)userLat * (float)DEG_TO_RAD;
        float uLonR = (float)userLon * (float)DEG_TO_RAD;

        // 计算观察者处的太阳仰角 (Solar Elevation Angle)
        float sin_sunEl = sinf(uLatR) * _sinSubLat + cosf(uLatR) * _cosSubLat * cosf(_subLonRad - uLonR);
        if (sin_sunEl > 1.0f) sin_sunEl = 1.0f;
        if (sin_sunEl < -1.0f) sin_sunEl = -1.0f;
        float sunElDeg = asinf(sin_sunEl) * (float)RAD_TO_DEG;

        // 1. 太阳仰角晨昏过渡渐变因子 (太阳仰角在 -1.0° 到 +9.0° 之间 10° 范围平滑渐变)
        float sunFade = (sunElDeg + 1.0f) * 0.1f;
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
            float dLon = _subLonRad - uLonR;
            float y = sinf(dLon) * _cosSubLat;
            float x = cosf(uLatR) * _sinSubLat - sinf(uLatR) * _cosSubLat * cosf(dLon);
            float sunAzRad = atan2f(y, x);

            // 日影方向 = 太阳反方向 (Sun Azimuth + 180deg)
            float shadowAzRad = sunAzRad + (float)M_PI;

            // 动态调节水滴阴影大头针的伸展长度 (正午短，早晚长)
            float clampEl = sunElDeg < 5.0f ? 5.0f : sunElDeg;
            float d_dist = 0.12f / tanf(clampEl * (float)DEG_TO_RAD);
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
}

void EarthRenderer::render(double centerLat, double centerLon, double userLat, double userLon, const std::vector<SatRenderData>& satellites) {
    static uint32_t lastTime = 0;
    static int frames = 0;
    static int currentFPS = 0;

    updateFrameConstants(centerLat, centerLon);

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

    const float sin_subLat = _sinSubLat;
    const float cos_subLat = _cosSubLat;
    const float sin_subLon = _sinSubLon;
    const float cos_subLon = _cosSubLon;

    const float sin_cLat = _sinCLat;
    const float cos_cLat = _cosCLat;
    const float sin_cLon = _sinCLon;
    const float cos_cLon = _cosCLon;

    const float sin_roll = _sinRoll;
    const float cos_roll = _cosRoll;

    const float sin_pitch = _sinPitch;
    const float cos_pitch = _cosPitch;
    const float z_limit = _zLimit;
    const bool zeroPitch = (_cameraPitch == 0.0f);

    const float r = (float)_earthRadius;

    int width = _canvas->width();
    int height = _canvas->height();
    std::uint16_t* buffer = (std::uint16_t*)_canvas->getBuffer();
    if (!buffer) return;

    // 快进时采用步长 2 + 1.25x 亮度能量守恒补偿，省去 50% 投影开销且夜景璀璨感无损
    const int step = _isFastForwarding ? 2 : 1;
    const float ffBoost = _isFastForwarding ? 6.25f : 5.0f; // (1 / 0.20) * (1.25 or 1.0)

    for (int i = 0; i < light_points_count; i += step) {
        float sin_lat = light_points[i].sinLat;
        float cos_lat = light_points[i].cosLat;
        float sin_lon = light_points[i].sinLon;
        float cos_lon = light_points[i].cosLon;

        // 1. Determine if the point is in darkness (cos_dist <= 0.05f)
        float cos_lon_subLon = cos_lon * cos_subLon + sin_lon * sin_subLon;
        float cos_dist = sin_subLat * sin_lat + cos_subLat * cos_lat * cos_lon_subLon;

        if (cos_dist <= 0.05f) {
            float cos_dLon = cos_lon * cos_cLon + sin_lon * sin_cLon;
            float cos_c = sin_cLat * sin_lat + cos_cLat * cos_lat * cos_dLon;
            if (zeroPitch && cos_c < 0.0f) continue;

            float sin_dLon = sin_lon * cos_cLon - cos_lon * sin_cLon;

            float x = r * cos_lat * sin_dLon;
            float y = r * (cos_cLat * sin_lat - sin_cLat * cos_lat * cos_dLon);
            float z = r * cos_c;

            float z_temp = z - _cameraFocusR;
            float z_pitched = y * sin_pitch + z_temp * cos_pitch;
            float z_actual = z_pitched + _cameraFocusR;

            if (z_actual >= z_limit) {
                float y_pitched = y * cos_pitch - z_temp * sin_pitch;
                float rotatedX = x * cos_roll - y_pitched * sin_roll;
                float rotatedY = x * sin_roll + y_pitched * cos_roll;

                int outX = _centerX + _centerOffsetX + (int)rotatedX;
                int outY = _centerY + _centerOffsetY - (int)rotatedY;

                if (outX >= 0 && outX < width && outY >= 0 && outY < height) {
                    float factor = (0.05f - cos_dist) * ffBoost;
                    if (factor > 1.0f) factor = 1.0f;
                    if (factor < 0.0f) factor = 0.0f;

                    int idx = outY * width + outX;
                    uint16_t cur565 = __builtin_bswap16(buffer[idx]);

                    uint16_t r5 = (cur565 >> 11) & 0x1F;
                    uint16_t g6 = (cur565 >> 5) & 0x3F;
                    uint16_t b5 = cur565 & 0x1F;

                    uint16_t dr5 = (uint16_t)(7 * factor);
                    uint16_t dg6 = (uint16_t)(9 * factor);
                    uint16_t db5 = (uint16_t)(1.5f * factor);

                    r5 = (r5 + dr5 > 31) ? 31 : (r5 + dr5);
                    g6 = (g6 + dg6 > 63) ? 63 : (g6 + dg6);
                    b5 = (b5 + db5 > 31) ? 31 : (b5 + db5);

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
    renderSatelliteIcon(_canvas, x, y, iconType, color, renderDark, intensity);
}

void EarthRenderer::renderSatelliteIcon(LGFX_Sprite* canvas, int x, int y, SatIconType iconType, uint16_t color, bool renderDark, float intensity) {
    if (!canvas) return;

    auto getScaled = [&](uint16_t baseCol) -> uint16_t {
        if (intensity >= 0.99f) return baseCol;
        return scaleColor(baseCol, intensity);
    };

    uint16_t darkGrayCol = getScaled(renderDark ? canvas->color565(80,80,80) : TFT_WHITE);
    uint16_t lightGrayCol = getScaled(renderDark ? canvas->color565(50,50,50) : TFT_LIGHTGRAY);
    uint16_t orangeCol = getScaled(TFT_ORANGE);
    uint16_t hamLightGray = getScaled(TFT_LIGHTGRAY);

    if (iconType == ICON_STATION) {
        // Space Station (Core module + big solar panels)
        canvas->fillRect(x - 2, y - 1, 5, 3, darkGrayCol);
        canvas->fillRect(x - 7, y - 3, 4, 7, color);
        canvas->fillRect(x + 4, y - 3, 4, 7, color);
    } else if (iconType == ICON_ROCKET) {
        // Rocket Debris (Cylinder + nozzle + engine flame)
        canvas->fillRect(x - 2, y - 4, 5, 8, darkGrayCol);
        canvas->fillTriangle(x - 2, y - 4, x + 2, y - 4, x, y - 7, color);
        canvas->fillRect(x - 2, y + 4, 2, 2, orangeCol); // Engine 1
        canvas->fillRect(x + 1, y + 4, 2, 2, orangeCol); // Engine 2
    } else if (iconType == ICON_TELESCOPE) {
        // Space Telescope (Tube + lens cover + solar panel)
        canvas->fillRect(x - 2, y - 3, 5, 7, darkGrayCol);
        canvas->fillRect(x - 3, y - 4, 7, 2, lightGrayCol);
        canvas->fillRect(x - 6, y, 3, 2, color);
        canvas->fillRect(x + 4, y, 3, 2, color);
    } else if (iconType == ICON_DEEPSPACE) {
        // Deep Space (Star flare)
        canvas->drawLine(x, y - 5, x, y + 5, color);
        canvas->drawLine(x - 5, y, x + 5, y, color);
        canvas->drawLine(x - 2, y - 2, x + 2, y + 2, darkGrayCol);
        canvas->drawLine(x - 2, y + 2, x + 2, y - 2, darkGrayCol);
    } else if (iconType == ICON_DEBRIS) {
        // Space Debris (Half regular solar grid panel on left, jagged outline and floating dots on right)
        canvas->fillRect(x - 6, y - 2, 6, 5, color);
        canvas->drawLine(x - 6, y - 2, x - 1, y - 2, lightGrayCol);
        canvas->drawLine(x - 6, y + 2, x - 1, y + 2, lightGrayCol);
        canvas->drawLine(x - 1, y - 2, x - 1, y + 2, lightGrayCol);
        
        // Jagged right half
        canvas->drawLine(x, y - 2, x + 3, y - 1, color);
        canvas->drawLine(x + 3, y - 1, x + 1, y + 1, color);
        canvas->drawLine(x + 1, y + 1, x, y + 2, color);
        
        // Detached drifting debris fragments
        canvas->drawPixel(x + 5, y - 3, color);
        canvas->drawPixel(x + 5, y + 2, color);
    } else if (iconType == ICON_DFH1) {
        // DongFangHong-1 (Spherical body + 4 antennas)
        canvas->fillCircle(x, y, 3, darkGrayCol);
        canvas->drawLine(x - 2, y - 2, x - 6, y - 6, color);
        canvas->drawLine(x + 2, y - 2, x + 6, y - 6, color);
        canvas->drawLine(x - 2, y + 2, x - 6, y + 6, color);
        canvas->drawLine(x + 2, y + 2, x + 6, y + 6, color);
    } else if (iconType == ICON_BLUEWALKER3) {
        // BlueWalker 3 (Center array + left/right huge flat solar arrays)
        canvas->fillRect(x - 1, y - 1, 3, 3, darkGrayCol);
        canvas->fillRect(x - 7, y - 3, 5, 7, color);
        canvas->fillRect(x + 3, y - 3, 5, 7, color);
        canvas->drawFastVLine(x - 5, y - 3, 7, TFT_BLACK);
        canvas->drawFastVLine(x + 5, y - 3, 7, TFT_BLACK);
        canvas->drawFastHLine(x - 7, y, 5, TFT_BLACK);
        canvas->drawFastHLine(x + 3, y, 5, TFT_BLACK);
    } else if (iconType == ICON_WEATHER) {
        // Weather Sat (Body + left panel + right instrument mount)
        canvas->fillRect(x - 1, y - 2, 3, 5, darkGrayCol);
        canvas->drawLine(x - 2, y, x - 6, y - 2, color);
        canvas->fillRect(x - 8, y - 4, 3, 3, color);
        canvas->drawFastHLine(x + 2, y, 2, color);
        canvas->drawPixel(x + 3, y - 1, color);
    } else if (iconType == ICON_NAVIGATION) {
        // Navigation Sat (Body + symmetric flat solar wings + lower helical antenna)
        canvas->fillRect(x - 1, y - 2, 3, 5, darkGrayCol);
        canvas->drawFastHLine(x - 6, y, 5, hamLightGray);
        canvas->drawFastHLine(x + 2, y, 5, hamLightGray);
        canvas->fillRect(x - 8, y - 1, 3, 3, color);
        canvas->fillRect(x + 6, y - 1, 3, 3, color);
        canvas->drawFastVLine(x, y + 3, 2, color);
        canvas->drawPixel(x, y + 5, color);
    } else if (iconType == ICON_COMMUNICATION) {
        // Comm Sat (Spherical core + top V-shape antenna + bottom dish antenna)
        canvas->fillCircle(x, y, 2, darkGrayCol);
        canvas->drawLine(x, y - 2, x - 3, y - 6, color);
        canvas->drawLine(x, y - 2, x + 3, y - 6, color);
        canvas->drawFastVLine(x, y + 2, 2, color);
        canvas->drawFastHLine(x - 2, y + 4, 5, color);
    } else if (iconType == ICON_SPACEPLANE) {
        // Spaceplane (X-37B) — top-down view: blunt nose, delta wings, vertical stabiliser
        // Nose
        canvas->fillRect(x - 1, y - 4, 3, 2, darkGrayCol);
        // Fuselage
        canvas->fillRect(x - 2, y - 2, 5, 5, darkGrayCol);
        // Delta wings (widest at centre)
        canvas->fillTriangle(x - 4, y + 1, x - 1, y - 1, x - 1, y + 3, color);
        canvas->fillTriangle(x + 5, y + 1, x + 2, y - 1, x + 2, y + 3, color);
        // Vertical tail fin (offset slightly right)
        canvas->drawFastVLine(x + 1, y + 3, 3, color);
    } else if (iconType == ICON_SOLAR_PROBE) {
        // Solar Probe (Parker) — heat shield (wide disc) + instrument boom + two tiny wings
        // Heat shield
        canvas->fillEllipse(x, y - 1, 4, 3, lightGrayCol);
        canvas->drawEllipse(x, y - 1, 4, 3, color);
        // Instrument boom below shield
        canvas->drawFastVLine(x, y + 2, 3, darkGrayCol);
        // Tiny solar panels flanking boom
        canvas->fillRect(x - 3, y + 3, 2, 1, color);
        canvas->fillRect(x + 2, y + 3, 2, 1, color);
    } else if (iconType == ICON_CHAIN_MONO) {
        // Chain Mono module — rectangular body with screen and Grove connector nub
        canvas->fillRect(x - 4, y - 3, 9, 7, darkGrayCol);   // Module body
        canvas->fillRect(x - 3, y - 2, 7, 5, color);          // Screen area (lighter)
        canvas->fillRect(x - 2, y - 1, 5, 3, darkGrayCol);    // Screen content (dark pixels)
        canvas->fillRect(x - 5, y + 1, 1, 2, lightGrayCol);   // Left Grove nub
    } else if (iconType == ICON_LANDER) {
        // Lander — hexagonal body + top antenna + three landing legs
        // Antenna
        canvas->drawFastVLine(x, y - 4, 2, color);
        // Main body (flat hexagon)
        canvas->fillRect(x - 2, y - 2, 5, 4, darkGrayCol);
        // Three landing legs (left, centre-right, right)
        canvas->drawLine(x - 2, y + 2, x - 4, y + 4, color);
        canvas->drawLine(x,     y + 2, x,     y + 4, color);
        canvas->drawLine(x + 2, y + 2, x + 4, y + 4, color);
        // Foot pads
        canvas->drawFastHLine(x - 5, y + 4, 2, color);
        canvas->drawPixel(x, y + 5, color);
        canvas->drawFastHLine(x + 4, y + 4, 2, color);
    } else {
        // Generic Sat (Tiny cube + single solar wing)
        canvas->fillRect(x - 1, y - 1, 3, 3, darkGrayCol);
        canvas->fillRect(x - 5, y - 1, 3, 3, color);
        canvas->drawLine(x - 2, y, x - 1, y, hamLightGray);
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

void EarthRenderer::drawPointNemo(double centerLat, double centerLon) {
    // 尼莫点精确地理坐标 (Point Nemo): 48°52.6′S, 123°23.6′W (大洋不可接近之极，世界航天器坟场)
    const double NEMO_LAT = -48.8767;
    const double NEMO_LON = -123.3933;

    int pnx, pny;
    if (!projectOrthographic(NEMO_LAT, NEMO_LON, 0.0, centerLat, centerLon, pnx, pny)) {
        return; // 位于地平线以下或地球背面
    }

    if (pnx < -25 || pnx > _canvas->width() + 25 || pny < -25 || pny > _canvas->height() + 25) {
        return;
    }

    uint32_t t = millis();

    // 1. 海洋水波涟漪动画 (两道同心深海涌波，带相位差)
    uint32_t phase1 = t % 2600;
    float prog1 = (float)phase1 / 2600.0f;
    int r1 = 3 + (int)(prog1 * 10.0f); // 3px ~ 13px
    uint8_t a1 = (uint8_t)(140.0f * (1.0f - prog1));
    if (a1 > 12) {
        uint8_t waveG = (uint8_t)(a1 * 0.7f);
        _canvas->drawCircle(pnx, pny, r1, _display->color565(0, waveG, a1));
    }

    uint32_t phase2 = (t + 1300) % 2600;
    float prog2 = (float)phase2 / 2600.0f;
    int r2 = 3 + (int)(prog2 * 10.0f);
    uint8_t a2 = (uint8_t)(140.0f * (1.0f - prog2));
    if (a2 > 12) {
        uint8_t waveG = (uint8_t)(a2 * 0.55f);
        _canvas->drawCircle(pnx, pny, r2, _display->color565(0, waveG, (uint8_t)(a2 * 0.85f)));
    }

    // 2. “和平号空间站 (Mir)”残骸水下沉浮轮廓 (Wreckage Silhouette)
    // 2001年3月23日受控再入沉睡于南太平洋的庞然大物 (DOS-7多舱段十字交叉桁架构型)
    uint16_t colCore = _display->color565(45, 125, 155);       // 核心舱基底色
    uint16_t colModule = _display->color565(35, 95, 125);      // 实验舱段色
    uint16_t colSolar = _display->color565(25, 75, 105);       // 太阳能帆板骨架
    uint16_t colHighlight = _display->color565(90, 190, 220);  // 金属高光/对接球

    // (1) 核心舱主轴体 (Base Block & Kvant-1, 斜向延伸)
    _canvas->drawLine(pnx - 3, pny - 1, pnx + 3, pny + 1, colCore);
    _canvas->drawPixel(pnx - 4, pny - 2, colHighlight); // 前向六向对接球节点 (Node)

    // (2) 侧向实验舱 (Kvant-2 / Kristall 交叉对接舱段)
    _canvas->drawLine(pnx - 1, pny - 3, pnx + 1, pny + 3, colModule);
    _canvas->drawPixel(pnx - 2, pny - 4, colModule);
    _canvas->drawPixel(pnx + 2, pny + 4, colModule);

    // (3) 残留折断的太阳能帆板桁架 (Broken Solar Arrays)
    // 一侧帆板向左上方折断残存
    _canvas->drawPixel(pnx - 3, pny - 5, colSolar);
    _canvas->drawPixel(pnx - 4, pny - 5, colSolar);
    // 另一侧帆板右下方残存
    _canvas->drawPixel(pnx + 3, pny + 2, colSolar);
    _canvas->drawPixel(pnx + 4, pny + 3, colSolar);

    // (4) 深海水下光影焦散 (Caustic Reflection) 呼吸反光
    float glint = (sinf((float)t * 0.0028f) + 1.0f) * 0.5f;
    uint8_t gVal = (uint8_t)(70 + 130 * glint);
    _canvas->drawPixel(pnx, pny, _display->color565(0, gVal, (uint8_t)(gVal * 0.95f)));

    // 3. 永久地标小字标注 (深海微光青色，含 Point Nemo 与 MIR 2001 残骸标识)
    _canvas->setTextColor(_display->color565(35, 140, 180));
    _canvas->drawString("Point Nemo", pnx + 7, pny - 5);
    _canvas->setTextColor(_display->color565(20, 85, 115));
    _canvas->drawString("MIR 2001", pnx + 7, pny + 2);
}
