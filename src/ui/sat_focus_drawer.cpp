#include "ui/sat_focus_drawer.h"
#include "core/earth_renderer.h"
#include "core/recent_launch_item.h"
#include "core/spring_animator.h"
#include "core/i18n.h"
#include "core/text_utils.h"
#include "core/santa_tracker.h"

extern const int MAX_SATELLITES;
extern SatProfile g_satellites[];
extern int NUM_SATELLITES;
extern std::vector<RecentLaunchItem> g_recentLaunches;
extern int focusSatIndex;
extern bool g_recentLaunchFocusMode;
extern bool g_santaFocusMode;
extern String recentLaunchActiveBatchId;
extern bool isSatViewMode;
extern volatile uint32_t current_unix;
extern int32_t timeMachineOffset;
extern void lockSatMutex();
extern void unlockSatMutex();

// 抽屉水平 X 坐标物理弹簧 (屏幕宽 240，完全收起 240.0f，展开 240 - 69 = 171.0f - lilt 物理模型)
static SpringValue s_drawerX(240.0f, SpringConfig(0.32f, 0.82f));

// 列表项垂直切换滚轮物理弹簧 (lilt 弹性平滑磁吸过渡，阻尼 0.76)
static SpringValue s_scrollOffsetY(0.0f, SpringConfig(0.24f, 0.76f));

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

void SatFocusDrawer::trigger(int dir) {
    bool wasFullyClosed = (!_active && s_drawerX.get() >= 238.0f);
    _active = true;
    _hideMs = millis() + 5000;
    _lastTriggerMs = millis();
    // 展开目标 X: 240 - 69 = 171
    s_drawerX.set(171.0f);

    // 注入 lilt 垂直切换弹性滚轮冲量
    if (!wasFullyClosed && dir != 0) {
        if (dir > 0) {
            // 按 '.' 向下切换目标，新目标从下方滑上来，产生 +15px 冲量后弹性归位
            s_scrollOffsetY.jump(15.0f);
        } else {
            // 按 ';' 向上切换目标，新目标从上方滑下来，产生 -15px 冲量后弹性归位
            s_scrollOffsetY.jump(-15.0f);
        }
        s_scrollOffsetY.set(0.0f);
    } else {
        s_scrollOffsetY.jump(0.0f);
        s_scrollOffsetY.set(0.0f);
    }
}

void SatFocusDrawer::hideImmediately() {
    _active = false;
    _hideMs = 0;
    s_drawerX.jump(240.0f);
    s_scrollOffsetY.jump(0.0f);
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

    const int drawerW = 69; // 宽度精简至 69px
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
            if (!g_santaFocusMode && !g_recentLaunchFocusMode && focusSatIndex == i) {
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

            if (!g_santaFocusMode && g_recentLaunchFocusMode && rItem.batchId == recentLaunchActiveBatchId) {
                item.isFocus = true;
                focusTargetIdx = (int)targets.size();
            }
            targets.push_back(item);
        }
    }

    // 圣诞彩蛋任务：在飞行巡航阶段注入 SNT-CLS
    if (SantaTracker::isDecember() && SantaTracker::isFlightPhase()) {
        DrawerTargetItem sItem;
        sItem.name = "SNT-CLS";
        sItem.color = canvas->color565(255, 75, 75);
        sItem.iconType = ICON_SATELLITE;
        if (g_santaFocusMode) {
            sItem.isFocus = true;
            focusTargetIdx = (int)targets.size();
        }
        targets.push_back(sItem);
    }

    unlockSatMutex();

    if (targets.empty()) {
        return;
    }

    if (focusTargetIdx == -1) {
        focusTargetIdx = 0;
        targets[0].isFocus = true;
    }

    // 与推荐过境事件列表保持完全一致的暗色背景 (15, 20, 25)
    uint16_t bgCol = canvas->color565(15, 20, 25);
    canvas->fillRect(curX, drawerY, drawerW, drawerH, bgCol);

    // 与推荐列表一致的灰色分割线 (TFT_DARKGREY)
    canvas->drawFastVLine(curX, drawerY, drawerH, TFT_DARKGREY);
    // 上下微弱暗色分割线
    canvas->drawFastHLine(curX, drawerY, drawerW, canvas->color565(35, 45, 55));
    canvas->drawFastHLine(curX, drawerY + drawerH - 1, drawerW, canvas->color565(35, 45, 55));

    // 中间固定焦点指示底框 (固定居中于第 3 行，科技暗蓝灰高亮，无任何刺眼杂色)
    int midRowY = drawerY + 2 + 2 * rowH;
    canvas->fillRect(curX + 1, midRowY, drawerW - 2, rowH, canvas->color565(25, 40, 58));
    canvas->drawRect(curX + 1, midRowY, drawerW - 2, rowH, canvas->color565(60, 85, 115));

    int targetCount = (int)targets.size();
    canvas->setFont(I18N::getFont());
    canvas->setTextSize(1);
    canvas->setTextDatum(TL_DATUM);

    // 开启抽屉硬件裁剪，使滚动中的行不越出抽屉边界
    canvas->setClipRect(curX, drawerY, drawerW, drawerH);

    float scrollOffY = s_scrollOffsetY.get();
    float centerY = (float)drawerY + (float)drawerH * 0.5f;

    // 渲染 7 行 (row = -1 ~ 5，包含上下各 1 行滚动候补目标，随 lilt 物理弹簧滚动)
    for (int row = -1; row <= rowCount; row++) {
        int rowY = drawerY + 2 + row * rowH + (int)roundf(scrollOffY);
        if (rowY + rowH < drawerY - 5 || rowY > drawerY + drawerH + 5) {
            continue;
        }

        int offset = row - 2;
        int candidateIdx = -1;

        if (targetCount >= 3) {
            candidateIdx = (focusTargetIdx + offset + targetCount * 20) % targetCount;
        } else if (targetCount == 2) {
            if (row == 2) candidateIdx = focusTargetIdx;
            else if (row == 1 || row == 3) candidateIdx = 1 - focusTargetIdx;
            else if (row == 0 || row == 4) candidateIdx = focusTargetIdx;
        } else if (targetCount == 1) {
            if (row == 2) candidateIdx = 0;
        }

        if (candidateIdx < 0 || candidateIdx >= targetCount) {
            continue;
        }

        const auto& tItem = targets[candidateIdx];

        // 动态基于与中心线垂直距离计算连续平滑淡入淡出 Alpha (lilt 视觉渐变)
        float itemCenterY = (float)rowY + (float)rowH * 0.5f;
        float distToCenter = fabsf(itemCenterY - centerY);
        float normDist = distToCenter / ((float)drawerH * 0.5f); // 0.0 (中心) -> 1.0 (边缘)
        float alpha = 1.0f - normDist * 0.82f;
        if (alpha > 1.0f) alpha = 1.0f;
        if (alpha < 0.12f) alpha = 0.12f;
        if (row == 2 && fabsf(scrollOffY) < 2.0f) alpha = 1.0f;

        // 绘制飞行器图标 (复用 EarthRenderer::renderSatelliteIcon，靠左紧凑，支持 lilt 动态淡入淡出)
        int iconCenterX = curX + 8;
        int iconCenterY = rowY + rowH / 2;
        uint16_t fadedIconCol = blendColor(tItem.color, bgCol, alpha);
        if (tItem.name == "SNT-CLS") {
            // 绘制专属圣诞雪橇小图标 (车身红 + 滑轨金 + 鹿鼻闪烁)
            uint16_t colS = blendColor(canvas->color565(230, 40, 40), bgCol, alpha);
            uint16_t colR = blendColor(canvas->color565(255, 215, 0), bgCol, alpha);
            uint16_t colN = blendColor(canvas->color565(255, 60, 60), bgCol, alpha);
            // 金滑轨
            canvas->drawFastHLine(iconCenterX - 5, iconCenterY + 2, 7, colR);
            canvas->drawPixel(iconCenterX + 2, iconCenterY + 1, colR);
            // 红车身
            canvas->fillRect(iconCenterX - 4, iconCenterY - 1, 5, 3, colS);
            // 圣诞老人白帽球
            canvas->drawPixel(iconCenterX - 2, iconCenterY - 3, blendColor(TFT_WHITE, bgCol, alpha));
            // 鲁道夫红鼻子
            canvas->drawPixel(iconCenterX + 4, iconCenterY, colN);
        } else {
            EarthRenderer::renderSatelliteIcon(canvas, iconCenterX, iconCenterY, tItem.iconType, fadedIconCol, false, alpha);
        }

        // 绘制卫星名称 (颜色与地球仪保持一致，支持动态淡入淡出与超长文字横向跑马灯滚动)
        int textStartX = curX + 17;
        int maxTextWidth = drawerW - 19; // 50px 可用文本宽度
        uint16_t fadedTextCol = blendColor(tItem.color, bgCol, alpha);
        canvas->setTextColor(fadedTextCol);

        int fullW = canvas->textWidth(tItem.name.c_str());
        int scrollOffset = 0;

        if (fullW > maxTextWidth) {
            int overflow = fullW - maxTextWidth;
            // 滚动周期: 0~800ms 静止 -> 匀速向左滚动 (25px/s) -> 末尾停留 800ms -> 400ms 平滑归位
            uint32_t scrollDuration = (uint32_t)(overflow * 40);
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

        // 嵌套局部裁剪矩形，确保单行文字滚动不会遮挡图标或越出右边缘
        canvas->setClipRect(textStartX, rowY, maxTextWidth, rowH);
        canvas->drawString(tItem.name.c_str(), textStartX - scrollOffset, rowY + 2);
        canvas->setClipRect(curX, drawerY, drawerW, drawerH); // 还原至抽屉全局裁剪
    }

    canvas->clearClipRect();
    canvas->setTextDatum(TL_DATUM);
}
