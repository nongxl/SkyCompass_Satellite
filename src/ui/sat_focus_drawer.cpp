#include "ui/sat_focus_drawer.h"
#include "core/earth_renderer.h"
#include "core/recent_launch_item.h"
#include "core/spring_animator.h"
#include "core/i18n.h"
#include "core/text_utils.h"

extern const int MAX_SATELLITES;
extern SatProfile g_satellites[];
extern int NUM_SATELLITES;
extern std::vector<RecentLaunchItem> g_recentLaunches;
extern int focusSatIndex;
extern bool g_recentLaunchFocusMode;
extern String recentLaunchActiveBatchId;
extern bool isSatViewMode;
extern volatile uint32_t current_unix;
extern int32_t timeMachineOffset;
extern void lockSatMutex();
extern void unlockSatMutex();

// 抽屉水平 X 坐标物理弹簧 (屏幕宽 240，完全收起 240.0f，展开 240 - 69 = 171.0f)
static SpringValue s_drawerX(240.0f, SpringConfig(0.26f, 0.78f));

// RGB565 颜色与背景进行 Alpha 柔和混色
static inline uint16_t blendColor(uint16_t fg, uint16_t bg, float alpha) {
    if (alpha >= 0.99f) return fg;
    if (alpha <= 0.02f) return bg;
    uint8_t r1 = (fg >> 11) & 0x1F;
    uint8_t g1 = (fg >> 5) & 0x3F;
    uint8_t b1 = fg & 0x1F;
    uint8_t r0 = (bg >> 11) & 0x1F;
    uint8_t g0 = (bg >> 5) & 0x3F;
    uint8_t b0 = bg & 0x1F;
    uint8_t r = (uint8_t)(r0 + (r1 - r0) * alpha);
    uint8_t g = (uint8_t)(g0 + (g1 - g0) * alpha);
    uint8_t b = (uint8_t)(b0 + (b1 - b0) * alpha);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

SatFocusDrawer::SatFocusDrawer() {
    _active = false;
    _hideMs = 0;
    _lastTriggerMs = 0;
}

SatFocusDrawer& SatFocusDrawer::getInstance() {
    static SatFocusDrawer instance;
    return instance;
}

void SatFocusDrawer::trigger() {
    _active = true;
    _hideMs = millis() + 5000;
    _lastTriggerMs = millis();
    // 抽屉宽度减少 1/4 (原 92 -> 现 69)，展开目标 X: 240 - 69 = 171
    s_drawerX.set(171.0f);
}

void SatFocusDrawer::hideImmediately() {
    _active = false;
    _hideMs = 0;
    s_drawerX.jump(240.0f);
}

bool SatFocusDrawer::isVisible() const {
    return _active || (s_drawerX.get() < 239.5f);
}

void SatFocusDrawer::draw(LGFX_Sprite* canvas) {
    if (!canvas) return;

    // 检查 5 秒无操作超时自动隐藏
    if (_active && millis() >= _hideMs) {
        _active = false;
        s_drawerX.set(240.0f);
    }

    float currentX = s_drawerX.get();
    if (!_active && currentX >= 239.0f) {
        return; // 完全收起状态，零 CPU 消耗
    }

    if (!isSatViewMode) {
        hideImmediately();
        return;
    }

    const int drawerW = 69; // 宽度减少 1/4 (原 92px -> 现 69px)
    const int rowH = 15;
    const int rowCount = 5;
    const int drawerH = rowCount * rowH + 4; // 79 px
    const int drawerY = (canvas->height() - drawerH) / 2; // 屏幕垂直居中 (135 - 79) / 2 = 28
    int curX = (int)roundf(currentX);

    // 收集当前地球仪上勾选的所有卫星目标 (常规卫星 + 近期发射批次)
    struct DrawerTargetItem {
        String name;
        uint16_t color = TFT_WHITE;
        SatIconType iconType = ICON_SATELLITE;
        bool isFocus = false;
    };

    std::vector<DrawerTargetItem> targets;
    int focusTargetIdx = -1;
    uint32_t simTime = (uint32_t)((int64_t)current_unix + (int64_t)timeMachineOffset);

    lockSatMutex();

    for (int i = 0; i < NUM_SATELLITES; i++) {
        if (g_satellites[i].selected) {
            DrawerTargetItem item;
            item.name = g_satellites[i].name;
            item.color = g_satellites[i].color;
            item.iconType = g_satellites[i].iconType;
            if (!g_recentLaunchFocusMode && focusSatIndex == i) {
                item.isFocus = true;
                focusTargetIdx = (int)targets.size();
            }
            targets.push_back(item);
        }
    }

    for (size_t i = 0; i < g_recentLaunches.size(); i++) {
        const auto& rItem = g_recentLaunches[i];
        if (rItem.selected) {
            DrawerTargetItem item;
            if (rItem.displayName.length() > 0) {
                item.name = rItem.displayName;
            } else if (rItem.repSatName.length() > 0) {
                item.name = rItem.repSatName;
            } else if (rItem.shortName.length() > 0) {
                item.name = rItem.shortName;
            } else {
                item.name = rItem.batchId;
            }
            item.iconType = rItem.iconType;

            // 与地球仪及 Sat View 规则保持严格一致的发射批次颜色
            double ageDays = 30.0;
            if (rItem.epoch > 0 && simTime >= rItem.epoch) {
                ageDays = (double)(simTime - rItem.epoch) / 86400.0;
            }
            if (ageDays <= 2.0) item.color = TFT_WHITE;
            else if (ageDays <= 14.0) item.color = 0x07FF; // TFT_CYAN
            else if (ageDays >= 365.0) item.color = canvas->color565(150, 150, 150);
            else item.color = 0x07FF; // TFT_CYAN

            if (g_recentLaunchFocusMode && rItem.batchId == recentLaunchActiveBatchId) {
                item.isFocus = true;
                focusTargetIdx = (int)targets.size();
            }
            targets.push_back(item);
        }
    }

    unlockSatMutex();

    if (targets.empty()) {
        return;
    }

    if (focusTargetIdx == -1) {
        focusTargetIdx = 0;
        targets[0].isFocus = true;
    }

    uint16_t bgCol = canvas->color565(16, 22, 32);

    // 绘制抽屉科技感背景 (深暗蓝黑，对比度极佳)
    canvas->fillRect(curX, drawerY, drawerW, drawerH, bgCol);
    // 抽屉边框与微发光分割线
    canvas->drawFastVLine(curX, drawerY, drawerH, canvas->color565(0, 160, 240));
    canvas->drawFastHLine(curX, drawerY, drawerW, canvas->color565(30, 50, 70));
    canvas->drawFastHLine(curX, drawerY + drawerH - 1, drawerW, canvas->color565(30, 50, 70));

    int targetCount = (int)targets.size();
    canvas->setFont(I18N::getFont());
    canvas->setTextSize(1);
    canvas->setTextDatum(TL_DATUM);

    // 逐行渲染 5 行列表 (中间第 3 行 row = 2 严格对应当前焦点目标，上下淡入淡出)
    for (int row = 0; row < rowCount; row++) {
        int rowY = drawerY + 2 + row * rowH;
        int offset = row - 2; // -2, -1, 0, +1, +2
        int candidateIdx = -1;

        if (targetCount >= 3) {
            candidateIdx = (focusTargetIdx + offset + targetCount * 10) % targetCount;
        } else if (targetCount == 2) {
            if (row == 2) candidateIdx = focusTargetIdx;
            else if (row == 1 || row == 3) candidateIdx = 1 - focusTargetIdx;
            // row == 0, 4 留空
        } else if (targetCount == 1) {
            if (row == 2) candidateIdx = 0;
            // 其余行留空
        }

        if (candidateIdx < 0 || candidateIdx >= targetCount) {
            continue;
        }

        const auto& tItem = targets[candidateIdx];
        bool isCenter = (row == 2);

        // 上下淡入淡出透明度系数: 中间 100%, 邻近 55%, 两端 22% 融入暗底
        float alpha = isCenter ? 1.0f : ((abs(offset) == 1) ? 0.55f : 0.22f);

        if (isCenter) {
            // 中间焦点行：高亮科技蓝背景条与微发光边框 (已去掉黄色指示箭头以节省宽度)
            canvas->fillRect(curX + 1, rowY, drawerW - 2, rowH, canvas->color565(20, 55, 95));
            canvas->drawRect(curX + 1, rowY, drawerW - 2, rowH, canvas->color565(40, 140, 220));
        }

        // 绘制飞行器图标 (复用 EarthRenderer::renderSatelliteIcon，图标靠左紧凑对齐，支持上下淡入淡出)
        int iconCenterX = curX + 8;
        int iconCenterY = rowY + rowH / 2;
        uint16_t fadedIconCol = blendColor(tItem.color, bgCol, alpha);
        EarthRenderer::renderSatelliteIcon(canvas, iconCenterX, iconCenterY, tItem.iconType, fadedIconCol, false, alpha);

        // 绘制卫星名称 (颜色与地球仪保持一致，支持上下淡入淡出与超长文字横向滚动)
        int textStartX = curX + 17;
        int maxTextWidth = drawerW - 19; // 69 - 19 = 50px 可用文本宽度
        uint16_t fadedTextCol = blendColor(tItem.color, bgCol, alpha);
        canvas->setTextColor(fadedTextCol);

        int fullW = canvas->textWidth(tItem.name.c_str());
        int scrollOffset = 0;

        if (fullW > maxTextWidth) {
            int overflow = fullW - maxTextWidth;
            // 滚动周期设计: 0~800ms 静止 -> 平滑向左滚到末尾 -> 停留 800ms -> 400ms 平滑复位
            uint32_t scrollDuration = (uint32_t)(overflow * 40); // 速度约 25px/s
            if (scrollDuration < 500) scrollDuration = 500;
            uint32_t totalPeriod = 800 + scrollDuration + 800 + 400;
            uint32_t elapsed = (millis() >= _lastTriggerMs) ? (millis() - _lastTriggerMs) : millis();
            uint32_t timeInPeriod = elapsed % totalPeriod;

            if (timeInPeriod < 800) {
                scrollOffset = 0;
            } else if (timeInPeriod < 800 + scrollDuration) {
                float progress = (float)(timeInPeriod - 800) / (float)scrollDuration;
                scrollOffset = (int)roundf(progress * overflow);
            } else if (timeInPeriod < 800 + scrollDuration + 800) {
                scrollOffset = overflow;
            } else {
                float progress = (float)(timeInPeriod - (800 + scrollDuration + 800)) / 400.0f;
                scrollOffset = (int)roundf((1.0f - progress) * overflow);
            }
        }

        // 使用局部硬件剪裁矩形，确保文字横向滚动不会遮挡图标或溢出抽屉右边缘
        canvas->setClipRect(textStartX, rowY, maxTextWidth, rowH);
        canvas->drawString(tItem.name.c_str(), textStartX - scrollOffset, rowY + 2);
        canvas->clearClipRect();
    }

    // 重置画布对齐方式，防止影响后续渲染
    canvas->setTextDatum(TL_DATUM);
}
