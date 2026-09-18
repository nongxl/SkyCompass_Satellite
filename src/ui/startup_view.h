#ifndef STARTUP_VIEW_H
#define STARTUP_VIEW_H

#include <Arduino.h>

class StartupView {
public:
    static void draw(int progressPercentage, bool showLangSelect = false, int selectedLangIndex = 0);
};

#endif // STARTUP_VIEW_H
