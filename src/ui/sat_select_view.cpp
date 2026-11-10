#include "sat_select_view.h"
#include "core/earth_renderer.h"
#include "core/position_manager.h"
#include "core/orbit_data_provider.h"
#include "core/log_manager.h"
#include "core/text_utils.h"
#include "core/observation_predictor.h"
#include "core/tle_updater.h"
#include "hal/hal_wifi.h"
#include "core/spring_animator.h"
#include <LittleFS.h>
#include "core/orbit_utils.h"
#include "core/space_audio.h"
#include "core/santa_tracker.h"

extern volatile bool isDownloadingCustom;
extern int getTotalSelectedSatelliteCount();
extern int g_satLimitShakeTrigger;
static SpringValue s_limitShakeX(0.0f, SpringConfig(0.16f, 0.45f));

extern EarthRenderer* earth_renderer;
extern PositionManager* pos_manager;
extern const int MAX_SATELLITES;
extern SatProfile g_satellites[];
extern int NUM_SATELLITES;
extern int NUM_BUILTIN_SATELLITES;
extern std::vector<RecentLaunchItem> g_recentLaunches;
extern int satSelectedIndex;
extern int recentLaunchSelectedIndex;
extern SatSelectTab currentSatTab;
extern std::vector<int> g_encyclopediaFilteredIndices;
extern Level3ObjectList g_level3Objects;
extern bool g_recentLaunchFocusMode;
extern String recentLaunchActiveBatchId;
extern bool g_repSatInitialized;
extern int focusSatIndex;
extern bool isSatViewMode;
extern volatile bool g_networkActive;
extern volatile bool g_wifiConnecting;
extern volatile bool recentLaunchDownloading;
extern uint32_t recentLaunchDownloadFinishedMs;
extern bool recentLaunchDownloadSuccess;
extern String recentLaunchErrorMsg;
extern String downloadErrorMsg;
extern uint32_t downloadFinishedMs;
extern volatile uint32_t current_unix;
extern int32_t timeMachineOffset;
extern String noradInput;
extern bool manualWifiToggle;
extern void lockPassMutex();
extern void unlockPassMutex();
extern void lockSatMutex();
extern void unlockSatMutex();
extern void saveCustomSatellites();
extern void validateSatViewFocusState();
extern void initRecentLaunchCalcs(RecentLaunchItem& item);
extern void loadLevel3ObjectsPage(const RecentLaunchItem& item, int page);
extern bool isSystemMemorySafeForNetwork();
extern double baseUserLat, baseUserLon, baseUserAlt;
extern std::vector<PassEvent> recommendedPasses;

extern bool g_showCategoryFilterDialog;
extern uint16_t g_selectedCategoryMask;
extern uint16_t g_tempCategoryMask;
extern int g_categoryFocusIndex;
extern bool showListHelp;
extern int deleteConfirmIndex;
extern bool recentLaunchInObjectsView;
extern int recentLaunchObjectPage;
extern std::vector<String> g_descWrappedLines;
extern int g_descLastSatIndex;
extern int g_descLastLang;
extern uint32_t g_lastSatSelectTime;
extern int g_descLabelAreaHeight;
extern bool g_descManualScrolled;
extern int g_descManualYOffset;
extern int g_descMaxScroll;
extern int g_descBounceTrigger;
extern int g_objPageBounceTrigger;

const FilterCategoryItem g_filterCategories[12] = {
    // Row 0: 特殊高频分类与可见星
    {"已选择", "Selected", "選択済", "Elegidos", Category::UNKNOWN, 0, 20, 90, 45, 0x07E0, 1},
    {"自定义", "Custom", "カスタム", "Personal", Category::UNKNOWN, 0, 30, 80, 110, 0x07FF, 2},
    {"肉眼可见", "Visible", "裸眼可視", "Visible", Category::UNKNOWN, FLAG_VISIBLE, 90, 80, 20, TFT_YELLOW, 0},

    // Row 1: 航天应用与科学
    {"载人航天", "Crewed", "有人宇宙", "Tripulado", Category::HUMAN_SPACEFLIGHT, FLAG_CREWED, 20, 90, 50, TFT_GREEN, 0},
    {"无线电", "Radio", "無線", "Radio", Category::UNKNOWN, FLAG_RADIO, 80, 30, 80, TFT_MAGENTA, 0},
    {"气象", "Weather", "気象", "Meteorol", Category::WEATHER, FLAG_WEATHER, 20, 60, 90, TFT_CYAN, 0},

    // Row 2: 卫星网络
    {"导航", "Navigation", "航法", "Navegac", Category::NAVIGATION, FLAG_NAVIGATION, 80, 20, 20, TFT_RED, 0},
    {"通信", "Comms", "通信", "Comun", Category::COMMUNICATIONS, 0, 60, 80, 110, TFT_WHITE, 0},
    {"地球观测", "Earth Obs", "地球観測", "Obs Terr", Category::EARTH_OBSERVATION, FLAG_EARTH_OBS, 30, 80, 80, TFT_GREEN, 0},

    // Row 3: 深空探索与操作
    {"科学天文", "Science", "科学", "Ciencia", Category::ASTRONOMY, FLAG_SCIENCE, 50, 30, 90, TFT_GOLD, 0},
    {"历史残骸", "Debris/Hist", "歴史・残骸", "Historico", Category::HISTORIC_EVENT, FLAG_HISTORIC | FLAG_ROCKET_BODY | FLAG_DEBRIS, 90, 50, 20, TFT_ORANGE, 0},
    {"重置全部", "Reset All", "全解除", "Restablec", Category::UNKNOWN, 0, 40, 50, 65, 0xCE79, 3}
};

static std::unique_ptr<SGP4Calc> s_previewSatCalc;
static int s_previewSatNorad = -1;
static String s_previewSatLine1;
static int s_deorbWarningLineCount = 0;

SatSelectView::SatSelectView() {
}

void SatSelectView::releasePreviewCalc() {
    s_previewSatCalc.reset();
    s_previewSatNorad = -1;
    s_previewSatLine1 = String();
    std::vector<String>().swap(g_descWrappedLines);
    g_descLastSatIndex = -1;
}

void SatSelectView::updateFilteredList() {
    g_encyclopediaFilteredIndices.clear();

    if (SantaTracker::isDecember()) {
        bool allowSanta = false;
        if (g_selectedCategoryMask == 0) {
            allowSanta = true;
        } else {
            for (int bit = 0; bit < 11; bit++) {
                if (g_selectedCategoryMask & (1 << bit)) {
                    if (g_filterCategories[bit].cat == Category::HISTORIC_EVENT ||
                        (g_filterCategories[bit].flag & FLAG_HISTORIC)) {
                        allowSanta = true;
                        break;
                    }
                }
            }
        }
        if (allowSanta) {
            g_encyclopediaFilteredIndices.push_back(SantaTracker::VIRTUAL_ENC_INDEX);
        }
    }

    for (int i = 0; i < NUM_SATELLITES; i++) {
        if (g_selectedCategoryMask == 0) {
            g_encyclopediaFilteredIndices.push_back(i);
            continue;
        }
        
        bool matched = false;
        uint32_t noradId = g_satellites[i].noradId;
        const EncyclopediaEntry* entry = (i < NUM_BUILTIN_SATELLITES) ? Encyclopedia::getEntryByNorad(noradId) : nullptr;
        
        for (int bit = 0; bit < 11; bit++) {
            if (g_selectedCategoryMask & (1 << bit)) {
                uint8_t spec = g_filterCategories[bit].specialType;
                if (spec == 1) { // “已选择”：筛选出所有已勾选的卫星
                    if (g_satellites[i].selected) {
                        matched = true;
                        break;
                    }
                } else if (spec == 2) { // “自定义”：筛选出用户自定添加的卫星
                    if (i >= NUM_BUILTIN_SATELLITES) {
                        matched = true;
                        break;
                    }
                } else {
                    if (g_filterCategories[bit].flag != 0) {
                        if ((g_filterCategories[bit].flag & FLAG_RADIO) && g_satellites[i].type == SAT_TYPE_HAM) {
                            matched = true;
                            break;
                        }
                        if (entry && (entry->flags & g_filterCategories[bit].flag)) {
                            matched = true;
                            break;
                        }
                    }
                    if (g_filterCategories[bit].cat != Category::UNKNOWN) {
                        if (entry && (entry->category == g_filterCategories[bit].cat ||
                                     (g_filterCategories[bit].cat == Category::HISTORIC_EVENT && entry->category == Category::ROCKET_BODY))) {
                            matched = true;
                            break;
                        }
                    }
                }
            }
        }
        if (matched) {
            g_encyclopediaFilteredIndices.push_back(i);
        }
    }
    
    int maxIdx = g_encyclopediaFilteredIndices.size();
    if (satSelectedIndex > maxIdx) {
        satSelectedIndex = maxIdx > 0 ? (maxIdx - 1) : 0;
    }
    if (satSelectedIndex < 0) satSelectedIndex = 0;
}

void SatSelectView::drawCategoryFilterDialog(LGFX_Sprite* canvas) {
    // 弹窗弹出弹性动画
    static SpringValue s_dialogScale(0.85f, SpringConfig::Bouncy);
    static bool s_lastDialogShow = false;
    if (!g_showCategoryFilterDialog) {
        s_lastDialogShow = false;
        return;
    }
    
    if (!s_lastDialogShow) {
        s_lastDialogShow = true;
        s_dialogScale.jump(0.88f);
        s_dialogScale.set(1.0f);
    }
    float scale = s_dialogScale.get();
    if (scale < 0.5f) scale = 0.5f;
    if (scale > 1.05f) scale = 1.05f;

    int w = (int)(224 * scale);
    int h = (int)(86 * scale);
    int x = (canvas->width() - w) / 2;
    int y = (canvas->height() - h) / 2;
    
    // 半透明深底与青色亮边框
    canvas->fillRoundRect(x, y, w, h, 4, canvas->color565(15, 20, 28));
    canvas->drawRoundRect(x, y, w, h, 4, TFT_CYAN);
    
    Language currL = I18N::getLanguage();
    
    // 4 行 × 3 列 胶囊徽章标签 (每行 3 个，共 12 个项)
    int baseStartX = (canvas->width() - 224) / 2 + 8;
    int baseStartY = (canvas->height() - 86) / 2 + 5;
    int rowH = 19;
    int colW = 66;
    int colGap = 5;
    
    for (int i = 0; i < 12; i++) {
        int r = i / 3;
        int c = i % 3;
        int bx = baseStartX + c * (colW + colGap);
        int by = baseStartY + r * rowH;
        
        bool isChecked = (i == 11) ? (g_tempCategoryMask == 0) : ((g_tempCategoryMask & (1 << i)) != 0);
        
        const char* label = (currL == LANG_ZH) ? g_filterCategories[i].name_zh :
                            ((currL == LANG_JA) ? g_filterCategories[i].name_ja :
                            ((currL == LANG_ES) ? g_filterCategories[i].name_es : g_filterCategories[i].name_en));
        
        uint16_t bgColor;
        uint16_t textColor;
        uint16_t borderColor;
        
        if (i == 11) { // “重置全部”特殊按钮
            if (g_tempCategoryMask == 0) {
                bgColor = canvas->color565(30, 50, 70);
                textColor = 0x07FF;
                borderColor = 0x07FF;
            } else {
                bgColor = canvas->color565(25, 30, 40);
                textColor = canvas->color565(140, 155, 175);
                borderColor = canvas->color565(45, 52, 65);
            }
        } else if (isChecked) {
            bgColor = canvas->color565(g_filterCategories[i].r, g_filterCategories[i].g, g_filterCategories[i].b);
            textColor = g_filterCategories[i].textColor;
            borderColor = textColor; // 边框呼应徽章专属高亮色
        } else {
            bgColor = canvas->color565(25, 30, 40);
            textColor = canvas->color565(90, 100, 115);
            borderColor = canvas->color565(45, 52, 65);
        }
        
        canvas->fillRoundRect(bx, by, colW, 16, 3, bgColor);
        canvas->drawRoundRect(bx, by, colW, 16, 3, borderColor);
        
        canvas->setTextColor(textColor);
        int tw = canvas->textWidth(label);
        canvas->drawString(label, bx + (colW - tw) / 2, by + 2);
    }

    // 2D 物理弹簧磁吸焦点框
    int focR = g_categoryFocusIndex / 3;
    int focC = g_categoryFocusIndex % 3;
    float targetFx = (float)(baseStartX + focC * (colW + colGap) - 1);
    float targetFy = (float)(baseStartY + focR * rowH - 1);

    static SpringValue s_focusX(100.0f, SpringConfig::Default);
    static SpringValue s_focusY(50.0f, SpringConfig::Default);
    static bool s_focusInit = false;
    if (!s_focusInit) {
        s_focusX.jump(targetFx);
        s_focusY.jump(targetFy);
        s_focusInit = true;
    } else {
        s_focusX.set(targetFx);
        s_focusY.set(targetFy);
    }

    float fx = s_focusX.get();
    float fy = s_focusY.get();
    canvas->drawRoundRect((int)roundf(fx), (int)roundf(fy), colW + 2, 18, 3, TFT_YELLOW);
    canvas->drawRoundRect((int)roundf(fx) - 1, (int)roundf(fy) - 1, colW + 4, 20, 4, TFT_WHITE);
}

void SatSelectView::draw(LGFX_Sprite* canvas) {
    if (!canvas && earth_renderer) canvas = earth_renderer->getCanvas();
    if (!canvas) return;
    canvas->setTextDatum(TL_DATUM);

    auto getBannerTextColor = [](const String& msg) -> uint16_t {
        String lower = msg;
        lower.toLowerCase();
        
        if (lower.indexOf("success") != -1 || msg.indexOf(u8"成功") != -1 ||
            lower.indexOf("updated") != -1 || msg.indexOf(u8"已更新") != -1 ||
            lower.indexOf("fresh") != -1 || lower.indexOf("ready") != -1) {
            return TFT_GREEN;
        }
        
        if (lower.indexOf("connecting") != -1 || msg.indexOf(u8"连接") != -1 ||
            lower.indexOf("refreshing") != -1 || msg.indexOf(u8"刷新") != -1 ||
            lower.indexOf("downloading") != -1 || msg.indexOf(u8"下载") != -1 ||
            lower.indexOf("syncing") != -1 || msg.indexOf(u8"同步") != -1 ||
            lower.indexOf("checking") != -1 || msg.indexOf(u8"检查") != -1 ||
            lower.indexOf("busy") != -1 || msg.indexOf(u8"繁忙") != -1 ||
            lower.indexOf("loading") != -1 || msg.indexOf(u8"加载") != -1 ||
            lower.indexOf("%") != -1) {
            return TFT_YELLOW;
        }

        if (lower.indexOf("limit") != -1 || msg.indexOf(u8"上限") != -1) {
            return TFT_ORANGE;
        }
        
        if (lower.indexOf("failed") != -1 || msg.indexOf(u8"失败") != -1 ||
            lower.indexOf("error") != -1 || msg.indexOf(u8"错误") != -1 ||
            lower.indexOf("no wifi") != -1 || msg.indexOf(u8"未连接") != -1 ||
            lower.indexOf("refused") != -1) {
            return TFT_RED;
        }
        
        return TFT_LIGHTGRAY;
    };

    static bool lastDownloading = false;
    if (lastDownloading && !recentLaunchDownloading) {
        recentLaunchDownloadFinishedMs = millis();
    }
    lastDownloading = recentLaunchDownloading;

    // canvas provided via parameter
    uint16_t width = canvas->width();
    uint16_t height = canvas->height();
    
    bool showBanner = false;
    if (currentSatTab == TAB_RECENT_LAUNCH) {
        if (recentLaunchDownloading || (recentLaunchDownloadFinishedMs > 0 && (millis() - recentLaunchDownloadFinishedMs < 3000))) {
            showBanner = true;
        }
    } else if (currentSatTab == TAB_ENCYCLOPEDIA) {
        if (g_wifiConnecting || g_networkActive || (downloadFinishedMs > 0 && (millis() - downloadFinishedMs < 3000))) {
            if (downloadErrorMsg.length() > 0) {
                showBanner = true;
            }
        } else {
            if (downloadErrorMsg.length() > 0) {
                downloadErrorMsg = "";
            }
        }
    }
    int bottomLimit = showBanner ? (height - 13) : height;

    
    // Background
    canvas->fillRect(0, 0, width, height, canvas->color565(20, 30, 40));
    
    // Top Bar - Dual Tabs
    canvas->fillRect(0, 0, width, 20, canvas->color565(30, 40, 50));
    
    // 物理弹簧滑动 Tab 高亮滑块
    static SpringValue s_tabHighlightX(0.0f, SpringConfig::Default);
    static bool s_tabInit = false;
    float targetTabX = (currentSatTab == TAB_ENCYCLOPEDIA) ? 0.0f : (float)(width / 2);
    if (!s_tabInit) {
        s_tabHighlightX.jump(targetTabX);
        s_tabInit = true;
    } else {
        s_tabHighlightX.set(targetTabX);
    }
    
    float tabX = s_tabHighlightX.get();
    canvas->fillRect((int)roundf(tabX), 0, width / 2, 20, canvas->color565(100, 50, 200));
    
    // Tab 1: Encyclopedia
    canvas->setTextColor((currentSatTab == TAB_ENCYCLOPEDIA) ? TFT_WHITE : TFT_LIGHTGRAY);
    canvas->setTextSize(1);
    canvas->drawString(I18N::get(TXT_TAB_ENCYCLOPEDIA), 70 - canvas->textWidth(I18N::get(TXT_TAB_ENCYCLOPEDIA))/2, 6);
    
    // Tab 2: Recent Launch
    canvas->setTextColor((currentSatTab == TAB_RECENT_LAUNCH) ? TFT_WHITE : TFT_LIGHTGRAY);
    canvas->drawString(I18N::get(TXT_TAB_RECENT_LAUNCH), 166 - canvas->textWidth(I18N::get(TXT_TAB_RECENT_LAUNCH))/2, 6);
    
    // Draw Memory Health Progress Bar on Top Bar Divider Line (y = 19..20)
    {
        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t totalHeap = ESP.getHeapSize();
        float memRatio = (totalHeap > 0) ? ((float)(totalHeap - freeHeap) / (float)totalHeap) : 0.0f;
        if (memRatio < 0.0f) memRatio = 0.0f;
        if (memRatio > 1.0f) memRatio = 1.0f;

        int fillWidth = (int)(width * memRatio);
        if (fillWidth > width) fillWidth = width;
        if (fillWidth < 0) fillWidth = 0;

        uint16_t memColor;
        if (freeHeap < 10000 || memRatio >= 0.85f) {
            memColor = TFT_RED;                       // 红色 (紧张)
        } else if (freeHeap < 25000 || memRatio >= 0.72f) {
            memColor = TFT_YELLOW;                    // 黄色 (适度偏高)
        } else if (memRatio >= 0.60f) {
            memColor = TFT_GREEN;                     // 绿色 (健康正常)
        } else {
            memColor = canvas->color565(0, 220, 255); // 青蓝色 (极为充裕，与 RF 控制台一致)
        }

        // 绘制 2px 内存进度条充当分割线
        canvas->fillRect(0, 19, width, 2, canvas->color565(35, 45, 55)); // 轨道背景
        if (fillWidth > 0) {
            canvas->fillRect(0, 19, fillWidth, 2, memColor);             // 填充内存使用率
        }
    }
    
    // Draw Top Bar Status Icons
    {
        // 1. WiFi Icon on Top-Left
        bool isConnected = HalWifi::isConnected();
        bool isConnecting = !isConnected && (g_wifiConnecting || (recentLaunchDownloading && recentLaunchErrorMsg.indexOf("WiFi") >= 0));
        
        int wifiX = 10; // Center of WiFi icon
        int wifiY = 16; // Bottom of WiFi icon
        
        if (isConnecting) {
            int flashStep = (millis() / 250) % 3;
            uint16_t c0 = (flashStep >= 0) ? TFT_GREEN : TFT_DARKGREY;
            uint16_t c1 = (flashStep >= 1) ? TFT_GREEN : TFT_DARKGREY;
            uint16_t c2 = (flashStep >= 2) ? TFT_GREEN : TFT_DARKGREY;
            
            canvas->fillCircle(wifiX, wifiY - 1, 1, c0);
            canvas->drawArc(wifiX, wifiY - 1, 4, 5, 225.0f, 315.0f, c1);
            canvas->drawArc(wifiX, wifiY - 1, 8, 9, 225.0f, 315.0f, c2);
        } else {
            uint16_t wifiColor = isConnected ? TFT_GREEN : TFT_DARKGREY;
            canvas->fillCircle(wifiX, wifiY - 1, 1, wifiColor);
            canvas->drawArc(wifiX, wifiY - 1, 4, 5, 225.0f, 315.0f, wifiColor);
            canvas->drawArc(wifiX, wifiY - 1, 8, 9, 225.0f, 315.0f, wifiColor);
        }
        
        // 2. Battery Icon on Top-Right with hysteresis filtering to prevent jitter
        static float filteredBat = -1.0f;
        static int lastDisplayedBat = -1;
        
        int rawBat = M5Cardputer.Power.getBatteryLevel();
        if (rawBat > 100) rawBat = 100;
        if (rawBat < 0) rawBat = 0;
        
        if (filteredBat < 0.0f) {
            filteredBat = (float)rawBat;
            lastDisplayedBat = rawBat;
        } else {
            // Smooth out high-frequency ADC voltage noise with a first-order low-pass filter
            filteredBat = filteredBat * 0.98f + (float)rawBat * 0.02f;
            // Apply 1.0% hysteresis band to prevent the integer display from toggling back-and-forth at boundary values
            if (fabsf(filteredBat - (float)lastDisplayedBat) >= 1.0f) {
                lastDisplayedBat = (int)(filteredBat + 0.5f);
            }
        }
        int batPct = lastDisplayedBat;
        
        uint16_t batColor = TFT_GREEN;
        if (batPct < 10) {
            batColor = TFT_RED;
        } else if (batPct < 70) {
            batColor = TFT_YELLOW;
        } else {
            batColor = TFT_GREEN;
        }
        
        int batX = width - 26;
        int batY = 4;
        int batW = 20;
        int batH = 12;
        
        // Hollow battery body
        canvas->drawRect(batX, batY, batW, batH, batColor);
        // Nipple on the right
        canvas->fillRect(batX + batW, batY + 4, 2, 4, batColor);
        
        // Battery percentage text inside (centered)
        canvas->setFont(&fonts::Font0);
        canvas->setTextColor(batColor);
        String pctStr = String(batPct);
        int textX = batX + (batW - canvas->textWidth(pctStr.c_str())) / 2;
        int textY = batY + 2; // standard char height is 8
        canvas->drawString(pctStr.c_str(), textX, textY);
        canvas->setFont(I18N::getFont());
    }

    
    if (currentSatTab == TAB_ENCYCLOPEDIA) {
        // Left Panel (List)
        int yPos = 25;
        int itemsPerPage = showBanner ? 6 : 8;
        int itemSpacing = 12;
        int filteredCount = g_encyclopediaFilteredIndices.size();
        int totalItems = filteredCount + 1;
        int startIndex = (satSelectedIndex / itemsPerPage) * itemsPerPage;
        
        // 弹簧物理光标动画 (平滑磁吸过渡)
        static SpringValue s_cursorY_encyclopedia(24.0f, SpringConfig::Default);
        static int s_lastStartIndex = -1;
        int selInPage = satSelectedIndex - startIndex;
        if (selInPage < 0) selInPage = 0;
        if (selInPage >= itemsPerPage) selInPage = itemsPerPage - 1;
        float targetCursorY = 25.0f - 1.0f + selInPage * itemSpacing;
        
        if (s_lastStartIndex != startIndex) {
            s_lastStartIndex = startIndex;
            s_cursorY_encyclopedia.jump(targetCursorY); // 跨页时直接复位
        } else {
            s_cursorY_encyclopedia.set(targetCursorY);
        }
        
        // 触发勾选上限物理震动 (Shake/Wiggle)
        if (g_satLimitShakeTrigger != 0) {
            s_limitShakeX.shift((float)g_satLimitShakeTrigger);
            g_satLimitShakeTrigger = 0;
        }
        int shakeX = (int)roundf(s_limitShakeX.get());

        // 绘制平滑物理磁吸高亮框
        float smoothY = s_cursorY_encyclopedia.get();
        canvas->fillRoundRect(2 + shakeX, (int)roundf(smoothY), 82, 12, 3, canvas->color565(0, 120, 255));
        
        for (int i = 0; i < itemsPerPage && (startIndex + i) < totalItems; i++) {
            int index = startIndex + i;
            if (index == satSelectedIndex) {
                canvas->setTextColor(TFT_WHITE);
            } else {
                canvas->setTextColor(TFT_LIGHTGRAY);
            }
            int curShakeX = (index == satSelectedIndex) ? shakeX : 0;
            
            if (index < filteredCount) {
                int realIdx = g_encyclopediaFilteredIndices[index];
                if (realIdx == SantaTracker::VIRTUAL_ENC_INDEX) {
                    canvas->setTextColor(index == satSelectedIndex ? TFT_WHITE : TFT_RED);
                    String checkBox = "[*]";
                    canvas->drawString(checkBox.c_str(), 4 + curShakeX, yPos);
                    if (index == satSelectedIndex) {
                        TextUtils::drawScrollingText(canvas, "SNT-CLS", 28 + curShakeX, yPos, 56, TFT_WHITE);
                    } else {
                        canvas->drawString("SNT-CLS", 28, yPos);
                    }
                } else {
                    String checkBox = g_satellites[realIdx].selected ? "[x]" : "[ ]";
                    canvas->drawString(checkBox.c_str(), 4 + curShakeX, yPos);
                    
                    if (index == satSelectedIndex) {
                        TextUtils::drawScrollingText(canvas, g_satellites[realIdx].name.c_str(), 28 + curShakeX, yPos, 56, TFT_WHITE);
                    } else {
                        String nameStr = g_satellites[realIdx].name;
                        if (nameStr.length() > 9) nameStr = nameStr.substring(0, 7) + "..";
                        canvas->drawString(nameStr.c_str(), 28, yPos);
                    }
                }
            } else {
                String text = isDownloadingCustom ? I18N::get(TXT_DOWNLOADING) : ("[+] " + noradInput + "_");
                canvas->drawString(text.c_str(), 4 + curShakeX, yPos);
            }
            
            yPos += itemSpacing;
        }
        
        // Draw page index indicator
        {
            int totalPages = (totalItems + itemsPerPage - 1) / itemsPerPage;
            int currentPage = (satSelectedIndex / itemsPerPage) + 1;
            char pageBuf[32];
            int totalSel = getTotalSelectedSatelliteCount();
            sprintf(pageBuf, "(%d/%d) [%d/%d]", currentPage, totalPages, totalSel, MAX_TOTAL_SELECTED_SATS);
            canvas->setTextColor(totalSel >= MAX_TOTAL_SELECTED_SATS ? TFT_ORANGE : canvas->color565(110, 150, 180));
            int pageX = 8;
            if (pageX + canvas->textWidth(pageBuf) > 83) {
                sprintf(pageBuf, "(%d/%d)[%d/%d]", currentPage, totalPages, totalSel, MAX_TOTAL_SELECTED_SATS);
                if (pageX + canvas->textWidth(pageBuf) > 83) {
                    pageX = 4;
                    if (pageX + canvas->textWidth(pageBuf) > 83) {
                        sprintf(pageBuf, "%d/%d [%d/%d]", currentPage, totalPages, totalSel, MAX_TOTAL_SELECTED_SATS);
                    }
                }
            }
            canvas->drawString(pageBuf, pageX, showBanner ? (bottomLimit - 12) : (bottomLimit - 13));
        }
        
        // Right Panel (Description)
        canvas->drawFastVLine(85, 20, bottomLimit - 20, TFT_DARKGREY);
        
        int rightX = 89;
        int descY = 25;
        if (satSelectedIndex < filteredCount) {
            int realIdx = g_encyclopediaFilteredIndices[satSelectedIndex];
            SatProfile selSat;
            if (realIdx == SantaTracker::VIRTUAL_ENC_INDEX) {
                selSat.noradId = 1225;
                selSat.name = "SNT-CLS";
                selSat.color = TFT_RED;
                selSat.iconType = ICON_SATELLITE;
                selSat.opsStatus = SantaTracker::isFlightPhase() ? '+' : 'P';
            } else {
                lockSatMutex();
                selSat = g_satellites[realIdx];
                unlockSatMutex();
            }
            
            // Draw 3x Scaled Icon
            int iconX = rightX + 21;
            int iconY = descY + 12;
            uint16_t satColor = selSat.color;
            SatIconType t = selSat.iconType;
            
            if (realIdx == SantaTracker::VIRTUAL_ENC_INDEX) {
                // SNT-CLS Sleigh & Rudolph icon
                canvas->drawFastHLine(iconX - 16, iconY + 8, 20, canvas->color565(255, 215, 0));
                canvas->drawPixel(iconX - 16, iconY + 7, canvas->color565(255, 215, 0));
                canvas->drawPixel(iconX + 3, iconY + 7, canvas->color565(255, 215, 0));
                canvas->fillRect(iconX - 14, iconY + 1, 14, 6, TFT_RED);
                canvas->fillRect(iconX - 11, iconY - 3, 8, 4, TFT_WHITE);
                canvas->fillCircle(iconX - 7, iconY - 5, 2, TFT_RED);
                canvas->fillRect(iconX + 7, iconY - 1, 8, 7, canvas->color565(160, 82, 45));
                canvas->fillCircle(iconX + 15, iconY + 1, 2, TFT_RED);
                canvas->drawPixel(iconX + 11, iconY - 3, canvas->color565(210, 180, 140));
                canvas->drawPixel(iconX + 12, iconY - 4, canvas->color565(210, 180, 140));
            } else if (t == ICON_STATION) {
                canvas->fillRect(iconX - 6, iconY - 3, 15, 9, TFT_WHITE);
                canvas->fillRect(iconX - 21, iconY - 9, 12, 21, satColor);
                canvas->fillRect(iconX + 12, iconY - 9, 12, 21, satColor);
            } else if (t == ICON_TELESCOPE) {
                canvas->fillRect(iconX - 6, iconY - 9, 15, 21, TFT_WHITE);
                canvas->fillRect(iconX - 9, iconY - 12, 21, 6, TFT_LIGHTGRAY);
                canvas->fillRect(iconX - 18, iconY, 9, 6, satColor);
                canvas->fillRect(iconX + 12, iconY, 9, 6, satColor);
            } else if (t == ICON_DEEPSPACE) {
                canvas->fillRect(iconX - 1, iconY - 15, 3, 31, satColor);
                canvas->fillRect(iconX - 15, iconY - 1, 31, 3, satColor);
                for (int i = -1; i <= 1; i++) {
                    canvas->drawLine(iconX - 6 + i, iconY - 6, iconX + 6 + i, iconY + 6, TFT_WHITE);
                    canvas->drawLine(iconX - 6 + i, iconY + 6, iconX + 6 + i, iconY - 6, TFT_WHITE);
                }
            } else if (t == ICON_ROCKET) {
                canvas->fillRect(iconX - 5, iconY - 8, 11, 16, TFT_WHITE);
                canvas->fillTriangle(iconX - 5, iconY - 8, iconX + 5, iconY - 8, iconX, iconY - 15, satColor);
                canvas->fillRect(iconX - 5, iconY + 8, 4, 4, TFT_ORANGE);
                canvas->fillRect(iconX + 2, iconY + 8, 4, 4, TFT_ORANGE);
            } else if (t == ICON_DFH1) {
                canvas->fillCircle(iconX, iconY, 9, TFT_WHITE);
                canvas->drawLine(iconX - 6, iconY - 6, iconX - 18, iconY - 18, satColor);
                canvas->drawLine(iconX + 6, iconY - 6, iconX + 18, iconY - 18, satColor);
                canvas->drawLine(iconX - 6, iconY + 6, iconX - 18, iconY + 18, satColor);
                canvas->drawLine(iconX + 6, iconY + 6, iconX + 18, iconY + 18, satColor);
            } else if (t == ICON_BLUEWALKER3) {
                canvas->fillRect(iconX - 3, iconY - 3, 9, 9, TFT_WHITE);
                canvas->fillRect(iconX - 21, iconY - 9, 15, 21, satColor);
                canvas->fillRect(iconX + 9, iconY - 9, 15, 21, satColor);
                canvas->drawFastVLine(iconX - 15, iconY - 9, 21, TFT_BLACK);
                canvas->drawFastVLine(iconX - 9, iconY - 9, 21, TFT_BLACK);
                canvas->drawFastVLine(iconX + 15, iconY - 9, 21, TFT_BLACK);
                canvas->drawFastVLine(iconX + 21, iconY - 9, 21, TFT_BLACK);
                canvas->drawFastHLine(iconX - 21, iconY, 15, TFT_BLACK);
                canvas->drawFastHLine(iconX + 9, iconY, 15, TFT_BLACK);
            } else if (t == ICON_WEATHER) {
                canvas->fillRect(iconX - 3, iconY - 6, 9, 15, TFT_WHITE);
                canvas->drawLine(iconX - 6, iconY, iconX - 18, iconY - 6, satColor);
                canvas->fillRect(iconX - 24, iconY - 12, 9, 9, satColor);
                canvas->fillRect(iconX + 6, iconY - 3, 6, 3, satColor);
                canvas->fillRect(iconX + 9, iconY - 6, 3, 3, satColor);
            } else if (t == ICON_NAVIGATION) {
                canvas->fillRect(iconX - 3, iconY - 6, 9, 15, TFT_WHITE);
                canvas->fillRect(iconX - 24, iconY - 3, 9, 9, satColor);
                canvas->fillRect(iconX + 15, iconY - 3, 9, 9, satColor);
                canvas->drawFastHLine(iconX - 15, iconY + 1, 12, TFT_LIGHTGRAY);
                canvas->drawFastHLine(iconX + 6, iconY + 1, 9, TFT_LIGHTGRAY);
                canvas->fillRect(iconX - 1, iconY + 9, 3, 6, satColor);
                canvas->fillCircle(iconX, iconY + 15, 3, satColor);
            } else if (t == ICON_COMMUNICATION) {
                canvas->fillCircle(iconX, iconY, 6, TFT_WHITE);
                canvas->drawLine(iconX, iconY - 6, iconX - 9, iconY - 18, satColor);
                canvas->drawLine(iconX, iconY - 6, iconX + 9, iconY - 18, satColor);
                canvas->drawFastVLine(iconX, iconY + 6, 6, satColor);
                canvas->drawFastHLine(iconX - 6, iconY + 12, 13, satColor);
                canvas->drawFastHLine(iconX - 3, iconY + 13, 7, satColor);
            } else if (t == ICON_DEBRIS) {
                // Space Debris: regular solar grid panel on left, jagged broken lines in middle, small drifting squares on right
                // 1. Regular panel on left
                canvas->fillRect(iconX - 18, iconY - 6, 18, 13, satColor);
                canvas->drawRect(iconX - 18, iconY - 6, 18, 13, TFT_BLACK);
                canvas->drawFastHLine(iconX - 18, iconY, 18, TFT_BLACK);
                canvas->drawFastVLine(iconX - 9, iconY - 6, 13, TFT_BLACK);
                
                // 2. Jagged edge and outline in middle
                canvas->drawLine(iconX, iconY - 6, iconX + 12, iconY - 3, satColor);
                canvas->drawLine(iconX + 12, iconY - 3, iconX + 6, iconY + 3, satColor);
                canvas->drawLine(iconX + 6, iconY + 3, iconX + 15, iconY + 7, satColor);
                canvas->drawLine(iconX + 15, iconY + 7, iconX, iconY + 7, satColor);
                canvas->drawLine(iconX, iconY, iconX + 9, iconY + 2, satColor);
                
                // 3. Detached debris chunks on right
                canvas->fillRect(iconX + 18, iconY - 9, 3, 3, satColor);
                canvas->fillRect(iconX + 15, iconY + 12, 4, 3, satColor);
                canvas->fillRect(iconX + 22, iconY + 2, 3, 4, satColor);
            } else if (t == ICON_SPACEPLANE) {
                // Spaceplane (X-37B) 3x — top-down delta wing, blunt nose, vertical tail
                // Nose
                canvas->fillRect(iconX - 3, iconY - 12, 9, 6, TFT_WHITE);
                // Fuselage
                canvas->fillRect(iconX - 6, iconY - 6, 15, 15, TFT_WHITE);
                // Delta wings (left & right triangles at widest point)
                canvas->fillTriangle(iconX - 12, iconY + 3, iconX - 3, iconY - 3, iconX - 3, iconY + 9, satColor);
                canvas->fillTriangle(iconX + 15, iconY + 3, iconX + 6, iconY - 3, iconX + 6, iconY + 9, satColor);
                // Vertical tail fin
                canvas->drawFastVLine(iconX + 3, iconY + 9, 9, satColor);
                canvas->drawFastHLine(iconX, iconY + 15, 9, satColor);
            } else if (t == ICON_SOLAR_PROBE) {
                // Solar Probe (Parker) 3x — wide heat shield disc + instrument boom + tiny solar wings
                // Heat shield (flat ellipse)
                canvas->fillEllipse(iconX, iconY - 3, 12, 9, TFT_LIGHTGRAY);
                canvas->drawEllipse(iconX, iconY - 3, 12, 9, satColor);
                // Instrument boom
                canvas->drawFastVLine(iconX, iconY + 6, 9, TFT_WHITE);
                // Tiny solar panels
                canvas->fillRect(iconX - 9, iconY + 9, 6, 3, satColor);
                canvas->fillRect(iconX + 4, iconY + 9, 6, 3, satColor);
            } else if (t == ICON_LANDER) {
                // Lander 3x — hexagonal body + top antenna + three landing legs with foot pads
                // Antenna
                canvas->drawFastVLine(iconX, iconY - 12, 6, satColor);
                canvas->drawPixel(iconX - 1, iconY - 12, satColor);
                canvas->drawPixel(iconX + 1, iconY - 12, satColor);
                // Main body
                canvas->fillRect(iconX - 6, iconY - 6, 15, 12, TFT_WHITE);
                // Three legs
                canvas->drawLine(iconX - 6, iconY + 6, iconX - 12, iconY + 12, satColor);
                canvas->drawLine(iconX + 1, iconY + 6, iconX + 1,  iconY + 12, satColor);
                canvas->drawLine(iconX + 8, iconY + 6, iconX + 14, iconY + 12, satColor);
                // Foot pads
                canvas->drawFastHLine(iconX - 15, iconY + 12, 6, satColor);
                canvas->drawFastHLine(iconX - 1, iconY + 13, 4, satColor);
                canvas->drawFastHLine(iconX + 12, iconY + 12, 6, satColor);
            } else {
                canvas->fillRect(iconX - 3, iconY - 3, 9, 9, TFT_WHITE);
                canvas->fillRect(iconX - 15, iconY - 3, 9, 9, satColor);
                canvas->fillRect(iconX - 6, iconY - 1, 3, 3, TFT_LIGHTGRAY);
            }
            
            TextUtils::drawScrollingText(canvas, selSat.name.c_str(), rightX + 48, descY + 6, width - rightX - 48 - 4, selSat.color);
            
            // Draw NORAD ID for tracking
            if (realIdx == SantaTracker::VIRTUAL_ENC_INDEX) {
                canvas->setTextColor(TFT_LIGHTGRAY);
                canvas->drawString((String(I18N::get(TXT_ID)) + "01225").c_str(), rightX + 48, descY + 20);
                canvas->setTextColor(TFT_GREEN);
                const char* stLabel = SantaTracker::isFlightPhase() ? "ACTIVE" : "STANDBY";
                canvas->drawString(stLabel, width - canvas->textWidth(stLabel) - 4, descY - 5);
            } else {
                canvas->setTextColor(TFT_LIGHTGRAY);
                canvas->drawString((String(I18N::get(TXT_ID)) + String(selSat.noradId)).c_str(), rightX + 48, descY + 20);
                
                if (selSat.tle.line1.length() >= 32) {
                    uint32_t currentSimTime = current_unix + timeMachineOffset;
                    uint32_t satEpoch = TLEUpdater::parseTleEpochPublic(selSat.tle.line1);
                    int ageDays = -1;
                    if (satEpoch > 0 && currentSimTime >= satEpoch) {
                        ageDays = (currentSimTime - satEpoch) / 86400;
                    }
                    
                    char ageBuf[32];
                    uint16_t ageColor = TFT_GREEN;
                    if (ageDays < 0) {
                        sprintf(ageBuf, "%s", I18N::get(TXT_GP_AGE_NA));
                        ageColor = TFT_RED;
                    } else {
                        sprintf(ageBuf, "%s%dd", I18N::get(TXT_GP_AGE), ageDays);
                        if (ageDays <= 7) ageColor = TFT_GREEN;
                        else if (ageDays <= 14) ageColor = TFT_ORANGE;
                        else ageColor = TFT_RED;
                    }
                    canvas->setTextColor(ageColor);
                    int ageW = canvas->textWidth(ageBuf);
                    canvas->drawString(ageBuf, width - ageW - 4, descY - 5);
                } else {
                    canvas->setTextColor(TFT_RED);
                    canvas->drawString(I18N::get(TXT_GP_AGE_NA), width - canvas->textWidth(I18N::get(TXT_GP_AGE_NA)) - 4, descY - 5);
                }
            }
            
            descY += 36;
            
            // 联网期间主动释放未勾选预览算子，为 WiFi/Socket 腾出连续堆空间（纯 Core 1 线程安全）
            if (g_networkActive || recentLaunchDownloading || isDownloadingCustom) {
                s_previewSatCalc.reset();
                s_previewSatNorad = -1;
            }
            // 已勾选卫星直接复用 selSat.calc；未勾选卫星按需懒加载单个预览算子（退出百科或联网时自动释放，0 永久静态开销）
            SGP4Calc* activePreviewCalc = (realIdx == SantaTracker::VIRTUAL_ENC_INDEX) ? nullptr : selSat.calc.get();
            if (realIdx != SantaTracker::VIRTUAL_ENC_INDEX && !activePreviewCalc && !g_networkActive && !recentLaunchDownloading && !isDownloadingCustom &&
                selSat.type != SAT_TYPE_GEO_TV &&
                selSat.tle.line1.length() >= 68 && selSat.tle.line2.length() >= 68) {
                if (!s_previewSatCalc || s_previewSatNorad != selSat.noradId || s_previewSatLine1 != selSat.tle.line1) {
                    if (!s_previewSatCalc) {
                        s_previewSatCalc.reset(new SGP4Calc());
                    }
                    s_previewSatCalc->init(selSat.tle);
                    s_previewSatNorad = selSat.noradId;
                    s_previewSatLine1 = selSat.tle.line1;
                }
                activePreviewCalc = s_previewSatCalc.get();
            }

            double tx, ty, tz;
            bool isTracking = false;
            double az = 0, el = 0, dist = 0;
            
            if (activePreviewCalc && activePreviewCalc->getTEME(current_unix + timeMachineOffset, tx, ty, tz)) {
                double gmst = CoordTransform::getGMST(CoordTransform::unixToJulian(current_unix + timeMachineOffset));
                ECEFCoord satEcef = CoordTransform::temeToECEF(tx, ty, tz, gmst);
                GeodeticCoord obsGeo = {baseUserLat, baseUserLon, baseUserAlt / 1000.0};
                TopocentricCoord topo = CoordTransform::ecefToTopocentric(obsGeo, satEcef);
                az = topo.az; el = topo.el; dist = topo.range;
                if (el > 0) isTracking = true;
            }
            
            int radioY = bottomLimit;
            canvas->setTextColor(TFT_LIGHTGRAY);
            
            // 1. 基础文字描述 (简介)
            Language currL = I18N::getLanguage();
            bool isZh = (currL == LANG_ZH);
            String finalDesc = "";
            String deorbPrefix = "";
            String specBlock = "";

            if (realIdx == SantaTracker::VIRTUAL_ENC_INDEX) {
                String statusStr = "";
                if (SantaTracker::isFlightPhase()) {
                    if (currL == LANG_ZH) statusStr = "运行状态: [+] 环球巡航投递中";
                    else if (currL == LANG_JA) statusStr = "運用状態: [+] 世界巡航配送中";
                    else if (currL == LANG_ES) statusStr = "Estado: [+] En vuelo global";
                    else statusStr = "Status: [+] Cruising & Delivering";
                } else {
                    if (currL == LANG_ZH) statusStr = "运行状态: [*] 北极母港整备中";
                    else if (currL == LANG_JA) statusStr = "運用状態: [*] 北極母港整備中";
                    else if (currL == LANG_ES) statusStr = "Estado: [*] En base Polo Norte";
                    else statusStr = "Status: [*] North Pole Base";
                }

                finalDesc = statusStr + "\n\n" + I18N::get(TXT_SANTA_ENC_DESC);

                char specBuf[256];
                if (currL == LANG_ZH) {
                    snprintf(specBuf, sizeof(specBuf),
                             "\n国际标识: 0000-SANTA\n"
                             "巡航动力: 9 驯鹿推进 (领航: 鲁道夫)\n"
                             "运行速度: 约 Mach 3000\n"
                             "巡航高度: 30 km (平流层/亚轨道)\n"
                             "雷达回波: 红鼻子极强红外特征");
                } else if (currL == LANG_JA) {
                    snprintf(specBuf, sizeof(specBuf),
                             "\n国際識別: 0000-SANTA\n"
                             "巡航動力: 9トナカイ (先導: ルドルフ)\n"
                             "飛行速度: 約マッハ 3000\n"
                             "巡航高度: 30 km\n"
                             "レーダー: 赤鼻強力赤外線シグネチャ");
                } else if (currL == LANG_ES) {
                    snprintf(specBuf, sizeof(specBuf),
                             "\nID COSPAR: 0000-SANTA\n"
                             "Propulsion: 9 renos (Lider: Rudolf)\n"
                             "Velocidad: Mach 3000\n"
                             "Altitud: 30 km\n"
                             "Firma radar: Nariz roja infrarroja");
                } else {
                    snprintf(specBuf, sizeof(specBuf),
                             "\nCOSPAR: 0000-SANTA\n"
                             "Propulsion: 9 Reindeer (Rudolf)\n"
                             "Speed: ~Mach 3000\n"
                             "Alt: 30 km\n"
                             "Radar: Red Nose High-IR");
                }
                specBlock = String(specBuf);

                float sLat = 0, sLon = 0;
                SantaTracker::getCurrentCoord(sLat, sLon);
                char locBuf[64];
                snprintf(locBuf, sizeof(locBuf), "\nLat: %+.1f°  Lon: %+.1f°", sLat, sLon);
                specBlock += String(locBuf);
            } else {
                if (realIdx >= NUM_BUILTIN_SATELLITES) {
                if (selSat.description && strlen(selSat.description) > 0) {
                    finalDesc = selSat.description;
                } else {
                    finalDesc = I18N::get(TXT_CUSTOM_ADDED_SAT);
                }
            } else {
                const char* localDesc = I18N::getSatDescription(selSat.noradId);
                if (localDesc) {
                    finalDesc = localDesc;
                } else if (selSat.description) {
                    finalDesc = selSat.description;
                }
            }
            
            // 已脱轨航天器科普特别前缀
            deorbPrefix = "";
            if (selSat.opsStatus == 'D') {
                if (currL == LANG_ZH) {
                    deorbPrefix = "【已再入烧毁】该航天器已脱轨坠毁并结束运行，轨道根数保留供历史科普参考。\n\n";
                } else if (currL == LANG_JA) {
                    deorbPrefix = "【大気圏再突入済】本目標は大気圏に再突入し運用終了、軌道要素は参考保存されています。\n\n";
                } else if (currL == LANG_ES) {
                    deorbPrefix = "[DESORBITADO] Ha reentrado en la atmosfera terrestre. Orbita historica para referencia.\n\n";
                } else {
                    deorbPrefix = "[DECAYED/DEORBITED] Re-entered Earth's atmosphere. Historical orbit kept for reference.\n\n";
                }
                finalDesc = deorbPrefix + finalDesc;
            }
            
            // 2. 拼接轨道参数与遥测细节 (COSPAR/周期/速度/倾角/高度/方位/仰角/频率)
            specBlock = "";
            
            // 运行状态：置于百科词条下方、“国际标识”字段之前
            {
                char opsBuf[128];
                const char* opsTitle = "运行状态:";
                if (currL == LANG_JA) opsTitle = "運用状態:";
                else if (currL == LANG_ES) opsTitle = "Estado:";
                else if (currL == LANG_EN) opsTitle = "Status:";
                
                if (selSat.noradId == 100532) {
                    if (currL == LANG_ZH) snprintf(opsBuf, sizeof(opsBuf), "\n\n%s [+] 往L2转移轨道巡航中", opsTitle);
                    else if (currL == LANG_JA) snprintf(opsBuf, sizeof(opsBuf), "\n\n%s [+] L2遷移軌道巡航中", opsTitle);
                    else if (currL == LANG_ES) snprintf(opsBuf, sizeof(opsBuf), "\n\n%s [+] En transito a L2", opsTitle);
                    else snprintf(opsBuf, sizeof(opsBuf), "\n\n%s [+] In-transit to L2", opsTitle);
                } else if (selSat.noradId == 34937) {
                    if (currL == LANG_ZH) snprintf(opsBuf, sizeof(opsBuf), "\n\n%s [P] 已退役(液氦耗尽)", opsTitle);
                    else if (currL == LANG_JA) snprintf(opsBuf, sizeof(opsBuf), "\n\n%s [P] 退役(液ヘリウム枯渇)", opsTitle);
                    else if (currL == LANG_ES) snprintf(opsBuf, sizeof(opsBuf), "\n\n%s [P] Retirado (Helio agotado)", opsTitle);
                    else snprintf(opsBuf, sizeof(opsBuf), "\n\n%s [P] Retired (Helium depleted)", opsTitle);
                } else {
                    snprintf(opsBuf, sizeof(opsBuf), "\n\n%s %s", opsTitle, I18N::getOpsStatusStr(selSat.opsStatus));
                }
                specBlock += String(opsBuf);
            }
            
            if (selSat.tle.line1.length() >= 60 && selSat.tle.line2.length() >= 60) {
                // 解析 COSPAR 国际标识
                String cospar = "";
                if (selSat.tle.line1.length() >= 17) {
                    String rawCospar = selSat.tle.line1.substring(9, 17);
                    rawCospar.trim();
                    if (rawCospar.length() >= 5) {
                        String yrStr = rawCospar.substring(0, 2);
                        int yr = yrStr.toInt();
                        String trueYr = (yr >= 50) ? ("19" + yrStr) : ("20" + yrStr);
                        cospar = trueYr + "-" + rawCospar.substring(2);
                    }
                }
                
                float inclination = selSat.tle.line2.substring(8, 16).toFloat();
                String eccRaw = selSat.tle.line2.substring(26, 33);
                eccRaw.trim();
                float eccentricity = 0.0f;
                if (eccRaw.length() > 0) {
                    eccentricity = ("0." + eccRaw).toFloat();
                }
                float meanMotion = selSat.tle.line2.substring(52, 63).toFloat();
                
                float periodMin = 0.0f;
                float perigee = 0.0f;
                float apogee = 0.0f;
                if (meanMotion > 0) {
                    periodMin = 1440.0f / meanMotion;
                    double n = meanMotion * 2.0 * 3.141592653589793 / 86400.0;
                    double mu = 3.986004418e14;
                    double a = pow(mu / (n * n), 1.0 / 3.0) / 1000.0;
                    perigee = a * (1.0f - eccentricity) - 6378.137f;
                    apogee = a * (1.0f + eccentricity) - 6378.137f;
                    if (perigee < 0) perigee = 0;
                    if (apogee < 0) apogee = 0;
                }
                
                // 实时 SGP4 速度计算 (km/s)
                double tx, ty, tz, vx, vy, vz;
                double realSpeed = 0.0;
                if (activePreviewCalc && activePreviewCalc->getTEME(current_unix + timeMachineOffset, tx, ty, tz, vx, vy, vz)) {
                    realSpeed = sqrt(vx * vx + vy * vy + vz * vz);
                }
                
                Language currL = I18N::getLanguage();
                char specBuf[256];
                if (currL == LANG_ZH) {
                    if (periodMin >= 120.0f) {
                        snprintf(specBuf, sizeof(specBuf),
                                 "\n国际标识: %s\n"
                                 "轨道周期: %.2f小时\n"
                                 "运行速度: %.2f km/s\n"
                                 "轨道倾角: %.2f°\n"
                                 "近/远地点: %.0f/%.0f km",
                                 cospar.length() > 0 ? cospar.c_str() : "未知",
                                 periodMin / 60.0f,
                                 realSpeed > 0 ? realSpeed : 7.66,
                                 inclination,
                                 perigee, apogee);
                    } else {
                        snprintf(specBuf, sizeof(specBuf),
                                 "\n国际标识: %s\n"
                                 "轨道周期: %.1f分钟\n"
                                 "运行速度: %.2f km/s\n"
                                 "轨道倾角: %.2f°\n"
                                 "近/远地点: %.0f/%.0f km",
                                 cospar.length() > 0 ? cospar.c_str() : "未知",
                                 periodMin,
                                 realSpeed > 0 ? realSpeed : 7.66,
                                 inclination,
                                 perigee, apogee);
                    }
                } else if (currL == LANG_JA) {
                    if (periodMin >= 120.0f) {
                        snprintf(specBuf, sizeof(specBuf),
                                 "\n国際識別: %s\n"
                                 "周回周期: %.2f時間\n"
                                 "飛行速度: %.2f km/s\n"
                                 "軌道傾角: %.2f°\n"
                                 "近/遠地点: %.0f/%.0f km",
                                 cospar.length() > 0 ? cospar.c_str() : "不明",
                                 periodMin / 60.0f,
                                 realSpeed > 0 ? realSpeed : 7.66,
                                 inclination,
                                 perigee, apogee);
                    } else {
                        snprintf(specBuf, sizeof(specBuf),
                                 "\n国際識別: %s\n"
                                 "周回周期: %.1f分\n"
                                 "飛行速度: %.2f km/s\n"
                                 "軌道傾角: %.2f°\n"
                                 "近/遠地点: %.0f/%.0f km",
                                 cospar.length() > 0 ? cospar.c_str() : "不明",
                                 periodMin,
                                 realSpeed > 0 ? realSpeed : 7.66,
                                 inclination,
                                 perigee, apogee);
                    }
                } else if (currL == LANG_ES) {
                    if (periodMin >= 120.0f) {
                        snprintf(specBuf, sizeof(specBuf),
                                 "\nID COSPAR: %s\n"
                                 "Periodo: %.2fh\n"
                                 "Velocidad: %.2f km/s\n"
                                 "Inclinacion: %.2f°\n"
                                 "Perigeo/Apogeo: %.0f/%.0f km",
                                 cospar.length() > 0 ? cospar.c_str() : "N/A",
                                 periodMin / 60.0f,
                                 realSpeed > 0 ? realSpeed : 7.66,
                                 inclination,
                                 perigee, apogee);
                    } else {
                        snprintf(specBuf, sizeof(specBuf),
                                 "\nID COSPAR: %s\n"
                                 "Periodo: %.1f min\n"
                                 "Velocidad: %.2f km/s\n"
                                 "Inclinacion: %.2f°\n"
                                 "Perigeo/Apogeo: %.0f/%.0f km",
                                 cospar.length() > 0 ? cospar.c_str() : "N/A",
                                 periodMin,
                                 realSpeed > 0 ? realSpeed : 7.66,
                                 inclination,
                                 perigee, apogee);
                    }
                } else {
                    if (periodMin >= 120.0f) {
                        snprintf(specBuf, sizeof(specBuf),
                                 "\nCOSPAR: %s\n"
                                 "Period: %.2fh\n"
                                 "Speed: %.2f km/s\n"
                                 "Incl: %.2f°\n"
                                 "Alt: %.0f/%.0f km",
                                 cospar.length() > 0 ? cospar.c_str() : "N/A",
                                 periodMin / 60.0f,
                                 realSpeed > 0 ? realSpeed : 7.66,
                                 inclination,
                                 perigee, apogee);
                    } else {
                        snprintf(specBuf, sizeof(specBuf),
                                 "\nCOSPAR: %s\n"
                                 "Period: %.1f min\n"
                                 "Speed: %.2f km/s\n"
                                 "Incl: %.2f°\n"
                                 "Alt: %.0f/%.0f km",
                                 cospar.length() > 0 ? cospar.c_str() : "N/A",
                                 periodMin,
                                 realSpeed > 0 ? realSpeed : 7.66,
                                 inclination,
                                 perigee, apogee);
                    }
                }
                specBlock += String(specBuf);
            }
            
            // 实时观察数据 (方位角 / 仰角) 分开单行显示，确保“仰角:”作为行首键名高亮为绿色
            if (isTracking) {
                char radioBuf[128];
                if (currL == LANG_ZH) {
                    snprintf(radioBuf, sizeof(radioBuf), "\n方位角: %03.0f°\n仰角: %02.0f°", az, el);
                } else if (currL == LANG_JA) {
                    snprintf(radioBuf, sizeof(radioBuf), "\n方位角: %03.0f°\n仰角: %02.0f°", az, el);
                } else if (currL == LANG_ES) {
                    snprintf(radioBuf, sizeof(radioBuf), "\nAzimut: %03.0f°\nElevacion: %02.0f°", az, el);
                } else {
                    snprintf(radioBuf, sizeof(radioBuf), "\nAzimuth: %03.0f°\nElevation: %02.0f°", az, el);
                }
                specBlock += String(radioBuf);
            }
            
            // 无线电频段细节 (下行 / 上行 / 亚音 / 调制模式)
            if (selSat.downlinkFreq.length() > 0) {
                char freqBuf[128];
                if (currL == LANG_ZH) {
                    snprintf(freqBuf, sizeof(freqBuf), "\n下行: %s MHz", selSat.downlinkFreq.c_str());
                } else if (currL == LANG_JA) {
                    snprintf(freqBuf, sizeof(freqBuf), "\n受信(Rx): %s MHz", selSat.downlinkFreq.c_str());
                } else if (currL == LANG_ES) {
                    snprintf(freqBuf, sizeof(freqBuf), "\nRx (Bajada): %s MHz", selSat.downlinkFreq.c_str());
                } else {
                    snprintf(freqBuf, sizeof(freqBuf), "\nRx: %s MHz", selSat.downlinkFreq.c_str());
                }
                specBlock += String(freqBuf);
            }
            if (selSat.uplinkFreq.length() > 0) {
                char txBuf[128];
                if (currL == LANG_ZH) {
                    snprintf(txBuf, sizeof(txBuf), "\n上行: %s MHz", selSat.uplinkFreq.c_str());
                } else if (currL == LANG_JA) {
                    snprintf(txBuf, sizeof(txBuf), "\n送信(Tx): %s MHz", selSat.uplinkFreq.c_str());
                } else if (currL == LANG_ES) {
                    snprintf(txBuf, sizeof(txBuf), "\nTx (Subida): %s MHz", selSat.uplinkFreq.c_str());
                } else {
                    snprintf(txBuf, sizeof(txBuf), "\nTx: %s MHz", selSat.uplinkFreq.c_str());
                }
                specBlock += String(txBuf);
            }
            if (selSat.tone.length() > 0) {
                char toneBuf[64];
                if (currL == LANG_ZH) {
                    snprintf(toneBuf, sizeof(toneBuf), "\n亚音: %s", selSat.tone.c_str());
                } else if (currL == LANG_JA) {
                    snprintf(toneBuf, sizeof(toneBuf), "\nトーン: %s", selSat.tone.c_str());
                } else if (currL == LANG_ES) {
                    snprintf(toneBuf, sizeof(toneBuf), "\nTono: %s", selSat.tone.c_str());
                } else {
                    snprintf(toneBuf, sizeof(toneBuf), "\nTone: %s", selSat.tone.c_str());
                }
                specBlock += String(toneBuf);
            }
            if (selSat.radioMode.length() > 0) {
                char modeBuf[64];
                if (currL == LANG_ZH) {
                    snprintf(modeBuf, sizeof(modeBuf), "\n调制模式: %s", selSat.radioMode.c_str());
                } else if (currL == LANG_JA) {
                    snprintf(modeBuf, sizeof(modeBuf), "\n変調方式: %s", selSat.radioMode.c_str());
                } else if (currL == LANG_ES) {
                    snprintf(modeBuf, sizeof(modeBuf), "\nModulacion: %s", selSat.radioMode.c_str());
                } else {
                    snprintf(modeBuf, sizeof(modeBuf), "\nMode: %s", selSat.radioMode.c_str());
                }
                specBlock += String(modeBuf);
            }
            } // end of else (normal sat specBlock)
            
            String fullTextToRender = finalDesc + specBlock;
            
            if (fullTextToRender.length() > 0) {
                int currLang = I18N::getLanguage();
                if (realIdx != g_descLastSatIndex || currLang != g_descLastLang) {
                    g_descLastSatIndex = realIdx;
                    g_descLastLang = currLang;
                    g_descManualScrolled = false;
                    g_descManualYOffset = 0;
                    g_descWrappedLines.clear();
                    s_deorbWarningLineCount = 0;
                    if (selSat.opsStatus == 'D' && deorbPrefix.length() > 0) {
                        std::vector<String> deorbLines;
                        TextUtils::wrapTextIntoLines(canvas, deorbPrefix, width - rightX - 5, deorbLines);
                        s_deorbWarningLineCount = deorbLines.size();
                    }
                    TextUtils::wrapTextIntoLines(canvas, fullTextToRender, width - rightX - 5, g_descWrappedLines);
                    
                    // 动态计算底部标签组占用的高度
                    g_descLabelAreaHeight = 20;
                    const EncyclopediaEntry* entry = Encyclopedia::getEntryByNorad(selSat.noradId);
                    if (entry) {
                        int simX = rightX;
                        int simY = 0;
                        String catName = Encyclopedia::getCategoryName(entry->category);
                        int catW = canvas->textWidth(catName.c_str()) + 6;
                        simX += catW + 4;
                        
                        uint32_t flags = entry->flags;
                        uint32_t allFlags[] = {
                            FLAG_VISIBLE, FLAG_CREWED, FLAG_HISTORIC, FLAG_ROCKET_BODY,
                            FLAG_DEBRIS, FLAG_WEATHER, FLAG_RADIO, FLAG_NAVIGATION,
                            FLAG_SCIENCE, FLAG_EARTH_OBS
                        };
                        for (uint32_t f : allFlags) {
                            if (flags & f) {
                                String fName = Encyclopedia::getFlagName(f);
                                if (fName.length() > 0) {
                                    if (catName.indexOf(fName) != -1 || fName.indexOf(catName) != -1) {
                                        continue;
                                    }
                                    int fW = canvas->textWidth(fName.c_str()) + 6;
                                    if (simX + fW > width - 4) {
                                        simY += 15;
                                        simX = rightX;
                                    }
                                    simX += fW + 4;
                                }
                            }
                        }
                        g_descLabelAreaHeight = simY + 20;
                    }
                    
                    g_lastSatSelectTime = millis();
                }

                if (!g_descWrappedLines.empty()) {
                    int descAreaHeight = bottomLimit - descY;
                    int totalLines = g_descWrappedLines.size();
                    int totalHeight = totalLines * 13 + g_descLabelAreaHeight;
                    g_descMaxScroll = (totalHeight > descAreaHeight) ? (totalHeight - descAreaHeight + 13) : 0;
                    
                    // 弹簧物理平滑翻页驱动器
                    static SpringValue s_smoothDescY(0.0f, SpringConfig::Default);
                    static int s_lastTrackedSatIdx = -1;
                    if (s_lastTrackedSatIdx != realIdx) {
                        s_lastTrackedSatIdx = realIdx;
                        s_smoothDescY.jump(0.0f);
                    }
                    
                    if (g_descBounceTrigger != 0) {
                        s_smoothDescY.shift((float)g_descBounceTrigger);
                        g_descBounceTrigger = 0;
                    }
                    
                    int yOffset = 0;
                    if (g_descManualScrolled) {
                        // 手动按中括号翻页模式下，平滑滑动到目标偏移
                        s_smoothDescY.set((float)g_descManualYOffset);
                        yOffset = (int)roundf(s_smoothDescY.get());
                    } else if (totalHeight > descAreaHeight && descAreaHeight > 13) {
                        // 默认自动循环滚动
                        int scrollSpeedMs = 66;
                        int holdTimeMs = 1500;
                        int scrollRange = totalHeight - descAreaHeight + 13;
                        int cycleTime = scrollRange * scrollSpeedMs + holdTimeMs * 2;
                        int t = (millis() - g_lastSatSelectTime) % cycleTime;
                        if (t < holdTimeMs) yOffset = 0;
                        else if (t < cycleTime - holdTimeMs) yOffset = (t - holdTimeMs) / scrollSpeedMs;
                        else yOffset = scrollRange;
                        s_smoothDescY.jump((float)yOffset);
                    }
                    
                    canvas->setClipRect(rightX, descY, width - rightX, descAreaHeight);
                    
                    // 1. 绘制简介与属性细节文本
                    bool inOpsStatusContinuation = false;
                    for (int idx = 0; idx < totalLines; idx++) {
                        int lineY = descY + idx * 13 - yOffset;
                        if (lineY >= descY - 13 && lineY <= descY + descAreaHeight) {
                            String line = g_descWrappedLines[idx];
                            
                            // 脱轨烧毁科普警示行（以确切行范围 idx < s_deorbWarningLineCount 驱动，无条件严格着色）
                            if (selSat.opsStatus == 'D' && idx < s_deorbWarningLineCount) {
                                int tagEnd = -1;
                                int closeZh = line.indexOf("】");
                                if (closeZh != -1) {
                                    tagEnd = closeZh + strlen("】");
                                } else {
                                    int closeEn = line.indexOf(']');
                                    if (closeEn != -1) tagEnd = closeEn + 1;
                                }
                                
                                if (tagEnd != -1) {
                                    // 包含闭合括号的行：括号及内部标签为红色，后续描述文字为橙色
                                    String tagPart = line.substring(0, tagEnd);
                                    String restPart = line.substring(tagEnd);
                                    canvas->setTextColor(TFT_RED);
                                    canvas->drawString(tagPart.c_str(), rightX, lineY);
                                    int tagW = canvas->textWidth(tagPart.c_str());
                                    canvas->setTextColor(TFT_ORANGE);
                                    canvas->drawString(restPart.c_str(), rightX + tagW, lineY);
                                } else {
                                    // 该段落后续折行：整行均为橙色
                                    canvas->setTextColor(TFT_ORANGE);
                                    canvas->drawString(line.c_str(), rightX, lineY);
                                }
                                inOpsStatusContinuation = false;
                                continue;
                            }

                            int colonIdx = line.indexOf(':');
                            if (colonIdx == -1) {
                                colonIdx = line.indexOf("：");
                            }
                            
                            if (colonIdx != -1) {
                                String namePart = line.substring(0, colonIdx + 1);
                                String valuePart = line.substring(colonIdx + 1);
                                
                                canvas->setTextColor(TFT_GREEN);
                                canvas->drawString(namePart.c_str(), rightX, lineY);
                                
                                int nameW = canvas->textWidth(namePart.c_str());
                                
                                // 运行状态的值部分应用专属 SATCAT 运营色彩
                                bool isStatusField = (namePart.startsWith("运行状态") || namePart.startsWith("運用状態") || 
                                                      namePart.startsWith("Status") || namePart.startsWith("Estado"));
                                uint16_t valColor = isStatusField ? I18N::getOpsStatusColor(selSat.opsStatus) : TFT_LIGHTGRAY;
                                inOpsStatusContinuation = isStatusField;
                                
                                canvas->setTextColor(valColor);
                                canvas->drawString(valuePart.c_str(), rightX + nameW, lineY);
                            } else {
                                // 没有冒号的延续行：若上一行是运行状态字段，则继承状态专属颜色；否则普通文本
                                if (inOpsStatusContinuation && line.length() > 0) {
                                    canvas->setTextColor(I18N::getOpsStatusColor(selSat.opsStatus));
                                } else {
                                    inOpsStatusContinuation = false;
                                    canvas->setTextColor(TFT_LIGHTGRAY);
                                }
                                canvas->drawString(line.c_str(), rightX, lineY);
                            }
                        } else {
                            // 视口外行亦保持状态标记同步，避免翻页回滚时状态断层
                            String line = g_descWrappedLines[idx];
                            int colonIdx = line.indexOf(':');
                            if (colonIdx == -1) colonIdx = line.indexOf("：");
                            if (colonIdx != -1) {
                                String namePart = line.substring(0, colonIdx + 1);
                                inOpsStatusContinuation = (namePart.startsWith("运行状态") || namePart.startsWith("運用状態") || 
                                                           namePart.startsWith("Status") || namePart.startsWith("Estado"));
                            } else if (line.length() == 0) {
                                inOpsStatusContinuation = false;
                            }
                        }
                    }
                    
                    // 2. 绘制分类标签与属性标记（置于文本下方，完美随滚动滑动）
                    int badgeY = descY + totalLines * 13 + 6 - yOffset;
                    const EncyclopediaEntry* entry = Encyclopedia::getEntryByNorad(selSat.noradId);
                    if (entry) {
                        int currBadgeX = rightX;
                        int currBadgeY = badgeY;
                        
                        String catName = Encyclopedia::getCategoryName(entry->category);
                        int catW = canvas->textWidth(catName.c_str()) + 6;
                        
                        if (currBadgeY >= descY - 13 && currBadgeY <= descY + descAreaHeight) {
                            canvas->fillRoundRect(currBadgeX, currBadgeY, catW, 13, 2, canvas->color565(60, 80, 110));
                            canvas->setTextColor(TFT_WHITE);
                            canvas->drawString(catName.c_str(), currBadgeX + 3, currBadgeY + 1);
                        }
                        currBadgeX += catW + 4;
                        
                        uint32_t flags = entry->flags;
                        uint32_t allFlags[] = {
                            FLAG_VISIBLE, FLAG_CREWED, FLAG_HISTORIC, FLAG_ROCKET_BODY,
                            FLAG_DEBRIS, FLAG_WEATHER, FLAG_RADIO, FLAG_NAVIGATION,
                            FLAG_SCIENCE, FLAG_EARTH_OBS
                        };
                        for (uint32_t f : allFlags) {
                            if (flags & f) {
                                String fName = Encyclopedia::getFlagName(f);
                                if (fName.length() > 0) {
                                    if (catName.indexOf(fName) != -1 || fName.indexOf(catName) != -1) {
                                        continue;
                                    }
                                    int fW = canvas->textWidth(fName.c_str()) + 6;
                                    if (currBadgeX + fW > width - 4) {
                                        currBadgeY += 15;
                                        currBadgeX = rightX;
                                    }
                                    
                                    if (currBadgeY >= descY - 13 && currBadgeY <= descY + descAreaHeight) {
                                        uint16_t bgColor = canvas->color565(40, 50, 60);
                                        uint16_t textColor = TFT_LIGHTGRAY;
                                        if (f == FLAG_VISIBLE) { bgColor = canvas->color565(90, 80, 20); textColor = TFT_YELLOW; }
                                        else if (f == FLAG_CREWED) { bgColor = canvas->color565(20, 90, 50); textColor = TFT_GREEN; }
                                        else if (f == FLAG_HISTORIC) { bgColor = canvas->color565(90, 50, 20); textColor = TFT_ORANGE; }
                                        else if (f == FLAG_RADIO) { bgColor = canvas->color565(80, 30, 80); textColor = TFT_MAGENTA; }
                                        else if (f == FLAG_SCIENCE) { bgColor = canvas->color565(50, 30, 90); textColor = TFT_GOLD; }
                                        else if (f == FLAG_WEATHER) { bgColor = canvas->color565(20, 60, 90); textColor = TFT_CYAN; }
                                        else if (f == FLAG_EARTH_OBS) { bgColor = canvas->color565(30, 80, 80); textColor = TFT_GREEN; }
                                        else if (f == FLAG_NAVIGATION) { bgColor = canvas->color565(80, 20, 20); textColor = TFT_RED; }
                                        
                                        canvas->fillRoundRect(currBadgeX, currBadgeY, fW, 13, 2, bgColor);
                                        canvas->setTextColor(textColor);
                                        canvas->drawString(fName.c_str(), currBadgeX + 3, currBadgeY + 1);
                                    }
                                    currBadgeX += fW + 4;
                                }
                            }
                        }
                    }
                    
                    canvas->clearClipRect();
                } else {
                    canvas->drawString(I18N::get(TXT_NO_DESCRIPTION), rightX, descY);
                }
            } else {
                canvas->drawString(I18N::get(TXT_NO_DESCRIPTION), rightX, descY);
            }
        } else {
            if (downloadErrorMsg.length() > 0) {
                canvas->setTextColor(getBannerTextColor(downloadErrorMsg));
                TextUtils::drawWrappedText(canvas, downloadErrorMsg.c_str(), rightX, descY, width - rightX - 5, 13);
            } else {
                canvas->setTextColor(TFT_LIGHTGRAY);
                int lines = TextUtils::drawWrappedText(canvas, I18N::get(TXT_ENTER_NORAD_ADD), rightX, descY, width - rightX - 5, 13);
                
                canvas->setTextColor(TFT_YELLOW);
                canvas->drawString(I18N::get(TXT_SOURCE_CELESTRAK), rightX, descY + lines * 13 + 4);
            }
        }
    } else {
        // TAB_RECENT_LAUNCH Tab
        lockSatMutex();
        bool isLaunchEmpty = g_recentLaunches.empty();
        int totalItems = g_recentLaunches.size();
        unlockSatMutex();
        
        if (!recentLaunchDownloadSuccess && isLaunchEmpty) {
            if (recentLaunchDownloading) {
                canvas->setTextColor(TFT_YELLOW);
                canvas->drawString(I18N::get(TXT_DOWNLOADING_GP_JSONS), width/2 - canvas->textWidth(I18N::get(TXT_DOWNLOADING_GP_JSONS))/2, height/2 - 10);
                if (recentLaunchErrorMsg.length() > 0) {
                    canvas->setTextColor(TFT_LIGHTGRAY);
                    canvas->drawString(recentLaunchErrorMsg.c_str(), width/2 - canvas->textWidth(recentLaunchErrorMsg.c_str())/2, height/2 + 5);
                }
            } else {
                canvas->fillRect(10, 30, width - 20, height - 55, canvas->color565(35, 45, 55));
                canvas->drawRect(10, 30, width - 20, height - 55, TFT_YELLOW);
                
                canvas->setTextColor(TFT_YELLOW);
                canvas->drawString(I18N::get(TXT_RL_ONLINE_FEATURE), width/2 - canvas->textWidth(I18N::get(TXT_RL_ONLINE_FEATURE))/2, height/2 - 20);
                canvas->setTextColor(TFT_WHITE);
                canvas->drawString(I18N::get(TXT_PRESS_W_CONNECT_WIFI), width/2 - canvas->textWidth(I18N::get(TXT_PRESS_W_CONNECT_WIFI))/2, height/2 - 2);
                canvas->drawString(I18N::get(TXT_DOWNLOAD_LATEST_GROUPS), width/2 - canvas->textWidth(I18N::get(TXT_DOWNLOAD_LATEST_GROUPS))/2, height/2 + 8);
                
                if (recentLaunchErrorMsg.length() > 0) {
                    canvas->setTextColor(TFT_RED);
                    canvas->drawString(recentLaunchErrorMsg.c_str(), width/2 - canvas->textWidth(recentLaunchErrorMsg.c_str())/2, height/2 + 22);
                }
            }
        } else {
            int yPos = 25;
            int itemsPerPage = showBanner ? 6 : 8;
            int itemSpacing = 12;
            int startIndex = (recentLaunchSelectedIndex / itemsPerPage) * itemsPerPage;
            
            // 弹簧物理光标动画 (平滑磁吸过渡)
            static SpringValue s_cursorY_recentLaunch(24.0f, SpringConfig::Default);
            static int s_lastRecentStartIndex = -1;
            int selInPage = recentLaunchSelectedIndex - startIndex;
            if (selInPage < 0) selInPage = 0;
            if (selInPage >= itemsPerPage) selInPage = itemsPerPage - 1;
            float targetCursorY = 25.0f - 1.0f + selInPage * itemSpacing;
            
            if (s_lastRecentStartIndex != startIndex) {
                s_lastRecentStartIndex = startIndex;
                s_cursorY_recentLaunch.jump(targetCursorY);
            } else {
                s_cursorY_recentLaunch.set(targetCursorY);
            }
            
            // 触发勾选上限物理震动 (Shake/Wiggle)
            if (g_satLimitShakeTrigger != 0) {
                s_limitShakeX.shift((float)g_satLimitShakeTrigger);
                g_satLimitShakeTrigger = 0;
            }
            int shakeX = (int)roundf(s_limitShakeX.get());

            // 绘制平滑物理磁吸高亮框
            float smoothY = s_cursorY_recentLaunch.get();
            canvas->fillRoundRect(2 + shakeX, (int)roundf(smoothY), 82, 12, 3, canvas->color565(0, 120, 255));
            
            for (int i = 0; i < itemsPerPage && (startIndex + i) < totalItems; i++) {
                int index = startIndex + i;
                if (index == recentLaunchSelectedIndex) {
                    canvas->setTextColor(TFT_WHITE);
                } else {
                    canvas->setTextColor(TFT_LIGHTGRAY);
                }
                int curShakeX = (index == recentLaunchSelectedIndex) ? shakeX : 0;
                
                lockSatMutex();
                bool itemSel = (index < (int)g_recentLaunches.size()) ? g_recentLaunches[index].selected : false;
                String nameStr = (index < (int)g_recentLaunches.size()) ? g_recentLaunches[index].displayName : "";
                bool itemIsGroup = (index < (int)g_recentLaunches.size()) ? g_recentLaunches[index].isGroup : false;
                int itemSatCnt = (index < (int)g_recentLaunches.size()) ? g_recentLaunches[index].satelliteCount : 0;
                unlockSatMutex();

                String checkBox = itemSel ? "[x]" : "[ ]";
                canvas->drawString(checkBox.c_str(), 4 + curShakeX, yPos);
                
                if (itemIsGroup) {
                    nameStr = nameStr + " (" + String(itemSatCnt) + ")";
                }
                if (index == recentLaunchSelectedIndex) {
                    TextUtils::drawScrollingText(canvas, nameStr.c_str(), 28 + curShakeX, yPos, 56, TFT_WHITE);
                } else {
                    if (nameStr.length() > 9) nameStr = nameStr.substring(0, 7) + "..";
                    canvas->drawString(nameStr.c_str(), 28, yPos);
                }
                
                yPos += itemSpacing;
            }
            
            // Draw page index indicator for Recent Launch
            if (totalItems > 0) {
                int totalCount = totalItems;
                int totalPages = (totalCount + itemsPerPage - 1) / itemsPerPage;
                int currentPage = (recentLaunchSelectedIndex / itemsPerPage) + 1;
                int currentIdx = recentLaunchSelectedIndex + 1;
                char pageBuf[32];
                int totalSel = getTotalSelectedSatelliteCount();
                sprintf(pageBuf, "(%d/%d) [%d/%d]", currentPage, totalPages, totalSel, MAX_TOTAL_SELECTED_SATS);
                canvas->setTextColor(totalSel >= MAX_TOTAL_SELECTED_SATS ? TFT_ORANGE : canvas->color565(110, 150, 180));
                int pageX = 8;
            if (pageX + canvas->textWidth(pageBuf) > 83) {
                sprintf(pageBuf, "(%d/%d)[%d/%d]", currentPage, totalPages, totalSel, MAX_TOTAL_SELECTED_SATS);
                if (pageX + canvas->textWidth(pageBuf) > 83) {
                    pageX = 4;
                    if (pageX + canvas->textWidth(pageBuf) > 83) {
                        sprintf(pageBuf, "%d/%d [%d/%d]", currentPage, totalPages, totalSel, MAX_TOTAL_SELECTED_SATS);
                    }
                }
            }
            canvas->drawString(pageBuf, pageX, showBanner ? (bottomLimit - 12) : (bottomLimit - 13));
            }
            
            canvas->drawFastVLine(85, 20, bottomLimit - 20, TFT_DARKGREY);
            
            int rightX = 89;
            lockSatMutex();
            RecentLaunchItem itemCopy;
            bool hasSelectedItem = false;
            if (recentLaunchSelectedIndex >= 0 && recentLaunchSelectedIndex < (int)g_recentLaunches.size()) {
                itemCopy = g_recentLaunches[recentLaunchSelectedIndex];
                hasSelectedItem = true;
            }
            unlockSatMutex();

            if (hasSelectedItem) {
                RecentLaunchItem& item = itemCopy;
                
                uint32_t epoch = item.epoch;
                float inclination = item.inclination;
                float avgAlt = item.avgAlt;
                
                uint32_t currentSimTime = current_unix + timeMachineOffset;
                int ageDays = -1;
                if (epoch > 0 && currentSimTime >= epoch) {
                    ageDays = (currentSimTime - epoch) / 86400;
                }
                
                // 1. Calculate Recommended Stars based on occupancy (compact trains get higher score)
                // Use ASCII stars '*' and '-' to avoid font rendering blocks on Cardputer TFT screen
                const char* stars = "**---";
                if (item.occupancy < 10.0f) stars = "*****";
                else if (item.occupancy < 30.0f) stars = "****-";
                else if (item.occupancy < 90.0f) stars = "***--";
                else stars = "**---";
                
                if (!recentLaunchInObjectsView) {
                    int y0 = (I18N::getLanguage() == LANG_ZH && !showBanner) ? 22 : 23;
                    auto getY = [&](int k) -> int {
                        if (I18N::getLanguage() == LANG_ZH && !showBanner) {
                            if (k <= 6) return y0 + k * 11;
                            if (k == 7) return y0 + 6 * 11 + 12; // 88 + 12 = 100
                            if (k == 8) return y0 + 6 * 11 + 24; // 88 + 24 = 112
                            if (k == 9) return y0 + 6 * 11 + 36; // 88 + 36 = 124
                        }
                        int localStep = (I18N::getLanguage() == LANG_ZH && !showBanner) ? 11 : 10;
                        return y0 + k * localStep;
                    };
                    
                    int starsW = canvas->textWidth(stars);
                    TextUtils::drawScrollingText(canvas, item.displayName.c_str(), rightX, getY(0), width - rightX - starsW - 8, TFT_GOLD);
                    
                    canvas->setTextColor(TFT_YELLOW);
                    canvas->drawString(stars, width - starsW - 4, getY(0));
                    
                    canvas->setTextColor(TFT_CYAN);
                    String formattedBatch = item.batchId;
                    if (item.batchId.length() == 5 && isdigit(item.batchId[0]) && isdigit(item.batchId[1])) {
                        int yr = item.batchId.substring(0, 2).toInt();
                        String century = (yr >= 50) ? "19" : "20";
                        formattedBatch = century + item.batchId.substring(0, 2) + "-" + item.batchId.substring(2);
                    }
                    canvas->drawString(("Batch: " + formattedBatch).c_str(), rightX, getY(1));
                    
                    // Age placement on the right
                    char ageBuf[32];
                    uint16_t ageColor = TFT_GREEN;
                    if (ageDays < 0) {
                        sprintf(ageBuf, "%sN/A", I18N::get(TXT_RL_AGE));
                        ageColor = TFT_RED;
                    } else {
                        sprintf(ageBuf, "%s%dd", I18N::get(TXT_RL_AGE), ageDays);
                        if (ageDays <= 7) ageColor = TFT_GREEN;
                        else if (ageDays <= 14) ageColor = TFT_ORANGE;
                        else ageColor = TFT_RED;
                    }
                    canvas->setTextColor(ageColor);
                    int ageW = canvas->textWidth(ageBuf);
                    canvas->drawString(ageBuf, width - ageW - 4, getY(1));
                    
                    char dateBuf[32];
                    if (epoch > 0) {
                        time_t tEpoch = (time_t)epoch;
                        struct tm timeinfo;
                        if (gmtime_r(&tEpoch, &timeinfo) != nullptr) {
                            strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", &timeinfo);
                        } else {
                            sprintf(dateBuf, "N/A");
                        }
                    } else {
                        sprintf(dateBuf, "N/A");
                    }
                    canvas->setTextColor(TFT_LIGHTGRAY);
                    canvas->drawString((String(I18N::get(TXT_RL_EPOCH)) + String(dateBuf)).c_str(), rightX, getY(2));
                    
                    // Draw representative satellite name
                    String repSatText = String(I18N::get(TXT_RL_REP)) + item.repSatName;
                    TextUtils::drawScrollingText(canvas, repSatText.c_str(), rightX, getY(3), width - rightX - 4, TFT_LIGHTGRAY);
                    
                    // Display real count of objects and clustered proxies
                    char satsBuf[64];
                    int proxyCount = item.proxyFormation.size();
                    if (I18N::getLanguage() == LANG_ZH || I18N::getLanguage() == LANG_JA) {
                        sprintf(satsBuf, "%s%d | 代理: %d", I18N::get(TXT_RL_OBJECTS), item.satelliteCount, proxyCount);
                    } else {
                        sprintf(satsBuf, "%s%d | Proxy: %d", I18N::get(TXT_RL_OBJECTS), item.satelliteCount, proxyCount);
                    }
                    canvas->drawString(satsBuf, rightX, getY(4));
                    
                    char orbitBuf[48];
                    sprintf(orbitBuf, "%s%dkm, %.1f*", I18N::get(TXT_RL_ORBIT), (int)avgAlt, inclination);
                    canvas->drawString(orbitBuf, rightX, getY(5));
                    
                    // Formation State & Occupancy degree
                    canvas->setTextColor(TFT_GREEN);
                    canvas->drawString(I18N::get(TXT_RL_STATUS), rightX, getY(6));
                    canvas->setTextColor(TFT_WHITE);
                    const char* formState = I18N::get(TXT_FORM_OPERATIONAL);
                    if (item.occupancy < 15.0f) formState = I18N::get(TXT_FORM_TIGHT_TRAIN);
                    else if (item.occupancy < 60.0f) formState = I18N::get(TXT_FORM_TRAIN_FORMATION);
                    else if (item.occupancy < 120.0f) formState = I18N::get(TXT_FORM_EXPANDING);
                    canvas->drawString(formState, rightX + 45, getY(6));
                    
                    char occBuf[32];
                    sprintf(occBuf, "Occ: %d*", (int)item.occupancy);
                    canvas->setTextColor(TFT_CYAN);
                    int occW = canvas->textWidth(occBuf);
                    canvas->drawString(occBuf, width - occW - 4, getY(8));
                    
                    // Distribution indicator (8 refined blocks)
                    canvas->setTextColor(TFT_GREEN);
                    canvas->drawString("Distribution:", rightX, getY(7));
                    
                    int barY = getY(8);
                    int filledCount = (int)((item.occupancy / 360.0f) * 8.0f + 0.5f);
                    if (filledCount < 1 && item.occupancy > 0.0f) filledCount = 1;
                    if (filledCount > 8) filledCount = 8;
                    
                    for (int k = 0; k < 8; k++) {
                        int bx = rightX + k * 10;
                        if (k < filledCount) {
                            canvas->fillRect(bx, barY, 8, 6, 0x07FF); // High-contrast Cyan block
                        } else {
                            canvas->drawRect(bx, barY, 8, 6, TFT_DARKGREY); // Empty block
                        }
                    }
                    
                    canvas->setTextColor(TFT_GREEN);
                    canvas->drawString(I18N::get(TXT_RL_VISIBILITY), rightX, getY(9));
                    canvas->setTextColor(TFT_YELLOW);
                    if (avgAlt >= 250 && avgAlt <= 600) {
                        canvas->drawString(I18N::get(TXT_VIS_EXCELLENT), rightX + 65, getY(9));
                    } else if (avgAlt > 0) {
                        canvas->drawString(I18N::get(TXT_VIS_MODERATE), rightX + 65, getY(9));
                    } else {
                        canvas->drawString(I18N::get(TXT_VIS_NA), rightX + 65, getY(9));
                    }
                } else {
                    String title = item.displayName + " Objects";
                    TextUtils::drawScrollingText(canvas, title.c_str(), rightX, 25, width - rightX - 4, TFT_GOLD);
                    
                    canvas->setTextColor(TFT_CYAN);
                    int startNum = recentLaunchObjectPage * 5 + 1;
                    int endNum = startNum + g_level3Objects.size() - 1;
                    char pageBuf[32];
                    sprintf(pageBuf, "Page %d (%d-%d)", recentLaunchObjectPage + 1, startNum, endNum);
                    canvas->drawString(pageBuf, rightX, 35);

                    // 翻页水平弹簧滑入动效
                    static SpringValue s_objPageOffsetX(0.0f, SpringConfig::Default);
                    static int s_lastObjPage = -1;
                    if (s_lastObjPage != recentLaunchObjectPage) {
                        if (s_lastObjPage != -1) {
                            s_objPageOffsetX.jump(recentLaunchObjectPage > s_lastObjPage ? 20.0f : -20.0f);
                            s_objPageOffsetX.set(0.0f);
                        }
                        s_lastObjPage = recentLaunchObjectPage;
                    }
                    if (g_objPageBounceTrigger != 0) {
                        s_objPageOffsetX.shift((float)g_objPageBounceTrigger);
                        g_objPageBounceTrigger = 0;
                    }
                    int slideX = (int)roundf(s_objPageOffsetX.get());
                    
                    canvas->setClipRect(rightX, 46, width - rightX, height - 46);
                    int memY = 48;
                    for (size_t s = 0; s < g_level3Objects.size(); s++) {
                        auto& obj = g_level3Objects[s];
                        String satName = obj.name;
                        if (satName.startsWith("STARLINK ")) {
                            satName = "SL " + satName.substring(9);
                        } else if (satName.startsWith("STARLINK-")) {
                            satName = "SL-" + satName.substring(9);
                        }
                        String lineText = "- " + satName + " (" + String(obj.orbit.catalogNumber) + ")";
                        
                        int altW = 0;
                        if (obj.lastGeoValid) {
                            char hBuf[16];
                            sprintf(hBuf, "%dkm", (int)obj.lastGeo.alt);
                            altW = canvas->textWidth(hBuf);
                            canvas->setTextColor(TFT_GREEN);
                            canvas->drawString(hBuf, width - altW - 2 + slideX, memY + s * 13);
                        }
                        
                        int maxW = obj.lastGeoValid ? (width - rightX - altW - 4) : (width - rightX - 4);
                        TextUtils::drawScrollingText(canvas, lineText.c_str(), rightX + slideX, memY + s * 13, maxW, TFT_LIGHTGRAY);
                    }
                    canvas->clearClipRect();
                    
                    if (g_level3Objects.empty()) {
                        if (recentLaunchDownloading) {
                            canvas->setTextColor(TFT_YELLOW);
                            canvas->drawString(I18N::get(TXT_DOWNLOADING), rightX, memY);
                        } else {
                            canvas->setTextColor(TFT_RED);
                            canvas->drawString(I18N::get(TXT_NO_OBJECTS_FOUND), rightX, memY);
                        }
                    }
                }
            }
        }
    }
    
    // Draw Bottom Guide Banner (Unified design & color logic)
    if (showBanner) {
        canvas->fillRect(0, height - 13, width, 13, canvas->color565(15, 20, 25));
        canvas->drawFastHLine(0, height - 13, width, TFT_DARKGREY);
        
        String msg = "";
        uint16_t textColor = TFT_LIGHTGRAY;
        
        if (currentSatTab == TAB_RECENT_LAUNCH) {
            if (recentLaunchDownloading) {
                textColor = TFT_YELLOW;
                msg = recentLaunchErrorMsg.length() > 0 ? recentLaunchErrorMsg : String(I18N::get(TXT_REFRESHING_GP));
            } else if (recentLaunchDownloadSuccess) {
                textColor = TFT_GREEN;
                msg = recentLaunchErrorMsg.length() > 0 ? recentLaunchErrorMsg : String(I18N::get(TXT_UPDATE_SUCCESS_CACHE));
            } else {
                textColor = getBannerTextColor(recentLaunchErrorMsg);
                msg = recentLaunchErrorMsg.length() > 0 ? recentLaunchErrorMsg : String(I18N::get(TXT_UPDATE_FAILED));
            }
        } else if (currentSatTab == TAB_ENCYCLOPEDIA) {
            msg = downloadErrorMsg;
            textColor = getBannerTextColor(downloadErrorMsg);
            if (msg.length() == 0) {
                if (SpaceAudio::isPlaying()) {
                    msg = (I18N::getLanguage() == LANG_ZH) ? "[B] 正在试听太空信标 · 按任意键停止" : "[B] Playing Beacon · Press key to stop";
                    textColor = TFT_GREEN;
                } else {
                    msg = (I18N::getLanguage() == LANG_ZH) ? "[B] 试听太空信标  [O] 卫星清单  [W] 联网" : "[B] Listen Beacon  [O] List  [W] WiFi";
                    textColor = TFT_CYAN;
                }
            }
        }
        
        if (msg.length() > 0) {
            canvas->setTextColor(textColor);
            if (canvas->textWidth(msg.c_str()) > width - 8) {
                msg = msg.substring(0, 35) + "...";
            }
            canvas->drawString(msg.c_str(), 4, height - 12);
        }
    }
    
    // Draw Delete Confirm Popup
    if (deleteConfirmIndex >= NUM_BUILTIN_SATELLITES && deleteConfirmIndex < NUM_SATELLITES && currentSatTab == TAB_ENCYCLOPEDIA) {
        canvas->fillRect(40, height/2 - 20, width - 80, 40, TFT_RED);
        canvas->drawRect(40, height/2 - 20, width - 80, 40, TFT_WHITE);
        canvas->setTextColor(TFT_WHITE);
        canvas->drawString("Delete Custom Sat?", 45, height/2 - 15);
        canvas->drawString("[y] Yes  [n] No", 45, height/2 + 5);
    }
    
    // Draw List Selection Page Help Overlay
    if (showListHelp) {
        uint16_t w = 216, h = 126;
        int x = (width - w) / 2;
        int y = (height - h) / 2;
        
        canvas->fillRect(x, y, w, h, canvas->color565(20, 30, 40));
        canvas->drawRect(x, y, w, h, TFT_LIGHTGRAY);
        
        Language currL = I18N::getLanguage();
        bool isZh = (currL == LANG_ZH);
        canvas->setTextColor(TFT_WHITE);
        canvas->setTextSize(1);
        const char* titleStr = (currL == LANG_ZH) ? "--- 列表快捷键指南 ---" : ((currL == LANG_JA) ? "--- ショートカットガイド ---" : ((currL == LANG_ES) ? "--- Guía de atajos ---" : "--- Setup Shortcuts ---"));
        canvas->drawString(titleStr, x + (w - canvas->textWidth(titleStr)) / 2, y + 5);

        auto drawHotKey = [&](const char* word, char keyChar, int dx, int dy) {
            int cx = dx;
            bool highlighted = false;
            int i = 0;
            while (word[i] != '\0') {
                int charLen = 1;
                unsigned char head = (unsigned char)word[i];
                if (head >= 0xF0) charLen = 4;
                else if (head >= 0xE0) charLen = 3;
                else if (head >= 0xC0) charLen = 2;
                
                char cstr[5] = {0};
                for (int j = 0; j < charLen && word[i + j] != '\0'; j++) {
                    cstr[j] = word[i + j];
                }
                
                if (charLen == 1 && !highlighted && tolower((unsigned char)cstr[0]) == tolower((unsigned char)keyChar) && keyChar != '\0') {
                    canvas->setTextColor(TFT_YELLOW);
                    highlighted = true;
                } else {
                    canvas->setTextColor(TFT_LIGHTGRAY);
                }
                
                canvas->drawString(cstr, cx, dy);
                cx += canvas->textWidth(cstr);
                i += charLen;
            }
        };
        
        int ty = y + 20;
        if (currentSatTab == TAB_ENCYCLOPEDIA) {
            drawHotKey(isZh ? "移动[ ; / . ]" : "Move[ ; / . ]", ';', x + 8, ty);
            drawHotKey(isZh ? "切分类[/]" : "Tab[/]", '/', x + 112, ty); ty += 14;
            
            drawHotKey(isZh ? "详情翻页[ [/] ]" : "Page[ [/] ]", '[', x + 8, ty);
            drawHotKey(isZh ? "勾选[Enter]" : "Select[Enter]", 'e', x + 112, ty); ty += 14;
            
            drawHotKey(isZh ? "删除自定[d]" : "Del Custom[d]", 'd', x + 8, ty);
            drawHotKey(isZh ? "刷新星历[w]" : "Refresh GP[w]", 'w', x + 112, ty); ty += 14;
            
            drawHotKey(isZh ? "分类筛选[f]" : "Filter[f]", 'f', x + 8, ty);
            drawHotKey(isZh ? "主题模式[Tab]" : "Theme[Tab]", 't', x + 112, ty); ty += 14;

            drawHotKey(isZh ? "返回地图[Esc]" : "Exit[Esc]", 'x', x + 8, ty); ty += 14;

        } else {
            if (recentLaunchInObjectsView) {
                drawHotKey(isZh ? "清单翻页[ [/] ]" : "Page[ [/] ]", '[', x + 8, ty);
                drawHotKey(isZh ? "主题模式[Tab]" : "Theme[Tab]", 't', x + 112, ty); ty += 14;

                drawHotKey(isZh ? "退出清单[Esc/o]" : "Back List[Esc/o]", 'o', x + 8, ty); ty += 14;
            } else {
                drawHotKey(isZh ? "移动[ ; / . ]" : "Move[ ; / . ]", ';', x + 8, ty);
                drawHotKey(isZh ? "切分类[/]" : "Tab[/]", '/', x + 112, ty); ty += 14;
                
                drawHotKey(isZh ? "勾选[Enter]" : "Select[Enter]", 'e', x + 8, ty);
                drawHotKey(isZh ? "展开清单[o]" : "Sub-List[o]", 'o', x + 112, ty); ty += 14;
                
                drawHotKey(isZh ? "云端更新[w/c]" : "Update[w/c]", 'c', x + 8, ty);
                drawHotKey(isZh ? "开关WiFi[w]" : "WiFi[w]", 'w', x + 112, ty); ty += 14;
                
                drawHotKey(isZh ? "主题模式[Tab]" : "Theme[Tab]", 't', x + 8, ty);
                drawHotKey(isZh ? "返回地图[Esc]" : "Exit[Esc]", 'x', x + 112, ty); ty += 14;
            }
        }
        
        const char* closePrompt = (currL == LANG_ZH) ? "按任意键关闭" : 
                                  ((currL == LANG_JA) ? "任意のキーを押して閉じる" :
                                  ((currL == LANG_ES) ? "Presione cualquier tecla para cerrar" : "Press any key to Close"));
        canvas->setTextColor(TFT_YELLOW);
        int promptW = canvas->textWidth(closePrompt);
        canvas->drawString(closePrompt, x + (w - promptW) / 2, y + h - 14);
    }
    
    // 绘制分类筛选弹窗
    drawCategoryFilterDialog(canvas);
}


