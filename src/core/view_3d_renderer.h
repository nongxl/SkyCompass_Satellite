#ifndef VIEW_3D_RENDERER_H
#define VIEW_3D_RENDERER_H

#include <Arduino.h>
#include <vector>
#include "hal/hal_display.h"
#include "sky_hemisphere.h"
#include "sun_calculator.h"
#include "moon_calculator.h"
#include "celestial_core.h"
#include "position_manager.h"
#include "app/time_machine.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef DEG_TO_RAD
#define DEG_TO_RAD (M_PI / 180.0)
#endif

#ifndef RAD_TO_DEG
#define RAD_TO_DEG (180.0 / M_PI)
#endif

struct Camera3D {
    float pitch;
    float roll;
    float yaw;
    float distance;
    float fov;
    float verticalExaggeration;
};

struct RenderConfig {
    bool showGrid;
    bool showHorizon;
    bool showCardinalPoints;
    bool showSunPath;
    bool showMoonPath;
    bool showGalaxyPath;
    bool showGround;
    uint16_t gridColor;
    uint16_t horizonColor;
    uint16_t groundColor;
    uint16_t skyColor;
};

class View3DRenderer {
public:
    View3DRenderer();
    
    struct CelestialStar {
        float ra;
        float dec;
    };
    
    bool begin(HalDisplay* display);
    
    // 只使用3D模式，移除ViewMode相关方法
    int getViewMode() const { return 1; } // 始终返回3D模式
    
    void setCamera(const Camera3D& camera);
    Camera3D getCamera() const;
    
    void updateCameraFromIMU(float pitch, float roll, float yaw);
    
    void setRenderConfig(const RenderConfig& config);
    RenderConfig getRenderConfig() const;
    
    void render(uint16_t centerX, uint16_t centerY, uint16_t radius);
    
    void setSunPosition(float azimuth, float altitude);
    void setMoonPosition(float azimuth, float altitude);
    
    void setVerticalExaggeration(float value);
    float getVerticalExaggeration() const;
    
    void zoom(float factor);
    void pan(int dx, int dy);
    void reset();
    
    void setReferenceOrientation(float pitch, float roll);
    void clearReferenceOrientation();
    
    void enableSmoothTransition(bool enable);
    void setTransitionSpeed(float speed);
    
    // 设置计算器、位置管理器和时间机器
    void setSunCalculator(SunCalculator* calculator);
    void setMoonCalculator(MoonCalculator* calculator);
    void setCelestialCore(CelestialCore* core);
    void setPositionManager(PositionManager* manager);
    void setTimeMachine(TimeMachine* timeMachine);
    
    // 绘制月相（供UIManager在潮汐页面使用）
    void drawMoonPhase3D(int16_t x, int16_t y, uint8_t size);
    
    // 获取缓存的太阳位置（供潮汐模式使用）
    bool getCachedSunPosition(uint32_t timestamp, double& azimuth, double& altitude);
    
    // 获取缓存的月亮位置（供潮汐模式使用）
    bool getCachedMoonPosition(uint32_t timestamp, double& azimuth, double& altitude);
    
    // 移除toggleViewMode方法，只使用3D模式

private:
    HalDisplay* _display;
    Camera3D _camera;
    Camera3D _targetCamera;
    RenderConfig _config;
    
    float _sunAzimuth;
    float _sunAltitude;
    float _moonAzimuth;
    float _moonAltitude;
    
    SunCalculator* _sunCalculator;
    MoonCalculator* _moonCalculator;
    CelestialCore* _celestialCore;
    PositionManager* _positionManager;
    TimeMachine* _timeMachine;
    
    std::vector<Point3D> _sunPathPoints; // 太阳轨迹点
    std::vector<Point3D> _moonPathPoints; // 月亮轨迹点
    
    struct ProjectedPoint {
        int16_t x, y;
        bool visible; // 用于标记是否渲染（根据前/后景逻辑）
        float z;      // 原始 3D 变换后的 Z 深度，用于层级判定
    };
    
    std::vector<ProjectedPoint> _sunPathPoints2D;  // 太阳轨迹 2D 投影缓存
    std::vector<ProjectedPoint> _moonPathPoints2D; // 月亮轨迹 2D 投影缓存
    
    std::vector<CelestialStar> _galaxyStars;       // 银河静态星表 (RA/Dec)
    std::vector<Point3D> _galaxyPathPoints; // 银河当前帧投影缓存（中间变量，将被移除或复用）

    struct GalaxyProjectedPoint {
        int16_t x, y;
        float z;
        uint8_t size;
        uint8_t colorR, colorG, colorB;
        bool isCore;
    };
    std::vector<GalaxyProjectedPoint> _galaxyProjectedPoints; // 银河当前帧最终投影缓存
    
    // 缓存机制：记录上次计算的日期，避免重复计算轨迹
    uint32_t _lastSunPathDay;  // 上次计算太阳轨迹的日期（timestamp / 86400）
    uint32_t _lastMoonPathDay; // 上次计算月亮轨迹的日期
    uint32_t _lastGalaxyPathDay; // 上次计算银河轨迹的日期（实际上现在是分钟）
    
    // 49点滑动窗口缓存（以当前UTC整点为中心，±12小时，30分钟步长）
    struct TrajectoryCache {
        std::vector<Point3D> points3D;      // 3D位置（49点，[0]=中心-12h, [24]=中心, [48]=中心+12h）
        uint32_t baseTimestamp;             // 缓存基准时间
        uint32_t cachedBaseHour;            // 窗口起始 UTC 小时（= currentHour - 12）
        bool isValid;                      // 缓存是否有效
    };
    
    TrajectoryCache _sunTrajectoryCache;
    TrajectoryCache _moonTrajectoryCache;
    
    // 银河24小时缓存
    struct GalaxyCache {
        std::vector<Point3D> hourlyPoints[24]; // 每小时的银河粒子位置
        uint32_t cachedBaseHour;               // 缓存对应的 UTC 起始小时（startOfDay/3600）
        bool isValid;                          // 缓存是否有效
    };
    
    GalaxyCache _galaxyCache;
    
    int _sunPathUpdateIndex;   // 太阳轨迹分步更新进度 (-1 表示已完成)
    int _moonPathUpdateIndex;  // 月亮轨迹分步更新进度 (-1 表示已完成)
    
    bool _hasReferenceOrientation;
    float _refPitch;
    float _refRoll;
    
    bool _smoothTransition;
    float _transitionSpeed;
    
    int _panOffsetX;
    int _panOffsetY;
    
    // 旋转矩阵预计算缓存 (每帧更新一次)
    float _cosR, _sinR, _cosP, _sinP, _cosY, _sinY;
    
    void render3D(uint16_t centerX, uint16_t centerY, uint16_t radius);
    
    void drawSkyGrid3D(uint16_t centerX, uint16_t centerY, uint16_t radius);
    void drawHorizon3D(uint16_t centerX, uint16_t centerY, uint16_t radius);
    void drawGroundPlane3D(uint16_t centerX, uint16_t centerY, uint16_t radius);
    void drawCardinalPoints3D(uint16_t centerX, uint16_t centerY, uint16_t radius);
    void drawSunPath3D(uint16_t centerX, uint16_t centerY, uint16_t radius, bool isForeground = true);
    void drawMoonPath3D(uint16_t centerX, uint16_t centerY, uint16_t radius, bool isForeground = true);
    void updateGalaxyProjectedPoints(uint16_t centerX, uint16_t centerY, uint16_t radius);
    void drawGalaxyPath3D(uint16_t centerX, uint16_t centerY, uint16_t radius, bool isForeground = true);
    void drawCelestialBody3D(uint16_t centerX, uint16_t centerY, uint16_t radius, float azimuth, float altitude, uint16_t color, uint8_t size);
    void drawWorldReferencePoint3D(uint16_t centerX, uint16_t centerY, uint16_t radius);
    
    void updateSmoothTransition();
    
    Point2D project3DPoint(const Point3D& point, uint16_t centerX, uint16_t centerY, uint16_t radius);
    Point3D applyCameraRotation(const Point3D& point);
    
    uint16_t interpolateColor(uint16_t color1, uint16_t color2, float t);
    float clampAngle(float angle, float minAngle, float maxAngle);
};

#endif
