#ifndef WIFI_SETUP_VIEW_H
#define WIFI_SETUP_VIEW_H

#include <Arduino.h>
#include <vector>
#include <M5Cardputer.h>
#include <M5GFX.h>
#include "hal/hal_wifi.h"
#include "core/i18n.h"

struct WifiConnectRequest {
    bool triggered = false;
    String ssid = "";
    String pass = "";
};

class WifiSetupView {
public:
    WifiSetupView();

    // 重置所有状态与网络列表
    void reset();

    // 标记开始扫描网络
    void startScan();

    // 是否处于扫描中
    bool isScanning() const { return _isScanning; }

    // 在帧渲染后执行实际的阻塞扫描
    void performScan();

    // 绘制 WiFi 页面到指定 canvas
    void draw(LGFX_Sprite* canvas);

    // 处理键盘交互
    // 返回 true 表示有事件需要主程序响应（例如请求退出或发起连接）
    bool handleInput(bool justEsc, bool justTick, bool justBack, bool justEnter, 
                     bool justR, bool justSemi, bool justDot, 
                     const Keyboard_Class::KeysState& keysState,
                     WifiConnectRequest& outConnectReq, bool& outExit);

    // 是否正在输入密码
    bool isInputtingPassword() const { return _isInputtingPassword; }

private:
    static String truncateUtf8(const String& str, size_t maxChars);

    std::vector<WiFiNetwork> _networks;
    int _selectedIndex;
    bool _isScanning;
    bool _isInputtingPassword;
    char _passwordBuffer[64];
    int _passwordLen;
};

#endif // WIFI_SETUP_VIEW_H
