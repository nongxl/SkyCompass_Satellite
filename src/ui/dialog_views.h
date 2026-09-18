#ifndef DIALOG_VIEWS_H
#define DIALOG_VIEWS_H

#include <M5GFX.h>

class DialogViews {
public:
    static void drawHelpDialog(LGFX_Sprite* canvas);
    static void drawLangSelectDialog(LGFX_Sprite* canvas, int selectedLangIndex);
};

#endif // DIALOG_VIEWS_H
