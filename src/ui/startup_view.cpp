#include "ui/startup_view.h"
#include <M5GFX.h>
#include "core/earth_renderer.h"
#include "core/attitude_estimator.h"
#include "core/i18n.h"
#include "core/image_utils.h"
#include "core/text_utils.h"
#include "core/trivia_data.h"

extern EarthRenderer* earth_renderer;
extern AttitudeEstimator* attitude;
extern double baseUserLat;
extern double baseUserLon;
extern uint32_t current_unix;
extern void getLoadingStatusText(char* outBuf, size_t maxLen);

static int s_triviaIndex = -1;
static uint32_t s_lastTriviaSwitchTime = 0;

void StartupView::nextTrivia() {
    s_triviaIndex = (s_triviaIndex + 1) % (int)TriviaManager::getCount();
    s_lastTriviaSwitchTime = millis();
}

void StartupView::prevTrivia() {
    if (s_triviaIndex <= 0) s_triviaIndex = (int)TriviaManager::getCount() - 1;
    else s_triviaIndex--;
    s_lastTriviaSwitchTime = millis();
}

void StartupView::setTriviaIndex(int idx) {
    if (idx >= 0 && idx < (int)TriviaManager::getCount()) {
        s_triviaIndex = idx;
        s_lastTriviaSwitchTime = millis();
    }
}

int StartupView::getTriviaIndex() {
    return s_triviaIndex;
}

void StartupView::draw(int progressPercentage, bool showLangSelect, int selectedLangIndex) {
    if (!earth_renderer) return;
    
    static float bootInitialPitch = 0.0f;
    static float bootInitialRoll = 0.0f;
    static bool bootInitialCaptured = false;
    
    float pitch = 0.0f;
    float roll = 0.0f;
    if (attitude) {
        AttitudeData att = attitude->getAttitude();
        pitch = att.pitch;
        roll = att.roll;
        if (!bootInitialCaptured && (fabs(pitch) > 0.01f || fabs(roll) > 0.01f || progressPercentage >= 10)) {
            bootInitialPitch = pitch;
            bootInitialRoll = roll;
            bootInitialCaptured = true;
        }
    }
    
    // 开机动态交互：以开机初识手持角度为基准，随手持转动/倾斜实时呈现 3D 地球仪动态转动
    float dPitch = bootInitialCaptured ? (pitch - bootInitialPitch) : 0.0f;
    float dRoll = bootInitialCaptured ? (roll - bootInitialRoll) : 0.0f;
    
    double viewLat = baseUserLat - dPitch;
    double viewLon = baseUserLon - dRoll;
    if (viewLat > 90.0) viewLat = 90.0;
    if (viewLat < -90.0) viewLat = -90.0;
    while (viewLon > 180.0) viewLon -= 360.0;
    while (viewLon < -180.0) viewLon += 360.0;
    
    // Set camera attitude to 0 (looking straight down) to pivot around the center of the Earth sphere
    earth_renderer->setCameraAttitude(0.0f, 0.0f, 0.0f);
    earth_renderer->setObserverConstrained(false);
    earth_renderer->setDrawDecorations(false);
    
    // Draw the background Earth globe (no satellites list)
    earth_renderer->setUnixTime(current_unix == 0 ? 1783300000 : current_unix);
    earth_renderer->render(viewLat, viewLon, baseUserLat, baseUserLon, {});
    
    LGFX_Sprite* canvas = earth_renderer->getCanvas();
    if (!canvas) return;
    canvas->setTextDatum(TL_DATUM);

    if (s_triviaIndex < 0) {
        s_triviaIndex = (int)(esp_random() % TriviaManager::getCount());
    }

    // 1. 顶部航天冷知识（直接绘制于开机画布层，无浮动卡片框）
    if (!showLangSelect) {
        Language lang = I18N::getLanguage();
        const SatelliteTrivia* trivia = TriviaManager::get((size_t)s_triviaIndex);
        if (trivia) {
            canvas->setFont(I18N::getFont());
            canvas->setTextSize(1);

            const char* headerStr = TriviaManager::getCardHeader(lang);
            const char* topicStr = TriviaManager::getTopic(trivia, lang);
            // 第 1 行：分类小标（左上角 X=8, Y=6，青色，完全避开极光区域）
            canvas->setTextDatum(TL_DATUM);
            canvas->setTextColor(canvas->color565(0, 225, 255));
            canvas->drawString(headerStr, 8, 6);

            // 第 2 行：知识标题错开一行居中显示，并加上中括号（黄色，居中 X=120, Y=20）
            canvas->setTextDatum(TC_DATUM);
            canvas->setTextColor(TFT_YELLOW);
            char topicBuf[96];
            if (lang == LANG_ZH || lang == LANG_JA) {
                snprintf(topicBuf, sizeof(topicBuf), "【 %s 】", topicStr);
            } else {
                snprintf(topicBuf, sizeof(topicBuf), "[ %s ]", topicStr);
            }
            canvas->drawString(topicBuf, 120, 20);

            // 第 3 行起：正文内容下移一行开始排版（Y=35），纯白纯净无黑边，左对齐
            canvas->setTextDatum(TL_DATUM);
            canvas->setTextColor(TFT_WHITE);
            const char* bodyStr = TriviaManager::getText(trivia, lang);
            TextUtils::drawWrappedText(canvas, bodyStr, 8, 35, 224, 13, true);
        }
    }

    // 2. 底部动态开机信息（一行居中，清晰黄色无黑边）与进度条
    canvas->setFont(I18N::getFont());
    canvas->setTextDatum(TC_DATUM);
    canvas->setTextSize(1);
    char statusBuf[64] = {0};
    getLoadingStatusText(statusBuf, sizeof(statusBuf));
    const char* statusStr = (statusBuf[0] != '\0') ? statusBuf : I18N::get(TXT_LOADING_MODELS);
    // 按键提示保持纯静态黄色，不闪烁不晃眼，方便用户静心阅读上方航天小知识
    canvas->setTextColor(TFT_YELLOW);
    canvas->drawString(statusStr, 120, 104);

    // 进度条线性平滑追踪器：通过帧间插值保证进度条匀速丝滑推进，消除底层跳变
    static float s_visualProgress = 0.0f;
    if (progressPercentage <= 5 && s_visualProgress > 30.0f) {
        s_visualProgress = (float)progressPercentage;
    }
    float targetProgress = (float)progressPercentage;
    if (s_visualProgress < targetProgress) {
        float diff = targetProgress - s_visualProgress;
        float step = diff * 0.10f + 0.25f;
        s_visualProgress += step;
        if (s_visualProgress > targetProgress) {
            s_visualProgress = targetProgress;
        }
    } else if (progressPercentage >= 100) {
        s_visualProgress = 100.0f;
    }

    // 进度条（置于 Y=121，与地球仪边缘保持舒适间隔）
    canvas->drawRect(35, 121, 170, 5, canvas->color565(40, 55, 70));
    int fillW = (int)(166.0f * (s_visualProgress / 100.0f));
    if (fillW > 166) fillW = 166;
    if (fillW < 0) fillW = 0;
    if (fillW > 0) {
        canvas->fillRect(37, 122, fillW, 3, canvas->color565(0, 230, 120));
    }

    // 绘制航天发射与入轨全流程动效：
    // 0% ~ 100%：整箭巡航推进（红色整流罩 + 白色耐热箭体 + 红色后掠尾翼 + 动态喷射橙色烈焰）
    // 到达 100% 后：按真实航天流程依次触发：
    //   阶段 1 (0~400ms)：一级关机与级间脱节后退
    //   阶段 2 (400~900ms)：红色整流罩上下对称翻转剥离抛出，露出内部核心载荷
    //   阶段 3 (900~1400ms)：星箭弹簧分离，上面级后退，卫星向前平稳滑出
    //   阶段 4 (1400~2000ms)：卫星双侧科技蓝太阳能帆板平滑展开
    //   阶段 5 (2000ms+)：卫星在轨巡航待命，天线周期性发射无线电遥测信标脉冲波
    int rx = 37 + fillW;
    if (rx > 203) rx = 203;
    if (rx < 37) rx = 37;
    int ry = 123; // 进度条中心轴线

    static uint32_t s_readyStartTime = 0;
    if (s_visualProgress >= 99.5f) {
        if (s_readyStartTime == 0) {
            s_readyStartTime = millis();
        }
    } else {
        s_readyStartTime = 0;
    }

    uint32_t elapsed = (s_readyStartTime > 0) ? (millis() - s_readyStartTime) : 0;

    if (elapsed == 0) {
        // ================= 阶段 0：整箭巡航推进 (0% ~ 100%) =================
        int nozzleX = rx - 6;

        // 1. 尾部动态喷射橙色火焰 (持续喷射，包含高光亮黄内焰与闪烁尾羽)
        uint32_t t = millis();
        int fPhase = (t / 60) % 4;
        int flameLen = (fPhase == 0) ? 9 : ((fPhase == 1) ? 12 : ((fPhase == 2) ? 8 : 11));

        // 橙色外焰羽流
        canvas->fillRect(nozzleX - 3, ry - 1, 3, 3, TFT_ORANGE);
        canvas->drawFastHLine(nozzleX - 5, ry - 1, 2, TFT_ORANGE);
        canvas->drawFastHLine(nozzleX - 5, ry + 1, 2, TFT_ORANGE);
        canvas->drawFastHLine(nozzleX - flameLen, ry, flameLen - 2, TFT_ORANGE);

        // 亮黄内焰核心与高温白炽点
        int coreLen = flameLen / 2 + 1;
        canvas->drawFastHLine(nozzleX - coreLen, ry, coreLen, TFT_YELLOW);
        canvas->drawPixel(nozzleX - 1, ry, TFT_WHITE);

        // 动态跳动火花
        if (fPhase == 0) {
            canvas->drawPixel(nozzleX - flameLen - 2, ry, TFT_ORANGE);
            canvas->drawPixel(nozzleX - flameLen - 1, ry - 1, canvas->color565(255, 190, 0));
        } else if (fPhase == 1) {
            canvas->drawPixel(nozzleX - flameLen - 1, ry + 1, TFT_ORANGE);
        } else if (fPhase == 2) {
            canvas->drawPixel(nozzleX - flameLen - 2, ry, canvas->color565(255, 190, 0));
            canvas->drawPixel(nozzleX - flameLen - 1, ry - 1, TFT_ORANGE);
        } else {
            canvas->drawPixel(nozzleX - flameLen - 1, ry, TFT_ORANGE);
        }

        // 2. 金属深灰发动机尾喷管
        canvas->fillRect(nozzleX, ry - 1, 2, 3, canvas->color565(90, 95, 105));

        // 3. 经典红色后掠气动稳定尾翼 (上下对称)
        canvas->drawFastVLine(rx - 4, ry - 5, 4, TFT_RED);
        canvas->drawFastVLine(rx - 3, ry - 4, 3, TFT_RED);
        canvas->drawFastVLine(rx - 2, ry - 4, 3, TFT_RED);
        canvas->drawFastVLine(rx - 1, ry - 3, 2, TFT_RED);
        canvas->drawFastVLine(rx,     ry - 3, 2, TFT_RED);

        canvas->drawFastVLine(rx - 4, ry + 2, 4, TFT_RED);
        canvas->drawFastVLine(rx - 3, ry + 2, 3, TFT_RED);
        canvas->drawFastVLine(rx - 2, ry + 2, 3, TFT_RED);
        canvas->drawFastVLine(rx - 1, ry + 2, 2, TFT_RED);
        canvas->drawFastVLine(rx,     ry + 2, 2, TFT_RED);

        // 4. 白色航天耐热箭体 (高 5px，含灰色级间刻线与青蓝色航天员舷窗)
        canvas->fillRect(rx - 4, ry - 2, 11, 5, TFT_WHITE);
        canvas->drawFastVLine(rx - 1, ry - 2, 5, canvas->color565(170, 175, 185)); // 级间刻线
        canvas->fillRect(rx + 3, ry - 1, 2, 2, canvas->color565(0, 190, 255));      // 舷窗
        canvas->drawFastHLine(rx - 4, ry + 2, 11, canvas->color565(210, 215, 225)); // 底部立体阴影

        // 5. 红色锥形流线型整流罩 (火箭头部)
        canvas->fillRect(rx + 7, ry - 2, 3, 5, TFT_RED);  // 根部与箭体平齐 (高 5px)
        canvas->fillRect(rx + 10, ry - 1, 2, 3, TFT_RED); // 中段收束 (高 3px)
        canvas->drawFastHLine(rx + 12, ry, 2, TFT_RED);   // 尖锥端点 (高 1px)
        canvas->drawFastHLine(rx + 7, ry - 2, 2, canvas->color565(255, 110, 110)); // 头部高光
        canvas->drawFastHLine(rx + 14, ry, 2, canvas->color565(255, 180, 180));     // 前端探针
    } else {
        // ================= 阶段 1~5：真实航天分离与释放卫星动效（脱落部件大范围空间漂移悬浮） =================

        // 1. 一级助推级分离脱节与大范围空间后漂 (从 X=203 顺畅滑移至左下旷阔太空 X≈60~75, Y≈108~125)
        float bProgress = (elapsed < 4500) ? ((float)elapsed / 4500.0f) : 1.0f;
        float bEase = 1.0f - powf(1.0f - bProgress, 2.2f);
        int bBaseX = rx - (int)(bEase * 135.0f); // 203 - 135 = 68，脱离进度条末端向左滑行至屏幕左半部
        int bBaseY = ry + (int)(sinf(bEase * 3.14159f) * 6.0f);

        // 在轨低频微重力漫游大范围漂浮 (慢周期 Lissajous 曲线，范围 ±7px, ±5px)
        float bWave = (float)elapsed * 0.0010f;
        int bx = bBaseX + (int)(cosf(bWave) * 7.0f);
        int by = bBaseY + (int)(sinf(bWave * 1.3f) * 5.0f);
        if (by < 100) by = 100; // 严格限制在屏幕下方，绝不遮挡 Y<=87 的小知识文本
        if (by > 129) by = 129;
        if (bx < 20) bx = 20;

        // 助推级尾喷管
        int bNozzleX = bx - 6;
        canvas->fillRect(bNozzleX, by - 1, 2, 3, canvas->color565(80, 85, 95));
        // 发动机关机微弱冷气余气 (前 150ms 闪现)
        if (elapsed < 150) {
            canvas->drawPixel(bNozzleX - 2, by, canvas->color565(220, 160, 60));
            canvas->drawPixel(bNozzleX - 4, by, canvas->color565(140, 140, 150));
        }

        // 助推级白色箭体 (长 5px)
        canvas->fillRect(bx - 4, by - 2, 5, 5, TFT_WHITE);
        // 助推级红色后掠气动稳定尾翼 (上下对称)
        canvas->drawFastVLine(bx - 4, by - 5, 4, TFT_RED);
        canvas->drawFastVLine(bx - 3, by - 4, 3, TFT_RED);
        canvas->drawFastVLine(bx - 2, by - 4, 3, TFT_RED);
        canvas->drawFastVLine(bx - 1, by - 3, 2, TFT_RED);

        canvas->drawFastVLine(bx - 4, by + 2, 4, TFT_RED);
        canvas->drawFastVLine(bx - 3, by + 2, 3, TFT_RED);
        canvas->drawFastVLine(bx - 2, by + 2, 3, TFT_RED);
        canvas->drawFastVLine(bx - 1, by + 2, 2, TFT_RED);

        // 2. 上面级/仪器舱过渡段箭体 (大范围漂移至中右侧空间 X≈155~165, Y≈110~118)
        float uProgress = (elapsed < 900) ? 0.0f : ((elapsed < 4200) ? ((float)(elapsed - 900) / 3300.0f) : 1.0f);
        float uEase = 1.0f - powf(1.0f - uProgress, 2.0f);
        int uBaseX = rx - (int)(uEase * 46.0f); // 203 - 46 = 157
        int uBaseY = ry - (int)(uEase * 8.0f);  // 123 - 8 = 115

        // 在轨悬浮漫游
        float uWave = (float)(elapsed + 800) * 0.0013f;
        int ax = uBaseX + (int)(sinf(uWave) * 6.0f);
        int ay = uBaseY + (int)(cosf(uWave * 1.2f) * 4.0f);
        if (ay < 96) ay = 96;
        if (ay > 128) ay = 128;

        canvas->fillRect(ax - 2, ay - 2, 5, 5, TFT_WHITE);
        canvas->drawFastVLine(ax - 2, ay - 2, 5, canvas->color565(170, 175, 185)); // 级间刻线
        canvas->drawFastVLine(ax + 2, ay - 2, 5, canvas->color565(120, 125, 135)); // 载荷分离环
        canvas->drawPixel(ax, ay, canvas->color565(90, 95, 105));                  // 姿控喷嘴

        // 3. 红色整流罩对称翻转剥离并大范围漂向上下两侧深空
        if (elapsed < 400) {
            // 尚未抛罩：完整头部整流罩
            canvas->fillRect(rx + 7, ry - 2, 3, 5, TFT_RED);
            canvas->fillRect(rx + 10, ry - 1, 2, 3, TFT_RED);
            canvas->drawFastHLine(rx + 12, ry, 2, TFT_RED);
            canvas->drawFastHLine(rx + 7, ry - 2, 2, canvas->color565(255, 110, 110));
            canvas->drawFastHLine(rx + 14, ry, 2, canvas->color565(255, 180, 180));
        } else {
            // 抛罩后：上下两半片向外翻转剥离，脱离进度条大范围漂浮
            float fProgress = (elapsed < 3600) ? ((float)(elapsed - 400) / 3200.0f) : 1.0f;
            float fEase = 1.0f - powf(1.0f - fProgress, 2.0f);

            // 上半片整流罩：大幅漂向右上空域 (X≈175~185, Y≈92~98，严格避开 Y<=87 的小知识文本)
            int fTopBaseX = rx + 7 - (int)(fEase * 30.0f);
            int fTopBaseY = ry - (int)(fEase * 28.0f); // 123 - 28 = 95
            float fTopWave = (float)(elapsed + 1200) * 0.0015f;
            int fTopX = fTopBaseX + (int)(sinf(fTopWave) * 5.0f);
            int fTopY = fTopBaseY + (int)(cosf(fTopWave) * 4.0f);
            if (fTopY < 90) fTopY = 90; // 底部净空保护区

            canvas->fillRect(fTopX, fTopY - 2, 3, 3, TFT_RED);
            canvas->fillRect(fTopX + 3, fTopY - 1, 2, 2, TFT_RED);
            canvas->drawPixel(fTopX + 5, fTopY, TFT_RED);
            canvas->drawFastHLine(fTopX, fTopY - 2, 2, canvas->color565(255, 110, 110));

            // 下半片整流罩：大幅漂向右下空域 (X≈165~175, Y≈128~132)
            int fBotBaseX = rx + 7 - (int)(fEase * 40.0f);
            int fBotBaseY = ry + (int)(fEase * 7.0f); // 123 + 7 = 130
            float fBotWave = (float)(elapsed + 2400) * 0.0014f;
            int fBotX = fBotBaseX + (int)(cosf(fBotWave) * 6.0f);
            int fBotY = fBotBaseY + (int)(sinf(fBotWave) * 2.0f);
            if (fBotY > 132) fBotY = 132;

            canvas->fillRect(fBotX, fBotY, 3, 3, TFT_RED);
            canvas->fillRect(fBotX + 3, fBotY, 2, 2, TFT_RED);
            canvas->drawPixel(fBotX + 5, fBotY + 1, TFT_RED);
            canvas->drawFastHLine(fBotX, fBotY + 2, 2, canvas->color565(200, 40, 40));
        }

        // 4. 卫星载荷：星箭分离、前行就位与太阳翼展开 (400ms+)
        if (elapsed >= 400) {
            float p_sat = (elapsed < 900) ? 0.0f : ((elapsed < 1400) ? ((float)(elapsed - 900) / 500.0f) : 1.0f);
            float easeSat = 1.0f - (1.0f - p_sat) * (1.0f - p_sat);
            int satDx = (int)(easeSat * 11.0f);
            int sx = rx + 6 + satDx; // 最终就位约 X=220
            int sy = ry;

            // 阶段 5 在轨微重力悬浮姿态 (2100ms+)
            if (elapsed >= 2100) {
                sy += (int)(sinf((float)elapsed * 0.0018f) * 2.0f);
            }

            // 卫星核心舱体 (航天金色多层隔热 MLI 质感)
            canvas->fillRect(sx - 2, sy - 2, 4, 5, canvas->color565(255, 215, 0)); // 钛金色核心
            canvas->drawRect(sx - 2, sy - 2, 4, 5, canvas->color565(180, 140, 20)); // 金色轮廓
            canvas->drawPixel(sx, sy, TFT_WHITE);                                   // 光学载荷透镜
            canvas->drawFastVLine(sx, sy - 4, 2, TFT_WHITE);                        // 前向测控天线

            // 太阳翼展开 (1400ms ~ 2000ms 逐步由 0 伸展至 5px)
            if (elapsed >= 1400) {
                float p_solar = (elapsed < 2000) ? ((float)(elapsed - 1400) / 600.0f) : 1.0f;
                int wingW = (int)(p_solar * 5.0f);
                if (wingW > 0) {
                    // 左侧太阳能帆板 (深蓝光伏基底 + 青白光电网格刻线)
                    canvas->fillRect(sx - 2 - wingW, sy - 1, wingW, 3, canvas->color565(0, 120, 220));
                    canvas->drawFastHLine(sx - 2 - wingW, sy - 1, wingW, canvas->color565(0, 170, 255));
                    canvas->drawPixel(sx - 2 - (wingW + 1) / 2, sy, canvas->color565(200, 240, 255));

                    // 右侧太阳能帆板
                    canvas->fillRect(sx + 2, sy - 1, wingW, 3, canvas->color565(0, 120, 220));
                    canvas->drawFastHLine(sx + 2, sy - 1, wingW, canvas->color565(0, 170, 255));
                    canvas->drawPixel(sx + 2 + (wingW + 1) / 2, sy, canvas->color565(200, 240, 255));
                }
            }

            // 5. 在轨无线电遥测脉冲信标波圈与指示灯 (2100ms+)
            if (elapsed >= 2100) {
                uint32_t beaconCycle = (elapsed - 2100) % 1400; // 每 1.4 秒发射一波脉冲
                if (beaconCycle < 800) {
                    int waveR = 2 + (int)((float)beaconCycle * 0.010f); // 半径 2 到 10
                    int ax_b = sx;
                    int ay_b = sy - 4;
                    uint16_t waveCol = (waveR < 6) ? canvas->color565(0, 230, 160) : canvas->color565(0, 120, 80);
                    canvas->drawCircle(ax_b, ay_b, waveR, waveCol);
                }
                // 天线顶端绿色闪烁指示灯
                bool ledOn = (beaconCycle < 250);
                canvas->drawPixel(sx, sy - 4, ledOn ? TFT_GREEN : TFT_CYAN);
            }
        }
    }
    
    // Draw language selection dialog if needed
    if (showLangSelect) {
        canvas->setTextDatum(TL_DATUM);
        int dialogW = 164;
        int dialogH = 84;
        int dialogX = 120 - dialogW / 2;
        int dialogY = 67 - dialogH / 2;
        
        canvas->fillRect(dialogX, dialogY, dialogW, dialogH, canvas->color565(25, 35, 45));
        canvas->drawRect(dialogX, dialogY, dialogW, dialogH, TFT_YELLOW);
        
        canvas->setTextColor(TFT_WHITE);
        canvas->setFont(I18N::getFont());
        canvas->drawString("Select Language", dialogX + (dialogW - canvas->textWidth("Select Language")) / 2, dialogY + 4);
        
        const char* options[4] = {"1. English", "2. 简体中文", "3. 日本語", "4. Español"};
        const lgfx::IFont* optFonts[4] = {
            &fonts::efontCN_12,
            &fonts::efontCN_12,
            &fonts::efontJA_12,
            &fonts::efontCN_12
        };
        
        for (int i = 0; i < 4; i++) {
            canvas->setFont(optFonts[i]);
            if (selectedLangIndex == i) {
                canvas->fillRect(dialogX + 6, dialogY + 18 + i * 15, dialogW - 12, 14, canvas->color565(0, 100, 200));
                canvas->setTextColor(TFT_WHITE);
            } else {
                canvas->setTextColor(TFT_LIGHTGRAY);
            }
            canvas->drawString(options[i], dialogX + 12, dialogY + 19 + i * 15);
        }
        canvas->setFont(I18N::getFont());
    }
    
    // Push to screen
    if (earth_renderer && earth_renderer->getVisualMode() == 1) {
        ImageUtils::applyNightVisionFilter(canvas);
    }
    canvas->pushSprite(0, 0);
    canvas->setTextDatum(TL_DATUM);
}
