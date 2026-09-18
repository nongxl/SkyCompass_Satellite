#ifndef SAT_SELECT_VIEW_H
#define SAT_SELECT_VIEW_H

#include <Arduino.h>
#include <vector>
#include <M5GFX.h>
#include "core/i18n.h"
#include "core/encyclopedia.h"
#include "core/recent_launch_item.h"



struct FilterCategoryItem {
    const char* name_zh;
    const char* name_en;
    const char* name_ja;
    const char* name_es;
    Category cat;
    uint32_t flag;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint16_t textColor;
    uint8_t specialType;
};

class SatSelectView {
public:
    SatSelectView();

    void draw(LGFX_Sprite* canvas);
    void drawCategoryFilterDialog(LGFX_Sprite* canvas);
    void updateFilteredList();
};

#endif // SAT_SELECT_VIEW_H
