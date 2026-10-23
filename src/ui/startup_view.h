#ifndef STARTUP_VIEW_H
#define STARTUP_VIEW_H

#include <Arduino.h>

class StartupView {
public:
    static void draw(int progressPercentage, bool showLangSelect = false, int selectedLangIndex = 0);
    static void nextTrivia();
    static void prevTrivia();
    static void setTriviaIndex(int idx);
    static int getTriviaIndex();
};

#endif // STARTUP_VIEW_H
