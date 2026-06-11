#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include <Arduino.h>
#include <vector>
#include "hal/hal_display.h"
#include "hal/hal_keyboard.h"
#include "sun_calculator.h"
#include "moon_calculator.h"
#include "celestial_core.h"
#include "attitude_estimator.h"
#include "position_manager.h"
#include "view_3d_renderer.h"

// 前向声明
class TimeMachine;

// 避免与sky_hemisphere.h中的结构体定义冲突
#ifndef POINT_STRUCTS_DEFINED
#define POINT_STRUCTS_DEFINED

/**
 * @brief 3D点结构体
 */
struct Point3D {
    float x, y, z;
    
    /**
     * @brief 构造函数
     */
    Point3D() : x(0), y(0), z(0) {}
    
    /**
     * @brief 构造函数
     * @param x X坐标
     * @param y Y坐标
     * @param z Z坐标
     */
    Point3D(float x, float y, float z) : x(x), y(y), z(z) {}
};

/**
 * @brief 2D点结构体
 */
struct Point2D {
    int16_t x, y;
    
    /**
     * @brief 构造函数
     */
    Point2D() : x(0), y(0) {}
    
    /**
     * @brief 构造函数
     * @param x X坐标
     * @param y Y坐标
     */
    Point2D(int16_t x, int16_t y) : x(x), y(y) {}
};

#endif // POINT_STRUCTS_DEFINED

/**
 * @brief 3D旋转函数
 * @param point 输入3D点
 * @param roll 横滚角（度）
 * @param pitch 俯仰角（度）
 * @param yaw 偏航角（度）
 * @return 旋转后的3D点
 */
Point3D rotate3D(Point3D point, float roll, float pitch, float yaw);

/**
 * @brief 3D透视投影函数
 * @param point 输入3D点
 * @param fov 视场角（度）
 * @param screenWidth 屏幕宽度
 * @param screenHeight 屏幕高度
 * @param centerX 投影中心X坐标
 * @param centerY 投影中心Y坐标
 * @return 投影后的2D点
 */
Point2D project3D(Point3D point, float fov, uint16_t screenWidth, uint16_t screenHeight, int16_t centerX, int16_t centerY);

/**
 * @brief UI页面枚举
 */
enum UIPage {
    PAGE_MAIN,        // 主页面（指南针和太阳位置）
    PAGE_MENU,        // 菜单页面（显示详细信息）
    PAGE_SUN_DATA,    // 太阳数据页面（详细参数）
    PAGE_TIME_MACHINE, // 时间机器页面（日期/时间设置）
    PAGE_SETTINGS,      // 设置页面
    PAGE_TIDE         // 潮汐曲线页面
};

/**
 * @brief UI管理器类
 */
class UIManager {
private:
    HalDisplay* _display;          // 显示模块指针
    HalKeyboard* _keyboard;        // 键盘模块指针
    SunCalculator* _sunCalculator; // 太阳位置计算器指针
    MoonCalculator* _moonCalculator; // 月亮位置计算器指针
    CelestialCore* _celestialCore; // 天体核心指针
    AttitudeEstimator* _attitudeEstimator; // 姿态估计器指针
    PositionManager* _positionManager; // 位置和时间管理器指针
    HalGnss* _gnss;                // GNSS模块指针
    TimeMachine* _timeMachine;     // 时间机器模块指针
    View3DRenderer* _view3DRenderer; // 3D视图渲染器指针
    UIPage _currentPage;           // 当前页面
    bool _timeMachineActive;        // 时间机器是否激活
    bool _use3DView;               // 是否使用3D视图模式
    
    // 位置设置相关变量
    double _editLongitude;         // 编辑中的经度
    double _editLatitude;          // 编辑中的纬度
    double _editAltitude;          // 编辑中的海拔
    uint8_t _selectedField;        // 当前选中的字段（0:经度, 1:纬度, 2:海拔）
    
    // 设置菜单相关变量
    uint8_t _settingsSelectedField; // 设置菜单中当前选中的字段
    char _settingsInputBuffer[3][32]; // 设置菜单输入缓冲区
    
    // 潮汐缓存结构体
    struct TideCache {
        float tideValues[49];        // 49个点（±12小时，30分钟步长）
        float tideMin;
        float tideMax;
        uint32_t lastTimestamp;      // 上次更新的 UTC 小时
        PositionData lastPosition;
        bool valid;
    } _tideCache;
    
    uint32_t _lastUpdateTime; // 上次更新时间

public:
    /**
     * @brief 构造函数
     * @param display 显示模块指针
     * @param keyboard 键盘模块指针
     * @param sunCalculator 太阳位置计算器指针
     * @param moonCalculator 月亮位置计算器指针
     * @param celestialCore 天体核心指针
     * @param attitudeEstimator 姿态估计器指针
     * @param positionManager 位置和时间管理器指针
     */
    UIManager(HalDisplay* display, HalKeyboard* keyboard, SunCalculator* sunCalculator, MoonCalculator* moonCalculator, CelestialCore* celestialCore, AttitudeEstimator* attitudeEstimator, PositionManager* positionManager);

    /**
     * @brief 初始化UI管理器
     * @return 初始化是否成功
     */
    bool begin();

    /**
     * @brief 更新UI显示
     */
    void update();

    /**
     * @brief 处理用户输入
     */
    void handleInput();

    /**
     * @brief 设置当前页面
     * @param page 页面枚举
     */
    void setPage(UIPage page);

    /**
     * @brief 激活/禁用时间机器功能
     * @param active 是否激活
     */
    void setTimeMachineActive(bool active);
    
    /**
     * @brief 设置时间机器模块指针
     * @param timeMachine 时间机器模块指针
     */
    void setTimeMachine(TimeMachine* timeMachine);
    
    /**
     * @brief 设置GNSS模块指针
     * @param gnss GNSS模块指针
     */
    void setGnss(HalGnss* gnss);
    
    /**
     * @brief 立即重绘主页面
     */
    void redrawMainPage();
    
    /**
     * @brief 立即重绘潮汐页面
     */
    void redrawTidePage();
    
    /**
     * @brief 获取当前页面
     * @return 当前页面枚举值
     */
    UIPage getCurrentPage();
    
    /**
     * @brief 设置3D视图渲染器
     * @param renderer 3D视图渲染器指针
     */
    void setView3DRenderer(View3DRenderer* renderer);
    
    /**
     * @brief 切换2D/3D视图模式
     */
    void toggleViewMode();
    
    /**
     * @brief 设置视图模式
     * @param use3D 是否使用3D视图
     */
    void setUse3DView(bool use3D);
    
    /**
     * @brief 获取当前视图模式
     * @return 是否使用3D视图
     */
    bool isUsing3DView() const;

    /**
     * @brief 初始化位置设置（从当前位置加载）
     */
    void initPositionSettings();

    /**
     * @brief 获取编辑中的经度
     * @return 经度值
     */
    double getEditLongitude() const { return _editLongitude; }

    /**
     * @brief 获取编辑中的纬度
     * @return 纬度值
     */
    double getEditLatitude() const { return _editLatitude; }

    /**
     * @brief 获取编辑中的海拔
     * @return 海拔值
     */
    double getEditAltitude() const { return _editAltitude; }

    /**
     * @brief 获取当前选中的字段
     * @return 字段索引（0:经度, 1:纬度, 2:海拔）
     */
    uint8_t getSelectedField() const { return _selectedField; }

    /**
     * @brief 选择下一个字段
     */
    void selectNextField();

    /**
     * @brief 选择上一个字段
     */
    void selectPreviousField();

    /**
     * @brief 增加当前选中字段的值
     */
    void incrementPositionField();

    /**
     * @brief 减少当前选中字段的值
     */
    void decrementPositionField();

    /**
     * @brief 应用位置设置（保存并生效）
     */
    void applyPositionSettings();

    /**
     * @brief 选择下一个设置字段
     */
    void selectNextSettingsField();

    /**
     * @brief 删除设置输入字符
     */
    void deleteSettingsChar();

    /**
     * @brief 添加设置输入字符
     * @param c 字符
     */
    void addSettingsChar(char c);

    /**
     * @brief 应用设置
     */
    void applySettings();

    /**
     * @brief 计算潮汐值
     */
    void calculateTideValues();

private:
    /**
     * @brief 绘制主页面（指南针和太阳位置）
     */
    void drawMainPage();

    /**
     * @brief 绘制太阳数据页面（详细参数）
     */
    void drawSunDataPage();

    /**
     * @brief 绘制时间机器页面（日期/时间设置）
     */
    void drawTimeMachinePage();

    /**
     * @brief 绘制设置页面
     */
    void drawSettingsPage();

    /**
     * @brief 绘制位置设置页面
     */
    void drawPositionSettingsPage();

    /**
     * @brief 绘制潮汐曲线页面
     */
    void drawTidePage();

    /**
     * @brief 绘制指南针
     * @param x 中心点X坐标
     * @param y 中心点Y坐标
     * @param radius 半径
     * @param heading 航向角（度）
     * @param sunAzimuth 太阳方位角（度）
     */
    void drawCompass(int16_t x, int16_t y, uint16_t radius, float heading, float sunAzimuth);

    /**
     * @brief 绘制仪表盘
     * @param x 中心点X坐标
     * @param y 中心点Y坐标
     * @param radius 半径
     * @param value 当前值
     * @param minValue 最小值
     * @param maxValue 最大值
     * @param label 标签
     */
    void drawGauge(int16_t x, int16_t y, uint16_t radius, float value, float minValue, float maxValue, const char* label);

    /**
     * @brief 绘制状态条
     * @param x X坐标
     * @param y Y坐标
     * @param width 宽度
     * @param height 高度
     * @param text 文本内容
     */
    void drawStatusBar(int16_t x, int16_t y, uint16_t width, uint16_t height, const char* text);
    
    /**
     * @brief 绘制天球图
     * @param x 中心点X坐标
     * @param y 中心点Y坐标
     * @param radius 半径
     * @param heading 航向角（度）
     * @param sunPos 太阳位置数据
     */
    void drawSkyMap(int16_t x, int16_t y, uint16_t radius, float heading, SunPositionData sunPos);
    
    /**
     * @brief 绘制太阳轨迹（2D版本）
     * @param x 中心点X坐标
     * @param y 中心点Y坐标
     * @param radius 半径
     * @param sunPos 太阳位置数据
     */
    void drawSunPath(int16_t x, int16_t y, uint16_t radius, SunPositionData sunPos);
    
    /**
     * @brief 绘制静态天空半球
     * @param x 中心点X坐标
     * @param y 中心点Y坐标
     * @param radius 半径
     */
    void drawStaticSkyHemisphere(int16_t x, int16_t y, uint16_t radius);
    
    /**
     * @brief 绘制太阳轨迹（3D版本）
     * @param screenWidth 屏幕宽度
     * @param screenHeight 屏幕高度
     * @param radius 半径
     * @param roll 横滚角（度）
     * @param pitch 俯仰角（度）
     * @param yaw 偏航角（度）
     * @param sunPos 太阳位置数据
     * @param centerX 中心点X坐标
     * @param centerY 中心点Y坐标
     */
    void drawSunPath3D(uint16_t screenWidth, uint16_t screenHeight, uint16_t radius, float roll, float pitch, float yaw, SunPositionData sunPos, int16_t centerX, int16_t centerY);
    
    /**
     * @brief 绘制月亮轨迹（3D版本）
     * @param screenWidth 屏幕宽度
     * @param screenHeight 屏幕高度
     * @param radius 半径
     * @param roll 横滚角（度）
     * @param pitch 俯仰角（度）
     * @param yaw 偏航角（度）
     * @param sunPos 太阳位置数据
     * @param centerX 中心点X坐标
     * @param centerY 中心点Y坐标
     */
    void drawMoonPath3D(uint16_t screenWidth, uint16_t screenHeight, uint16_t radius, float roll, float pitch, float yaw, SunPositionData sunPos, int16_t centerX, int16_t centerY);
    
    /**
     * @brief 绘制时间轴
     * @param x X坐标
     * @param y Y坐标
     * @param width 宽度
     * @param height 高度
     */
    void drawTimeAxis(int16_t x, int16_t y, uint16_t width, uint16_t height);
    
    /**
     * @brief 绘制状态信息
     * @param x X坐标
     * @param y Y坐标
     * @param width 宽度
     */
    void drawStatusInfo(int16_t x, int16_t y, uint16_t width);
    
    /**
     * @brief 绘制磁偏角信息
     * @param x X坐标
     * @param y Y坐标
     * @param width 宽度
     */
    void drawMagneticDeclination(int16_t x, int16_t y, uint16_t width);
    
    /**
     * @brief 绘制天体信息
     * @param x X坐标
     * @param y Y坐标
     * @param width 宽度
     */
    void drawCelestialInfo(int16_t x, int16_t y, uint16_t width);
    
    /**
     * @brief 绘制底部按钮提示
     * @param x X坐标
     * @param y Y坐标
     * @param width 宽度
     */
    void drawButtonHints(int16_t x, int16_t y, uint16_t width);
    
    /**
     * @brief 绘制当前时间（右下角）
     * @param x X坐标
     * @param y Y坐标
     */
    void drawCurrentTime(int16_t x, int16_t y);
    
    /**
     * @brief 绘制IMU传感器数据标签
     */
    void drawIMUDataLabel();
    
    /**
     * @brief 调整时间
     * @param seconds 调整的秒数（正数增加，负数减少）
     */
    void adjustTime(int seconds);
    
    /**
     * @brief 重置时间到当前系统时间
     */
    void resetTime();
    
    /**
     * @brief 绘制菜单页面
     */
    void drawMenuPage();
};

#endif // UI_MANAGER_H

