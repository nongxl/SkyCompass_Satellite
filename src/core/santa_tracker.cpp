#include "santa_tracker.h"
#include "earth_renderer.h"
#include "i18n.h"
#include <time.h>
#include <math.h>

SantaPhase SantaTracker::s_phase = SantaPhase::INACTIVE;
SantaTelemetry SantaTracker::s_telemetry = {0.0, 0.0, 450.0, 0.0f, 99999.0f, false, -1, -1};
int16_t SantaTracker::s_simYear = 2026;
uint8_t SantaTracker::s_simMonth = 1;
uint8_t SantaTracker::s_simDay = 1;
uint8_t SantaTracker::s_simHour = 0;
uint8_t SantaTracker::s_simMinute = 0;
uint8_t SantaTracker::s_simSecond = 0;

int16_t SantaTracker::s_deliveredYear = 0;
float SantaTracker::s_deliveredUserLat = 999.0f;
float SantaTracker::s_deliveredUserLon = 999.0f;
uint32_t SantaTracker::s_deliveryAnimStartMs = 0;
bool SantaTracker::s_isDelivering = false;
bool SantaTracker::s_deliveryArmed = true;
bool SantaTracker::s_hasDeliveredPresent = false;
bool SantaTracker::s_showDeliveryBanner = false;
int16_t SantaTracker::s_prepRadioYear = 0;
int SantaTracker::s_rudolphScreenX = -100;
int SantaTracker::s_rudolphScreenY = -100;

// 24 个全球环球巡游基础航点 (按自东向西太阳时区推进)
struct Waypoint {
    float lat;
    float lon;
};

static const Waypoint s_globalWaypoints[24] = {
    { 90.0f,    0.0f },    // 0: 北极起点 (North Pole Launch)
    { -36.85f, 174.76f },  // 1: 新西兰奥克兰 (Auckland)
    { -33.87f, 151.21f },  // 2: 澳大利亚悉尼 (Sydney)
    { 35.68f,  139.77f },  // 3: 日本东京 (Tokyo)
    { 37.56f,  126.97f },  // 4: 韩国首尔 (Seoul)
    { 39.90f,  116.40f },  // 5: 中国北京 (Beijing)
    { 22.82f,  108.32f },  // 6: 中国华南/南宁 (Nanning)
    { 13.75f,  100.50f },  // 7: 泰国曼谷 (Bangkok)
    { 28.61f,   77.20f },  // 8: 印度新德里 (New Delhi)
    { 25.20f,   55.27f },  // 9: 阿联酋迪拜 (Dubai)
    { -1.29f,   36.82f },  // 10: 肯尼亚内罗毕 (Nairobi)
    { -33.92f,  18.42f },  // 11: 南非开普敦 (Cape Town)
    { 30.04f,   31.23f },  // 12: 埃及开罗 (Cairo)
    { 55.75f,   37.62f },  // 13: 俄罗斯莫斯科 (Moscow)
    { 52.52f,   13.40f },  // 14: 德国柏林 (Berlin)
    { 48.85f,    2.35f },  // 15: 法国巴黎 (Paris)
    { 51.50f,   -0.12f },  // 16: 英国伦敦 (London)
    { 64.14f,  -21.94f },  // 17: 冰岛雷克雅未克 (Reykjavik)
    { 40.71f,  -74.00f },  // 18: 美国纽约 (New York)
    { 41.87f,  -87.62f },  // 19: 美国芝加哥 (Chicago)
    { 34.05f, -118.24f },  // 20: 美国洛杉矶 (Los Angeles)
    { -22.90f, -43.17f },  // 21: 巴西里约热内卢 (Rio de Janeiro)
    { 21.30f, -157.85f },  // 22: 太平洋夏威夷檀香山 (Honolulu)
    { 90.0f,    0.0f }     // 23: 北极终点 (Return to North Pole)
};

void SantaTracker::init() {
    s_phase = SantaPhase::INACTIVE;
    s_deliveredYear = 0;
    s_deliveryAnimStartMs = 0;
    s_isDelivering = false;
    s_deliveryArmed = true;
    s_prepRadioYear = 0;
}

double SantaTracker::calculateHaversineKm(double lat1, double lon1, double lat2, double lon2) {
    double dLat = (lat2 - lat1) * DEG_TO_RAD;
    double dLon = (lon2 - lon1) * DEG_TO_RAD;
    double a = sin(dLat / 2.0) * sin(dLat / 2.0) +
               cos(lat1 * DEG_TO_RAD) * cos(lat2 * DEG_TO_RAD) *
               sin(dLon / 2.0) * sin(dLon / 2.0);
    double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
    return 6371.0 * c;
}

void SantaTracker::update(double continuousSimTime, double userLat, double userLon, bool isUserLocationValid) {
    time_t rawTime = (time_t)continuousSimTime;
    struct tm t;
    gmtime_r(&rawTime, &t);

    s_simYear = (int16_t)(t.tm_year + 1900);
    s_simMonth = (uint8_t)(t.tm_mon + 1);
    s_simDay = (uint8_t)t.tm_mday;
    s_simHour = (uint8_t)t.tm_hour;
    s_simMinute = (uint8_t)t.tm_min;
    s_simSecond = (uint8_t)t.tm_sec;

    // 状态判定
    if (s_simMonth != 12) {
        s_phase = SantaPhase::INACTIVE;
        s_isDelivering = false;
        return;
    }

    if (s_simDay < 24) {
        s_phase = SantaPhase::PREPARATION;
        s_isDelivering = false;
    } else if (s_simDay == 24 || s_simDay == 25) {
        s_phase = SantaPhase::MISSION_ACTIVE;
    } else {
        s_phase = SantaPhase::MISSION_COMPLETE;
        s_isDelivering = false;
    }

    // 若进入活跃巡游期，解算雪橇位置与大圆距离
    if (s_phase == SantaPhase::MISSION_ACTIVE) {
        computeInterpolatedPosition(continuousSimTime, userLat, userLon, isUserLocationValid);

        // 礼物投递事件检测
        if (isUserLocationValid && userLat <= 90.0) {
            double dist = calculateHaversineKm(s_telemetry.lat, s_telemetry.lon, userLat, userLon);
            s_telemetry.distToUserKm = (float)dist;

            // 触发阈值 800km：接近时触发
            if (dist <= 800.0) {
                if (!s_isDelivering && (s_deliveredYear != s_simYear || s_deliveryArmed)) {
                    s_deliveredYear = s_simYear;
                    s_deliveryArmed = false;
                    s_deliveredUserLat = (float)userLat;
                    s_deliveredUserLon = (float)userLon;
                    s_deliveryAnimStartMs = millis();
                    s_isDelivering = true;
                    s_hasDeliveredPresent = true;
                    s_showDeliveryBanner = true;
                }
            } else if (dist > 1200.0) {
                // 飞离 1200km 之外，重新准备好下一次飞经触发，便于测试
                s_deliveryArmed = true;
            }
        } else {
            s_telemetry.distToUserKm = 99999.0f;
        }
    } else {
        s_telemetry.distToUserKm = 99999.0f;
    }

    // 投递动画持续 6.5 秒后自动平滑结束
    if (s_isDelivering) {
        if (millis() - s_deliveryAnimStartMs >= 6500) {
            s_isDelivering = false;
        }
    }
}

void SantaTracker::computeInterpolatedPosition(double continuousSimTime, double userLat, double userLon, bool userValid) {
    // 12月24日 00:00:00 (UTC) 至 12月25日 23:59:59 (UTC) 共 48 小时
    struct tm startTm = {0};
    startTm.tm_year = s_simYear - 1900;
    startTm.tm_mon = 11; // 12月
    startTm.tm_mday = 24;
    startTm.tm_hour = 0;
    startTm.tm_min = 0;
    startTm.tm_sec = 0;
    time_t tStart = mktime(&startTm);
    double secFromStart = continuousSimTime - (double)tStart;
    double totalSec = 48.0 * 3600.0; // 172800 秒

    double u = secFromStart / totalSec;
    if (u < 0.0) u = 0.0;
    if (u > 1.0) u = 1.0;

    int totalSegments = 23;
    double segProg = u * totalSegments;
    int segIdx = (int)segProg;
    if (segIdx >= totalSegments) segIdx = totalSegments - 1;
    float localT = (float)(segProg - segIdx);

    Waypoint wp1 = s_globalWaypoints[segIdx];
    Waypoint wp2 = s_globalWaypoints[segIdx + 1];

    // 若用户定位有效，动态将用户坐标织入经度最为接近的航段中，确保雪橇飞经用户头顶
    if (userValid && userLat <= 90.0) {
        float userNormLon = (float)userLon;
        float dLon1 = fabs(wp1.lon - userNormLon);
        if (dLon1 > 180.0f) dLon1 = 360.0f - dLon1;
        float dLon2 = fabs(wp2.lon - userNormLon);
        if (dLon2 > 180.0f) dLon2 = 360.0f - dLon2;

        if (dLon1 < 45.0f && dLon2 < 45.0f) {
            // 在此航段正中动态插入用户经纬度插值权重
            float bellWeight = sinf(localT * (float)M_PI);
            wp1.lat = wp1.lat * (1.0f - bellWeight * 0.45f) + (float)userLat * (bellWeight * 0.45f);
            wp2.lat = wp2.lat * (1.0f - bellWeight * 0.45f) + (float)userLat * (bellWeight * 0.45f);
        }
    }

    // 球面大圆 Slerp 插值
    float lat1R = wp1.lat * DEG_TO_RAD;
    float lon1R = wp1.lon * DEG_TO_RAD;
    float lat2R = wp2.lat * DEG_TO_RAD;
    float lon2R = wp2.lon * DEG_TO_RAD;

    float x1 = cosf(lat1R) * cosf(lon1R);
    float y1 = cosf(lat1R) * sinf(lon1R);
    float z1 = sinf(lat1R);

    float x2 = cosf(lat2R) * cosf(lon2R);
    float y2 = cosf(lat2R) * sinf(lon2R);
    float z2 = sinf(lat2R);

    float dot = x1 * x2 + y1 * y2 + z1 * z2;
    if (dot > 0.9999f) dot = 0.9999f;
    if (dot < -0.9999f) dot = -0.9999f;
    float omega = acosf(dot);
    float sinOmega = sinf(omega);

    float cx, cy, cz;
    if (sinOmega > 1e-4f) {
        float scale1 = sinf((1.0f - localT) * omega) / sinOmega;
        float scale2 = sinf(localT * omega) / sinOmega;
        cx = scale1 * x1 + scale2 * x2;
        cy = scale1 * y1 + scale2 * y2;
        cz = scale1 * z1 + scale2 * z2;
    } else {
        cx = x1 * (1.0f - localT) + x2 * localT;
        cy = y1 * (1.0f - localT) + y2 * localT;
        cz = z1 * (1.0f - localT) + z2 * localT;
    }

    float norm = sqrtf(cx * cx + cy * cy + cz * cz);
    if (norm > 1e-6f) {
        cx /= norm; cy /= norm; cz /= norm;
    }

    float curLat = asinf(cz) * RAD_TO_DEG;
    float curLon = atan2f(cy, cx) * RAD_TO_DEG;

    s_telemetry.lat = curLat;
    s_telemetry.lon = curLon;
    s_telemetry.altKm = 450.0;

    // 计算飞行航向角
    float dLonH = (wp2.lon - curLon) * DEG_TO_RAD;
    float yH = sinf(dLonH) * cosf(wp2.lat * DEG_TO_RAD);
    float xH = cosf(curLat * DEG_TO_RAD) * sinf(wp2.lat * DEG_TO_RAD) -
               sinf(curLat * DEG_TO_RAD) * cosf(wp2.lat * DEG_TO_RAD) * cosf(dLonH);
    float heading = atan2f(yH, xH) * RAD_TO_DEG;
    if (heading < 0.0f) heading += 360.0f;
    s_telemetry.headingDeg = heading;
}

SantaPhase SantaTracker::getPhase() { return s_phase; }
bool SantaTracker::isDecember() { return (s_simMonth == 12); }
bool SantaTracker::isMissionActive() { return (s_phase == SantaPhase::MISSION_ACTIVE); }
const SantaTelemetry& SantaTracker::getTelemetry() { return s_telemetry; }
void SantaTracker::getCurrentCoord(float& outLat, float& outLon) {
    outLat = (float)s_telemetry.lat;
    outLon = (float)s_telemetry.lon;
}
bool SantaTracker::isDeliveringGift() { return s_isDelivering; }
bool SantaTracker::isDeliveryBannerActive() { return s_showDeliveryBanner; }
void SantaTracker::dismissDeliveryBanner() { s_showDeliveryBanner = false; }
float SantaTracker::getDeliveryProgress() {
    if (!s_isDelivering) return 0.0f;
    uint32_t elapsed = millis() - s_deliveryAnimStartMs;
    float prog = (float)elapsed / 4500.0f;
    return (prog > 1.0f) ? 1.0f : prog;
}

void SantaTracker::drawOnGlobe(EarthRenderer* renderer, LGFX_Sprite* canvas,
                               double centerLat, double centerLon,
                               double userLat, double userLon,
                               bool isSatViewMode, bool isRadarMode) {
    if (!renderer || !canvas || s_phase == SantaPhase::INACTIVE) return;

    uint32_t nowMs = millis();

    // 1. 北极母港基地 (12月1日 ~ 12月31日 持续驻留显示)
    int nx, ny;
    if (renderer->projectOrthographic(90.0, 0.0, 0.0, centerLat, centerLon, nx, ny)) {
        // 北极控制中心脉冲信标
        if (s_phase == SantaPhase::PREPARATION) {
            uint32_t beaconT = nowMs % 1500;
            float bProg = (float)beaconT / 1500.0f;
            int rRing = 3 + (int)(bProg * 12.0f);
            uint8_t aRing = (uint8_t)(255 * (1.0f - bProg));
            if (aRing > 10) {
                canvas->drawCircle(nx, ny, rRing, canvas->color565(aRing, (uint8_t)(aRing * 0.85f), 0));
            }
        }
        canvas->fillCircle(nx, ny, 3, (s_phase == SantaPhase::MISSION_COMPLETE) ? canvas->color565(0, 255, 200) : canvas->color565(255, 215, 0));
        canvas->drawPixel(nx, ny, TFT_WHITE);

        // 标注多语言文字 (增加纵向间隔至 14px，避免行间重叠)
        canvas->setTextDatum(top_left);
        canvas->setTextColor((s_phase == SantaPhase::MISSION_COMPLETE) ? canvas->color565(0, 255, 200) : canvas->color565(255, 215, 0));
        canvas->drawString(I18N::get(TXT_SANTA_NORTH_POLE_BASE), nx + 7, ny - 8);
        canvas->setTextColor(canvas->color565(180, 220, 255));
        if (s_phase == SantaPhase::PREPARATION) {
            canvas->drawString(I18N::get(TXT_SANTA_STATUS_PREP), nx + 7, ny + 6);
        } else if (s_phase == SantaPhase::MISSION_ACTIVE) {
            canvas->drawString(I18N::get(TXT_SANTA_STATUS_FLYING), nx + 7, ny + 6);
        } else {
            canvas->drawString(I18N::get(TXT_SANTA_STATUS_RESTING), nx + 7, ny + 6);
        }
    }

    // 每次时间切入 12 月时在屏幕顶部浮现无线电通告横幅 (显示 5.5 秒)
    static uint8_t s_lastSeenMonth = 0;
    static uint32_t s_prepBannerStart = 0;
    if (s_simMonth == 12 && s_lastSeenMonth != 12) {
        s_prepBannerStart = nowMs;
    }
    s_lastSeenMonth = s_simMonth;

    if (s_prepBannerStart > 0 && (nowMs - s_prepBannerStart < 5500)) {
        canvas->fillRect(0, 0, 240, 15, canvas->color565(20, 35, 25));
        canvas->drawFastHLine(0, 14, 240, canvas->color565(255, 215, 0));
        canvas->setTextDatum(middle_center);
        canvas->setTextColor(canvas->color565(255, 215, 0));
        canvas->drawString(I18N::get(TXT_SANTA_PREP_TITLE), 120, 7);
    }

    // 非活跃飞行期直接返回
    if (s_phase != SantaPhase::MISSION_ACTIVE) return;

    // 2. 环球雪橇飞行与护航战机伴飞 (12月24日 ~ 25日)
    int sx = -1, sy = -1;
    bool visible = renderer->projectOrthographic(s_telemetry.lat, s_telemetry.lon, s_telemetry.altKm, centerLat, centerLon, sx, sy);
    s_telemetry.isVisible = visible;
    s_telemetry.screenX = sx;
    s_telemetry.screenY = sy;

    if (visible && sx >= -30 && sx <= canvas->width() + 30 && sy >= -30 && sy <= canvas->height() + 30) {
        // 计算航向单位矢量与侧向垂直矢量
        float headingRad = s_telemetry.headingDeg * DEG_TO_RAD;
        float fwdX = sinf(headingRad);
        float fwdY = -cosf(headingRad);
        float rgtX = -fwdY;
        float rgtY = fwdX;

        // (1) 金色与彩虹星尘尾迹 (Stardust Trail, 向后流光)
        for (int p = 1; p <= 7; p++) {
            int tx = sx - (int)(fwdX * p * 3.5f) + ((nowMs / 70 + p) % 3 - 1);
            int ty = sy - (int)(fwdY * p * 3.5f) + ((nowMs / 90 + p * 2) % 3 - 1);
            uint16_t dustCol = (p <= 2) ? canvas->color565(255, 255, 200) :
                               (p <= 4) ? canvas->color565(255, 215, 0) :
                                          canvas->color565(255, 140, 50);
            canvas->drawPixel(tx, ty, dustCol);
            if (p <= 3) canvas->drawPixel(tx + 1, ty, canvas->color565(255, 230, 100));
        }

        // (2) NORAD 军机护航编队 (双架喷气僚机，在雪橇后方左右两翼伴飞)
        auto drawEscortJet = [&](int jx, int jy, bool isLeft) {
            // 引擎喷气白色双尾流
            int jetTailX = jx - (int)(fwdX * 6.0f);
            int jetTailY = jy - (int)(fwdY * 6.0f);
            canvas->drawLine(jx, jy, jetTailX, jetTailY, canvas->color565(210, 230, 250));

            // 三角翼空优隐身战斗机身
            uint16_t colJet = canvas->color565(160, 175, 190);
            uint16_t colCanopy = canvas->color565(40, 90, 140);
            int jNoseX = jx + (int)(fwdX * 5.0f);
            int jNoseY = jy + (int)(fwdY * 5.0f);
            int leftWingX = jx - (int)(fwdX * 2.0f) - (int)(rgtX * 4.0f);
            int leftWingY = jy - (int)(fwdY * 2.0f) - (int)(rgtY * 4.0f);
            int rightWingX = jx - (int)(fwdX * 2.0f) + (int)(rgtX * 4.0f);
            int rightWingY = jy - (int)(fwdY * 2.0f) + (int)(rgtY * 4.0f);

            canvas->fillTriangle(jNoseX, jNoseY, leftWingX, leftWingY, rightWingX, rightWingY, colJet);
            canvas->drawPixel(jx, jy, colCanopy);

            // 翼尖防撞航行灯 (左红右绿高频闪烁)
            uint8_t navBlink = (nowMs / 220) % 2;
            if (navBlink) {
                canvas->drawPixel(leftWingX, leftWingY, TFT_RED);
                canvas->drawPixel(rightWingX, rightWingY, TFT_GREEN);
            }
        };

        // 左右僚机位置 (拉开间距，斜后方 18px，左右偏移 16px，避免遮挡雪橇本体)
        int jetLeftX = sx - (int)(fwdX * 18.0f) - (int)(rgtX * 16.0f);
        int jetLeftY = sy - (int)(fwdY * 18.0f) - (int)(rgtY * 16.0f);
        int jetRightX = sx - (int)(fwdX * 18.0f) + (int)(rgtX * 16.0f);
        int jetRightY = sy - (int)(fwdY * 18.0f) + (int)(rgtY * 16.0f);
        drawEscortJet(jetLeftX, jetLeftY, true);
        drawEscortJet(jetRightX, jetRightY, false);

        // (3) 高辨识度像素艺术：领航驯鹿队与鲁道夫大红鼻子 (Reindeer & Rudolph)
        int dir = (fwdX >= -0.1f) ? 1 : -1; // 飞行水平朝向 (1: 朝右飞, -1: 朝左飞)
        int deerBaseX = sx + dir * 18;
        int deerBaseY = sy - 2;

        uint16_t colDeer = canvas->color565(175, 95, 45);
        uint16_t colDeerDark = canvas->color565(120, 60, 25);
        uint16_t colAntler = canvas->color565(255, 220, 100);

        // 驯鹿飞跃身形 (鹿身 7x4，飞奔蹄腿)
        canvas->fillRect(deerBaseX - dir * 3, deerBaseY - 1, 7, 4, colDeer);
        canvas->drawFastHLine(deerBaseX - dir * 2, deerBaseY + 3, 5, colDeerDark); // 腹部阴影
        // 飞跃前蹄与后蹬后腿
        canvas->drawLine(deerBaseX + dir * 3, deerBaseY + 2, deerBaseX + dir * 6, deerBaseY + 5, colDeerDark);
        canvas->drawLine(deerBaseX - dir * 3, deerBaseY + 2, deerBaseX - dir * 6, deerBaseY + 5, colDeerDark);

        // 鹿颈与鹿头 (向上向前昂扬)
        canvas->fillRect(deerBaseX + dir * 2, deerBaseY - 4, 3, 4, colDeer);
        canvas->fillRect(deerBaseX + dir * 4, deerBaseY - 3, 3, 3, colDeer);
        canvas->drawPixel(deerBaseX + dir * 3, deerBaseY, canvas->color565(255, 215, 0)); // 金色铃铛圈

        // 挺拔分叉金色鹿角 (Antlers)
        canvas->drawFastVLine(deerBaseX + dir * 2, deerBaseY - 7, 4, colAntler);
        canvas->drawPixel(deerBaseX + dir * 1, deerBaseY - 6, colAntler);
        canvas->drawPixel(deerBaseX + dir * 3, deerBaseY - 8, colAntler);
        canvas->drawPixel(deerBaseX, deerBaseY - 7, canvas->color565(255, 240, 180));

        // ★ 核心焦点：鲁道夫发光大红鼻子 (Shiny Red Nose)
        int noseX = deerBaseX + dir * 8;
        int noseY = deerBaseY - 2;
        s_rudolphScreenX = noseX;
        s_rudolphScreenY = noseY;

        uint8_t noseBlink = (nowMs / 140) % 2;
        canvas->fillCircle(noseX, noseY, 2, canvas->color565(255, 25, 25));
        canvas->drawPixel(noseX, noseY, TFT_WHITE); // 中心白炽闪烁核心
        if (noseBlink) {
            // 鲜亮红粉色呼吸外光晕
            canvas->drawPixel(noseX - 2, noseY, canvas->color565(255, 120, 120));
            canvas->drawPixel(noseX + 2, noseY, canvas->color565(255, 120, 120));
            canvas->drawPixel(noseX, noseY - 2, canvas->color565(255, 120, 120));
            canvas->drawPixel(noseX, noseY + 2, canvas->color565(255, 120, 120));
        }

        // 双道金黄魔法牵引缰绳
        uint16_t colReins = canvas->color565(255, 225, 80);
        canvas->drawLine(sx + dir * 6, sy, deerBaseX + dir * 2, deerBaseY - 1, colReins);
        canvas->drawLine(sx + dir * 6, sy + 2, deerBaseX + dir * 1, deerBaseY + 1, colReins);

        // (4) 经典大号圣诞红金雪橇与圣诞老人 (Sleigh & Santa)
        uint16_t colSleigh = canvas->color565(225, 20, 20);
        uint16_t colSleighDark = canvas->color565(160, 10, 10);
        uint16_t colRunner = canvas->color565(255, 215, 0);

        // 金色弯曲滑轨 (底部平直长 14px，前端高高优雅向上翘起，后部微翘)
        int runnerBaseY = sy + 4;
        canvas->drawFastHLine(sx - dir * 6, runnerBaseY, 13, colRunner);
        canvas->drawPixel(sx + dir * 7, runnerBaseY - 1, colRunner);
        canvas->drawPixel(sx + dir * 8, runnerBaseY - 2, colRunner);
        canvas->drawPixel(sx + dir * 9, runnerBaseY - 3, colRunner); // 翘头弧度
        canvas->drawPixel(sx - dir * 7, runnerBaseY - 1, colRunner); // 后微翘
        // 滑轨支撑金属斜柱
        canvas->drawFastVLine(sx - dir * 3, runnerBaseY - 2, 2, colRunner);
        canvas->drawFastVLine(sx + dir * 3, runnerBaseY - 2, 2, colRunner);

        // 红色饱满车体斗 (前低后高流线型)
        canvas->fillRect(sx - dir * 5, sy - 1, 11, 4, colSleigh);
        canvas->drawFastHLine(sx - dir * 5, sy + 3, 11, colSleighDark); // 车底阴影
        canvas->drawFastHLine(sx - dir * 4, sy - 2, 9, canvas->color565(255, 220, 100)); // 金色护栏滚边
        // 车后升起的高靠背
        canvas->drawFastVLine(sx - dir * 6, sy - 3, 4, colSleigh);

        // 后座堆满的五彩礼物包裹 (Sack of Gifts)
        canvas->fillRect(sx - dir * 5, sy - 4, 4, 3, canvas->color565(35, 155, 55)); // 绿色大礼包
        canvas->drawPixel(sx - dir * 3, sy - 5, canvas->color565(255, 215, 0));      // 金色十字结
        canvas->fillRect(sx - dir * 4, sy - 6, 3, 2, canvas->color565(30, 180, 240)); // 天蓝色礼盒
        canvas->drawPixel(sx - dir * 3, sy - 7, TFT_WHITE);                         // 礼盒白色蝴蝶结

        // 端坐在驾驶席的圣诞老人 (Santa Claus)
        // 红大衣身躯
        canvas->fillRect(sx - dir * 1, sy - 3, 4, 3, colSleigh);
        canvas->drawFastHLine(sx - dir * 1, sy - 1, 4, TFT_BLACK); // 黑色腰带
        canvas->drawPixel(sx, sy - 1, colRunner);                  // 金色皮带扣
        // 桃红脸颊
        canvas->drawPixel(sx + dir * 1, sy - 4, canvas->color565(255, 200, 180));
        // 经典超大蓬松白胡子 (包裹下巴与胸口，极显眼)
        canvas->fillRect(sx + dir * 1, sy - 3, 3, 2, TFT_WHITE);
        canvas->drawPixel(sx + dir * 2, sy - 1, TFT_WHITE);
        // 经典红色尖顶睡帽 + 白毛绒球
        canvas->drawFastHLine(sx - dir * 1, sy - 5, 4, TFT_WHITE); // 帽檐洁白羊羔绒
        canvas->fillRect(sx - dir * 1, sy - 7, 3, 2, colSleigh);   // 红色帽尖
        canvas->drawPixel(sx - dir * 2, sy - 8, TFT_WHITE);        // 垂下的白毛绒球
        // 戴白手套向上挥动招手
        canvas->drawPixel(sx + dir * 3, sy - 4, TFT_WHITE);

        // (5) 目标标识文字：单行精简标注，根据飞行方向与屏幕位置智能侧向避让，绝不遮挡雪橇本体与驯鹿红鼻子
        int textX = (dir > 0) ? (sx - 12) : (sx + 12);
        int textY = (sy < 40) ? (sy + 10) : (sy - 15);
        canvas->setTextDatum((dir > 0) ? top_right : top_left);
        canvas->setTextColor(canvas->color565(255, 80, 80));
        canvas->drawString("SNT-CLS", textX, textY);
    }

    // 3. 礼物投递伞降动画与地表永久留存 (用户未重启前礼物永久留存地表)
    if (s_hasDeliveredPresent && s_deliveredUserLat <= 90.0) {
        int ux, uy;
        if (renderer->projectOrthographic(s_deliveredUserLat, s_deliveredUserLon, 0.0, centerLat, centerLon, ux, uy)) {
            if (s_isDelivering) {
                float prog = getDeliveryProgress(); // 0.0 ~ 1.0

                // 礼物下落起始点与终点插值
                int startX = (sx > 0 && visible) ? sx : ux;
                int startY = (sy > 0 && visible) ? sy : (uy - 55);
                int curGiftX = startX + (int)((ux - startX) * prog);
                int curGiftY = startY + (int)((uy - startY) * prog);

                // 白色与红条纹大号降落伞 (Parachute, 半径 6px 饱满半圆)
                if (prog < 0.92f) {
                    canvas->fillCircle(curGiftX, curGiftY - 8, 6, TFT_WHITE);
                    canvas->fillCircle(curGiftX, curGiftY - 7, 5, canvas->color565(220, 220, 220));
                    canvas->drawFastHLine(curGiftX - 6, curGiftY - 8, 13, canvas->color565(255, 50, 50));
                    // 4 根细吊索
                    canvas->drawLine(curGiftX - 5, curGiftY - 7, curGiftX - 2, curGiftY - 2, canvas->color565(180, 180, 180));
                    canvas->drawLine(curGiftX + 5, curGiftY - 7, curGiftX + 2, curGiftY - 2, canvas->color565(180, 180, 180));
                    canvas->drawLine(curGiftX - 2, curGiftY - 6, curGiftX - 1, curGiftY - 2, canvas->color565(180, 180, 180));
                    canvas->drawLine(curGiftX + 2, curGiftY - 6, curGiftX + 1, curGiftY - 2, canvas->color565(180, 180, 180));
                }

                // 大号红色礼物盒 + 金色缎带十字结 (Gift Box, 7x7 像素)
                canvas->fillRect(curGiftX - 3, curGiftY - 2, 7, 7, canvas->color565(230, 30, 30));
                canvas->drawFastHLine(curGiftX - 3, curGiftY + 1, 7, canvas->color565(255, 215, 0));
                canvas->drawFastVLine(curGiftX, curGiftY - 2, 7, canvas->color565(255, 215, 0));
                canvas->drawPixel(curGiftX, curGiftY - 3, canvas->color565(255, 230, 100)); // 顶部蝴蝶结

                // 着陆时在用户地表激发出礼花光环
                if (prog >= 0.75f) {
                    float landProg = (prog - 0.75f) / 0.25f;
                    int rLand = 3 + (int)(landProg * 18.0f);
                    uint8_t aLand = (uint8_t)(255 * (1.0f - landProg));
                    canvas->drawCircle(ux, uy, rLand, canvas->color565(aLand, aLand, 0));
                    canvas->drawCircle(ux, uy, rLand / 2, canvas->color565(0, aLand, aLand));
                }
            } else {
                // 动画已结束：礼物永久留在用户地面位置 (直到设备重启)
                canvas->fillRect(ux - 3, uy - 3, 7, 7, canvas->color565(230, 30, 30));
                canvas->drawFastHLine(ux - 3, uy, 7, canvas->color565(255, 215, 0));
                canvas->drawFastVLine(ux, uy - 3, 7, canvas->color565(255, 215, 0));
                canvas->drawPixel(ux, uy - 4, canvas->color565(255, 230, 100)); // 顶部蝴蝶结
                // 礼物盒周围带有微弱金色节日星光点缀
                if (((nowMs / 600) % 2) == 0) {
                    canvas->drawPixel(ux - 5, uy - 3, canvas->color565(255, 230, 120));
                    canvas->drawPixel(ux + 5, uy + 1, canvas->color565(255, 230, 120));
                }
            }
        }
    }

    // 4. 屏幕顶部投递确认庆贺横幅 (按任意键后消失，不自动消失)
    if (s_showDeliveryBanner) {
        canvas->fillRect(0, 0, 240, 16, canvas->color565(160, 20, 20));
        canvas->drawFastHLine(0, 15, 240, canvas->color565(255, 215, 0));
        canvas->setTextDatum(middle_center);
        canvas->setTextColor(TFT_WHITE);
        canvas->drawString(I18N::get(TXT_SANTA_DELIVERY), 120, 8);
    }
}

void SantaTracker::drawRadarOverlay(LGFX_Sprite* canvas, int centerX, int centerY, int radius, uint32_t nowMs) {
    if (!canvas || radius <= 10) return;

    // 1. 同心雷达测距环 (Range Rings)
    uint16_t colGrid = canvas->color565(0, 70, 20);
    canvas->drawCircle(centerX, centerY, (int)(radius * 0.33f), colGrid);
    canvas->drawCircle(centerX, centerY, (int)(radius * 0.66f), colGrid);
    canvas->drawCircle(centerX, centerY, radius, colGrid);

    // 十字刻度线
    canvas->drawFastHLine(centerX - radius, centerY, radius * 2, colGrid);
    canvas->drawFastVLine(centerX, centerY - radius, radius * 2, colGrid);

    // 2. 旋转的绿磷雷达扫描扇线 (Phosphor Radar Sweep Line)
    // 周期约 3.6 秒一整圈 (角速度 100 deg/s)
    float sweepAngleRad = (float)((nowMs % 3600) / 3600.0f) * 2.0f * (float)M_PI;
    int sweepEndX = centerX + (int)(cosf(sweepAngleRad) * radius);
    int sweepEndY = centerY + (int)(sinf(sweepAngleRad) * radius);
    canvas->drawLine(centerX, centerY, sweepEndX, sweepEndY, canvas->color565(50, 255, 80));

    // 3. 若雪橇在正面，绘制雷达目标锁定框 (准星精确锁定鲁道夫发光红鼻子)
    if (s_telemetry.isVisible && s_phase == SantaPhase::MISSION_ACTIVE) {
        int tx = (s_rudolphScreenX >= -10 && s_rudolphScreenX < canvas->width() + 10) ? s_rudolphScreenX : s_telemetry.screenX;
        int ty = (s_rudolphScreenY >= -10 && s_rudolphScreenY < canvas->height() + 10) ? s_rudolphScreenY : s_telemetry.screenY;
        if (tx >= 0 && tx < canvas->width() && ty >= 0 && ty < canvas->height()) {
            uint16_t colLock = ((nowMs / 200) % 2) ? canvas->color565(80, 255, 100) : canvas->color565(20, 160, 40);
            canvas->drawRect(tx - 7, ty - 7, 15, 15, colLock);
            canvas->drawFastHLine(tx - 11, ty, 4, colLock);
            canvas->drawFastHLine(tx + 8, ty, 4, colLock);
            canvas->drawFastVLine(tx, ty - 11, 4, colLock);
            canvas->drawFastVLine(tx, ty + 8, 4, colLock);
            canvas->setTextDatum(top_left);
            canvas->setTextColor(colLock);
            canvas->drawString("TRK: RUDOLPH", tx + 9, ty - 10);
        }
    }
}

