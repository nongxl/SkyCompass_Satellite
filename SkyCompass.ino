#include <M5Cardputer.h>
#include "src/app/app_main.h"

// 创建应用实例
AppMain app;

void setup() {
    // 初始化M5Cardputer
    M5.begin();
    
    // 初始化应用
    if (!app.begin()) {
        // 初始化失败，显示错误信息
        M5.Lcd.fillScreen(BLACK);
        M5.Lcd.setTextColor(RED);
        M5.Lcd.setTextSize(2);
        M5.Lcd.setCursor(10, 10);
        M5.Lcd.println("Initialization failed!");
        while (1) {
            // 停止执行
        }
    }
    
}

void loop() {
    // 运行应用主循环
    app.run();
}
