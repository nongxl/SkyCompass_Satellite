#include "earth_renderer.h"
#include "earth_data.h"
#include <math.h>

#define DEG_TO_RAD 0.017453292519943295769236907684886

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
        r += sqrtf((float)alt) * 0.4f; 
    }

    float cos_c = sinf(cLatRad) * sinf(latRad) + cosf(cLatRad) * cosf(latRad) * cosf(lonRad - cLonRad);
    if (cos_c < 0) return false; // Behind the Earth

    float x = r * cosf(latRad) * sinf(lonRad - cLonRad);
    float y = r * (cosf(cLatRad) * sinf(latRad) - sinf(cLatRad) * cosf(latRad) * cosf(lonRad - cLonRad));

    // AR Camera Effect: True 3D Roll (rotating the camera around the forward axis)
    float rollRad = -_cameraRoll * DEG_TO_RAD; // Negative to match natural tilt direction
    float rotatedX = x * cosf(rollRad) - y * sinf(rollRad);
    float rotatedY = x * sinf(rollRad) + y * cosf(rollRad);

    outX = _centerX + (int)rotatedX;
    outY = _centerY - (int)rotatedY;
    return true;
}

void EarthRenderer::drawContinents(double centerLat, double centerLon) {
    // Pre-calculate constants for this frame to save THOUSANDS of CPU cycles
    float cLatRad = (float)centerLat * DEG_TO_RAD;
    float cLonRad = (float)centerLon * DEG_TO_RAD;
    float sin_cLat = sinf(cLatRad);
    float cos_cLat = cosf(cLatRad);
    float rollRad = -_cameraRoll * DEG_TO_RAD;
    float sin_roll = sinf(rollRad);
    float cos_roll = cosf(rollRad);
    
    float subLatR = (float)_subsolarLat * DEG_TO_RAD;
    float subLonR = (float)_subsolarLon * DEG_TO_RAD;
    float sin_subLat = sinf(subLatR);
    float cos_subLat = cosf(subLatR);

    auto drawPath = [&](const float* pts, int count) {
        int prevX = -1, prevY = -1;
        bool prevVisible = false;
        for (int j = 0; j < count; j++) {
            float lat = pts[j*2];
            float lon = pts[j*2+1];
            
            // Inline projection to use precalculated trig
            float latRad = lat * DEG_TO_RAD;
            float lonRad = lon * DEG_TO_RAD;
            float sin_lat = sinf(latRad);
            float cos_lat = cosf(latRad);
            float dLon = lonRad - cLonRad;
            float cos_dLon = cosf(dLon);
            float sin_dLon = sinf(dLon);
            
            float cos_c = sin_cLat * sin_lat + cos_cLat * cos_lat * cos_dLon;
            
            if (cos_c >= 0) {
                float r = (float)_earthRadius;
                float x = r * cos_lat * sin_dLon;
                float y = r * (cos_cLat * sin_lat - sin_cLat * cos_lat * cos_dLon);
                
                float rotatedX = x * cos_roll - y * sin_roll;
                float rotatedY = x * sin_roll + y * cos_roll;
                int outX = _centerX + (int)rotatedX;
                int outY = _centerY - (int)rotatedY;
                
                if (prevVisible) {
                    if (abs(outX - prevX) < 100 && abs(outY - prevY) < 100) {
                        float cos_dist = sin_subLat * sin_lat + cos_subLat * cos_lat * cosf(lonRad - subLonR);
                        
                        uint8_t cr = 50, cg = 150, cb = 50;
                        if (lat > 0) {
                            float factor = lat / 90.0f;
                            cr = (uint8_t)(50 * (1 - factor));
                            cb = (uint8_t)(50 * (1 - factor) + 150 * factor);
                        } else {
                            float factor = -lat / 90.0f;
                            cg = (uint8_t)(150 * (1 - factor) + 50 * factor);
                            cb = (uint8_t)(50 * (1 - factor) + 150 * factor);
                        }
                        
                        if (_hasSunData) {
                            float illum = (cos_dist + 0.2f) / 0.4f;
                            if (illum > 1.0f) illum = 1.0f;
                            if (illum < 0.2f) illum = 0.2f;
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

void EarthRenderer::drawEarth(double centerLat, double centerLon, double userLat, double userLon) {
    // Draw Earth circle (darker base for night side feeling)
    _canvas->fillCircle(_centerX, _centerY, _earthRadius, _display->color565(5, 15, 30));
    _canvas->drawCircle(_centerX, _centerY, _earthRadius, _display->color565(30, 60, 100));
    
    // Draw continents
    drawContinents(centerLat, centerLon);
    
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
                bool visible = cos_c >= 0;
                
                int x = -1, y = -1;
                if (visible) {
                    float proj_x = r * (P_y * cos_cLon - P_x * sin_cLon);
                    float proj_y = r * (cos_cLat * P_z - sin_cLat * term2);
                    float rotatedX = proj_x * cos_roll - proj_y * sin_roll;
                    float rotatedY = proj_x * sin_roll + proj_y * cos_roll;
                    x = _centerX + (int)rotatedX;
                    y = _centerY - (int)rotatedY;
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
    {
        int nx, ny;
        double targetLat = centerLat + 1.0;
        double targetLon = centerLon;
        if (targetLat > 90.0) {
            targetLat = 89.0;
            targetLon = centerLon + 180.0;
        }
        
        if (projectOrthographic(targetLat, targetLon, 0, centerLat, centerLon, nx, ny)) {
            float dx = nx - _centerX;
            float dy = ny - _centerY;
            float len = sqrt(dx*dx + dy*dy);
            if (len > 0.1f) {
                dx /= len;
                dy /= len;
                
                int cx = _canvas->width() - 25;
                int cy = 25;
                
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
}

void EarthRenderer::drawSatellite(const SatRenderData& sat, double centerLat, double centerLon, double userLat, double userLon) {
    if (sat.pastOrbit.empty() && sat.futureOrbit.empty()) return;
    
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
        
        isVisibleToObserver = (isNight && isAboveHorizon && !isEclipsed);
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
                    bool shadow = isSatelliteInShadow(pt.lat, pt.lon, pt.alt, _subsolarLat, _subsolarLon, _hasSunData);
                    uint16_t color = shadow ? _display->color565(30, 30, 30) : baseColor;
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
    
    drawOrbit(sat.pastOrbit, pastColor);
    drawOrbit(sat.futureOrbit, futureColor);
    
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
        _canvas->drawString(sat.name.c_str(), sx + 8, sy - 4);
    }
}

void EarthRenderer::render(double centerLat, double centerLon, double userLat, double userLon, const std::vector<SatRenderData>& satellites) {
    static uint32_t lastTime = 0;
    static int frames = 0;
    static int currentFPS = 0;
    
    _canvas->fillSprite(BLACK);
    
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
