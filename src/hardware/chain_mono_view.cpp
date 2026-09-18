#include "hardware/chain_mono_view.h"
#include <M5Chain.h>
#include <math.h>
#include <vector>
#include "core/mono_animator.h"
#include "core/mono_icons.h"
#include "core/earth_renderer.h"
#include "core/tle_data.h"
#include "core/recent_launch_item.h"
#include "core/observation_predictor.h"

enum MonoState {
    MONO_STATE_NONE,       // 未定义状态，用于开机强制刷新
    MONO_STATE_IDLE,       // 默认呼吸圆圈状态
    MONO_STATE_COUNTDOWN,  // 倒计时滚动字符状态
    MONO_STATE_PASSING     // 正在过境像素闪烁状态
};

extern Chain M5Chain;
extern bool isMonoInitialized;
extern uint8_t mono_id;
extern uint8_t operation_status;

extern bool isSatViewMode;
extern int focusSatIndex;
extern int NUM_SATELLITES;
extern SatProfile g_satellites[];
extern SatRealtimeCache g_satCaches[];

extern bool g_recentLaunchFocusMode;
extern RecentLaunchRealtimeCache g_repSatCache;
extern String g_repSatName;

extern void lockPassMutex();
extern void unlockPassMutex();
extern std::vector<PassEvent> recommendedPasses;
extern uint32_t current_unix;
extern int32_t timeMachineOffset;

void ChainMonoView::update() {
    // Update Chain Mono Display (dynamic interval: 100ms normally)
    static unsigned long lastChainMonoTick = 0;
    if (isMonoInitialized && millis() - lastChainMonoTick >= 100) {
        lastChainMonoTick = millis();
        
        bool anyVisibleNow = false;
        String visibleSatName = "";
        SatIconType visibleSatIconType = ICON_SATELLITE;
        
        if (isSatViewMode && focusSatIndex >= 0 && focusSatIndex < NUM_SATELLITES) {
            anyVisibleNow = true;
            visibleSatName = g_satellites[focusSatIndex].name;
            visibleSatIconType = g_satellites[focusSatIndex].iconType;
        } else {
            if (g_recentLaunchFocusMode) {
                if (g_repSatCache.lastGeoValid && g_repSatCache.isVisible) {
                    anyVisibleNow = true;
                    visibleSatName = g_repSatName;
                    visibleSatIconType = ICON_SATELLITE;
                }
            } else {
                for (int i = 0; i < NUM_SATELLITES; i++) {
                    if (g_satellites[i].selected && g_satCaches[i].lastGeoValid && g_satCaches[i].isVisible) {
                        anyVisibleNow = true;
                        visibleSatName = g_satellites[i].name;
                        visibleSatIconType = g_satellites[i].iconType;
                        break;
                    }
                }
            }
        }
        
        static MonoState state = MONO_STATE_NONE;
        static int lastDispMinutes = -1;
        static int lastDispSeconds = -1;
        
        bool isUpcomingPass = false;
        int timeDiff = -1;
        
        if (!anyVisibleNow) {
            // 寻找即将到来的最早可见过境倒计时（全局搜索最小的 aosTime）
            PassEvent nextPass;
            bool foundNextPass = false;
            uint32_t earliestAos = 0xFFFFFFFF;
            
            lockPassMutex();
            for (const auto& pass : recommendedPasses) {
                uint32_t passTime = pass.aosTime;
                uint32_t currentSimTime = current_unix + timeMachineOffset;
                if (passTime > currentSimTime && pass.isVisible) {
                    if (passTime < earliestAos) {
                        earliestAos = passTime;
                        nextPass = pass;
                        foundNextPass = true;
                    }
                }
            }
            unlockPassMutex();
            
            if (foundNextPass) {
                timeDiff = nextPass.aosTime - (current_unix + timeMachineOffset);
                // 只有在未来 10 分钟（600 秒）内发生的过境，才在 Chain 屏显示倒计时
                if (timeDiff >= 0 && timeDiff <= 600) {
                    isUpcomingPass = true;
                    visibleSatName = nextPass.satName;
                    // 遍历 g_satellites 寻找匹配的 iconType
                    for (int i = 0; i < NUM_SATELLITES; i++) {
                        if (g_satellites[i].name == nextPass.satName) {
                            visibleSatIconType = g_satellites[i].iconType;
                            break;
                        }
                    }
                }
            }
        }
        
        if (anyVisibleNow) {
            // --- 状态 1：当前有可见过境，以缓慢呼吸效果显示飞行器图标 ---
            if (state != MONO_STATE_PASSING) {
                state = MONO_STATE_PASSING;
                lastDispMinutes = -1;
                lastDispSeconds = -1;
                M5Chain.setMonoMode(mono_id, MONO_PIXEL_MODE, &operation_status);
                M5Chain.setMonoClear(mono_id, &operation_status);
            }
            
            // 选择对应的 8x8 像素图标
            const uint8_t* icon = mono_icon_satellite;
            if (visibleSatIconType == ICON_STATION) icon = mono_icon_station;
            else if (visibleSatIconType == ICON_TELESCOPE) icon = mono_icon_telescope;
            else if (visibleSatIconType == ICON_ROCKET) icon = mono_icon_rocket;
            else if (visibleSatIconType == ICON_DEEPSPACE) icon = mono_icon_deepspace;
            else if (visibleSatIconType == ICON_DFH1) icon = mono_icon_dfh1;
            else if (visibleSatIconType == ICON_BLUEWALKER3) icon = mono_icon_bw3;
            else if (visibleSatIconType == ICON_WEATHER) icon = mono_icon_weather;
            else if (visibleSatIconType == ICON_NAVIGATION) icon = mono_icon_navi;
            else if (visibleSatIconType == ICON_COMMUNICATION) icon = mono_icon_comm;
            else if (visibleSatIconType == ICON_DEBRIS) icon = mono_icon_debris;
            
            // 缓慢呼吸效果 design：正在过境 2.5 秒一个周期
            float theta = millis() * 0.00251f;
            float breatheVal = 0.5f + 0.5f * sinf(theta - 1.57079f); // 从最暗起步
            
            // 亮度在 1 到 7 之间变化
            mono_brightness_level_t brightness = (mono_brightness_level_t)(MONO_BRIGHTNESS_LEVEL_1 + (uint8_t)(breatheVal * 6.0f));
            M5Chain.setMonoBrightness(mono_id, brightness, &operation_status);
            
            uint8_t temp[8];
            memcpy(temp, icon, 8);
            M5Chain.setMonoBufferRefresh(mono_id, temp, &operation_status);
            
        } else if (isUpcomingPass) {
            // --- 状态 2：10分钟倒计时阶段，显示静止数字，从60秒开始每秒刷新 ---
            if (state != MONO_STATE_COUNTDOWN) {
                state = MONO_STATE_COUNTDOWN;
                lastDispMinutes = -1;
                lastDispSeconds = -1;
                M5Chain.setMonoMode(mono_id, MONO_PIXEL_MODE, &operation_status);
                M5Chain.setMonoBrightness(mono_id, MONO_BRIGHTNESS_LEVEL_6, &operation_status);
                M5Chain.setMonoClear(mono_id, &operation_status);
            }
            
            if (timeDiff <= 60) {
                // 秒阶段：每秒刷新，从 60 秒到 0 秒
                if (timeDiff != lastDispSeconds) {
                    lastDispSeconds = timeDiff;
                    lastDispMinutes = -1;
                    
                    uint8_t D1 = timeDiff / 10;
                    uint8_t D2 = timeDiff % 10;
                    
                    uint8_t temp[8] = {0};
                    for (int r = 0; r < 5; r++) {
                        temp[r + 2] = (font_3x5[D1][r] << 5) | font_3x5[D2][r];
                    }
                    M5Chain.setMonoBufferRefresh(mono_id, temp, &operation_status);
                }
            } else {
                // 分钟阶段：10分到1分静止显示，不闪烁。采用两位数显示（如 05），使其完全对称
                int minutes = (timeDiff == 600) ? 10 : (timeDiff / 60);
                if (minutes != lastDispMinutes) {
                    lastDispMinutes = minutes;
                    lastDispSeconds = -1;
                    
                    uint8_t D1 = minutes / 10;
                    uint8_t D2 = minutes % 10;
                    
                    uint8_t temp[8] = {0};
                    for (int r = 0; r < 5; r++) {
                        temp[r + 2] = (font_3x5[D1][r] << 5) | font_3x5[D2][r];
                    }
                    M5Chain.setMonoBufferRefresh(mono_id, temp, &operation_status);
                }
            }
        } else {
            // --- 状态 3：无临近过境，显示动态圆圈 ---
            if (state != MONO_STATE_IDLE) {
                state = MONO_STATE_IDLE;
                lastDispMinutes = -1;
                lastDispSeconds = -1;
                M5Chain.setMonoMode(mono_id, MONO_PIXEL_MODE, &operation_status);
                M5Chain.setMonoBrightness(mono_id, MONO_BRIGHTNESS_LEVEL_6, &operation_status);
                M5Chain.setMonoClear(mono_id, &operation_status);
            }
            
            uint8_t temp[8];
            drawMonoVisualAnimation(temp);
            M5Chain.setMonoBufferRefresh(mono_id, temp, &operation_status);
        }
    }
}
