#include <M5Cardputer.h>
#include "app/app_main.h"
#include "hal/hal_gnss.h"

// 声明全局gnss实例
extern HalGnss* gnss;

// 创建应用实例
AppMain app;

// 串口输出相关变量
unsigned long lastSerialOutput = 0;
const unsigned long serialOutputInterval = 2000; // 串口输出间隔，单位毫秒

void setup() {
    Serial.begin(115200);
    Serial.println(F("Starting Sky Compass setup..."));
    
    Serial.println(F("[SETUP] Initializing M5Cardputer..."));
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    
    // 强制提高 I2C 总线频率到 400kHz，减少 [LGFX] ack wait 错误和总线阻塞
    Wire.setClock(400000);
    
    M5Cardputer.Display.setBrightness(64);
    Serial.println(F("[SETUP] M5Cardputer initialized"));
    
    Serial.println(F("[SETUP] Starting app.begin()..."));
    Serial.flush();
    
    if (!app.begin()) {
        Serial.println(F("[SETUP] app.begin() FAILED!"));
        Serial.flush();
        M5.Lcd.fillScreen(BLACK);
        M5.Lcd.setTextColor(RED);
        M5.Lcd.setTextSize(2);
        M5.Lcd.setCursor(10, 10);
        M5.Lcd.println("Init Failed!");
        M5.Lcd.setTextSize(1);
        M5.Lcd.setCursor(10, 40);
        M5.Lcd.println("Check serial for details.");
        delay(1000);
    } else {
        Serial.println(F("[SETUP] app.begin() SUCCESS"));
        Serial.flush();
    }
    
    Serial.println(F("[SETUP] Setup complete, entering main loop"));
    Serial.flush();
}

void loop() {
    // 运行应用主循环
    app.run();
    
    // 更新M5Cardputer状态
    M5Cardputer.update();
}