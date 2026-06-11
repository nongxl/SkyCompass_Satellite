#include "view_3d_renderer.h"
#include "ui_manager.h"
#include <math.h>

View3DRenderer::View3DRenderer() :
    _display(nullptr),
    _sunAzimuth(0),
    _sunAltitude(45),
    _moonAzimuth(180),
    _moonAltitude(30),
    _sunCalculator(nullptr),
    _moonCalculator(nullptr),
    _celestialCore(nullptr),
    _positionManager(nullptr),
    _timeMachine(nullptr),
    _hasReferenceOrientation(false),
    _refPitch(0),
    _refRoll(0),
    _smoothTransition(false),
    _transitionSpeed(0.15f),
    _panOffsetX(0),
    _panOffsetY(0),
    _lastSunPathDay(0),
    _lastMoonPathDay(0),
    _lastGalaxyPathDay(0),
    _moonPathUpdateIndex(-1) {
    
    // 初始化25点滑动窗口缓存
    _sunTrajectoryCache.isValid = false;
    _sunTrajectoryCache.baseTimestamp = 0;
    _sunTrajectoryCache.cachedBaseHour = 0;
    
    _moonTrajectoryCache.isValid = false;
    _moonTrajectoryCache.baseTimestamp = 0;
    _moonTrajectoryCache.cachedBaseHour = 0;
    
    _galaxyCache.isValid = false;
    _galaxyCache.cachedBaseHour = 0;
    for (int i = 0; i < 24; i++) {
        _galaxyCache.hourlyPoints[i].clear();
    }
    
    _camera.pitch = 0;
    _camera.roll = 0;
    _camera.yaw = 0;
    _camera.distance = 100;
    _camera.fov = 60;
    _camera.verticalExaggeration = 1.0f;
    
    _targetCamera = _camera;
    
    _config.showGrid = false;
    _config.showHorizon = true;
    _config.showCardinalPoints = true;
    _config.showSunPath = true;
    _config.showMoonPath = true;
    _config.showGalaxyPath = true;
    _config.showGround = true;
    _config.gridColor = 0x4A49;
    _config.horizonColor = 0xFFFF;
    _config.groundColor = 0x5ACB;
    _config.skyColor = 0x18C3;
}

bool View3DRenderer::begin(HalDisplay* display) {
    if (!display) {
        return false;
    }
    _display = display;
    return true;
}



void View3DRenderer::setCamera(const Camera3D& camera) {
    _targetCamera = camera;
    if (!_smoothTransition) {
        _camera = _targetCamera;
    }
}

Camera3D View3DRenderer::getCamera() const {
    return _camera;
}

void View3DRenderer::updateCameraFromIMU(float pitch, float roll, float yaw) {
    // 限制 pitch 在 0 到 90 度之间，与地平圈逻辑一致
    _targetCamera.pitch = clampAngle(pitch, 0, 90);
    _targetCamera.roll = clampAngle(-roll, -180, 180);
    _targetCamera.yaw = yaw;
    
    // 立即更新相机参数，不使用平滑过渡
    _camera.pitch = _targetCamera.pitch;
    _camera.roll = _targetCamera.roll;
    _camera.yaw = _targetCamera.yaw;
}

void View3DRenderer::setRenderConfig(const RenderConfig& config) {
    _config = config;
}

RenderConfig View3DRenderer::getRenderConfig() const {
    return _config;
}

void View3DRenderer::render(uint16_t centerX, uint16_t centerY, uint16_t radius) {
    if (!_display) return;
    
    updateSmoothTransition();
    
    centerX += _panOffsetX;
    centerY += _panOffsetY;
    
    // 只使用3D模式
    render3D(centerX, centerY, radius);
}

void View3DRenderer::setSunPosition(float azimuth, float altitude) {
    _sunAzimuth = azimuth;
    _sunAltitude = altitude;
}

void View3DRenderer::setMoonPosition(float azimuth, float altitude) {
    _moonAzimuth = azimuth;
    _moonAltitude = altitude;
}

void View3DRenderer::setVerticalExaggeration(float value) {
    _camera.verticalExaggeration = constrain(value, 0.1f, 10.0f);
    _targetCamera.verticalExaggeration = _camera.verticalExaggeration;
}

float View3DRenderer::getVerticalExaggeration() const {
    return _camera.verticalExaggeration;
}

void View3DRenderer::zoom(float factor) {
    _targetCamera.distance *= factor;
    _targetCamera.distance = constrain(_targetCamera.distance, 20, 500);
    if (!_smoothTransition) {
        _camera.distance = _targetCamera.distance;
    }
}

void View3DRenderer::pan(int dx, int dy) {
    _panOffsetX += dx;
    _panOffsetY += dy;
    
    if (_display) {
        int maxPanX = _display->getWidth() / 2;
        int maxPanY = _display->getHeight() / 2;
        _panOffsetX = constrain(_panOffsetX, -maxPanX, maxPanX);
        _panOffsetY = constrain(_panOffsetY, -maxPanY, maxPanY);
    }
}

void View3DRenderer::reset() {
    _camera.pitch = 0;
    _camera.roll = 0;
    _camera.yaw = 0;
    _camera.distance = 100;
    _camera.verticalExaggeration = 1.0f;
    _targetCamera = _camera;
    _panOffsetX = 0;
    _panOffsetY = 0;
    _hasReferenceOrientation = false;
}

void View3DRenderer::setSunCalculator(SunCalculator* calculator) {
    _sunCalculator = calculator;
}

void View3DRenderer::setMoonCalculator(MoonCalculator* calculator) {
    _moonCalculator = calculator;
}

void View3DRenderer::setPositionManager(PositionManager* manager) {
    _positionManager = manager;
}

void View3DRenderer::setCelestialCore(CelestialCore* core) {
    _celestialCore = core;
}

void View3DRenderer::setTimeMachine(TimeMachine* timeMachine) {
    _timeMachine = timeMachine;
}

void View3DRenderer::setReferenceOrientation(float pitch, float roll) {
    _hasReferenceOrientation = true;
    _refPitch = pitch;
    _refRoll = roll;
}

void View3DRenderer::clearReferenceOrientation() {
    _hasReferenceOrientation = false;
    _refPitch = 0;
    _refRoll = 0;
}

void View3DRenderer::enableSmoothTransition(bool enable) {
    _smoothTransition = enable;
}

void View3DRenderer::setTransitionSpeed(float speed) {
    _transitionSpeed = constrain(speed, 0.01f, 1.0f);
}





void View3DRenderer::render3D(uint16_t centerX, uint16_t centerY, uint16_t radius) {
    if (!_display) return;
    
    uint16_t width = _display->getWidth();
    uint16_t height = _display->getHeight();

    // 0. 旋转矩阵预计算 (极致性能优化：每帧只计算一次三角函数)
    float rollRad = _camera.roll * DEG_TO_RAD;
    float pitchRad = _camera.pitch * DEG_TO_RAD;
    float yawRad = _camera.yaw * DEG_TO_RAD;
    _cosR = cos(rollRad); _sinR = sin(rollRad);
    _cosP = cos(pitchRad); _sinP = sin(pitchRad);
    _cosY = cos(yawRad); _sinY = sin(yawRad);

    // 0.1 获取投影中心和半径，用于预投影
    // 这些通常在 render3D 的外部参数中传入，保持一致
    
    // 0.2 轨迹预计算与滑动窗口同步 (30分钟粒度，49点)
    uint32_t currentTimestamp = _positionManager->getTimestamp();
    {
        uint32_t currentHalfHour = currentTimestamp / 1800; // 30分钟为单位的块
        uint32_t windowStartBlock = currentHalfHour - 24;   // 窗口起始：12小时前
        PositionData pos = _positionManager->getPosition();

        // 太阳轨迹同步 (30min 粒度)
        if (!_sunTrajectoryCache.isValid) {
            _sunTrajectoryCache.points3D.clear();
            for (int i = 0; i <= 48; i++) {
                uint32_t ts = (windowStartBlock + i) * 1800;
                CelestialVector v = _celestialCore->getCelestialVector(ts, pos.latitude, pos.longitude, CELESTIAL_SUN);
                _sunTrajectoryCache.points3D.push_back({(float)v.x, (float)v.y, (float)v.z});
            }
            _sunTrajectoryCache.cachedBaseHour = windowStartBlock; // 这里复用成员变量存 Block 索引
            _sunTrajectoryCache.isValid = true;
        } else if (windowStartBlock != _sunTrajectoryCache.cachedBaseHour) {
            int32_t diff = (int32_t)windowStartBlock - (int32_t)_sunTrajectoryCache.cachedBaseHour;
            if (abs(diff) >= 49) { _sunTrajectoryCache.isValid = false; }
            else if (diff > 0) {
                for (int32_t d = 0; d < diff; d++) {
                    _sunTrajectoryCache.points3D.erase(_sunTrajectoryCache.points3D.begin());
                    uint32_t newTs = (_sunTrajectoryCache.cachedBaseHour + 49) * 1800;
                    CelestialVector v = _celestialCore->getCelestialVector(newTs, pos.latitude, pos.longitude, CELESTIAL_SUN);
                    _sunTrajectoryCache.points3D.push_back({(float)v.x, (float)v.y, (float)v.z});
                    _sunTrajectoryCache.cachedBaseHour++;
                }
            } else {
                for (int32_t d = 0; d < -diff; d++) {
                    _sunTrajectoryCache.points3D.pop_back();
                    _sunTrajectoryCache.cachedBaseHour--;
                    uint32_t newTs = _sunTrajectoryCache.cachedBaseHour * 1800;
                    CelestialVector v = _celestialCore->getCelestialVector(newTs, pos.latitude, pos.longitude, CELESTIAL_SUN);
                    _sunTrajectoryCache.points3D.insert(_sunTrajectoryCache.points3D.begin(), {(float)v.x, (float)v.y, (float)v.z});
                }
            }
        }
        _sunPathPoints = _sunTrajectoryCache.points3D;

        // 月亮轨迹同步 (30min 粒度)
        if (!_moonTrajectoryCache.isValid) {
            _moonTrajectoryCache.points3D.clear();
            for (int i = 0; i <= 48; i++) {
                uint32_t ts = (windowStartBlock + i) * 1800;
                CelestialVector v = _celestialCore->getCelestialVector(ts, pos.latitude, pos.longitude, CELESTIAL_MOON);
                _moonTrajectoryCache.points3D.push_back({(float)v.x, (float)v.y, (float)v.z});
            }
            _moonTrajectoryCache.cachedBaseHour = windowStartBlock;
            _moonTrajectoryCache.isValid = true;
        } else if (windowStartBlock != _moonTrajectoryCache.cachedBaseHour) {
            int32_t diff = (int32_t)windowStartBlock - (int32_t)_moonTrajectoryCache.cachedBaseHour;
            if (abs(diff) >= 49) { _moonTrajectoryCache.isValid = false; }
            else if (diff > 0) {
                for (int32_t d = 0; d < diff; d++) {
                    _moonTrajectoryCache.points3D.erase(_moonTrajectoryCache.points3D.begin());
                    uint32_t newTs = (_moonTrajectoryCache.cachedBaseHour + 49) * 1800;
                    CelestialVector v = _celestialCore->getCelestialVector(newTs, pos.latitude, pos.longitude, CELESTIAL_MOON);
                    _moonTrajectoryCache.points3D.push_back({(float)v.x, (float)v.y, (float)v.z});
                    _moonTrajectoryCache.cachedBaseHour++;
                }
            } else {
                for (int32_t d = 0; d < -diff; d++) {
                    _moonTrajectoryCache.points3D.pop_back();
                    _moonTrajectoryCache.cachedBaseHour--;
                    uint32_t newTs = _moonTrajectoryCache.cachedBaseHour * 1800;
                    CelestialVector v = _celestialCore->getCelestialVector(newTs, pos.latitude, pos.longitude, CELESTIAL_MOON);
                    _moonTrajectoryCache.points3D.insert(_moonTrajectoryCache.points3D.begin(), {(float)v.x, (float)v.y, (float)v.z});
                }
            }
        }
        _moonPathPoints = _moonTrajectoryCache.points3D;
    }

    // 0.3 轨迹预投影流水线 (确保基于最新的轨迹点进行投影)
    _sunPathPoints2D.clear();
    for (const auto& p3d : _sunPathPoints) {
        Point3D rotated = applyCameraRotation(p3d);
        Point2D p2d = project3DPoint(rotated, centerX, centerY, radius);
        _sunPathPoints2D.push_back({(int16_t)p2d.x, (int16_t)p2d.y, true, rotated.z});
    }
    
    _moonPathPoints2D.clear();
    for (const auto& p3d : _moonPathPoints) {
        Point3D rotated = applyCameraRotation(p3d);
        Point2D p2d = project3DPoint(rotated, centerX, centerY, radius);
        _moonPathPoints2D.push_back({(int16_t)p2d.x, (int16_t)p2d.y, true, rotated.z});
    }

    // 0.4 获取当前本地时间，用于整点吸附判定
    TimeData localTime = _positionManager->getLocalTimeData(currentTimestamp);
    bool isOnWholeHour = (localTime.minute == 0 && localTime.second == 0);
    
    // 1. 绘制地下天体轨迹 (远景，Z < 0)
    if (_config.showSunPath) drawSunPath3D(centerX, centerY, radius, false);
    if (_config.showMoonPath) drawMoonPath3D(centerX, centerY, radius, false);
    if (_config.showGalaxyPath) {
        updateGalaxyProjectedPoints(centerX, centerY, radius);
        drawGalaxyPath3D(centerX, centerY, radius, false);
    }
    
    // 地下的实体 (远景)
    float sunAz = _sunAzimuth, sunAlt = _sunAltitude;
    float moonAz = _moonAzimuth, moonAlt = _moonAltitude;
    
    // 性能优化与吸附：如果是整点，且轨迹缓存已就绪，直接对齐到标记点
    if (isOnWholeHour) {
        int hourIdx = localTime.hour;
        if ((int)_sunPathPoints.size() > hourIdx) {
            // 注意：这里需要考虑 Az/Alt 还是直接 3D 向量？
            // 实体绘制 drawCelestialBody3D 需要 Az/Alt。
            // 由于轨迹点存的是 3D 向量，我们需要反算或直接在标记处绘制。
            // 为了保持物理引擎一致性，我们在这里仅保持输入参数的一致性。
        }
    }

    if (_sunAltitude < 0 && _sunAltitude > -90) drawCelestialBody3D(centerX, centerY, radius, sunAz, sunAlt, 0xFFE0, 8);
    if (_moonAltitude < 0 && _moonAltitude > -90) drawCelestialBody3D(centerX, centerY, radius, moonAz, moonAlt, 0xFFFF, 6);

    // 2. 地平面遮挡层 (Z = 0)
    // 根据画家的法则，这一层中的实心黑色底板将遮盖掉之前画在屏幕背后的第一层轨道。
    // 但是地平线本身（也就是白线 drawHorizon3D 和 N/S/E/W）不能被它遮盖！所以必须在地平面画完后再画线。
    if (_config.showGround) drawGroundPlane3D(centerX, centerY, radius);
    if (_config.showHorizon) drawHorizon3D(centerX, centerY, radius);
    if (_config.showCardinalPoints) drawCardinalPoints3D(centerX, centerY, radius);
    
    // 3. 绘制天上天体轨迹 (近景，Z >= 0)
    if (_config.showSunPath) drawSunPath3D(centerX, centerY, radius, true);
    if (_config.showMoonPath) drawMoonPath3D(centerX, centerY, radius, true);
    if (_config.showGalaxyPath) drawGalaxyPath3D(centerX, centerY, radius, true);
    
    // 4. 天上的实体 (近景)
    if (_sunAltitude >= 0) drawCelestialBody3D(centerX, centerY, radius, sunAz, sunAlt, 0xFFE0, 8);
    if (_moonAltitude >= 0) drawCelestialBody3D(centerX, centerY, radius, moonAz, moonAlt, 0xFFFF, 6);
    
    // 绘制世界固定参考点（空间锚点）
    drawWorldReferencePoint3D(centerX, centerY, radius);
}

void View3DRenderer::drawSkyGrid3D(uint16_t centerX, uint16_t centerY, uint16_t radius) {
    if (!_display) return;
    
    uint16_t width = _display->getWidth();
    uint16_t height = _display->getHeight();
    
    _display->setColor(_config.gridColor >> 8, _config.gridColor & 0xFF, 0);
    
    for (int alt = 0; alt <= 90; alt += 30) {
        Point2D lastPoint;
        bool firstPoint = true;
        
        for (int az = 0; az <= 360; az += 30) {
            Point2D currentPoint = SkyHemisphere::azAltToScreenWithIMU(
                az, alt, _camera.roll, _camera.pitch, _camera.yaw,
                width, height, radius, centerX, centerY);
            
            if (currentPoint.x >= 0 && currentPoint.x < width && 
                currentPoint.y >= 0 && currentPoint.y < height) {
                if (!firstPoint) {
                    _display->drawLine(lastPoint.x, lastPoint.y, currentPoint.x, currentPoint.y);
                }
                lastPoint = currentPoint;
                firstPoint = false;
            }
        }
    }
    
    for (int az = 0; az < 360; az += 60) {
        Point2D lastPoint;
        bool firstPoint = true;
        
        for (int alt = 0; alt <= 90; alt += 30) {
            Point2D currentPoint = SkyHemisphere::azAltToScreenWithIMU(
                az, alt, _camera.roll, _camera.pitch, _camera.yaw,
                width, height, radius, centerX, centerY);
            
            if (currentPoint.x >= 0 && currentPoint.x < width && 
                currentPoint.y >= 0 && currentPoint.y < height) {
                if (!firstPoint) {
                    _display->drawLine(lastPoint.x, lastPoint.y, currentPoint.x, currentPoint.y);
                }
                lastPoint = currentPoint;
                firstPoint = false;
            }
        }
    }
}

void View3DRenderer::drawHorizon3D(uint16_t centerX, uint16_t centerY, uint16_t radius) {
    if (!_display) return;
    
    uint16_t width = _display->getWidth();
    uint16_t height = _display->getHeight();
    
    // 正确解析 16 位 RGB565 颜色值
    uint8_t r = ((_config.horizonColor >> 11) & 0x1F) * 8;
    uint8_t g = ((_config.horizonColor >> 5) & 0x3F) * 4;
    uint8_t b = (_config.horizonColor & 0x1F) * 8;
    _display->setColor(r, g, b);
    
    Point2D lastPoint;
    bool firstPoint = true;
    
    for (int az = 0; az <= 360; az += 5) { // 减小步进提升圆滑度
        Point2D currentPoint = SkyHemisphere::azAltToScreenWithIMU(
            az, 0, _camera.roll, _camera.pitch, _camera.yaw,
            width, height, radius, centerX, centerY);
        
        if (currentPoint.x >= 0 && currentPoint.x < width && 
            currentPoint.y >= 0 && currentPoint.y < height) {
            if (!firstPoint) {
                // 原有的一根线改为画粗线（三根相邻的线）
                _display->drawLine(lastPoint.x, lastPoint.y, currentPoint.x, currentPoint.y);
                _display->drawLine(lastPoint.x, lastPoint.y - 1, currentPoint.x, currentPoint.y - 1);
                _display->drawLine(lastPoint.x, lastPoint.y + 1, currentPoint.x, currentPoint.y + 1);
            }
            lastPoint = currentPoint;
            firstPoint = false;
        }
    }
}

void View3DRenderer::drawGroundPlane3D(uint16_t centerX, uint16_t centerY, uint16_t radius) {
    if (!_display) return;
    
    uint16_t width = _display->getWidth();
    uint16_t height = _display->getHeight();
    
    // 颜色设定推迟到画具体地网时再设置
    
    std::vector<Point2D> groundPoints;
    
    for (int az = 0; az <= 360; az += 10) {
        Point2D groundPoint = SkyHemisphere::azAltToScreenWithIMU(
            az, 0, _camera.roll, _camera.pitch, _camera.yaw,
            width, height, radius, centerX, centerY);
        
        if (groundPoint.x >= 0 && groundPoint.x < width && 
            groundPoint.y >= 0 && groundPoint.y < height) {
            groundPoints.push_back(groundPoint);
        }
    }
    
    if (groundPoints.size() > 2) {
        // STEP 1: 填充整个地平线多边形，实现物理遮挡地下轨迹
        // 修改为绿色 RGB(40, 60, 40)
        _display->setColor(40, 60, 40);
        
        int minY = height;
        int maxY = 0;
        for (const auto& point : groundPoints) {
            if (point.y < minY) minY = point.y;
            if (point.y > maxY) maxY = point.y;
        }
        
        minY = max(0, minY);
        maxY = min((int)height - 1, maxY);
        
        if (minY <= maxY) {
            for (int h = minY; h <= maxY; h++) {
                int leftBound = width;
                int rightBound = 0;
                
                for (size_t j = 0; j < groundPoints.size(); j++) {
                    const Point2D& p1 = groundPoints[j];
                    const Point2D& p2 = groundPoints[(j + 1) % groundPoints.size()];
                    
                    if ((p1.y <= h && p2.y > h) || (p1.y > h && p2.y <= h)) {
                        float t = (float)(h - p1.y) / (p2.y - p1.y);
                        int xIntersect = (int)(p1.x + t * (p2.x - p1.x));
                        
                        if (xIntersect < leftBound) leftBound = xIntersect;
                        if (xIntersect > rightBound) rightBound = xIntersect;
                    }
                }
                
                if (leftBound < rightBound && leftBound >= 0 && rightBound < width) {
                    leftBound = max(0, leftBound);
                    rightBound = min((int)width - 1, rightBound);
                    _display->drawLine(leftBound, h, rightBound, h);
                }
            }
        }
        
        // STEP 2: 绘制随地平面旋转的三维透视网格线
        _display->setColor(103, 160, 130);
        
        // 1. 绘制放射线（从圆心向外的直线）
        // 增加了密度，改为每隔 10 度画一条线
        for (int az = 0; az < 360; az += 10) {
            float azimuthRad = az * DEG_TO_RAD;
            Point3D edgeP3d = { (float)sin(azimuthRad), (float)cos(azimuthRad), 0.0f };
            Point3D rotatedEdge = applyCameraRotation(edgeP3d);
            Point2D edgePoint = project3DPoint(rotatedEdge, centerX, centerY, radius);
            
            // 同样投影圆心
            Point3D centerP3d = {0, 0, 0};
            Point3D rotatedCenter = applyCameraRotation(centerP3d);
            Point2D centerProj = project3DPoint(rotatedCenter, centerX, centerY, radius);
            
            _display->drawLine(centerProj.x, centerProj.y, edgePoint.x, edgePoint.y);
        }
        
        // 2. 绘制同心圆环
        // 半径划分为 6 个等距圈，增加网格密度
        // 半径划分为 4 个等距圈 (原 6 个)，降低密度以提速
        int numRings = 4;
        for (int rIndex = 1; rIndex < numRings; rIndex++) {
            float rRatio = (float)rIndex / numRings;
            Point2D lastPoint;
            bool firstPoint = true;
            
            // 步长从 5 度提高到 15 度，减少 2/3 的 drawLine 调用
            for (int az = 0; az <= 360; az += 15) {
                // 真正的地平同心圆网格应该固定在地平面上 (Z=0)
                float azimuthRad = az * DEG_TO_RAD;
                Point3D p3d = {
                    (float)(rRatio * sin(azimuthRad)), // X: East
                    (float)(rRatio * cos(azimuthRad)), // Y: North
                    0.0f                               // Z: Up (Ground plane)
                };
                
                // 应用相机旋转并投影
                Point3D rotated = applyCameraRotation(p3d);
                Point2D ringPoint = project3DPoint(rotated, centerX, centerY, radius);
                
                if (firstPoint) {
                    lastPoint = ringPoint;
                    firstPoint = false;
                } else {
                    _display->drawLine(lastPoint.x, lastPoint.y, ringPoint.x, ringPoint.y);
                    lastPoint = ringPoint;
                }
            }
        }
    }
}

void View3DRenderer::drawCardinalPoints3D(uint16_t centerX, uint16_t centerY, uint16_t radius) {
    if (!_display) return;
    
    uint16_t width = _display->getWidth();
    uint16_t height = _display->getHeight();
    
    _display->setColor(0xFF, 0xFF, 0xFF);
    
    _display->setColor(0xFF, 0xFF, 0xFF);
    
    // 性能优化：直接使用矩阵旋转而非调用 SkyHemisphere 静态函数
    auto drawLabel = [&](float az, const char* label) {
        float azRad = az * DEG_TO_RAD;
        Point3D p3d = {(float)sin(azRad), (float)cos(azRad), 0.0f};
        Point3D rotated = applyCameraRotation(p3d);
        Point2D sp = project3DPoint(rotated, centerX, centerY, radius);
        if (sp.x >= 0 && sp.x < width && sp.y >= 0 && sp.y < height) {
            _display->drawText(sp.x - 4, sp.y - 12, label, 1);
        }
    };

    drawLabel(0, "N");
    drawLabel(90, "E");
    drawLabel(180, "S");
    drawLabel(270, "W");
    
    // 注释掉天顶绘制，避免显示额外的白点
    /*
    Point2D zenithPoint = SkyHemisphere::azAltToScreenWithIMU(
        0, 90, _camera.roll, _camera.pitch, _camera.yaw,
        width, height, radius, centerX, centerY);
    if (zenithPoint.x >= 0 && zenithPoint.x < width && zenithPoint.y >= 0 && zenithPoint.y < height) {
        _display->drawCircle(zenithPoint.x, zenithPoint.y, 3, true);
    }
    */
}

void View3DRenderer::drawWorldReferencePoint3D(uint16_t centerX, uint16_t centerY, uint16_t radius) {
    if (!_display) return;
    
    uint16_t width = _display->getWidth();
    uint16_t height = _display->getHeight();
    
    // 绘制世界固定参考点：正北方向（方位角0度，高度角0度）
    Point3D p3d = {0.0f, 1.0f, 0.0f}; // 正北方向向量
    Point3D rotated = applyCameraRotation(p3d);
    Point2D screenPoint = project3DPoint(rotated, centerX, centerY, radius);
    
    // 确保点在屏幕范围内
    if (screenPoint.x >= 0 && screenPoint.x < width && screenPoint.y >= 0 && screenPoint.y < height) {
        // 绘制红色十字作为参考点
        _display->setColor(255, 0, 0); // 红色
        int crossSize = 5;
        
        // 绘制十字线
        _display->drawLine(screenPoint.x - crossSize, screenPoint.y, screenPoint.x + crossSize, screenPoint.y);
        _display->drawLine(screenPoint.x, screenPoint.y - crossSize, screenPoint.x, screenPoint.y + crossSize);
    }
}

void View3DRenderer::drawSunPath3D(uint16_t centerX, uint16_t centerY, uint16_t radius, bool isForeground) {
    if (!_display || !_celestialCore || !_positionManager) return;
    
    uint16_t width = _display->getWidth();
    uint16_t height = _display->getHeight();
    
    // 绘制轨迹 (极致性能优化：直接使用预投影缓存)
    _display->setColor(255, 165, 0); // 橙色
    if (_sunPathPoints2D.size() > 1) {
        for (size_t i = 1; i < _sunPathPoints2D.size(); i++) {
            const auto& p1 = _sunPathPoints2D[i-1];
            const auto& p2 = _sunPathPoints2D[i];
            
            // 基于 Z 轴深度的前/后景判定
            bool p1In = (isForeground ? (p1.z >= 0) : (p1.z < 0));
            bool p2In = (isForeground ? (p2.z >= 0) : (p2.z < 0));
            
            if (p1In && p2In) {
                if (p1.x >= 0 && p1.x < width && p1.y >= 0 && p1.y < height &&
                    p2.x >= 0 && p2.x < width && p2.y >= 0 && p2.y < height) {
                    _display->drawLine(p1.x, p1.y, p2.x, p2.y);
                }
            }
            
            // 每小时节点绘制小圆点
            if ((i + _sunTrajectoryCache.cachedBaseHour) % 2 == 0) {
                if (isForeground ? (p2.z >= 0) : (p2.z < 0)) {
                    if (p2.x >= 0 && p2.x < width && p2.y >= 0 && p2.y < height) {
                        _display->drawCircle(p2.x, p2.y, 2, true);
                    }
                }
            }
        }
    }
}

void View3DRenderer::drawMoonPath3D(uint16_t centerX, uint16_t centerY, uint16_t radius, bool isForeground) {
    if (!_display || !_celestialCore || !_positionManager) return;
    
    uint16_t width = _display->getWidth();
    uint16_t height = _display->getHeight();
    
    // 绘制轨迹 (极致性能优化：直接使用预投影缓存)
    _display->setColor(150, 150, 200); // 浅蓝色
    if (_moonPathPoints2D.size() > 1) {
        for (size_t i = 1; i < _moonPathPoints2D.size(); i++) {
            const auto& p1 = _moonPathPoints2D[i-1];
            const auto& p2 = _moonPathPoints2D[i];
            
            // 基于 Z 轴深度的前/后景判定
            bool p1In = (isForeground ? (p1.z >= 0) : (p1.z < 0));
            bool p2In = (isForeground ? (p2.z >= 0) : (p2.z < 0));
            
            if (p1In && p2In) {
                if (p1.x >= 0 && p1.x < width && p1.y >= 0 && p1.y < height &&
                    p2.x >= 0 && p2.x < width && p2.y >= 0 && p2.y < height) {
                    _display->drawLine(p1.x, p1.y, p2.x, p2.y);
                }
            }
            
            // 每小时节点绘制小圆点
            if ((i + _moonTrajectoryCache.cachedBaseHour) % 2 == 0) {
                if (isForeground ? (p2.z >= 0) : (p2.z < 0)) {
                    if (p2.x >= 0 && p2.x < width && p2.y >= 0 && p2.y < height) {
                        _display->drawCircle(p2.x, p2.y, 2, true);
                    }
                }
            }
        }
    }
}

void View3DRenderer::updateGalaxyProjectedPoints(uint16_t centerX, uint16_t centerY, uint16_t radius) {
    if (!_display || !_celestialCore || !_positionManager) return;

    uint32_t currentTimestamp = _positionManager->getTimestamp();
    PositionData position = _positionManager->getPosition();
    double lat = position.latitude;
    double lon = position.longitude;

    // 1. 静态星表初始化
    if (_galaxyStars.empty()) {
        const int numLonPoints = 24; 
        const int particlesPerPoint = 10;
        for (int i = 0; i < numLonPoints; i++) {
            double baseLon = (double)i / numLonPoints * 360.0;
            for (int p = 0; p < particlesPerPoint; p++) {
                double seed_source = (double)i * 2.368 + (double)p * 7.421;
                double seedLat = sin(seed_source * 1.57);
                double seedLon = cos(seed_source * 2.63);
                double galacticLon = baseLon + seedLon * 4.0;
                if (galacticLon < 0) galacticLon += 360.0;
                if (galacticLon >= 360.0) galacticLon -= 360.0;
                double normDist = fabs(galacticLon);
                if (normDist > 180.0) normDist = 360.0 - normDist;
                normDist /= 180.0;
                double latSpread = 1.0 + (5.0 * (1.0 - normDist * normDist));
                double galacticLat = seedLat * latSpread;
                CelestialEquatorial eq = _celestialCore->getGalacticEquatorial(galacticLon, galacticLat);
                CelestialStar star = {(float)eq.ra, (float)eq.dec};
                _galaxyStars.push_back(star);
            }
        }
    }

    // 2. 银河24小时投影缓存更新
    uint32_t currentUTCHour = currentTimestamp / 3600;
    uint32_t windowStartHour = currentUTCHour - 12; 
    if (!_galaxyCache.isValid) {
        for (int i = 0; i < 24; i++) {
            uint32_t ts = (windowStartHour + i) * 3600;
            double lst = _celestialCore->calculateLST(ts, lon);
            _galaxyCache.hourlyPoints[i].clear();
            for (const auto& star : _galaxyStars) {
                CelestialVector gVec = _celestialCore->raDecToHorizontal(star.ra, star.dec, lst, lat);
                _galaxyCache.hourlyPoints[i].push_back({(float)gVec.x, (float)gVec.y, (float)gVec.z});
            }
        }
        _galaxyCache.cachedBaseHour = windowStartHour;
        _galaxyCache.isValid = true;
    } else if (windowStartHour != _galaxyCache.cachedBaseHour) {
        int32_t hDiff = (int32_t)windowStartHour - (int32_t)_galaxyCache.cachedBaseHour;
        if (abs(hDiff) >= 24) { _galaxyCache.isValid = false; }
        else if (hDiff > 0) {
            for (int d = 0; d < hDiff; d++) {
                for (int i = 0; i < 23; i++) _galaxyCache.hourlyPoints[i] = std::move(_galaxyCache.hourlyPoints[i+1]);
                uint32_t nextTs = (_galaxyCache.cachedBaseHour + 24) * 3600;
                double lst = _celestialCore->calculateLST(nextTs, lon);
                _galaxyCache.hourlyPoints[23].clear();
                for (const auto& star : _galaxyStars) {
                    CelestialVector gVec = _celestialCore->raDecToHorizontal(star.ra, star.dec, lst, lat);
                    _galaxyCache.hourlyPoints[23].push_back({(float)gVec.x, (float)gVec.y, (float)gVec.z});
                }
                _galaxyCache.cachedBaseHour++;
            }
        } else {
            for (int d = 0; d < -hDiff; d++) {
                for (int i = 23; i > 0; i--) _galaxyCache.hourlyPoints[i] = std::move(_galaxyCache.hourlyPoints[i-1]);
                _galaxyCache.cachedBaseHour--;
                uint32_t prevTs = _galaxyCache.cachedBaseHour * 3600;
                double lst = _celestialCore->calculateLST(prevTs, lon);
                _galaxyCache.hourlyPoints[0].clear();
                for (const auto& star : _galaxyStars) {
                    CelestialVector gVec = _celestialCore->raDecToHorizontal(star.ra, star.dec, lst, lat);
                    _galaxyCache.hourlyPoints[0].push_back({(float)gVec.x, (float)gVec.y, (float)gVec.z});
                }
            }
        }
    }

    // 3. 计算可见度和基础颜色
    float galaxyVisibility = 1.0f;
    if (_sunAltitude > -12) {
        float sunFactor = (_sunAltitude + 12) / 6.0f;
        galaxyVisibility *= max(0.0f, 1.0f - sunFactor);
    }
    if (_moonAltitude > 0) { 
        float moonFactor = _moonAltitude / 45.0f;
        galaxyVisibility *= max(0.0f, 1.0f - moonFactor);
    }
    galaxyVisibility = max(0.0f, min(1.0f, galaxyVisibility));
    _config.skyColor = interpolateColor(0x0000, 0x18C3, galaxyVisibility);

    uint8_t baseR, baseG, baseB;
    if (galaxyVisibility < 0.1f) { baseR = 32; baseG = 32; baseB = 48; }
    else if (galaxyVisibility < 0.5f) {
        baseR = 32 + (uint8_t)((8 - 32) * galaxyVisibility / 0.5f);
        baseG = 32 + (uint8_t)((66 - 32) * galaxyVisibility / 0.5f);
        baseB = 48 + (uint8_t)((128 - 48) * galaxyVisibility / 0.5f);
    } else {
        baseR = 8 + (uint8_t)((16 - 8) * (galaxyVisibility - 0.5f) / 0.5f);
        baseG = 66 + (uint8_t)((132 - 66) * (galaxyVisibility - 0.5f) / 0.5f);
        baseB = 128 + (uint8_t)((255 - 128) * (galaxyVisibility - 0.5f) / 0.5f);
    }

    // 4. 计算所有投影点
    int32_t hIdx = (int32_t)currentUTCHour - (int32_t)_galaxyCache.cachedBaseHour;
    if (hIdx < 0) hIdx = 0; if (hIdx > 22) hIdx = 22;
    float interpolationFactor = (float)(currentTimestamp % 3600) / 3600.0f;

    _galaxyProjectedPoints.clear();
    const int particlesPerPoint = 10;
    const int totalParticles = _galaxyStars.size();
    const int numLonGroups = totalParticles / particlesPerPoint;

    for (size_t i = 0; i < _galaxyStars.size(); i++) {
        if (i >= _galaxyCache.hourlyPoints[hIdx].size() || i >= _galaxyCache.hourlyPoints[hIdx+1].size()) continue;
        
        const Point3D& p0 = _galaxyCache.hourlyPoints[hIdx][i];
        const Point3D& p1 = _galaxyCache.hourlyPoints[hIdx+1][i];
        
        Point3D interpolated = {
            p0.x + (p1.x - p0.x) * interpolationFactor,
            p0.y + (p1.y - p0.y) * interpolationFactor,
            p0.z + (p1.z - p0.z) * interpolationFactor
        };
        
        Point3D rotated = applyCameraRotation(interpolated);
        Point2D p2d = project3DPoint(rotated, centerX, centerY, radius);
        
        // 核球计算
        int lonIdx = i / particlesPerPoint; 
        float dist = (float)lonIdx;
        if (dist > (float)numLonGroups / 2.0f) dist = (float)numLonGroups - dist;
        float maxSize = 6.0f;
        float minSize = 0.5f;
        float halfN = (float)numLonGroups / 2.0f;
        float t = dist / halfN;
        float size = minSize + (maxSize - minSize) * (1.0f - t * t);
        bool isCore = (dist < numLonGroups / 8.0f);

        GalaxyProjectedPoint gp;
        gp.x = (int16_t)p2d.x;
        gp.y = (int16_t)p2d.y;
        gp.z = rotated.z;
        gp.size = (uint8_t)(isCore ? size : (size / 2.0f + 0.5f));
        gp.colorR = baseR; gp.colorG = baseG; gp.colorB = baseB;
        gp.isCore = isCore;
        
        _galaxyProjectedPoints.push_back(gp);
    }

    uint32_t currentDay = currentTimestamp / 86400;
    if (_lastGalaxyPathDay != currentDay) {
        _lastGalaxyPathDay = currentDay;
        _celestialCore->calculateGalaxyCenter(currentTimestamp, lat, lon);
    }
}

void View3DRenderer::drawGalaxyPath3D(uint16_t centerX, uint16_t centerY, uint16_t radius, bool isForeground) {
    if (!_display || _galaxyProjectedPoints.empty()) return;
    
    uint16_t width = _display->getWidth();
    uint16_t height = _display->getHeight();
    
    // 设置基础颜色
    const auto& firstP = _galaxyProjectedPoints[0];
    _display->setColor(firstP.colorR, firstP.colorG, firstP.colorB);
    
    for (const auto& gp : _galaxyProjectedPoints) {
        bool isPointIn = (isForeground ? (gp.z >= 0) : (gp.z < 0));
        if (!isPointIn) continue;
        
        if (gp.x >= 0 && gp.x < width && gp.y >= 0 && gp.y < height) {
            if (gp.size <= 1 || (!gp.isCore && gp.size <= 2)) {
                // 性能优化：极小或非核心次小粒子直接画点，减少绘图驱动负担
                _display->drawPixel(gp.x, gp.y);
            } else if (gp.size == 2) {
                // 核心区域小粒子画 2x2 块，保持紧凑感
                _display->drawRect(gp.x - 1, gp.y - 1, 2, 2, true);
            } else {
                // 核心或较大粒子保留圆型质感
                _display->drawCircle(gp.x, gp.y, gp.size, true);
            }
        }
    }
}

void View3DRenderer::drawCelestialBody3D(uint16_t centerX, uint16_t centerY, uint16_t radius,
                                          float azimuth, float altitude, uint16_t color, uint8_t size) {
    if (!_display) return;
    
    uint16_t width = _display->getWidth();
    uint16_t height = _display->getHeight();
    
    // 将方位角和高度角转换为3D向量
    // 坐标系: X=正东, Y=正北, Z=正上（与 CelestialCore::azAltToVector 保持一致）
    double cosAlt = cos(altitude * DEG_TO_RAD);
    double azimuthRad = azimuth * DEG_TO_RAD;  // 直接转换，不取反
    Point3D bodyVector = {
        (float)(cosAlt * sin(azimuthRad)),       // Az=90°(E) → x>0 → 屏幕右侧 ✓
        (float)(cosAlt * cos(azimuthRad)),        // Y 轴指向北
        (float)sin(altitude * DEG_TO_RAD)
    };
    
    // 应用IMU旋转
    Point3D rotatedPoint = applyCameraRotation(bodyVector);
    
    // 采用新策略：不再通过判断背后剪裁（因为透明天球不剪背面）
    // 此处实体会被 Z=0 的黑色地平盘按 Z序 遮挡。
    
    // 投影到屏幕
    Point2D bodyPoint = project3DPoint(rotatedPoint, centerX, centerY, radius);
    
    // 打印天体的屏幕坐标（已注释掉以减少调试信息）
    /*
    if (color == 0xFFE0) {
        Serial.printf("[DEBUG] drawCelestialBody3D - SUN - Az=%.2f, Alt=%.2f, Screen=(%d, %d), Camera=(Roll=%.2f, Pitch=%.2f, Yaw=%.2f)\n",
                    azimuth, altitude, bodyPoint.x, bodyPoint.y, _camera.roll, _camera.pitch, _camera.yaw);
    } else if (color == 0xC618) {
        Serial.printf("[DEBUG] drawCelestialBody3D - MOON - Az=%.2f, Alt=%.2f, Screen=(%d, %d), Camera=(Roll=%.2f, Pitch=%.2f, Yaw=%.2f)\n",
                    azimuth, altitude, bodyPoint.x, bodyPoint.y, _camera.roll, _camera.pitch, _camera.yaw);
    }
    */
    
    if (bodyPoint.x >= 0 && bodyPoint.x < width && 
        bodyPoint.y >= 0 && bodyPoint.y < height) {
        // 检查是否是月亮（通过颜色判断，白色或灰色）
        if ((color == 0xFFFF) || (color == 0xC8C8) || (color == 0x8410)) {
            // 绘制月相
            drawMoonPhase3D(bodyPoint.x, bodyPoint.y, size);
        } else {
            // 绘制其他天体
            _display->setColor(color >> 8, color & 0xFF, 0);
            _display->drawCircle(bodyPoint.x, bodyPoint.y, size, true);
        }
    }
}

void View3DRenderer::drawMoonPhase3D(int16_t x, int16_t y, uint8_t size) {
    if (!_display || !_sunCalculator || !_moonCalculator || !_positionManager) return;
    
    // 月相缓存机制，避免频繁计算
    static struct {
        uint32_t lastTimestamp;
        double k;
        Point3D sunVector;
        Point3D moonVector;
    } moonPhaseCache = {0, 0, {0,0,0}, {0,0,0}};
    
    // 获取当前时间戳（精确到分钟）
    uint32_t currentTimestamp = _positionManager->getTimestamp();
    uint32_t minuteTimestamp = currentTimestamp - (currentTimestamp % 60); // 精确到分钟
    
    // 只有当时间变化时才重新计算
    if (minuteTimestamp != moonPhaseCache.lastTimestamp) {
        // 获取当前位置
        PositionData position = _positionManager->getPosition();
        double latitude = position.latitude;
        double longitude = position.longitude;
        
        // 计算太阳位置
        SunPositionData sunPos = _sunCalculator->calculatePosition(currentTimestamp, latitude, longitude);
        
        // 计算月亮位置
        MoonPositionData moonPos = _moonCalculator->calculatePosition(currentTimestamp, latitude, longitude);
        
        // 计算日月夹角（phase angle）
        double raDiff = sunPos.ra - moonPos.ra;
        double cosPhaseAngle = sin(sunPos.dec * DEG_TO_RAD) * sin(moonPos.dec * DEG_TO_RAD) + 
                              cos(sunPos.dec * DEG_TO_RAD) * cos(moonPos.dec * DEG_TO_RAD) * cos(raDiff * DEG_TO_RAD);
        
        // 确保cosPhaseAngle在[-1, 1]范围内
        if (cosPhaseAngle > 1.0) cosPhaseAngle = 1.0;
        if (cosPhaseAngle < -1.0) cosPhaseAngle = -1.0;
        
        // 计算终止线位置
        moonPhaseCache.k = cosPhaseAngle;
        
        // 计算太阳和月亮的3D向量
        moonPhaseCache.sunVector = SkyHemisphere::azAltToVector(sunPos.azimuth, sunPos.altitude);
        moonPhaseCache.moonVector = SkyHemisphere::azAltToVector(moonPos.azimuth, moonPos.altitude);
        
        // 更新缓存时间戳
        moonPhaseCache.lastTimestamp = minuteTimestamp;
    }
    
    // 使用缓存的月相数据
    
    // 这里不再应用 IMU（相机）旋转矩阵。因为月相是指地球观察者看到的日照阴影部分
    // 它只受日月真实坐标系统影响。假如我们在屏幕中心看到它，仅仅偏航角改变（相机转了而已），月亮照亮的弦角是不会自旋的。
    // 计算太阳到月亮的单位向量
    Point3D sunToMoon = { moonPhaseCache.sunVector.x - moonPhaseCache.moonVector.x, moonPhaseCache.sunVector.y - moonPhaseCache.moonVector.y, moonPhaseCache.sunVector.z - moonPhaseCache.moonVector.z };
    
    // 归一化太阳到月亮的向量
    double sunToMoonLength = sqrt(sunToMoon.x * sunToMoon.x + sunToMoon.y * sunToMoon.y + sunToMoon.z * sunToMoon.z);
    if (sunToMoonLength > 0) {
        sunToMoon.x /= sunToMoonLength;
        sunToMoon.y /= sunToMoonLength;
        sunToMoon.z /= sunToMoonLength;
    }
    
    // 计算月球表面的法向量（指向观察者）
    Point3D moonNormal = { -moonPhaseCache.moonVector.x, -moonPhaseCache.moonVector.y, -moonPhaseCache.moonVector.z };
    
    // 计算月球表面的切向量
    Point3D tangent1, tangent2;
    
    // 如果月球法线接近Z轴，使用X轴作为参考
    if (fabs(moonNormal.z) > 0.8) {
        tangent1 = { 1.0, 0.0, 0.0 };
    } else {
        tangent1 = { 0.0, 0.0, 1.0 };
    }
    
    // 计算垂直于法线的切向量（叉积）
    tangent1 = { 
        tangent1.y * moonNormal.z - tangent1.z * moonNormal.y,
        tangent1.z * moonNormal.x - tangent1.x * moonNormal.z,
        tangent1.x * moonNormal.y - tangent1.y * moonNormal.x
    };
    
    // 归一化切向量
    double tangent1Length = sqrt(tangent1.x * tangent1.x + tangent1.y * tangent1.y + tangent1.z * tangent1.z);
    if (tangent1Length > 0) {
        tangent1.x /= tangent1Length;
        tangent1.y /= tangent1Length;
        tangent1.z /= tangent1Length;
    }
    
    // 计算第二个切向量（垂直于法线和第一个切向量）
    tangent2 = { 
        moonNormal.y * tangent1.z - moonNormal.z * tangent1.y,
        moonNormal.z * tangent1.x - moonNormal.x * tangent1.z,
        moonNormal.x * tangent1.y - moonNormal.y * tangent1.x
    };
    
    // 归一化第二个切向量
    double tangent2Length = sqrt(tangent2.x * tangent2.x + tangent2.y * tangent2.y + tangent2.z * tangent2.z);
    if (tangent2Length > 0) {
        tangent2.x /= tangent2Length;
        tangent2.y /= tangent2Length;
        tangent2.z /= tangent2Length;
    }
    
    // 计算太阳方向在月亮视平面上的投影
    double sunX = sunToMoon.x * tangent1.x + sunToMoon.y * tangent1.y + sunToMoon.z * tangent1.z;
    double sunY = sunToMoon.x * tangent2.x + sunToMoon.y * tangent2.y + sunToMoon.z * tangent2.z;
    
    // 计算太阳方向的角度
    double sunAngle = atan2(sunY, sunX);
    
    // 绘制月面
    _display->setColor(255, 255, 255); // 白色
    _display->drawCircle(x, y, size, true);
    
    // 绘制阴影部分
    double k = moonPhaseCache.k;
    if (k < 1.0) {
        _display->setColor(0, 0, 0); // 黑色
        
        // 计算阴影圆心的偏移量：沿光照方向平移 R * k
        double offset = size * k;
        double shadowOffsetX = cos(sunAngle) * offset;
        double shadowOffsetY = sin(sunAngle) * offset;
        
        // 绘制阴影圆（半径与月亮相同）
        int shadowX = x + shadowOffsetX;
        int shadowY = y + shadowOffsetY;
        _display->drawCircle(shadowX, shadowY, size, true);
    }
}

void View3DRenderer::updateSmoothTransition() {
    if (!_smoothTransition) return;
    
    _camera.pitch += (_targetCamera.pitch - _camera.pitch) * _transitionSpeed;
    _camera.roll += (_targetCamera.roll - _camera.roll) * _transitionSpeed;
    _camera.yaw += (_targetCamera.yaw - _camera.yaw) * _transitionSpeed;
    _camera.distance += (_targetCamera.distance - _camera.distance) * _transitionSpeed;
    _camera.verticalExaggeration += (_targetCamera.verticalExaggeration - _camera.verticalExaggeration) * _transitionSpeed;
}

Point2D View3DRenderer::project3DPoint(const Point3D& point, uint16_t centerX, uint16_t centerY, uint16_t radius) {
    // 注意：调用者已预先完成了应用 IMU 旋转的 3D 变换。
    // 使用正交投影逻辑（Orthogonal Projection）以确保天体轨迹与基于 SkyHemisphere 的地平圈完全同步。
    
    // 我们在这里实现一个等效于 SkyHemisphere::azAltToScreen 的 3D 投影逻辑：
    // x = centerX + x_3d * radius
    // y = centerY - y_3d * radius
    
    float sx = (float)centerX + point.x * (float)radius;
    float sy = (float)centerY - point.y * (float)radius;
    
    return Point2D((int16_t)sx, (int16_t)sy);
}

Point3D View3DRenderer::applyCameraRotation(const Point3D& point) {
    // 使用预计算的正余弦值 (极致性能优化：消除每点 6 次的三角函数运算)
    // 调整旋转顺序，与 SkyHemisphere::applyIMURotation 一致
    // 先绕X轴旋转（Roll）
    float x1 = point.x * _cosR + point.z * _sinR;
    float z1 = -point.x * _sinR + point.z * _cosR;
    float y1 = point.y;
    
    // 再绕Y轴旋转（Pitch）
    float y2 = y1 * _cosP + z1 * _sinP;
    float z2 = -y1 * _sinP + z1 * _cosP;
    float x2 = x1;
    
    // 最后绕Z轴旋转（Yaw）
    float x3 = x2 * _cosY - y2 * _sinY;
    float y3 = x2 * _sinY + y2 * _cosY;
    float z3 = z2;
    
    return Point3D(x3, y3, z3);
}

uint16_t View3DRenderer::interpolateColor(uint16_t color1, uint16_t color2, float t) {
    uint8_t r1 = (color1 >> 11) & 0x1F;
    uint8_t g1 = (color1 >> 5) & 0x3F;
    uint8_t b1 = color1 & 0x1F;
    
    uint8_t r2 = (color2 >> 11) & 0x1F;
    uint8_t g2 = (color2 >> 5) & 0x3F;
    uint8_t b2 = color2 & 0x1F;
    
    uint8_t r = r1 + (r2 - r1) * t;
    uint8_t g = g1 + (g2 - g1) * t;
    uint8_t b = b1 + (b2 - b1) * t;
    
    return (r << 11) | (g << 5) | b;
}

float View3DRenderer::clampAngle(float angle, float minAngle, float maxAngle) {
    while (angle < minAngle) angle += 360;
    while (angle > maxAngle) angle -= 360;
    return constrain(angle, minAngle, maxAngle);
}

// 获取缓存的太阳位置（供潮汐模式使用）
bool View3DRenderer::getCachedSunPosition(uint32_t timestamp, double& azimuth, double& altitude) {
    if (!_positionManager || !_celestialCore) return false;
    
    // 确保缓存有效
    if (!_sunTrajectoryCache.isValid || _sunTrajectoryCache.points3D.size() < 49) return false;
    
    // 计算目标时间对应的块索引 (30分钟为单位)
    uint32_t targetBlock = timestamp / 1800;
    int32_t idx = (int32_t)targetBlock - (int32_t)_sunTrajectoryCache.cachedBaseHour;
    
    if (idx >= 0 && idx < 48) {
        // 亚 30 分钟插值
        float t = (float)(timestamp % 1800) / 1800.0f;
        const Point3D& p0 = _sunTrajectoryCache.points3D[idx];
        const Point3D& p1 = _sunTrajectoryCache.points3D[idx + 1];
        float ix = p0.x + (p1.x - p0.x) * t;
        float iy = p0.y + (p1.y - p0.y) * t;
        float iz = p0.z + (p1.z - p0.z) * t;
        float r = sqrt(ix * ix + iy * iy + iz * iz);
        if (r > 0) {
            azimuth = atan2(ix, iy) * RAD_TO_DEG;  // x=E, y=N → az=atan2(x,y)
            if (azimuth < 0) azimuth += 360.0;
            altitude = asin(iz / r) * RAD_TO_DEG;
            return true;
        }
    }
    return false;
}

// 获取缓存的月亮位置（供潮汐模式使用）
bool View3DRenderer::getCachedMoonPosition(uint32_t timestamp, double& azimuth, double& altitude) {
    if (!_positionManager || !_celestialCore) return false;
    
    if (!_moonTrajectoryCache.isValid || _moonTrajectoryCache.points3D.size() < 49) return false;
    
    uint32_t targetBlock = timestamp / 1800;
    int32_t idx = (int32_t)targetBlock - (int32_t)_moonTrajectoryCache.cachedBaseHour;
    
    if (idx >= 0 && idx < 48) {
        float t = (float)(timestamp % 1800) / 1800.0f;
        const Point3D& p0 = _moonTrajectoryCache.points3D[idx];
        const Point3D& p1 = _moonTrajectoryCache.points3D[idx + 1];
        float ix = p0.x + (p1.x - p0.x) * t;
        float iy = p0.y + (p1.y - p0.y) * t;
        float iz = p0.z + (p1.z - p0.z) * t;
        float r = sqrt(ix * ix + iy * iy + iz * iz);
        if (r > 0) {
            azimuth = atan2(ix, iy) * RAD_TO_DEG;
            if (azimuth < 0) azimuth += 360.0;
            altitude = asin(iz / r) * RAD_TO_DEG;
            return true;
        }
    }
    return false;
}
