#ifndef SANTA_TRACKER_H
#define SANTA_TRACKER_H

#include <Arduino.h>
#include <M5GFX.h>

class EarthRenderer;

enum class SantaPhase : uint8_t {
    INACTIVE = 0,       // 1月1日 ~ 11月30日 (非12月，完全静默)
    PREPARATION,        // 12月1日 ~ 12月23日 (北极基地预热，飞行准备)
    MISSION_ACTIVE,     // 12月24日 ~ 12月25日 (环球飞行追踪与礼物投递)
    MISSION_COMPLETE    // 12月26日 ~ 12月31日 (任务圆满完成)
};

struct SantaTelemetry {
    double lat;         // 当前纬度 (-90 ~ +90)
    double lon;         // 当前经度 (-180 ~ +180)
    double altKm;       // 巡航对地高度 (450km)
    float headingDeg;   // 飞行航向角 (0~360度，正北为0，顺时针)
    float distToUserKm; // 距离用户当前地面定位的大圆距离 (km)
    bool isVisible;     // 是否位于地球仪正面半球
    int screenX;        // 屏幕投影像素 X
    int screenY;        // 屏幕投影像素 Y
};

class SantaTracker {
public:
    static void init();

    /**
     * @brief 每帧在主循环中更新时间与雪橇位置
     * @param continuousSimTime 当前连续模拟时间 (Unix 时间戳，含亚秒)
     * @param userLat 用户地面纬度
     * @param userLon 用户地面经度
     * @param isUserLocationValid 用户定位是否有效
     */
    static void update(double continuousSimTime, double userLat, double userLon, bool isUserLocationValid);

    // 状态查询
    static SantaPhase getPhase();
    static bool isDecember();
    static bool isMissionActive();
    static inline bool isFlightPhase() { return isMissionActive(); }
    static const SantaTelemetry& getTelemetry();
    static void getCurrentCoord(float& outLat, float& outLon);
    static bool isDeliveringGift();
    static float getDeliveryProgress(); // 0.0f ~ 1.0f

    /**
     * @brief 在 3D 地球仪上绘制圣诞雪橇、尾迹与礼物投递动画
     */
    static void drawOnGlobe(EarthRenderer* renderer, LGFX_Sprite* canvas,
                            double centerLat, double centerLon,
                            double userLat, double userLon,
                            bool isSatViewMode, bool isRadarMode);

    /**
     * @brief 绘制雷达模式专属扫描与目标锁定框
     */
    static void drawRadarOverlay(LGFX_Sprite* canvas, int centerX, int centerY, int radius, uint32_t nowMs);

    // 虚拟百科词条常数 (enum 杜绝 odr-use 产生外部符号引用)
    enum : int {
        VIRTUAL_NORAD_ID = 99999,
        VIRTUAL_ENC_INDEX = 9999
    };

private:
    static SantaPhase s_phase;
    static SantaTelemetry s_telemetry;
    static int16_t s_simYear;
    static uint8_t s_simMonth;
    static uint8_t s_simDay;
    static uint8_t s_simHour;
    static uint8_t s_simMinute;
    static uint8_t s_simSecond;

    // 礼物投递与去重状态
    static int16_t s_deliveredYear;
    static float s_deliveredUserLat;
    static float s_deliveredUserLon;
    static uint32_t s_deliveryAnimStartMs;
    static bool s_isDelivering;
    static bool s_deliveryArmed;

    // 鲁道夫红鼻子实时屏幕像素坐标 (供雷达瞄准与追焦方块锁定)
    static int s_rudolphScreenX;
    static int s_rudolphScreenY;

    // 北极基地电文弹窗
    static int16_t s_prepRadioYear;

    // 大圆插值与距离计算工具
    static void computeInterpolatedPosition(double continuousSimTime, double userLat, double userLon, bool userValid);
    static double calculateHaversineKm(double lat1, double lon1, double lat2, double lon2);
};

#endif // SANTA_TRACKER_H

