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
int16_t SantaTracker::s_prepRadioYear = 0;

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

            // 触发阈值 650km
            if (dist <= 650.0) {
                if (s_deliveredYear != s_simYear) {
                    s_deliveredYear = s_simYear;
                    s_deliveredUserLat = (float)userLat;
                    s_deliveredUserLon = (float)userLon;
                    s_deliveryAnimStartMs = millis();
                    s_isDelivering = true;
                }
            }
        } else {
            s_telemetry.distToUserKm = 99999.0f;
        }
    } else {
        s_telemetry.distToUserKm = 99999.0f;
    }

    // 投递动画持续 4.5 秒后自动平滑结束
    if (s_isDelivering) {
        if (millis() - s_deliveryAnimStartMs >= 4500) {
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

    // 阶段 1: 12月1日 ~ 23日 北极基地预热 (Preparation)
    if (s_phase == SantaPhase::PREPARATION) {
        int nx, ny;
        if (renderer->projectOrthographic(90.0, 0.0, 0.0, centerLat, centerLon, nx, ny)) {
            // 北极控制中心脉冲信标
            uint32_t beaconT = nowMs % 1500;
            float bProg = (float)beaconT / 1500.0f;
            int rRing = 3 + (int)(bProg * 12.0f);
            uint8_t aRing = (uint8_t)(255 * (1.0f - bProg));
            if (aRing > 10) {
                canvas->drawCircle(nx, ny, rRing, canvas->color565(aRing, (uint8_t)(aRing * 0.85f), 0));
            }
            canvas->fillCircle(nx, ny, 3, canvas->color565(255, 215, 0)); // 金色北极基地核心点
            canvas->drawPixel(nx, ny, TFT_WHITE);

            // 标注文字
            canvas->setTextDatum(top_left);
            canvas->setTextColor(canvas->color565(255, 215, 0));
            canvas->drawString("NORTH POLE BASE", nx + 7, ny - 5);
            canvas->setTextColor(canvas->color565(180, 220, 255));
            canvas->drawString("[PREP ACTIVE]", nx + 7, ny + 3);
        }

        // 首次进入 12 月时在屏幕顶部浮现无线电通告横幅 (显示 5 秒后淡出)
        static uint32_t s_prepBannerStart = 0;
        if (s_prepRadioYear != s_simYear) {
            s_prepRadioYear = s_simYear;
            s_prepBannerStart = nowMs;
        }
        if (s_prepBannerStart > 0 && (nowMs - s_prepBannerStart < 5500)) {
            canvas->fillRect(0, 0, 240, 15, canvas->color565(20, 35, 25));
            canvas->drawFastHLine(0, 14, 240, canvas->color565(255, 215, 0));
            canvas->setTextDatum(middle_center);
            canvas->setTextColor(canvas->color565(255, 215, 0));
            canvas->drawString(I18N::get(TXT_SANTA_PREP_TITLE), 120, 7);
        }
        return;
    }

    // 阶段 3: 12月26日 ~ 31日 任务圆满完成 (Mission Complete)
    if (s_phase == SantaPhase::MISSION_COMPLETE) {
        int nx, ny;
        if (renderer->projectOrthographic(90.0, 0.0, 0.0, centerLat, centerLon, nx, ny)) {
            canvas->fillCircle(nx, ny, 3, canvas->color565(0, 255, 200));
            canvas->drawPixel(nx, ny, TFT_WHITE);
            canvas->setTextDatum(top_left);
            canvas->setTextColor(canvas->color565(0, 255, 200));
            canvas->drawString("NORTH POLE BASE", nx + 7, ny - 5);
            canvas->setTextColor(canvas->color565(255, 215, 0));
            canvas->drawString("MISSION COMPLETE", nx + 7, ny + 3);
        }
        return;
    }

    // 阶段 2: 12月24日 ~ 25日 环球雪橇飞行与礼物投递 (Mission Active)
    int sx = -1, sy = -1;
    bool visible = renderer->projectOrthographic(s_telemetry.lat, s_telemetry.lon, s_telemetry.altKm, centerLat, centerLon, sx, sy);
    s_telemetry.isVisible = visible;
    s_telemetry.screenX = sx;
    s_telemetry.screenY = sy;

    if (visible && sx >= -20 && sx <= canvas->width() + 20 && sy >= -20 && sy <= canvas->height() + 20) {
        // (1) 金色星尘粒子流光尾迹 (Stardust Trail)
        float headingRad = s_telemetry.headingDeg * DEG_TO_RAD;
        float tailDx = -sinf(headingRad);
        float tailDy = cosf(headingRad);

        for (int p = 1; p <= 5; p++) {
            int tx = sx + (int)(tailDx * p * 3.5f) + ((nowMs / 80 + p) % 3 - 1);
            int ty = sy + (int)(tailDy * p * 3.5f) + ((nowMs / 100 + p * 2) % 3 - 1);
            uint16_t dustCol = (p <= 2) ? canvas->color565(255, 255, 180) : canvas->color565(255, (uint8_t)(210 - p * 30), 0);
            canvas->drawPixel(tx, ty, dustCol);
            if (p <= 2) canvas->drawPixel(tx + 1, ty, canvas->color565(255, 215, 0));
        }

        // (2) 红色圣诞雪橇与圣诞老人轮廓 (Sleigh & Santa)
        // 雪橇车身 (红色加深与金色滑道)
        uint16_t colSleigh = canvas->color565(230, 30, 30);
        uint16_t colRunner = canvas->color565(255, 215, 0);
        canvas->fillRect(sx - 3, sy - 1, 6, 3, colSleigh);
        canvas->drawLine(sx - 4, sy + 3, sx + 3, sy + 3, colRunner); // 雪橇滑板
        canvas->drawPixel(sx + 4, sy + 2, colRunner);                 // 翘起的雪橇前头

        // 圣诞老人白胡子与红帽子
        canvas->drawPixel(sx - 1, sy - 2, colSleigh);  // 红帽
        canvas->drawPixel(sx - 2, sy - 3, TFT_WHITE);  // 绒球
        canvas->drawPixel(sx, sy - 1, TFT_WHITE);      // 白胡子

        // (3) 牵引缰绳与领头红鼻子驯鹿 Rudolph (Reindeer & Rudolph)
        int deerX = sx + (int)(-tailDx * 9.0f);
        int deerY = sy + (int)(-tailDy * 9.0f);
        // 金黄色魔力牵引缰绳
        canvas->drawLine(sx + 2, sy, deerX - 2, deerY, canvas->color565(255, 220, 50));

        // 驯鹿身躯 (深褐色/金褐色)
        uint16_t colDeer = canvas->color565(160, 95, 45);
        canvas->fillRect(deerX - 2, deerY - 1, 4, 3, colDeer);
        canvas->drawPixel(deerX + 1, deerY - 2, colDeer);             // 鹿角
        canvas->drawPixel(deerX + 2, deerY - 2, canvas->color565(120, 70, 30));

        // ★ 核心彩蛋：Rudolph 闪烁的红鼻子 (Red Nose Beacon)
        uint8_t noseBlink = (nowMs / 180) % 2;
        canvas->drawPixel(deerX + 3, deerY - 1, noseBlink ? TFT_RED : canvas->color565(255, 120, 120));

        // (4) 目标标识文字
        canvas->setTextDatum(top_left);
        canvas->setTextColor(canvas->color565(255, 70, 70));
        canvas->drawString("SNT-CLS", sx + 10, sy - 5);
        canvas->setTextColor(canvas->color565(255, 215, 0));
        canvas->drawString("RED NOSE", sx + 10, sy + 3);
    }

    // (5) 礼物投递伞降动画与庆贺横幅 (Gift Drop & Delivery Confirmed)
    if (s_isDelivering && userLat <= 90.0) {
        float prog = getDeliveryProgress(); // 0.0 ~ 1.0

        int ux, uy;
        if (renderer->projectOrthographic(userLat, userLon, 0.0, centerLat, centerLon, ux, uy)) {
            // 礼物下落起始点与终点插值
            int startX = (sx > 0 && visible) ? sx : ux;
            int startY = (sy > 0 && visible) ? sy : (uy - 40);
            int curGiftX = startX + (int)((ux - startX) * prog);
            int curGiftY = startY + (int)((uy - startY) * prog);

            // 白色降落伞 (Parachute)
            if (prog < 0.90f) {
                canvas->fillCircle(curGiftX, curGiftY - 6, 4, TFT_WHITE);
                canvas->drawLine(curGiftX - 4, curGiftY - 6, curGiftX - 1, curGiftY - 1, canvas->color565(200, 200, 200));
                canvas->drawLine(curGiftX + 4, curGiftY - 6, curGiftX + 1, curGiftY - 1, canvas->color565(200, 200, 200));
            }

            // 红色礼物盒 + 金色丝带十字结 (Gift Box)
            canvas->fillRect(curGiftX - 2, curGiftY - 2, 5, 5, TFT_RED);
            canvas->drawFastHLine(curGiftX - 2, curGiftY, 5, canvas->color565(255, 215, 0));
            canvas->drawFastVLine(curGiftX, curGiftY - 2, 5, canvas->color565(255, 215, 0));

            // 着陆时在用户地表激发出礼花光环
            if (prog >= 0.80f) {
                float landProg = (prog - 0.80f) / 0.20f;
                int rLand = 3 + (int)(landProg * 14.0f);
                uint8_t aLand = (uint8_t)(255 * (1.0f - landProg));
                canvas->drawCircle(ux, uy, rLand, canvas->color565(aLand, aLand, 0));
            }
        }

        // 屏幕顶部投递确认庆贺横幅
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

    // 3. 若雪橇在正面，绘制雷达目标菱形锁定框 (Target Lock Box)
    if (s_telemetry.isVisible && s_phase == SantaPhase::MISSION_ACTIVE) {
        int tx = s_telemetry.screenX;
        int ty = s_telemetry.screenY;
        if (tx >= 0 && tx < canvas->width() && ty >= 0 && ty < canvas->height()) {
            uint16_t colLock = ((nowMs / 200) % 2) ? canvas->color565(80, 255, 100) : canvas->color565(20, 160, 40);
            canvas->drawRect(tx - 6, ty - 6, 13, 13, colLock);
            canvas->drawFastHLine(tx - 9, ty, 3, colLock);
            canvas->drawFastHLine(tx + 7, ty, 3, colLock);
            canvas->drawFastVLine(tx, ty - 9, 3, colLock);
            canvas->drawFastVLine(tx, ty + 7, 3, colLock);
            canvas->setTextDatum(top_left);
            canvas->setTextColor(colLock);
            canvas->drawString("TRK: SNT-CLS", tx + 8, ty - 9);
        }
    }
}

