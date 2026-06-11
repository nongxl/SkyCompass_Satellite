#include "ui_manager.h"
#include "app/time_machine.h"
#include "core/sky_hemisphere.h"
#include "hal/hal_gnss.h"
#include <math.h>

/**
 * @brief 构造函数
 * @param display 显示模块指针
 * @param keyboard 键盘模块指针
 * @param sunCalculator 太阳位置计算器指针
 * @param attitudeEstimator 姿态估计器指针
 */
UIManager::UIManager(HalDisplay* display, HalKeyboard* keyboard, SunCalculator* sunCalculator, MoonCalculator* moonCalculator, CelestialCore* celestialCore, AttitudeEstimator* attitudeEstimator, PositionManager* positionManager) :
    _display(display),
    _keyboard(keyboard),
    _sunCalculator(sunCalculator),
    _moonCalculator(moonCalculator),
    _celestialCore(celestialCore),
    _attitudeEstimator(attitudeEstimator),
    _positionManager(positionManager),
    _gnss(nullptr),
    _timeMachine(nullptr),
    _view3DRenderer(nullptr),
    _currentPage(PAGE_MAIN),
    _timeMachineActive(false),
    _use3DView(true),
    _lastUpdateTime(0),
    _editLongitude(116.4074),
    _editLatitude(39.9042),
    _editAltitude(0.0),
    _selectedField(0),
    _settingsSelectedField(0) {
    // 初始化设置菜单输入缓冲区
    for (int i = 0; i < 3; i++) {
        memset(_settingsInputBuffer[i], 0, 32);
    }
    // 设置默认值
    sprintf(_settingsInputBuffer[0], "116.4074"); // 经度
    sprintf(_settingsInputBuffer[1], "39.9042");  // 纬度
    sprintf(_settingsInputBuffer[2], "0.0");      // 海拔
}

/**
 * @brief 初始化UI管理器
 * @return 初始化是否成功
 */
bool UIManager::begin() {
    // 清除显示内容
    _display->clear();
    
    return true;
}

/**
 * @brief 更新UI显示
 */
void UIManager::update() {
    // 帧率限制：每16毫秒更新一次（约60fps）
    static uint32_t lastRenderTime = 0;
    uint32_t currentTime = millis();
    if (currentTime - lastRenderTime < 16) {
        return;
    }
    lastRenderTime = currentTime;
    
    // 检查姿态估计器是否有新数据
    bool attitudeUpdated = false;
    if (_attitudeEstimator) {
        attitudeUpdated = _attitudeEstimator->update();
    }
    
    // 根据当前页面绘制不同的内容
    switch (_currentPage) {
        case PAGE_MAIN:
            // 姿态数据更新时重绘，或者每秒强制重绘一次（确保时间显示正确）
            static uint32_t lastForceRenderTime = 0;
            if (attitudeUpdated || (currentTime - lastForceRenderTime > 1000)) {
                drawMainPage();
                _display->update();
                lastForceRenderTime = currentTime;
            }
            break;
        case PAGE_MENU:
            drawMenuPage();
            _display->update();
            break;
        case PAGE_SUN_DATA:
            drawSunDataPage();
            _display->update();
            break;
        case PAGE_TIME_MACHINE:
            drawTimeMachinePage();
            _display->update();
            break;
        case PAGE_SETTINGS:
            drawSettingsPage();
            _display->update();
            break;
        case PAGE_TIDE:
            // 潮汐模式：每秒重绘一次（确保时间显示正确）
            static uint32_t lastTideRenderTime = 0;
            if (currentTime - lastTideRenderTime > 1000) {
                drawTidePage();
                _display->update();
                lastTideRenderTime = currentTime;
            }
            break;
    }
}

/**
 * @brief 绘制静态天空半球
 * @param x 中心点X坐标
 * @param y 中心点Y坐标
 * @param radius 半径
 */



/**
 * @brief 处理用户输入
 */
void UIManager::handleInput() {
    if (_keyboard->available()) {
        Key key = _keyboard->getKey();
        
        switch (_currentPage) {
            case PAGE_MAIN:
                if (key == KEY_OK) {
                    resetTime();
                    drawMainPage();
                    _display->update();
                } else if (key == KEY_S) {
                    _attitudeEstimator->calibrateHeading();
                    drawMainPage();
                    _display->update();
                } else if (key == KEY_V) {
                    toggleViewMode();
                    drawMainPage();
                    _display->update();
                } else if (key == KEY_UP) {
                    if (_view3DRenderer) {
                        _view3DRenderer->zoom(1.1);
                        drawMainPage();
                        _display->update();
                    }
                } else if (key == KEY_DOWN) {
                    if (_view3DRenderer) {
                        _view3DRenderer->zoom(0.9);
                        drawMainPage();
                        _display->update();
                    }
                }
                break;
            case PAGE_MENU:
                if (key == KEY_BACK || key == KEY_ESC) {
                    _currentPage = PAGE_MAIN;
                } else if (key == KEY_S) {
                    _currentPage = PAGE_SETTINGS;
                }
                break;
            case PAGE_SUN_DATA:
                if (key == KEY_BACK || key == KEY_ESC) {
                    _currentPage = PAGE_MAIN;
                }
                break;
            case PAGE_TIME_MACHINE:
                if (key == KEY_BACK || key == KEY_ESC) {
                    _currentPage = PAGE_MAIN;
                }
                break;
        }
    }
}

void UIManager::setView3DRenderer(View3DRenderer* renderer) {
    Serial.printf("[UIManager] setView3DRenderer called with renderer: %p\n", renderer);
    _view3DRenderer = renderer;
    if (_view3DRenderer) {
        _view3DRenderer->setSunCalculator(_sunCalculator);
        _view3DRenderer->setMoonCalculator(_moonCalculator);
        _view3DRenderer->setCelestialCore(_celestialCore);
        _view3DRenderer->setPositionManager(_positionManager);
        _view3DRenderer->setTimeMachine(_timeMachine);
        Serial.println("[UIManager] View3DRenderer set successfully");
    }
}

void UIManager::toggleViewMode() {
    // 切换视图模式（虽然只使用3D模式，但保持函数存在）
    _use3DView = !_use3DView;
}

void UIManager::setUse3DView(bool use3D) {
    _use3DView = use3D;
    // 只使用3D模式，移除ViewMode设置
}

bool UIManager::isUsing3DView() const {
    return _use3DView;
}

/**
 * @brief 设置当前页面
 * @param page 页面枚举
 */
void UIManager::setPage(UIPage page) {
    _currentPage = page;
}

/**
 * @brief 激活/禁用时间机器功能
 * @param active 是否激活
 */
void UIManager::setTimeMachineActive(bool active) {
    _timeMachineActive = active;
}

/**
 * @brief 设置时间机器模块指针
 * @param timeMachine 时间机器模块指针
 */
void UIManager::setTimeMachine(TimeMachine* timeMachine) {
    _timeMachine = timeMachine;
}

void UIManager::setGnss(HalGnss* gnss) {
    _gnss = gnss;
}

/**
 * @brief 初始化位置设置（从当前位置加载）
 */
void UIManager::initPositionSettings() {
    if (_positionManager) {
        PositionData pos = _positionManager->getPosition();
        _editLongitude = pos.longitude;
        _editLatitude = pos.latitude;
        _editAltitude = pos.altitude;
    } else {
        // 默认值：北京
        _editLongitude = 116.4074;
        _editLatitude = 39.9042;
        _editAltitude = 0.0;
    }
    _selectedField = 0;
}

/**
 * @brief 选择下一个字段
 */
void UIManager::selectNextField() {
    _selectedField = (_selectedField + 1) % 3;
}

/**
 * @brief 选择上一个字段
 */
void UIManager::selectPreviousField() {
    if (_selectedField == 0) {
        _selectedField = 2;
    } else {
        _selectedField--;
    }
}

/**
 * @brief 增加当前选中字段的值
 */
void UIManager::incrementPositionField() {
    switch (_selectedField) {
        case 0: // 经度
            _editLongitude += 0.0001;
            if (_editLongitude > 180.0) _editLongitude = 180.0;
            break;
        case 1: // 纬度
            _editLatitude += 0.0001;
            if (_editLatitude > 90.0) _editLatitude = 90.0;
            break;
        case 2: // 海拔
            _editAltitude += 1.0;
            break;
    }
}

/**
 * @brief 减少当前选中字段的值
 */
void UIManager::decrementPositionField() {
    switch (_selectedField) {
        case 0: // 经度
            _editLongitude -= 0.0001;
            if (_editLongitude < -180.0) _editLongitude = -180.0;
            break;
        case 1: // 纬度
            _editLatitude -= 0.0001;
            if (_editLatitude < -90.0) _editLatitude = -90.0;
            break;
        case 2: // 海拔
            _editAltitude -= 1.0;
            break;
    }
}

/**
 * @brief 应用位置设置（保存并生效）
 */
void UIManager::applyPositionSettings() {
    if (_positionManager) {
        PositionData pos;
        pos.longitude = _editLongitude;
        pos.latitude = _editLatitude;
        pos.altitude = _editAltitude;
        
        // 设置手动位置并启用
        _positionManager->setManualPosition(pos);
        _positionManager->enableManualPosition(true);
        
        Serial.printf("[UIManager] Applied manual position: %.6f, %.6f, %.1f\n", 
            _editLongitude, _editLatitude, _editAltitude);
        
        // 重新绘制主页面以更新天体轨迹
        drawMainPage();
        _display->update();
    }
}

/**
 * @brief 选择下一个设置字段
 */
void UIManager::selectNextSettingsField() {
    _settingsSelectedField = (_settingsSelectedField + 1) % 3;
    drawSettingsPage();
    _display->update();
}

/**
 * @brief 删除设置输入字符
 */
void UIManager::deleteSettingsChar() {
    int length = strlen(_settingsInputBuffer[_settingsSelectedField]);
    if (length > 0) {
        _settingsInputBuffer[_settingsSelectedField][length - 1] = '\0';
        drawSettingsPage();
        _display->update();
    }
}

/**
 * @brief 添加设置输入字符
 * @param c 字符
 */
void UIManager::addSettingsChar(char c) {
    int length = strlen(_settingsInputBuffer[_settingsSelectedField]);
    
    // 根据字段类型限制最大长度
    int maxLength = 31;
    if (_settingsSelectedField == 0 || _settingsSelectedField == 1) {
        maxLength = 12; // 经纬度最多12个字符
    } else if (_settingsSelectedField == 2) {
        maxLength = 8; // 海拔最多8个字符
    }
    
    if (length >= maxLength) {
        return; // 超过最大长度，不再添加
    }
    
    // 如果是小数点，检查是否已经有小数点
    if (c == '.') {
        bool hasDot = false;
        for (int i = 0; i < length; i++) {
            if (_settingsInputBuffer[_settingsSelectedField][i] == '.') {
                hasDot = true;
                break;
            }
        }
        if (hasDot) {
            return; // 已经有小数点，不再添加
        }
    }
    
    // 如果是负号，检查是否已经有负号或数字
    if (c == '-') {
        if (length > 0) {
            return; // 只能在开头输入负号
        }
    }
    
    _settingsInputBuffer[_settingsSelectedField][length] = c;
    _settingsInputBuffer[_settingsSelectedField][length + 1] = '\0';
    drawSettingsPage();
    _display->update();
}

/**
 * @brief 应用设置
 */
void UIManager::applySettings() {
    double longitude = atof(_settingsInputBuffer[0]);
    double latitude = atof(_settingsInputBuffer[1]);
    double altitude = atof(_settingsInputBuffer[2]);
    
    // 验证经纬度范围
    if (longitude < -180.0 || longitude > 180.0) {
        Serial.println("[UIManager] Invalid longitude value");
        return; // 经度超出范围
    }
    if (latitude < -90.0 || latitude > 90.0) {
        Serial.println("[UIManager] Invalid latitude value");
        return; // 纬度超出范围
    }
    
    // 验证海拔范围
    if (altitude < -1000.0 || altitude > 8848.0) {
        Serial.println("[UIManager] Invalid altitude value");
        return; // 海拔超出范围
    }
    
    // 验证输入是否为空或无效
    if (strlen(_settingsInputBuffer[0]) == 0 || strlen(_settingsInputBuffer[1]) == 0 || strlen(_settingsInputBuffer[2]) == 0) {
        Serial.println("[UIManager] Empty input value");
        return; // 输入为空
    }
    
    if (_positionManager) {
        PositionData pos;
        pos.longitude = longitude;
        pos.latitude = latitude;
        pos.altitude = altitude;
        
        // 设置手动位置并启用
        _positionManager->setManualPosition(pos);
        _positionManager->enableManualPosition(true);
        
        Serial.printf("[UIManager] Applied manual position: %.6f, %.6f, %.1f\n", 
            longitude, latitude, altitude);
    }
    
    drawMainPage();
    _display->update();
}

/**
 * @brief 立即重绘主页面
 */
void UIManager::redrawMainPage() {
    drawMainPage();
    _display->update();
}

/**
 * @brief 立即重绘潮汐页面
 */
void UIManager::redrawTidePage() {
    drawTidePage();
    _display->update();
}

/**
 * @brief 获取当前页面
 * @return 当前页面枚举值
 */
UIPage UIManager::getCurrentPage() {
    return _currentPage;
}

/**
 * @brief 绘制主页面（天球显示）
 */
void UIManager::drawMainPage() {
    uint16_t width = _display->getWidth();
    uint16_t height = _display->getHeight();
    
    int16_t skyCenterX = width * 2 / 3;
    int16_t skyCenterY = height / 2;
    uint16_t skyWidth = width * 5/6;
    uint16_t skyRadius = min((uint16_t)skyWidth, height) / 2 - 5;
    
    float heading = 0.0;
    SunPositionData sunPos = {0, 0, 0, 0, 0};
    
    if (_attitudeEstimator) {
        AttitudeData attitude = _attitudeEstimator->getAttitude();
        heading = attitude.heading;
    }
    
    _display->setColor(0, 0, 0);
    _display->drawRect(0, 0, width, height, true);
    
    // 只使用 3D 渲染，不使用 2D 渲染
    if (!_view3DRenderer) {
        Serial.println("[UIManager] _view3DRenderer is nullptr, skipping render");
        return;
    }
    
    //Serial.println("[UIManager] Using 3D render path");
    
    float roll = 0.0, pitch = 0.0, yaw = heading;
    if (_attitudeEstimator) {
        AttitudeData attitude = _attitudeEstimator->getMappedAttitude();
        roll = attitude.roll;
        pitch = attitude.pitch;
        yaw = _attitudeEstimator->getVirtualHeading();
    }
    
    _view3DRenderer->updateCameraFromIMU(pitch, roll, yaw);
    
    // 获取当前位置
    PositionData position = _positionManager->getPosition();
    double latitude = position.latitude;
    double longitude = position.longitude;
    
    // 获取时间戳（使用时间机器的目标时间或当前时间）
    uint32_t timestamp;
    // 统一使用 PositionManager 的时间戳，确保与轨迹生成使用相同的时间源
    timestamp = _positionManager->getTimestamp();
    
    // 太阳和月亮位置缓存机制，避免频繁计算
    static struct {
        uint32_t lastTimestamp;
        double sunAz, sunAlt;
        double moonAz, moonAlt;
        bool valid;
    } celestialCache = {0, 0, 0, 0, 0, false};
    
    // 只有当时间变化时才重新获取（精确到秒，因为有缓存插值）
    if (!celestialCache.valid || timestamp != celestialCache.lastTimestamp) {
        bool sunOk = false, moonOk = false;
        
        // 1. 尝试从 3D 渲染器的轨迹缓存中直接获取插值后的位置 (极快)
        if (_view3DRenderer) {
            sunOk = _view3DRenderer->getCachedSunPosition(timestamp, celestialCache.sunAz, celestialCache.sunAlt);
            moonOk = _view3DRenderer->getCachedMoonPosition(timestamp, celestialCache.moonAz, celestialCache.moonAlt);
        }
        
        // 2. 如果缓存未就绪，则回退到全量天文计算 (较慢)
        if (!sunOk && _sunCalculator) {
            SunPositionData sunPos = _sunCalculator->calculatePosition(timestamp, latitude, longitude);
            celestialCache.sunAz = sunPos.azimuth;
            celestialCache.sunAlt = sunPos.altitude;
        }
        if (!moonOk && _moonCalculator) {
            MoonPositionData moonPos = _moonCalculator->calculatePosition(timestamp, latitude, longitude);
            celestialCache.moonAz = moonPos.azimuth;
            celestialCache.moonAlt = moonPos.altitude;
        }
        
        // 更新渲染器位置
        if (_view3DRenderer) {
            _view3DRenderer->setSunPosition(celestialCache.sunAz, celestialCache.sunAlt);
            _view3DRenderer->setMoonPosition(celestialCache.moonAz, celestialCache.moonAlt);
        }
        
        celestialCache.lastTimestamp = timestamp;
        celestialCache.valid = true;
    }
    
    _view3DRenderer->render(skyCenterX, skyCenterY, skyRadius);
    
    drawIMUDataLabel();
    drawCurrentTime(width - 235, height - 10);
}



/**
 * @brief 应用3D旋转
 * @param point 3D点
 * @param roll 横滚角（度）
 * @param pitch 俯仰角（度）
 * @param yaw 偏航角（度）
 * @return 旋转后的3D点
 */
Point3D rotate3D(Point3D point, float roll, float pitch, float yaw) {
    float rollRad = roll * DEG_TO_RAD;
    float pitchRad = pitch * DEG_TO_RAD;
    float yawRad = yaw * DEG_TO_RAD;
    
    // 先绕X轴旋转（横滚）
    float y1 = point.y * cos(rollRad) - point.z * sin(rollRad);
    float z1 = point.y * sin(rollRad) + point.z * cos(rollRad);
    
    // 再绕Y轴旋转（俯仰）
    float x2 = point.x * cos(pitchRad) + z1 * sin(pitchRad);
    float z2 = -point.x * sin(pitchRad) + z1 * cos(pitchRad);
    
    // 最后绕Z轴旋转（偏航）
    float x3 = x2 * cos(yawRad) - y1 * sin(yawRad);
    float y3 = x2 * sin(yawRad) + y1 * cos(yawRad);
    
    return Point3D(x3, y3, z2);
}

/**
 * @brief 限制值在指定范围内
 * @param value 输入值
 * @param min 最小值
 * @param max 最大值
 * @return 限制后的值
 */
template<typename T>
T clamp(T value, T min, T max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

/**
 * @brief 计算姿态（偏航角）
 * @param gyroZ 陀螺仪Z轴数据
 * @param deltaTime 时间差（毫秒）
 * @param yaw 偏航角引用
 */
void calculateAttitudeYaw(float gyroZ, float deltaTime, float& yaw) {
    yaw += gyroZ * deltaTime;
}

/**
 * @brief 平滑姿态数据
 * @param current 当前值
 * @param target 目标值
 * @param smoothFactor 平滑因子（0-1）
 * @return 平滑后的值
 */
float smoothValue(float current, float target, float smoothFactor = 0.1f) {
    return current + (target - current) * smoothFactor;
}

/**
 * @brief 透视投影
 * @param point 3D点
 * @param fov 视场角（度）
 * @param screenWidth 屏幕宽度
 * @param screenHeight 屏幕高度
 * @param centerX 投影中心X坐标
 * @param centerY 投影中心Y坐标
 * @return 投影后的2D点
 */
Point2D project3D(Point3D point, float fov, uint16_t screenWidth, uint16_t screenHeight, int16_t centerX, int16_t centerY) {
    float distance = 100.0; // 观察距离
    float scale = tan(fov * 0.5 * DEG_TO_RAD) * distance;
    
    if (point.z > -distance) {
        float x = (point.x / (point.z + distance)) * scale + centerX;
        float y = (-point.y / (point.z + distance)) * scale + centerY;
        return Point2D((int16_t)x, (int16_t)y);
    } else {
        return Point2D(-1, -1); // 不可见
    }
}

/**
 * @brief 绘制天球图
 * @param x 中心点X坐标
 * @param y 中心点Y坐标
 * @param radius 半径
 * @param heading 航向角（度）
 * @param sunPos 太阳位置数据
 */
void UIManager::drawSkyMap(int16_t x, int16_t y, uint16_t radius, float heading, SunPositionData sunPos) {
    uint16_t width = _display->getWidth();
    uint16_t height = _display->getHeight();
    
    float roll = 0.0, pitch = 0.0, yaw = heading;
    if (_attitudeEstimator) {
        AttitudeData attitude = _attitudeEstimator->getAttitude();
        roll = attitude.roll;
        pitch = attitude.pitch;
        yaw = _attitudeEstimator->getVirtualHeading();
    }
    
    // 绘制天球背景（半球体效果）
    // 移除网格线绘制，只绘制一个简单的背景
    //_display->setColor(40, 40, 60);
    //_display->drawCircle(x, y, radius, true);
    
    // 绘制地平面背景色（新实现：作为3D几何区域处理）
    _display->setColor(80, 60, 40); // 棕色，代表地面
    
    // 地平面定义为Z=0的平面，我们需要绘制地平面以下（Z<0）的区域
    // 1. 采样地平面上的点（Z=0），形成一个多边形
    // 2. 应用与天球相同的旋转矩阵
    // 3. 投影到2D屏幕坐标
    // 4. 填充多边形区域
    
    // 采样地平面上的点（方位角0-360度，高度角0度）
    std::vector<Point2D> groundPlanePoints;
    for (int i = 0; i <= 360; i += 5) {
        float azimuth = i;
        float altitude = 0; // 地平圈
        
        // 使用SkyHemisphere计算屏幕坐标，并应用IMU姿态旋转
        // 与天球使用相同的旋转矩阵
        Point2D groundPoint = SkyHemisphere::azAltToScreenWithIMU(azimuth, altitude, roll, pitch, yaw, width, height, radius, x, y);
        
        // 确保点在屏幕范围内
        if (groundPoint.x >= 0 && groundPoint.x < width && groundPoint.y >= 0 && groundPoint.y < height) {
            groundPlanePoints.push_back(groundPoint);
        }
    }
    
    // 如果采集到足够的点，绘制地平面区域
    if (groundPlanePoints.size() > 2) {
        // 计算地平面多边形的最低点
        int groundBottom = 0;
        for (const auto& point : groundPlanePoints) {
            if (point.y > groundBottom) {
                groundBottom = point.y;
            }
        }
        
        // 从地平面多边形的最低点向下绘制水平线段，填充整个地平面区域
        // 这种方法模拟了地平面以下区域的填充，与天球旋转保持一致
        if (groundBottom < height) {
            for (int h = groundBottom; h < height; h++) {
                // 对于每一行，找到左右边界
                int leftBound = width;
                int rightBound = 0;
                
                // 遍历地平面多边形的点，找到当前行对应的左右边界
                for (size_t j = 0; j < groundPlanePoints.size(); j++) {
                    const Point2D& p1 = groundPlanePoints[j];
                    const Point2D& p2 = groundPlanePoints[(j + 1) % groundPlanePoints.size()];
                    
                    // 检查当前行是否与线段p1-p2相交
                    if ((p1.y <= h && p2.y > h) || (p1.y > h && p2.y <= h)) {
                        // 计算交点的x坐标
                        float t = (float)(h - p1.y) / (p2.y - p1.y);
                        int xIntersect = (int)(p1.x + t * (p2.x - p1.x));
                        
                        if (xIntersect < leftBound) leftBound = xIntersect;
                        if (xIntersect > rightBound) rightBound = xIntersect;
                    }
                }
                
                // 绘制当前行的水平线段
                if (leftBound < rightBound && leftBound >= 0 && rightBound < width) {
                    _display->drawLine(leftBound, h, rightBound, h);
                }
            }
        }
    }
    
    // 绘制地平圈（半球的截面，随设备旋转）
    _display->setColor(150, 150, 150);
    
    // 绘制旋转的地平圈
    Point2D lastPoint;
    bool firstPoint = true;
    
    for (int i = 0; i <= 360; i += 5) {
        float azimuth = i;
        float altitude = 0; // 地平圈
        
        // 使用SkyHemisphere计算屏幕坐标，并应用IMU姿态旋转（包含yaw）
        Point2D currentPoint = SkyHemisphere::azAltToScreenWithIMU(azimuth, altitude, roll, pitch, yaw, width, height, radius, x, y);
        
        // 确保点在地平圈范围内
        if (currentPoint.y >= y - radius && currentPoint.y <= y + radius) {
            if (firstPoint) {
                lastPoint = currentPoint;
                firstPoint = false;
            } else {
                _display->drawLine(lastPoint.x, lastPoint.y, currentPoint.x, currentPoint.y);
                lastPoint = currentPoint;
            }
        }
    }
    
    // 绘制方向标记（N, E, S, W）- 随地平圈旋转
    _display->setColor(255, 255, 255);
    
    // 北
    Point2D northPoint = SkyHemisphere::azAltToScreenWithIMU(0, 0, roll, pitch, yaw, width, height, radius, x, y);
    if (northPoint.x >= 0 && northPoint.x < width && northPoint.y >= 0 && northPoint.y < height) {
        _display->drawText(northPoint.x - 8, northPoint.y - 10, "N", 2);
    }
    
    // 东
    Point2D eastPoint = SkyHemisphere::azAltToScreenWithIMU(90, 0, roll, pitch, yaw, width, height, radius, x, y);
    if (eastPoint.x >= 0 && eastPoint.x < width && eastPoint.y >= 0 && eastPoint.y < height) {
        _display->drawText(eastPoint.x - 8, eastPoint.y - 10, "E", 2);
    }
    
    // 南
    Point2D southPoint = SkyHemisphere::azAltToScreenWithIMU(180, 0, roll, pitch, yaw, width, height, radius, x, y);
    if (southPoint.x >= 0 && southPoint.x < width && southPoint.y >= 0 && southPoint.y < height) {
        _display->drawText(southPoint.x - 8, southPoint.y - 10, "S", 2);
    }
    
    // 西
    Point2D westPoint = SkyHemisphere::azAltToScreenWithIMU(270, 0, roll, pitch, yaw, width, height, radius, x, y);
    if (westPoint.x >= 0 && westPoint.x < width && westPoint.y >= 0 && westPoint.y < height) {
        _display->drawText(westPoint.x - 8, westPoint.y - 10, "W", 2);
    }
    
    // 绘制天顶（Z+）标记
    //_display->setColor(255, 255, 255); // 白色
    //Point2D zenithPoint = SkyHemisphere::azAltToScreenWithIMU(0, 90, roll, pitch, yaw, width, height, radius, x, y);
    //if (zenithPoint.x >= 0 && zenithPoint.x < width && zenithPoint.y >= 0 && zenithPoint.y < height) {
    //    _display->drawCircle(zenithPoint.x, zenithPoint.y, 5, true);
    //    _display->drawText(zenithPoint.x - 12, zenithPoint.y - 20, "Z+", 2);
    //}
    
    // 添加虚拟北的说明，让用户知道这是虚拟北而不是地理北
    _display->setColor(180, 180, 180);
    _display->drawText(10, height - 30, "Camera View Mode", 1);
    _display->setColor(150, 150, 150);
    _display->drawText(10, height - 15, "Press 'S' to set reference", 1);
    

    
    // 绘制太阳位置
    if (sunPos.altitude > -90) {
        // 使用SkyHemisphere计算屏幕坐标，并应用IMU姿态旋转
        Point2D sunPoint = SkyHemisphere::azAltToScreenWithIMU(sunPos.azimuth, sunPos.altitude, roll, pitch, yaw, width, height, radius, x, y);
        
        // 确保太阳在天球范围内
        if (sunPoint.x >= 0 && sunPoint.x < width && sunPoint.y >= 0 && sunPoint.y < height) {
            _display->setColor(255, 255, 0); // 黄色
            _display->drawCircle(sunPoint.x, sunPoint.y, 8, true);
        }
    }
    
    // 绘制月亮位置（模拟）
    _display->setColor(200, 200, 200); // 灰色
    float moonAzimuth = sunPos.azimuth + 90;
    float moonAltitude = sunPos.altitude - 20;
    if (moonAltitude > -90) {
        // 使用SkyHemisphere计算屏幕坐标，并应用IMU姿态旋转
        Point2D moonPoint = SkyHemisphere::azAltToScreenWithIMU(moonAzimuth, moonAltitude, roll, pitch, yaw, width, height, radius, x, y);
        
        // 确保月亮在天球范围内
        if (moonPoint.x >= 0 && moonPoint.x < width && moonPoint.y >= 0 && moonPoint.y < height) {
            _display->drawCircle(moonPoint.x, moonPoint.y, 6, true);
        }
    }
    
    // 绘制当前方向指示器（随设备旋转）
    _display->setColor(0, 255, 0); // 绿色
    int16_t headingX = x + (int16_t)(sin((0 - yaw) * DEG_TO_RAD) * radius * 0.8);
    int16_t headingY = y - (int16_t)(cos((0 - yaw) * DEG_TO_RAD) * radius * 0.8 * cos(pitch * DEG_TO_RAD));
    _display->drawLine(x, y, headingX, headingY);
    _display->drawCircle(x, y, 3, true);
    
    // 绘制地平线阴影，增强半球效果
    // 随地平圈一起旋转，根据俯仰角调整形状
    _display->setColor(0, 0, 10); // 深色阴影
    
    // 计算地平线阴影的形状（考虑俯仰角）
    float shadowScale = sin(pitch * DEG_TO_RAD);
    if (shadowScale < 0) shadowScale = 0;
    
    for (int i = 0; i <= radius; i++) {
        int shadowWidth = (int)(i * shadowScale);
        if (shadowWidth > 0) {
            _display->drawLine(x - shadowWidth, y, x + shadowWidth, y);
        }
    }
}

/**
 * @brief 绘制太阳轨迹
 * @param x 中心点X坐标
 * @param y 中心点Y坐标
 * @param radius 半径
 * @param sunPos 太阳位置数据
 */
/**
 * @brief 绘制月亮轨迹（3D版本）
 * @param screenWidth 屏幕宽度
 * @param screenHeight 屏幕高度
 * @param radius 半径
 * @param roll 横滚角（度）
 * @param pitch 俯仰角（度）
 * @param yaw 偏航角（度）
 * @param sunPos 太阳位置数据
 * @param centerX 投影中心X坐标
 * @param centerY 投影中心Y坐标
 */
void UIManager::drawMoonPath3D(uint16_t screenWidth, uint16_t screenHeight, uint16_t radius, float roll, float pitch, float yaw, SunPositionData sunPos, int16_t centerX, int16_t centerY) {
    _display->setColor(150, 150, 200); // 浅蓝色
    
    // 绘制月亮轨迹（简化版，基于太阳轨迹偏移）
    Point2D lastPoint2D;
    bool firstPoint = true;
    
    for (int i = 0; i < 24; i++) {
        // 计算每个小时的月亮位置（基于太阳位置偏移）
        float moonAzimuth = sunPos.azimuth + 90 + i * 15; // 每小时移动15度
        float moonAltitude = sin((i - 12) * DEG_TO_RAD * 15) * 70 - 10; // 模拟月亮轨迹
        
        // 只绘制地平面以上的部分
        if (moonAltitude > 0) {
            // 使用SkyHemisphere计算屏幕坐标，并应用IMU姿态旋转（已锁死Yaw）
            Point2D currentPoint = SkyHemisphere::azAltToScreenWithIMU(moonAzimuth, moonAltitude, roll, pitch, 0, screenWidth, screenHeight, radius, centerX, centerY);
            
            // 确保月亮在天球范围内
            if (currentPoint.y >= centerY - radius && currentPoint.y <= centerY + radius) {
                Point2D currentPoint2D(currentPoint.x, currentPoint.y);
                if (firstPoint) {
                    lastPoint2D = currentPoint2D;
                    firstPoint = false;
                } else {
                    _display->drawLine(lastPoint2D.x, lastPoint2D.y, currentPoint2D.x, currentPoint2D.y);
                    lastPoint2D = currentPoint2D;
                }
            }
        }
    }
}

/**
 * @brief 绘制太阳轨迹（3D版本）
 * @param screenWidth 屏幕宽度
 * @param screenHeight 屏幕高度
 * @param radius 半径
 * @param roll 横滚角（度）
 * @param pitch 俯仰角（度）
 * @param yaw 偏航角（度）
 * @param sunPos 太阳位置数据
 * @param centerX 投影中心X坐标
 * @param centerY 投影中心Y坐标
 */
void UIManager::drawSunPath3D(uint16_t screenWidth, uint16_t screenHeight, uint16_t radius, float roll, float pitch, float yaw, SunPositionData sunPos, int16_t centerX, int16_t centerY) {
    _display->setColor(255, 150, 50); // 亮橙色，增加可见性
    
    // 绘制太阳轨迹（简化版）
    Point2D lastPoint2D;
    bool firstPoint = true;
    
    for (int i = 0; i < 24; i++) {
        // 计算每个小时的太阳位置
        float sunAzimuth = sunPos.azimuth - 180 + i * 15; // 从东到西
        float sunAltitude = sin((i - 12) * DEG_TO_RAD * 15) * 90; // 中午最高
        
        // 只绘制地平面以上的部分
        if (sunAltitude > 0) {
            // 使用SkyHemisphere计算屏幕坐标，并应用IMU姿态旋转（已锁死Yaw）
            Point2D currentPoint = SkyHemisphere::azAltToScreenWithIMU(sunAzimuth, sunAltitude, roll, pitch, 0, screenWidth, screenHeight, radius, centerX, centerY);
            
            // 确保太阳在天球范围内
            if (currentPoint.y >= centerY - radius && currentPoint.y <= centerY + radius) {
                if (firstPoint) {
                    lastPoint2D = currentPoint;
                    firstPoint = false;
                } else {
                    _display->drawLine(lastPoint2D.x, lastPoint2D.y, currentPoint.x, currentPoint.y);
                    lastPoint2D = currentPoint;
                }
            }
        }
    }
}

/**
 * @brief 绘制太阳轨迹（2D版本，兼容旧代码）
 * @param x 中心点X坐标
 * @param y 中心点Y坐标
 * @param radius 半径
 * @param sunPos 太阳位置数据
 */
void UIManager::drawSunPath(int16_t x, int16_t y, uint16_t radius, SunPositionData sunPos) {
    // 保持旧方法的兼容性
    _display->setColor(255, 150, 50); // 亮橙色，增加可见性
    
    // 获取当前时间和位置
    uint32_t currentTimestamp = _positionManager ? _positionManager->getTimestamp() : 0;
    PositionData defaultPos = PositionManager::getDefaultPosition();
    double latitude = defaultPos.latitude; // 默认北京纬度
    double longitude = defaultPos.longitude; // 默认北京经度
    
    // 绘制太阳轨迹（简化版）
    int16_t lastX = 0, lastY = 0;
    bool firstPoint = true;
    
    for (int i = 0; i < 24; i++) {
        // 计算每个小时的太阳位置
        uint32_t timestamp = currentTimestamp - 12 * 3600 + i * 3600;
        SunPositionData hourPos = {0, 0, 0, 0, 0};
        
        if (_sunCalculator) {
            hourPos = _sunCalculator->calculatePosition(timestamp, latitude, longitude);
        }
        
        // 即使高度角小于0也绘制，显示完整轨迹
        int16_t posX = x + cos((90 - hourPos.azimuth) * DEG_TO_RAD) * (radius * (hourPos.altitude + 90) / 180);
        int16_t posY = y - sin((90 - hourPos.azimuth) * DEG_TO_RAD) * (radius * (hourPos.altitude + 90) / 180);
        
        if (firstPoint) {
            lastX = posX;
            lastY = posY;
            firstPoint = false;
        } else {
            // 绘制更粗的线条
            _display->drawLine(lastX, lastY, posX, posY);
            lastX = posX;
            lastY = posY;
        }
    }
    
    // 绘制时间标记
    _display->setColor(200, 200, 200); // 更亮的颜色
    for (int i = 6; i <= 18; i += 2) {
        uint32_t timestamp = currentTimestamp - 12 * 3600 + i * 3600;
        SunPositionData hourPos = {0, 0, 0, 0, 0};
        
        if (_sunCalculator) {
            hourPos = _sunCalculator->calculatePosition(timestamp, latitude, longitude);
        }
        
        int16_t posX = x + cos((90 - hourPos.azimuth) * DEG_TO_RAD) * (radius * (hourPos.altitude + 90) / 180);
        int16_t posY = y - sin((90 - hourPos.azimuth) * DEG_TO_RAD) * (radius * (hourPos.altitude + 90) / 180);
        
        char hourText[3];
        sprintf(hourText, "%d", i);
        _display->drawText(posX - 8, posY - 8, hourText, 1);
    }
}

/**
 * @brief 绘制时间轴
 * @param x X坐标
 * @param y Y坐标
 * @param width 宽度
 * @param height 高度
 */
void UIManager::drawTimeAxis(int16_t x, int16_t y, uint16_t width, uint16_t height) {
    // 绘制时间轴背景
    _display->setColor(32, 32, 32);
    _display->drawRect(x, y, width, height, true);
    
    // 绘制时间轴
    _display->setColor(64, 64, 64);
    _display->drawLine(x + 50, y + 15, width - 50, y + 15);
    
    // 绘制时间标记
    _display->setColor(150, 150, 150);
    for (int i = 0; i <= 24; i += 2) {
        int16_t posX = x + 50 + (width - 100) * i / 24;
        _display->drawLine(posX, y + 12, posX, y + 18);
        
        char timeText[4];
        if (i < 10) {
            sprintf(timeText, "0%d", i);
        } else {
            sprintf(timeText, "%d", i);
        }
        _display->drawText(posX - 8, y + 20, timeText, 1);
    }
    
    // 获取当前时间
    TimeData currentTime = _positionManager ? _positionManager->getTime() : TimeData{26, 3, 2, 12, 0, 0};
    int currentHour = currentTime.hour;
    
    // 绘制时间轴滑块
    int16_t sliderX = x + 50 + (width - 100) * currentHour / 24;
    _display->setColor(255, 255, 0);
    _display->drawCircle(sliderX, y + 15, 5, true);
    
    // 绘制当前时间
    char timeText[9];
    sprintf(timeText, "%02d:%02d", currentTime.hour, currentTime.minute);
    _display->setColor(255, 255, 255);
    _display->drawText(width / 2 - 15, y + 3, timeText, 1);
    
    // 绘制日期
    char dateText[11];
    sprintf(dateText, "%04d/%02d/%02d", currentTime.year + 2000, currentTime.month, currentTime.day);
    _display->drawText(x + 10, y + 3, dateText, 1);
    
    // 绘制时间轴操作提示
    _display->setColor(100, 100, 100);
    _display->drawText(width - 100, y + 20, "<--[=====O=====]-->", 1);
}

/**
 * @brief 绘制状态信息
 * @param x X坐标
 * @param y Y坐标
 * @param width 宽度
 */
void UIManager::drawStatusInfo(int16_t x, int16_t y, uint16_t width) {
    // 获取太阳位置
    SunPositionData sunPos = _sunCalculator->calculateCurrentPosition();
    
    // 绘制太阳信息
    _display->setColor(255, 255, 0);
    char sunText[64];
    sprintf(sunText, "Sun: %.1f° %.1f°", sunPos.azimuth, sunPos.altitude);
    _display->drawText(x + 10, y + 10, sunText, 1);
    
    // 绘制时间机器状态
    if (_timeMachineActive) {
        _display->setColor(255, 255, 0); // 黄色
        _display->drawText(width - 120, y + 10, "Time Machine: ACTIVE", 1);
    } else {
        _display->setColor(128, 128, 128); // 灰色
        _display->drawText(width - 120, y + 10, "Time Machine: OFF", 1);
    }
}

/**
 * @brief 绘制太阳数据页面（详细参数）
 */
void UIManager::drawSunDataPage() {
    // 获取显示尺寸
    uint16_t width = _display->getWidth();
    uint16_t height = _display->getHeight();
    
    // 获取太阳位置
    SunPositionData sunPos = _sunCalculator->calculateCurrentPosition();
    
    // 绘制标题
    _display->setColor(255, 255, 255);
    _display->drawText(10, 10, "Sun Data", 2);
    
    // 绘制太阳参数
    char buffer[64];
    
    sprintf(buffer, "Azimuth: %.2f°", sunPos.azimuth);
    _display->drawText(10, 40, buffer, 1);
    
    sprintf(buffer, "Altitude: %.2f°", sunPos.altitude);
    _display->drawText(10, 60, buffer, 1);
    
    sprintf(buffer, "Distance: %.4f AU", sunPos.distance);
    _display->drawText(10, 80, buffer, 1);
    
    // 绘制日出日落时间
    uint32_t sunrise = sunPos.sunrise;
    uint32_t sunset = sunPos.sunset;
    uint32_t noon = sunPos.noon;
    
    sprintf(buffer, "Sunrise: %02d:%02d", sunrise / 3600, (sunrise % 3600) / 60);
    _display->drawText(10, 100, buffer, 1);
    
    sprintf(buffer, "Sunset: %02d:%02d", sunset / 3600, (sunset % 3600) / 60);
    _display->drawText(10, 120, buffer, 1);
    
    sprintf(buffer, "Noon: %02d:%02d", noon / 3600, (noon % 3600) / 60);
    _display->drawText(10, 140, buffer, 1);
    
    // 绘制提示信息
    _display->setColor(128, 128, 128);
    _display->drawText(10, height - 20, "Press ESC to return", 1);
}

/**
 * @brief 绘制时间机器页面（日期/时间设置）
 */
void UIManager::drawTimeMachinePage() {
    // 获取显示尺寸
    uint16_t width = _display->getWidth();
    uint16_t height = _display->getHeight();
    
    // 绘制标题
    _display->setColor(255, 255, 255);
    _display->drawText(10, 10, "Time Machine", 2);
    
    // 绘制提示信息
    _display->setColor(255, 255, 0);
    _display->drawText(10, 40, "Use arrow keys to adjust time", 1);
    _display->drawText(10, 60, "Press OK to confirm", 1);
    _display->drawText(10, 80, "Press ESC to cancel", 1);
    
    // 绘制当前时间（这里需要与时间机器模块集成）
    _display->setColor(255, 255, 255);
    _display->drawText(10, 120, "Current Time:", 1);
    _display->drawText(10, 140, "YYYY-MM-DD HH:MM:SS", 1);
}

/**
 * @brief 绘制设置页面
 */
void UIManager::drawSettingsPage() {
    // 获取显示尺寸
    uint16_t width = _display->getWidth();
    uint16_t height = _display->getHeight();
    
    // 绘制弹窗背景
    _display->setColor(0, 0, 0);
    _display->drawRect(0, 0, width, height, true);
    
    // 弹窗尺寸
    uint16_t dialogWidth = 280;
    uint16_t dialogHeight = 200;
    int16_t dialogX = (width - dialogWidth) / 2;
    int16_t dialogY = (height - dialogHeight) / 2;
    
    // 绘制弹窗边框（绿色，与主程序左上角文字颜色一致）
    _display->setColor(40, 255, 120);
    _display->drawRect(dialogX - 2, dialogY - 2, dialogWidth + 4, dialogHeight + 4, true);
    
    // 绘制弹窗背景
    _display->setColor(0, 0, 0);
    _display->drawRect(dialogX, dialogY, dialogWidth, dialogHeight, true);
    
    // 绘制标题
    _display->setColor(255, 255, 255);
    _display->drawText(dialogX + 10, dialogY + 10, "Settings", 2);
    
    // 绘制经度输入
    _display->setColor(255, 255, 255);
    _display->drawText(dialogX + 10, dialogY + 40, "Lon:", 2);
    
    // 绘制经度输入框（默认白色，选中时绿色）
    if (_settingsSelectedField == 0) {
        _display->setColor(40, 255, 120);
    } else {
        _display->setColor(255, 255, 255);
    }
    _display->drawRect(dialogX + 120, dialogY + 35, 120, 20, false);
    
    // 绘制经度输入内容
    _display->setColor(255, 255, 255);
    _display->drawText(dialogX + 125, dialogY + 37, _settingsInputBuffer[0], 2);
    
    // 如果当前选中经度，绘制光标（紧贴文字末尾）
    if (_settingsSelectedField == 0) {
        _display->setColor(40, 255, 120);
        int16_t cursorX = dialogX + 125 + strlen(_settingsInputBuffer[0]) * 12;
        _display->drawLine(cursorX, dialogY + 37, cursorX, dialogY + 52);
    }
    
    // 绘制纬度输入
    _display->setColor(255, 255, 255);
    _display->drawText(dialogX + 10, dialogY + 70, "Lat:", 2);
    
    // 绘制纬度输入框（默认白色，选中时绿色）
    if (_settingsSelectedField == 1) {
        _display->setColor(40, 255, 120);
    } else {
        _display->setColor(255, 255, 255);
    }
    _display->drawRect(dialogX + 120, dialogY + 65, 120, 20, false);
    
    // 绘制纬度输入内容
    _display->setColor(255, 255, 255);
    _display->drawText(dialogX + 125, dialogY + 67, _settingsInputBuffer[1], 2);
    
    // 如果当前选中纬度，绘制光标（紧贴文字末尾）
    if (_settingsSelectedField == 1) {
        _display->setColor(40, 255, 120);
        int16_t cursorX = dialogX + 125 + strlen(_settingsInputBuffer[1]) * 12;
        _display->drawLine(cursorX, dialogY + 67, cursorX, dialogY + 82);
    }
    
    // 绘制海拔输入
    _display->setColor(255, 255, 255);
    _display->drawText(dialogX + 10, dialogY + 100, "Alt(m):", 2);
    
    // 绘制海拔输入框（默认白色，选中时绿色）
    if (_settingsSelectedField == 2) {
        _display->setColor(40, 255, 120);
    } else {
        _display->setColor(255, 255, 255);
    }
    _display->drawRect(dialogX + 120, dialogY + 95, 120, 20, false);
    
    // 绘制海拔输入内容
    _display->setColor(255, 255, 255);
    _display->drawText(dialogX + 125, dialogY + 97, _settingsInputBuffer[2], 2);
    
    // 如果当前选中海拔，绘制光标（紧贴文字末尾）
    if (_settingsSelectedField == 2) {
        _display->setColor(40, 255, 120);
        int16_t cursorX = dialogX + 125 + strlen(_settingsInputBuffer[2]) * 12;
        _display->drawLine(cursorX, dialogY + 97, cursorX, dialogY + 112);
    }
    
    // 绘制OK按钮
    _display->setColor(40, 255, 120);
    _display->drawRect(dialogX + 110, dialogY + 138, 60, 25, true);
    _display->setColor(255, 255, 255);
    _display->drawText(dialogX + 128, dialogY + 144, "OK", 2);
    
    
    // 绘制操作提示
    _display->setColor(128, 128, 128);
    _display->drawText(dialogX + 10, dialogY + 120, "Tab: field, Del: delete, OK: save", 1);
}

/**
 * @brief 绘制位置设置页面
 */
void UIManager::drawPositionSettingsPage() {
    // 获取显示尺寸
    uint16_t width = _display->getWidth();
    uint16_t height = _display->getHeight();
    
    // 绘制背景
    _display->setColor(0, 0, 0);
    _display->drawRect(0, 0, width, height, true);
    
    // 绘制标题
    _display->setColor(255, 255, 255);
    _display->drawText(10, 10, "Position Settings", 2);
    
    // 绘制经度
    _display->setColor(_selectedField == 0 ? 0 : 255, _selectedField == 0 ? 255 : 255, _selectedField == 0 ? 255 : 255);
    _display->drawText(10, 50, "Longitude:", 1);
    char lonStr[20];
    dtostrf(_editLongitude, 10, 6, lonStr);
    _display->drawText(10, 70, lonStr, 2);
    
    // 绘制纬度
    _display->setColor(_selectedField == 1 ? 0 : 255, _selectedField == 1 ? 255 : 255, _selectedField == 1 ? 255 : 255);
    _display->drawText(10, 100, "Latitude:", 1);
    char latStr[20];
    dtostrf(_editLatitude, 10, 6, latStr);
    _display->drawText(10, 120, latStr, 2);
    
    // 绘制海拔
    _display->setColor(_selectedField == 2 ? 0 : 255, _selectedField == 2 ? 255 : 255, _selectedField == 2 ? 255 : 255);
    _display->drawText(10, 150, "Altitude (m):", 1);
    char altStr[20];
    dtostrf(_editAltitude, 10, 1, altStr);
    _display->drawText(10, 170, altStr, 2);
    
    // 绘制操作提示
    _display->setColor(128, 128, 128);
    _display->drawText(10, height - 50, "UP/DOWN: +/- value", 1);
    _display->drawText(10, height - 35, "LEFT/RIGHT: select field", 1);
    _display->drawText(10, height - 20, "OK: save  ESC: cancel", 1);
}

/**
 * @brief 绘制指南针
 * @param x 中心点X坐标
 * @param y 中心点Y坐标
 * @param radius 半径
 * @param heading 航向角（度）
 * @param sunAzimuth 太阳方位角（度）
 */
void UIManager::drawCompass(int16_t x, int16_t y, uint16_t radius, float heading, float sunAzimuth) {
    // 绘制指南针外圈
    _display->setColor(128, 128, 128);
    _display->drawCircle(x, y, radius, false);
    
    // 绘制方向标记（N, E, S, W）
    _display->setColor(255, 255, 255);
    
    // 北
    float northAngle = 0.0;
    int16_t northX = x + cos((90 - northAngle) * DEG_TO_RAD) * radius;
    int16_t northY = y - sin((90 - northAngle) * DEG_TO_RAD) * radius;
    _display->drawText(northX - 10, northY - 10, "N", 2);
    
    // 东
    float eastAngle = 90.0;
    int16_t eastX = x + cos((90 - eastAngle) * DEG_TO_RAD) * radius;
    int16_t eastY = y - sin((90 - eastAngle) * DEG_TO_RAD) * radius;
    _display->drawText(eastX - 5, eastY - 10, "E", 2);
    
    // 南
    float southAngle = 180.0;
    int16_t southX = x + cos((90 - southAngle) * DEG_TO_RAD) * radius;
    int16_t southY = y - sin((90 - southAngle) * DEG_TO_RAD) * radius;
    _display->drawText(southX - 10, southY - 10, "S", 2);
    
    // 西
    float westAngle = 270.0;
    int16_t westX = x + cos((90 - westAngle) * DEG_TO_RAD) * radius;
    int16_t westY = y - sin((90 - westAngle) * DEG_TO_RAD) * radius;
    _display->drawText(westX - 10, westY - 10, "W", 2);
    
    // 绘制当前航向
    _display->setColor(0, 255, 0); // 绿色
    _display->drawCompassNeedle(x, y, radius - 10, heading, 0.8);
    
    // 绘制太阳方向
    _display->setColor(255, 255, 0); // 黄色
    _display->drawCompassNeedle(x, y, radius - 10, sunAzimuth, 0.6);
    
    // 绘制太阳标记
    int16_t sunX = x + cos((90 - sunAzimuth) * DEG_TO_RAD) * (radius - 20);
    int16_t sunY = y - sin((90 - sunAzimuth) * DEG_TO_RAD) * (radius - 20);
    _display->drawCircle(sunX, sunY, 5, true);
    
    // 绘制中心点
    _display->setColor(255, 255, 255);
    _display->drawCircle(x, y, 3, true);
}

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
void UIManager::drawGauge(int16_t x, int16_t y, uint16_t radius, float value, float minValue, float maxValue, const char* label) {
    // 绘制仪表盘外圈
    _display->setColor(128, 128, 128);
    _display->drawCircle(x, y, radius, false);
    
    // 计算指针角度
    float angle = (value - minValue) / (maxValue - minValue) * 180 - 90;
    
    // 绘制指针
    _display->setColor(255, 0, 0);
    _display->drawCompassNeedle(x, y, radius - 10, angle, 0.8);
    
    // 绘制标签
    _display->setColor(255, 255, 255);
    _display->drawText(x - 30, y + radius + 10, label, 1);
    
    // 绘制当前值
    char buffer[16];
    sprintf(buffer, "%.1f", value);
    _display->drawText(x - 15, y - 10, buffer, 1);
}

/**
 * @brief 绘制状态条
 * @param x X坐标
 * @param y Y坐标
 * @param width 宽度
 * @param height 高度
 * @param text 文本内容
 */
void UIManager::drawStatusBar(int16_t x, int16_t y, uint16_t width, uint16_t height, const char* text) {
    // 绘制背景
    _display->setColor(64, 64, 64);
    _display->drawRect(x, y, width, height, true);
    
    // 绘制文本
    _display->setColor(255, 255, 255);
    _display->drawText(x + 10, y + 5, text, 1);
}

/**
 * @brief 绘制磁偏角信息
 * @param x X坐标
 * @param y Y坐标
 * @param width 宽度
 */
void UIManager::drawMagneticDeclination(int16_t x, int16_t y, uint16_t width) {
    // 绘制磁偏角信息（模拟值）
    _display->setColor(200, 200, 200);
    _display->drawText(width / 2 - 40, y + 5, "11.3°磁(-2.7°)", 1);
}

/**
 * @brief 绘制天体信息
 * @param x X坐标
 * @param y Y坐标
 * @param width 宽度
 */
void UIManager::drawCelestialInfo(int16_t x, int16_t y, uint16_t width) {
    // 获取太阳位置
    SunPositionData sunPos = _sunCalculator->calculateCurrentPosition();
    
    // 绘制背景
    _display->setColor(32, 32, 32);
    _display->drawRect(x, y, width, 50, true);
    
    // 绘制太阳信息
    _display->setColor(255, 255, 0);
    char sunText[32];
    sprintf(sunText, "Sun: %.0f° %.0f°", sunPos.azimuth, sunPos.altitude);
    _display->drawText(x + 10, y + 10, sunText, 1);
    
    // 绘制月亮信息（模拟）
    _display->setColor(200, 200, 200);
    char moonText[32];
    float moonAzimuth = sunPos.azimuth + 90;
    float moonAltitude = sunPos.altitude - 20;
    if (moonAltitude < 0) moonAltitude = 0;
    sprintf(moonText, "Moon: %.0f° %.0f°", moonAzimuth, moonAltitude);
    _display->drawText(x + 150, y + 10, moonText, 1);
    
    // 绘制日期时间
    TimeData localTime = _positionManager->getLocalTimeData(_positionManager->getTimestamp());
    char dateText[32];
    sprintf(dateText, "%04d/%02d/%02d", localTime.year + 2000, localTime.month, localTime.day);
    char timeText[32];
    sprintf(timeText, "%02d:%02d", localTime.hour, localTime.minute);
    
    _display->setColor(255, 255, 255);
    _display->drawText(x + 10, y + 30, dateText, 1);
    _display->drawText(x + 150, y + 30, timeText, 1);
}

/**
 * @brief 绘制底部按钮提示
 * @param x X坐标
 * @param y Y坐标
 * @param width 宽度
 */
void UIManager::drawButtonHints(int16_t x, int16_t y, uint16_t width) {
    // 绘制按钮提示
    _display->setColor(100, 100, 100);
    _display->drawText(x + 20, y - 15, "M", 1);
    _display->drawText(x + 100, y - 15, "LEFT", 1);
    _display->drawText(x + 180, y - 15, "RIGHT", 1);
    _display->drawText(x + 260, y - 15, "OK", 1);
}

/**
 * @brief 绘制当前时间（右下角）
 * @param x X坐标
 * @param y Y坐标
 */
void UIManager::drawCurrentTime(int16_t x, int16_t y) {
    if (!_positionManager) return;
    
    // 从位置管理器获取全套本地时间（包含时区带来的日期自动溢出）
    TimeData localTime = _positionManager->getLocalTimeData(_positionManager->getTimestamp());
    
    // 渲染日期和时间
    _display->setColor(255, 255, 255);
    char dateTimeText[32];
    // 修正年份 4026 问题：由于 PositionManager::getTimeData 已统一返回 26 格式，此处加 2000 后为 2026
    sprintf(dateTimeText, "%04d/%02d/%02d %02d:%02d", 
            localTime.year + 2000, localTime.month, localTime.day, 
            localTime.hour, localTime.minute);
    _display->drawText(x, y, dateTimeText, 1);
}

/**
 * @brief 绘制IMU传感器数据标签
 */
void UIManager::drawIMUDataLabel() {
    float roll = 0.0, pitch = 0.0, yaw = 0.0;
    float virtualHeading = 0.0;
    
    if (_attitudeEstimator) {
        AttitudeData attitude = _attitudeEstimator->getAttitude();
        roll = attitude.roll;
        pitch = attitude.pitch;
        yaw = attitude.yaw;
        virtualHeading = _attitudeEstimator->getVirtualHeading();
    }
    
    _display->setColor(143, 200, 170);
    char imuText[32];
    sprintf(imuText, "R:%+.1f P:%+.1f", roll, pitch);
    _display->drawText(6, 2, imuText, 1);
    
    // 显示GNSS状态在右上角
    if (_gnss) {
        GnssStatus gnssStatus = _gnss->getStatus();
        bool gnssEnabled = _gnss->isEnabled();
        bool gnssFixed = gnssStatus == GNSS_STATUS_LOCKED;
        bool gnssSearching = gnssStatus == GNSS_STATUS_SEARCHING;
        
        // 设置颜色：FIX=绿色, SRCH=黄色, OFF=灰色
        if (gnssFixed) {
            _display->setColor(0, 255, 0); // 绿色 - 已定位
        } else if (gnssSearching) {
            _display->setColor(255, 255, 0); // 黄色 - 搜索中
        } else {
            _display->setColor(128, 128, 128); // 灰色 - 关闭
        }
        
        // 显示GNSS状态：FIX 或 SRCH（搜索中）、OFF（未启用）
        sprintf(imuText, "GNSS:%s", gnssEnabled ? (gnssFixed ? "FIX" : "SRCH") : "OFF");
        
        uint16_t width = _display->getWidth();
        _display->drawText(width - 60, 2, imuText, 1);
        
        // --- 新增：显示位置和海拔信息在左侧 ---
        PositionData pos = _positionManager->getPosition();
        _display->setColor(40, 255, 120); // 亮绿色
        sprintf(imuText, "LON:%.5f", pos.longitude);
        _display->drawText(6, 18, imuText, 1);
        sprintf(imuText, "LAT:%.5f", pos.latitude);
        _display->drawText(6, 34, imuText, 1);
        sprintf(imuText, "ALT:%.1fm", pos.altitude);
        _display->drawText(6, 50, imuText, 1);
    }
    
}

/**
 * @brief 调整时间
 * @param seconds 调整的秒数（正数增加，负数减少）
 */
void UIManager::adjustTime(int seconds) {
    if (_timeMachine) {
        _timeMachine->adjustTime(seconds);
    }
}

/**
 * @brief 重置时间到当前系统时间
 */
void UIManager::resetTime() {
    if (_timeMachine) {
        _timeMachine->resetToCurrentTime();
    }
}

/**
 * @brief 绘制菜单页面
 */
void UIManager::drawMenuPage() {
    // 获取显示尺寸
    uint16_t width = _display->getWidth();
    uint16_t height = _display->getHeight();
    
    // 清除背景
    _display->setColor(0, 0, 0);
    _display->drawRect(0, 0, width, height, true);
    
    // 绘制标题
    _display->setColor(255, 255, 255);
    _display->drawText(10, 10, "Sky Compass - Menu", 2);
    
    // 绘制分隔线
    _display->setColor(64, 64, 64);
    _display->drawLine(0, 40, width, 40);
    
    // 获取太阳位置
    SunPositionData sunPos = _sunCalculator->calculateCurrentPosition();
    
    // 绘制太阳事件
    _display->setColor(255, 255, 0);
    _display->drawText(10, 50, "1. Solar Events", 1);
    
    char buffer[64];
    sprintf(buffer, "   Sunrise: %02d:%02d", sunPos.sunrise / 3600, (sunPos.sunrise % 3600) / 60);
    _display->setColor(200, 200, 200);
    _display->drawText(10, 70, buffer, 1);
    
    sprintf(buffer, "   Sunset: %02d:%02d", sunPos.sunset / 3600, (sunPos.sunset % 3600) / 60);
    _display->drawText(10, 85, buffer, 1);
    
    sprintf(buffer, "   Noon: %02d:%02d", sunPos.noon / 3600, (sunPos.noon % 3600) / 60);
    _display->drawText(10, 100, buffer, 1);
    
    // 绘制月亮事件（模拟）
    _display->setColor(200, 200, 200);
    _display->drawText(10, 110, "2. Lunar Events", 1);
    
    sprintf(buffer, "   Moonrise: 09:41 (101.0°)");
    _display->drawText(10, 130, buffer, 1);
    
    sprintf(buffer, "   Moonset: 21:40 (267.6°)");
    _display->drawText(10, 145, buffer, 1);
    
    sprintf(buffer, "   Phase: 11.7%% Waxing");
    _display->drawText(10, 160, buffer, 1);
    
    // 绘制设置选项
    _display->setColor(150, 150, 150);
    _display->drawText(10, 185, "3. Settings", 1);
    
    // 绘制提示信息
    _display->setColor(100, 100, 100);
    _display->drawText(10, height - 20, "Press ESC to return", 1);
}

/**
 * @brief 绘制静态天空半球
 * @param x 中心点X坐标
 * @param y 中心点Y坐标
 * @param radius 半径
 */
void UIManager::drawStaticSkyHemisphere(int16_t x, int16_t y, uint16_t radius) {
    uint16_t width = _display->getWidth();
    uint16_t height = _display->getHeight();
    
    // 绘制半球背景
    for (int h = 0; h <= radius; h++) {
        int intensity = 60 - (int)(h * 20.0 / radius);
        _display->setColor(intensity, intensity, intensity + 20);
        int lineWidth = 2 * sqrt(radius * radius - h * h);
        if (lineWidth > 0) {
            int startX = x - lineWidth / 2;
            _display->drawLine(startX, y - h, startX + lineWidth, y - h);
        }
    }
    
    // 1. 绘制地平线圆弧（半圆）
    _display->setColor(150, 150, 150);
    Point2D lastPoint;
    bool firstPoint = true;
    
    for (int azimuth = 0; azimuth <= 360; azimuth += 5) {
        // 使用SkyHemisphere计算坐标（高度角为0，地平线）
        Point2D screenPoint = SkyHemisphere::azAltToScreen(azimuth, 0, width, height, radius, x, y);
        
        if (firstPoint) {
            lastPoint = screenPoint;
            firstPoint = false;
        } else {
            _display->drawLine(lastPoint.x, lastPoint.y, screenPoint.x, screenPoint.y);
            lastPoint = screenPoint;
        }
    }
    
    // 2. 绘制天顶点（Altitude = 90°）
    //_display->setColor(255, 255, 255);
    //Point2D zenithPoint = SkyHemisphere::azAltToScreen(0, 90, width, height, radius, x, y);
    //_display->drawCircle(zenithPoint.x, zenithPoint.y, 5, true);
    
    // 3. 绘制示例天体轨道（固定高度30°）
    _display->setColor(255, 150, 50);
    firstPoint = true;
    
    for (int azimuth = 0; azimuth <= 360; azimuth += 5) {
        // 使用SkyHemisphere计算坐标（高度角为30°）
        Point2D orbitPoint = SkyHemisphere::azAltToScreen(azimuth, 30, width, height, radius, x, y);
        
        if (firstPoint) {
            lastPoint = orbitPoint;
            firstPoint = false;
        } else {
            _display->drawLine(lastPoint.x, lastPoint.y, orbitPoint.x, orbitPoint.y);
            lastPoint = orbitPoint;
        }
    }
}

/**
 * @brief 计算潮汐值
 */
void UIManager::calculateTideValues() {
    uint32_t currentTime = _positionManager->getTimestamp();
    PositionData position = _positionManager->getPosition();
    
    // 30分钟为单位的块索引
    uint32_t currentBlock = currentTime / 1800;
    uint32_t windowStartBlock = currentBlock - 24; // 12小时前
    
    bool positionChanged = (fabs(position.latitude - _tideCache.lastPosition.latitude) > 0.0001 || 
                           fabs(position.longitude - _tideCache.lastPosition.longitude) > 0.0001);
    
    if (!_tideCache.valid || positionChanged) {
        // 全量初始化 49 个点
        for (int i = 0; i < 49; i++) {
            uint32_t ts = (windowStartBlock + i) * 1800;
            _tideCache.tideValues[i] = _celestialCore->calculateTideValue(ts, position.latitude, position.longitude);
        }
        _tideCache.lastTimestamp = windowStartBlock; // 存 block 索引
        _tideCache.lastPosition = position;
        _tideCache.valid = true;
    } else if (windowStartBlock != _tideCache.lastTimestamp) {
        // 增量滑动更新
        int32_t diff = (int32_t)windowStartBlock - (int32_t)_tideCache.lastTimestamp;
        if (abs(diff) >= 49) {
            _tideCache.valid = false; // 跨度太大，标记失效触发重算
        } else if (diff > 0) {
            for (int d = 0; d < diff; d++) {
                // 移除旧点，添加新点
                for (int i = 0; i < 48; i++) {
                    _tideCache.tideValues[i] = _tideCache.tideValues[i+1];
                }
                uint32_t nextTs = (_tideCache.lastTimestamp + 49) * 1800;
                _tideCache.tideValues[48] = _celestialCore->calculateTideValue(nextTs, position.latitude, position.longitude);
                _tideCache.lastTimestamp++;
            }
        } else {
            for (int d = 0; d < -diff; d++) {
                for (int i = 48; i > 0; i--) {
                    _tideCache.tideValues[i] = _tideCache.tideValues[i-1];
                }
                _tideCache.lastTimestamp--;
                uint32_t prevTs = _tideCache.lastTimestamp * 1800;
                _tideCache.tideValues[0] = _celestialCore->calculateTideValue(prevTs, position.latitude, position.longitude);
            }
        }
    }
    
    // 重新扫描最大/最小值
    _tideCache.tideMin = _tideCache.tideValues[0];
    _tideCache.tideMax = _tideCache.tideValues[0];
    for (int i = 1; i < 49; i++) {
        if (_tideCache.tideValues[i] < _tideCache.tideMin) _tideCache.tideMin = _tideCache.tideValues[i];
        if (_tideCache.tideValues[i] > _tideCache.tideMax) _tideCache.tideMax = _tideCache.tideValues[i];
    }
}

/**
 * @brief 绘制潮汐曲线页面
 */
void UIManager::drawTidePage() {
    // 1. 数据准备
    calculateTideValues();
    float tideMin = _tideCache.tideMin;
    float tideMax = _tideCache.tideMax;
    float adjustedTideMax = ceil((tideMax + 0.5f) * 2.0f) / 2.0f;
    if (adjustedTideMax < 2.5f) adjustedTideMax = 2.5f;

    uint16_t width = _display->getWidth();
    uint16_t height = _display->getHeight();
    _display->clear();

    // 2. 页眉渲染 (Title, Time, Value, Moon)
    _display->setColor(255, 255, 255);
    _display->drawText(5, 5, "Tide Curve", 2);
    
    drawCurrentTime(width - 155, 120);
    
    if (_view3DRenderer) {
        _view3DRenderer->drawMoonPhase3D(width - 15, 20, 8); // 下移 10px (15->25)
    }

    // 3. 图表布局定义 (宽度增加 10px，Y轴下移 5px)
    uint16_t graphX = 30; // 左移 5px
    uint16_t graphY = 33; // 下移 5px
    uint16_t graphWidth = width - graphX - 2; // 宽度增加
    uint16_t graphHeight = height - graphY - 20;

    // 4. 坐标轴与网格
    _display->setColor(150, 150, 150);
    _display->drawLine(graphX, graphY, graphX, graphY + graphHeight); // Y轴
    _display->drawLine(graphX, graphY + graphHeight, graphX + graphWidth, graphY + graphHeight); // X轴

    // Y 轴刻度文本
    _display->setColor(200, 200, 200);
    char buf[16];
    sprintf(buf, "%.1f", adjustedTideMax);
    _display->drawText(graphX - 27, graphY, buf, 1);
    sprintf(buf, "%.1f", tideMin);
    _display->drawText(graphX - 27, graphY + graphHeight - 8, buf, 1);

    // 5. 滚动参数计算 (固定中心指示线，滚动曲线)
    uint32_t currentTime = _positionManager->getTimestamp();
    float minuteFrac = (float)(currentTime % 1800) / 1800.0f;
    float pixelOffset = minuteFrac * (graphWidth / 48.0f);
    uint16_t centerX = graphX + (graphWidth / 2); // 固定在图表中心

    // 6. 背景网格与曲线绘制 (统一直向左偏移 pixelOffset)
    // 水平网格线
    _display->setColor(60, 60, 60);
    for (float tideLevel = tideMin; tideLevel <= adjustedTideMax; tideLevel += 1.0f) {
        float norm = (tideLevel - tideMin) / (adjustedTideMax - tideMin);
        uint16_t y = graphY + graphHeight - (uint16_t)(norm * graphHeight);
        _display->drawLine(graphX, y, graphX + graphWidth, y);
    }

    // 垂直网格线 (每 3 小时，相对时间固定)
    _display->setColor(60, 60, 60);
    for (int i = 0; i <= 48; i += 6) {
        int16_t x = graphX + (i * graphWidth) / 48;
        if (x >= graphX && x <= graphX + graphWidth) {
            _display->drawLine(x, graphY, x, graphY + graphHeight);
        }
    }

    // 曲线绘制
    _display->setColor(150, 150, 200);
    Point2D lastPoint;
    for (int i = 0; i < 49; i++) {
        int16_t x = (int16_t)(graphX + (i * graphWidth) / 48) - (int16_t)pixelOffset;
        float norm = (_tideCache.tideValues[i] - tideMin) / (adjustedTideMax - tideMin);
        uint16_t y = graphY + graphHeight - (uint16_t)(norm * graphHeight);
        
        if (i > 0) {
            // 裁剪逻辑：确保线段在绘图区内
            int16_t x1 = lastPoint.x;
            int16_t x2 = x;
            if (x2 >= graphX && x1 <= graphX + graphWidth) {
                _display->drawLine(max((int)x1, (int)graphX), lastPoint.y, min((int)x2, (int)(graphX + graphWidth)), y);
            }
        }
        lastPoint = { x, (int16_t)y };
    }

    // 7. 指标指示 (Now 线固定不动)
    _display->setColor(255, 255, 0);
    _display->drawLine(centerX, graphY, centerX, graphY + graphHeight);

    // 插值当前点
    float nowTide = _tideCache.tideValues[24] * (1.0f - minuteFrac) + _tideCache.tideValues[25] * minuteFrac;
    float nowNorm = (nowTide - tideMin) / (adjustedTideMax - tideMin);
    uint16_t nowY = graphY + graphHeight - (uint16_t)(nowNorm * graphHeight);
    _display->drawCircle(centerX, nowY, 4, true);

    // 潮位及页眉其它文本 (下移 10px)
    sprintf(buf, "Tide: %.2fm", nowTide);
    _display->setColor(180, 180, 255);
    _display->drawText(width - 138, 25, buf, 1);

    // 底部标签 (随 graphY 下移)
    _display->setColor(180, 180, 180);
    _display->drawText(graphX, graphY + graphHeight + 6, "-12h", 1);
    //_display->drawText(centerX - 10, graphY + graphHeight + 6, "Now", 1);
    _display->drawText(graphX + graphWidth - 25, graphY + graphHeight + 6, "+12h", 1);
}


