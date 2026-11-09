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

// 抽屉水平 X 坐标物理弹簧 (屏幕宽 240，完全收起 240.0f，展开 148.0f)
static SpringValue s_drawerX(240.0f, SpringConfig(0.26f, 0.78f));

SatFocusDrawer::SatFocusDrawer() {
    _active = false;
    _hideMs = 0;
}

SatFocusDrawer& SatFocusDrawer::getInstance() {
    static SatFocusDrawer instance;
    return instance;
}

void SatFocusDrawer::trigger() {
    _active = true;
    _hideMs = millis() + 5000;
    // 展开目标 X: 240 - 92 = 148
    s_drawerX.set(148.0f);
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

    const int drawerW = 92;
    const int rowH = 15;
    const int rowCount = 5;
    const int drawerH = rowCount * rowH + 4; // 79 px
    const int drawerY = (canvas->height() - drawerH) / 2; // 屏幕居中 (135 - 79) / 2 = 28
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

    // 绘制抽屉科技感背景 (深暗蓝黑，对比度极佳)
    canvas->fillRect(curX, drawerY, drawerW, drawerH, canvas->color565(16, 22, 32));
    // 抽屉边框与微发光分割线
    canvas->drawFastVLine(curX, drawerY, drawerH, canvas->color565(0, 160, 240));
    canvas->drawFastHLine(curX, drawerY, drawerW, canvas->color565(30, 50, 70));
    canvas->drawFastHLine(curX, drawerY + drawerH - 1, drawerW, canvas->color565(30, 50, 70));

    int targetCount = (int)targets.size();
    canvas->setFont(I18N::getFont());
    canvas->setTextSize(1);
    canvas->setTextDatum(TL_DATUM);

    // 逐行渲染 5 行列表 (中间第 3 行 row = 2 严格对应当前焦点目标)
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

        if (isCenter) {
            // 中间焦点行：高亮科技蓝背景条与微发光边框
            canvas->fillRect(curX + 1, rowY, drawerW - 2, rowH, canvas->color565(20, 55, 95));
            canvas->drawRect(curX + 1, rowY, drawerW - 2, rowH, canvas->color565(40, 140, 220));
            // 左侧黄色焦点指示箭头
            canvas->fillTriangle(curX + 3, rowY + 3, curX + 3, rowY + rowH - 4, curX + 7, rowY + rowH / 2, TFT_YELLOW);
        }

        // 绘制飞行器图标 (复用 EarthRenderer::renderSatelliteIcon)
        int iconCenterX = curX + 14;
        int iconCenterY = rowY + rowH / 2;
        EarthRenderer::renderSatelliteIcon(canvas, iconCenterX, iconCenterY, tItem.iconType, tItem.color, false, isCenter ? 1.0f : 0.85f);

        // 绘制卫星名称 (颜色与地球仪保持严格一致)
        canvas->setTextColor(tItem.color);
        int textStartX = curX + 23;
        int maxTextWidth = drawerW - 25;

        // 若卫星名称超出抽屉可用宽度，自动做安全截断
        String dispName = tItem.name;
        if (canvas->textWidth(dispName.c_str()) > maxTextWidth) {
            while (dispName.length() > 0 && canvas->textWidth((dispName + "..").c_str()) > maxTextWidth) {
                dispName.remove(dispName.length() - 1);
            }
            dispName += "..";
        }

        canvas->drawString(dispName.c_str(), textStartX, rowY + 2);
    }

    // 重置画布对齐方式，防止影响后续渲染
    canvas->setTextDatum(TL_DATUM);
}

