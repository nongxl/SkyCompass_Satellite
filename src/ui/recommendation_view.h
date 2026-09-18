#ifndef RECOMMENDATION_VIEW_H
#define RECOMMENDATION_VIEW_H

#include <M5GFX.h>
#include <vector>
#include "core/observation_predictor.h"

struct TreeItem {
    bool isCategory;
    int categoryIndex; // 0=Today, 1=This Week, 2=This Month, 3=Favorites
    int passIndex;     // Index in recommendedPasses
};

extern bool catExpanded[4];
void rebuildTreeLocal(std::vector<TreeItem>& tree, const std::vector<PassEvent>& passes, uint32_t current_unix);

class RecommendationView {
public:
    RecommendationView();
    void draw(LGFX_Sprite* canvas);
};

#endif // RECOMMENDATION_VIEW_H
