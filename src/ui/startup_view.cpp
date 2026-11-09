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

    // 绘制随进度向前飞行的放大版航天火箭 (红色头部整流罩 + 白色耐热箭体 + 红色后掠尾翼 + 动态喷射橙色烈焰)
    int rx = 37 + fillW;
    if (rx > 203) rx = 203;
    if (rx < 37) rx = 37;
    int ry = 123; // 进度条中心轴线

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
    // 上侧尾翼
    canvas->drawFastVLine(rx - 4, ry - 5, 4, TFT_RED);
    canvas->drawFastVLine(rx - 3, ry - 4, 3, TFT_RED);
    canvas->drawFastVLine(rx - 2, ry - 4, 3, TFT_RED);
    canvas->drawFastVLine(rx - 1, ry - 3, 2, TFT_RED);
    canvas->drawFastVLine(rx,     ry - 3, 2, TFT_RED);
    // 下侧尾翼
    canvas->drawFastVLine(rx - 4, ry + 2, 4, TFT_RED);
    canvas->drawFastVLine(rx - 3, ry + 2, 3, TFT_RED);
    canvas->drawFastVLine(rx - 2, ry + 2, 3, TFT_RED);
    canvas->drawFastVLine(rx - 1, ry + 2, 2, TFT_RED);
    canvas->drawFastVLine(rx,     ry + 2, 2, TFT_RED);

    // 4. 白色航天耐热级体 (高 5px，含灰色级间刻线与青蓝色航天员舷窗)
    canvas->fillRect(rx - 4, ry - 2, 11, 5, TFT_WHITE);
    canvas->drawFastVLine(rx - 1, ry - 2, 5, canvas->color565(170, 175, 185)); // 级间刻线
    canvas->fillRect(rx + 3, ry - 1, 2, 2, canvas->color565(0, 190, 255));      // 舷窗
    canvas->drawFastHLine(rx - 4, ry + 2, 11, canvas->color565(210, 215, 225)); // 底部轻微立体阴影

    // 5. 红色锥形流线型整流罩 (火箭头部)
    canvas->fillRect(rx + 7, ry - 2, 3, 5, TFT_RED);  // 根部与箭体平齐 (高 5px)
    canvas->fillRect(rx + 10, ry - 1, 2, 3, TFT_RED); // 中段收束 (高 3px)
    canvas->drawFastHLine(rx + 12, ry, 2, TFT_RED);   // 尖锥端点 (高 1px)
    canvas->drawFastHLine(rx + 7, ry - 2, 2, canvas->color565(255, 110, 110)); // 头部高光
    canvas->drawFastHLine(rx + 14, ry, 2, canvas->color565(255, 180, 180));     // 前端探针 / 逃逸塔天线
    
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
