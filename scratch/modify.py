import sys

def modify():
    with open('src/main.cpp', 'r') as f:
        lines = f.readlines()
        
    # Find block 1: Help Menu
    # Find block 2: showRecommendations
    # Find block 3: Time Machine
    
    # We will just inject showHud logic where needed and do a multi replace
    # Let's just use Python to find and replace everything precisely.

    content = "".join(lines)
    
    # 1. showHud definition
    content = content.replace(
        "bool showHelp = false;\n",
        "bool showHelp = false;\nbool showHud = true;\n"
    )
    
    # 2. KEY_BACKSPACE
    content = content.replace(
        """                } else if (M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE) || M5Cardputer.Keyboard.isKeyPressed(27) || M5Cardputer.Keyboard.isKeyPressed('`')) {
                    if (showRecommendations) {
                        if (selectedPassIndex != -1) {
                            selectedPassIndex = -1; // Back to tree
                        } else {
                            showRecommendations = false; // Close panel
                        }
                    }
                } else if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {""",
        """                } else if (M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE) || M5Cardputer.Keyboard.isKeyPressed(27) || M5Cardputer.Keyboard.isKeyPressed('`')) {
                    if (showRecommendations) {
                        if (selectedPassIndex != -1) {
                            selectedPassIndex = -1; // Back to tree
                        } else {
                            showRecommendations = false; // Close panel
                        }
                    } else if (showHelp) {
                        showHelp = false;
                    } else if (isManualLocationMode) {
                        isManualLocationMode = false;
                    } else if (isSatViewMode) {
                        isSatViewMode = false;
                    } else {
                        showHud = !showHud;
                    }
                } else if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {"""
    )
    
    # 3. Coordinate overlay condition
    content = content.replace(
        "if (!showRecommendations && !showHelp && appState == STATE_MAIN) {",
        "if (!showRecommendations && !showHelp && appState == STATE_MAIN && showHud) {"
    )

    # 4. TLE Epoch rendering in showRecommendations
    # Actually, we need to move Time Machine and Sat View BEFORE showRecommendations.
    # Let's find the showRecommendations block and Time Machine block.
    
    tm_start = content.find("        // Draw Time Machine at bottom right")
    tm_end = content.find("        // M5.Display.pushImage(0, 0, 240, 135, (uint16_t*)earth_renderer->getCanvas()->getBuffer());")
    
    time_machine_code = content[tm_start:tm_end]
    content = content[:tm_start] + content[tm_end:]
    
    rec_start = content.find("        if (showRecommendations) {")
    content = content[:rec_start] + time_machine_code + content[rec_start:]

    # Now time_machine_code is inserted BEFORE showRecommendations.
    # We must update time_machine_code conditions: add `&& showHud && !showHelp && !showRecommendations`
    # Because we don't want them to draw over help menu, and we don't want them to draw if hud is off.
    # Actually, if we draw BEFORE showRecommendations, then showRecommendations will draw ON TOP of it!
    # That is what the user wants: "按enter打开推介列表时，要显示在左下角数据的上层，避免被左下角三行数据遮挡"
    
    content = content.replace(
        "if (appState == STATE_MAIN) {\n            char timeStr[32];",
        "if (appState == STATE_MAIN && showHud && !showHelp) {\n            char timeStr[32];"
    )

    # 5. Redesign Help Menu
    help_old = '''            canvas->setTextColor(TFT_CYAN);
            int ty = y + 20;
            canvas->drawString("[w]", x + 5, ty); canvas->setTextColor(TFT_LIGHTGRAY); canvas->drawString("WiFi Toggle", x + 30, ty); ty += 12;
            canvas->setTextColor(TFT_CYAN);
            canvas->drawString("[S]", x + 5, ty); canvas->setTextColor(TFT_LIGHTGRAY); canvas->drawString("Satellites", x + 35, ty); ty += 12;
            canvas->setTextColor(TFT_CYAN);
            canvas->drawString("[C]", x + 5, ty); canvas->setTextColor(TFT_LIGHTGRAY); canvas->drawString("Manual Loc/Alt (; . , / [])", x + 35, ty); ty += 12;
            canvas->setTextColor(TFT_CYAN);
            canvas->drawString("[,][/]", x + 5, ty); canvas->setTextColor(TFT_LIGHTGRAY); canvas->drawString("Time Machine", x + 45, ty); ty += 12;
            canvas->setTextColor(TFT_CYAN);
            canvas->drawString("[Enter]", x + 5, ty); canvas->setTextColor(TFT_LIGHTGRAY); canvas->drawString("Toggle Pass List", x + 50, ty); ty += 12;
            canvas->setTextColor(TFT_CYAN);
            canvas->drawString("[H]", x + 5, ty); canvas->setTextColor(TFT_LIGHTGRAY); canvas->drawString("Toggle Help", x + 35, ty); ty += 12;
            canvas->setTextColor(TFT_CYAN);
            canvas->drawString("[g/G]", x + 5, ty); canvas->setTextColor(TFT_LIGHTGRAY); canvas->drawString("GNSS Toggle", x + 40, ty); ty += 12;
            canvas->setTextColor(TFT_CYAN);
            canvas->drawString("[v/V]", x + 5, ty); canvas->setTextColor(TFT_LIGHTGRAY); canvas->drawString("Sat View (; .)", x + 40, ty); ty += 12;
            canvas->setTextColor(TFT_CYAN);
            canvas->drawString("[Space]", x + 5, ty); canvas->setTextColor(TFT_LIGHTGRAY); canvas->drawString("Lock/Unlock View", x + 45, ty); ty += 12;'''

    help_new = '''            auto drawHotKey = [&](const char* word, char keyChar, int dx, int dy) {
                int cx = dx;
                bool highlighted = false;
                for (int i = 0; word[i] != '\\0'; i++) {
                    if (!highlighted && tolower(word[i]) == tolower(keyChar) && keyChar != '\\0') {
                        canvas->setTextColor(TFT_YELLOW);
                        highlighted = true;
                    } else {
                        canvas->setTextColor(TFT_LIGHTGRAY);
                    }
                    char cstr[2] = {word[i], '\\0'};
                    canvas->drawString(cstr, cx, dy);
                    cx += canvas->textWidth(cstr);
                }
            };

            int ty = y + 20;
            drawHotKey("WiFi", 'w', x + 5, ty); 
            drawHotKey("Satellites", 's', x + 105, ty); ty += 12;
            
            drawHotKey("Config(Loc)", 'c', x + 5, ty); 
            drawHotKey("Time( , / . )", ',', x + 105, ty); ty += 12;
            
            drawHotKey("Help", 'h', x + 5, ty); 
            drawHotKey("PassList[Ent]", 'e', x + 105, ty); ty += 12;
            
            drawHotKey("GNSS", 'g', x + 5, ty); 
            drawHotKey("View(Sat)", 'v', x + 105, ty); ty += 12;
            
            drawHotKey("Lock[Spc]", 'l', x + 5, ty); 
            drawHotKey("HUD[Del]", 'd', x + 105, ty); ty += 12;'''
            
    content = content.replace(help_old, help_new)
    
    with open('src/main.cpp', 'w') as f:
        f.write(content)

if __name__ == '__main__':
    modify()
