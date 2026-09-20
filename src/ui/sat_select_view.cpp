#include "sat_select_view.h"
#include "core/earth_renderer.h"
#include "core/position_manager.h"
#include "core/orbit_data_provider.h"
#include "core/log_manager.h"
#include "core/text_utils.h"
#include "core/observation_predictor.h"
#include "core/tle_updater.h"
#include "hal/hal_wifi.h"
#include <LittleFS.h>

extern volatile bool isDownloadingCustom;
extern int getTotalSelectedSatelliteCount();

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

SatSelectView::SatSelectView() {
}

void SatSelectView::updateFilteredList() {
    g_encyclopediaFilteredIndices.clear();
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
    if (!g_showCategoryFilterDialog) return;
    
    int w = 224;
    int h = 86;
    int x = (canvas->width() - w) / 2;
    int y = (canvas->height() - h) / 2;
    
    // 半透明深底与青色亮边框
    canvas->fillRoundRect(x, y, w, h, 4, canvas->color565(15, 20, 28));
    canvas->drawRoundRect(x, y, w, h, 4, TFT_CYAN);
    
    Language currL = I18N::getLanguage();
    
    // 4 行 × 3 列 胶囊徽章标签 (每行 3 个，共 12 个项)
    int startY = y + 5;
    int rowH = 19;
    int colW = 66;
    int startX = x + 8;
    int colGap = 5;
    
    for (int i = 0; i < 12; i++) {
        int r = i / 3;
        int c = i % 3;
        int bx = startX + c * (colW + colGap);
        int by = startY + r * rowH;
        
        bool isChecked = (i == 11) ? (g_tempCategoryMask == 0) : ((g_tempCategoryMask & (1 << i)) != 0);
        bool isFocused = (i == g_categoryFocusIndex);
        
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
        
        // 如果被聚焦，绘制醒目的黄色双层外边框
        if (isFocused) {
            canvas->drawRoundRect(bx - 1, by - 1, colW + 2, 18, 3, TFT_YELLOW);
            canvas->drawRoundRect(bx - 2, by - 2, colW + 4, 20, 4, TFT_WHITE);
        }
        
        canvas->setTextColor(textColor);
        int tw = canvas->textWidth(label);
        canvas->drawString(label, bx + (colW - tw) / 2, by + 2);
    }
}

void SatSelectView::draw(LGFX_Sprite* canvas) {
    if (!canvas && earth_renderer) canvas = earth_renderer->getCanvas();
    if (!canvas) return;

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
    
    // Tab 1: Encyclopedia
    uint16_t tab1Bg = (currentSatTab == TAB_ENCYCLOPEDIA) ? canvas->color565(100, 50, 200) : canvas->color565(30, 40, 50);
    canvas->fillRect(0, 0, width/2, 20, tab1Bg);
    canvas->setTextColor(TFT_WHITE);
    canvas->setTextSize(1);
    canvas->drawString(I18N::get(TXT_TAB_ENCYCLOPEDIA), 70 - canvas->textWidth(I18N::get(TXT_TAB_ENCYCLOPEDIA))/2, 6);
    
    // Tab 2: Recent Launch
    uint16_t tab2Bg = (currentSatTab == TAB_RECENT_LAUNCH) ? canvas->color565(100, 50, 200) : canvas->color565(30, 40, 50);
    canvas->fillRect(width/2, 0, width/2, 20, tab2Bg);
    canvas->drawString(I18N::get(TXT_TAB_RECENT_LAUNCH), 166 - canvas->textWidth(I18N::get(TXT_TAB_RECENT_LAUNCH))/2, 6);
    
    // Draw Memory Usage Progress Bar on Top Bar Divider Line (y = 19..20)
    {
        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t totalHeap = ESP.getHeapSize();
        float memRatio = 0.0f;
        if (totalHeap > 0) {
            memRatio = (float)(totalHeap - freeHeap) / (float)totalHeap;
        }
        int fillWidth = (int)(width * memRatio);
        if (fillWidth > width) fillWidth = width;
        if (fillWidth < 0) fillWidth = 0;

        uint16_t memColor;
        if (memRatio < 0.70f) {
            memColor = canvas->color565(0, 220, 255); // 青色/天蓝色 (正常健康 <70%, FreeHeap > 85KB)
        } else if (memRatio < 0.82f) {
            memColor = TFT_YELLOW;                   // 黄色 (预警 70%-82%)
        } else {
            memColor = TFT_RED;                      // 红色 (高占用 >82%)
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
        
        for (int i = 0; i < itemsPerPage && (startIndex + i) < totalItems; i++) {
            int index = startIndex + i;
            if (index == satSelectedIndex) {
                canvas->fillRect(2, yPos - 1, 82, 12, canvas->color565(0, 120, 255));
                canvas->setTextColor(TFT_WHITE);
            } else {
                canvas->setTextColor(TFT_LIGHTGRAY);
            }
            
            if (index < filteredCount) {
                int realIdx = g_encyclopediaFilteredIndices[index];
                String checkBox = g_satellites[realIdx].selected ? "[x]" : "[ ]";
                canvas->drawString(checkBox.c_str(), 4, yPos);
                
                if (index == satSelectedIndex) {
                    TextUtils::drawScrollingText(canvas, g_satellites[realIdx].name.c_str(), 28, yPos, 56, TFT_WHITE);
                } else {
                    String nameStr = g_satellites[realIdx].name;
                    if (nameStr.length() > 9) nameStr = nameStr.substring(0, 7) + "..";
                    canvas->drawString(nameStr.c_str(), 28, yPos);
                }
            } else {
                String text = isDownloadingCustom ? I18N::get(TXT_DOWNLOADING) : ("[+] " + noradInput + "_");
                canvas->drawString(text.c_str(), 4, yPos);
            }
            
            yPos += itemSpacing;
        }
        
        // Draw page index indicator
        {
            int totalPages = (totalItems + itemsPerPage - 1) / itemsPerPage;
            int currentPage = (satSelectedIndex / itemsPerPage) + 1;
            char pageBuf[32];
            int totalSel = getTotalSelectedSatelliteCount();
            if (g_selectedCategoryMask != 0) {
                sprintf(pageBuf, "(%d/%d) [%d/30]*", currentPage, totalPages, totalSel);
            } else {
                sprintf(pageBuf, "(%d/%d) [%d/30]", currentPage, totalPages, totalSel);
            }
            canvas->setTextColor(totalSel >= 30 ? TFT_ORANGE : canvas->color565(110, 150, 180));
            canvas->drawString(pageBuf, 8, showBanner ? (bottomLimit - 12) : (bottomLimit - 13));
        }
        
        // Right Panel (Description)
        canvas->drawFastVLine(85, 20, bottomLimit - 20, TFT_DARKGREY);
        
        int rightX = 89;
        int descY = 25;
        if (satSelectedIndex < filteredCount) {
            int realIdx = g_encyclopediaFilteredIndices[satSelectedIndex];
            SatProfile selSat;
            lockSatMutex();
            selSat = g_satellites[realIdx];
            unlockSatMutex();
            
            // Draw 3x Scaled Icon
            int iconX = rightX + 21;
            int iconY = descY + 12;
            uint16_t satColor = selSat.color;
            SatIconType t = selSat.iconType;
            
            if (t == ICON_STATION) {
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
            
            descY += 36;
            
            double tx, ty, tz;
            bool isTracking = false;
            double az = 0, el = 0, dist = 0;
            
            if (selSat.calc.getTEME(current_unix + timeMachineOffset, tx, ty, tz)) {
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
            
            // 2. 拼接轨道参数与遥测细节 (COSPAR/周期/速度/倾角/高度/方位/仰角/频率)
            String specBlock = "";
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
                if (selSat.calc.getTEME(current_unix + timeMachineOffset, tx, ty, tz, vx, vy, vz)) {
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
            
            // 运行状态与无线电频段细节
            if (selSat.type == SAT_TYPE_HISTORICAL || selSat.noradId == 4382 || selSat.noradId == 5 || selSat.noradId == 27386 || selSat.noradId == 25576) {
                char statusBuf[64];
                if (currL == LANG_ZH) {
                    snprintf(statusBuf, sizeof(statusBuf), "\n运行状态: 已失效/默音在轨");
                } else if (currL == LANG_JA) {
                    snprintf(statusBuf, sizeof(statusBuf), "\n運用状態: 非運用/静寂");
                } else if (currL == LANG_ES) {
                    snprintf(statusBuf, sizeof(statusBuf), "\nEstado: Inactivo/Silencioso");
                } else {
                    snprintf(statusBuf, sizeof(statusBuf), "\nStatus: Inactive/Silent");
                }
                specBlock += String(statusBuf);
            }
            
            if (selSat.noradId == 100532) {
                char statusBuf[96];
                if (currL == LANG_ZH) {
                    snprintf(statusBuf, sizeof(statusBuf), "\n运行状态: 往L2转移轨道巡航中(预计9月底入轨)");
                } else if (currL == LANG_JA) {
                    snprintf(statusBuf, sizeof(statusBuf), "\n運用状態: 地球-L2遷移軌道巡航中(9月下旬投入予定)");
                } else if (currL == LANG_ES) {
                    snprintf(statusBuf, sizeof(statusBuf), "\nEstado: En transito a L2 (Llegada fin de sep 2026)");
                } else {
                    snprintf(statusBuf, sizeof(statusBuf), "\nStatus: In-transit to L2 (Arrival late Sep 2026)");
                }
                specBlock += String(statusBuf);
            }
            
            if (selSat.noradId == 34937) {
                char statusBuf[96];
                if (currL == LANG_ZH) {
                    snprintf(statusBuf, sizeof(statusBuf), "\n运行状态: 已退役(液氦耗尽停止工作)");
                } else if (currL == LANG_JA) {
                    snprintf(statusBuf, sizeof(statusBuf), "\n運用状態: 退役(液体ヘリウム枯渇)");
                } else if (currL == LANG_ES) {
                    snprintf(statusBuf, sizeof(statusBuf), "\nEstado: Retirado (Helio agotado)");
                } else {
                    snprintf(statusBuf, sizeof(statusBuf), "\nStatus: Retired (Helium depleted)");
                }
                specBlock += String(statusBuf);
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
            
            String fullTextToRender = finalDesc + specBlock;
            
            if (fullTextToRender.length() > 0) {
                int currLang = I18N::getLanguage();
                if (realIdx != g_descLastSatIndex || currLang != g_descLastLang) {
                    g_descLastSatIndex = realIdx;
                    g_descLastLang = currLang;
                    g_descManualScrolled = false;
                    g_descManualYOffset = 0;
                    g_descWrappedLines.clear();
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
                    
                    int yOffset = 0;
                    if (totalHeight > descAreaHeight && descAreaHeight > 13) {
                        if (g_descManualScrolled) {
                            // 手动按中括号翻页模式下，暂停自动滚动功能
                            yOffset = g_descManualYOffset;
                        } else {
                            // 默认自动循环滚动
                            int scrollSpeedMs = 66;
                            int holdTimeMs = 1500;
                            int scrollRange = totalHeight - descAreaHeight + 13;
                            int cycleTime = scrollRange * scrollSpeedMs + holdTimeMs * 2;
                            int t = (millis() - g_lastSatSelectTime) % cycleTime;
                            if (t < holdTimeMs) yOffset = 0;
                            else if (t < cycleTime - holdTimeMs) yOffset = (t - holdTimeMs) / scrollSpeedMs;
                            else yOffset = scrollRange;
                        }
                    }
                    
                    canvas->setClipRect(rightX, descY, width - rightX, descAreaHeight);
                    
                    // 1. 绘制简介与属性细节文本
                    for (int idx = 0; idx < totalLines; idx++) {
                        int lineY = descY + idx * 13 - yOffset;
                        if (lineY >= descY - 13 && lineY <= descY + descAreaHeight) {
                            String line = g_descWrappedLines[idx];
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
                                canvas->setTextColor(TFT_LIGHTGRAY);
                                canvas->drawString(valuePart.c_str(), rightX + nameW, lineY);
                            } else {
                                canvas->setTextColor(TFT_LIGHTGRAY);
                                canvas->drawString(line.c_str(), rightX, lineY);
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
            
            for (int i = 0; i < itemsPerPage && (startIndex + i) < totalItems; i++) {
                int index = startIndex + i;
                if (index == recentLaunchSelectedIndex) {
                    canvas->fillRect(2, yPos - 1, 82, 12, canvas->color565(0, 120, 255));
                    canvas->setTextColor(TFT_WHITE);
                } else {
                    canvas->setTextColor(TFT_LIGHTGRAY);
                }
                
                lockSatMutex();
                bool itemSel = (index < (int)g_recentLaunches.size()) ? g_recentLaunches[index].selected : false;
                String nameStr = (index < (int)g_recentLaunches.size()) ? g_recentLaunches[index].displayName : "";
                bool itemIsGroup = (index < (int)g_recentLaunches.size()) ? g_recentLaunches[index].isGroup : false;
                int itemSatCnt = (index < (int)g_recentLaunches.size()) ? g_recentLaunches[index].satelliteCount : 0;
                unlockSatMutex();

                String checkBox = itemSel ? "[x]" : "[ ]";
                canvas->drawString(checkBox.c_str(), 4, yPos);
                
                if (itemIsGroup) {
                    nameStr = nameStr + " (" + String(itemSatCnt) + ")";
                }
                if (index == recentLaunchSelectedIndex) {
                    TextUtils::drawScrollingText(canvas, nameStr.c_str(), 28, yPos, 56, TFT_WHITE);
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
                sprintf(pageBuf, "(%d/%d) [%d/30]", currentPage, totalPages, totalSel);
                canvas->setTextColor(totalSel >= 30 ? TFT_ORANGE : canvas->color565(110, 150, 180));
                canvas->drawString(pageBuf, 8, showBanner ? (bottomLimit - 12) : (bottomLimit - 13));
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
                            canvas->drawString(hBuf, width - altW - 2, memY + s * 13);
                        }
                        
                        int maxW = obj.lastGeoValid ? (width - rightX - altW - 4) : (width - rightX - 4);
                        TextUtils::drawScrollingText(canvas, lineText.c_str(), rightX, memY + s * 13, maxW, TFT_LIGHTGRAY);
                    }
                    
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
    if (g_showCategoryFilterDialog) {
        drawCategoryFilterDialog(canvas);
    }
}


