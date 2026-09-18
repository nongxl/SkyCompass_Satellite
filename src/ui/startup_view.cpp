#include "ui/startup_view.h"
#include <M5GFX.h>
#include "core/earth_renderer.h"
#include "core/attitude_estimator.h"
#include "core/i18n.h"
#include "core/image_utils.h"

extern EarthRenderer* earth_renderer;
extern AttitudeEstimator* attitude;
extern double baseUserLat;
extern double baseUserLon;
extern uint32_t current_unix;
extern String g_loadingStatusText;

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
    
    // Draw overlay UI
    canvas->setTextColor(TFT_WHITE);
    canvas->setTextSize(2);
    canvas->drawString("SkyCompass", 120 - canvas->textWidth("SkyCompass") / 2, 20);
    
    canvas->setTextColor(TFT_YELLOW);
    canvas->setTextSize(1);
    String statusStr = (g_loadingStatusText.length() > 0) ? g_loadingStatusText : I18N::get(TXT_LOADING_MODELS);
    canvas->drawString(statusStr.c_str(), 120 - canvas->textWidth(statusStr.c_str()) / 2, 50);
    
    // Draw progress bar
    canvas->drawRect(35, 108, 170, 8, TFT_DARKGREY);
    canvas->fillRect(37, 110, (int)(166.0f * (progressPercentage / 100.0f)), 4, TFT_GREEN);
    
    // Draw language selection dialog if needed
    if (showLangSelect) {
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
}
