#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <vector>
#include <Preferences.h>

struct WiFiNetwork {
    String ssid;
    int32_t rssi;
    uint8_t encryptionType;
    int32_t channel = 0;
    uint8_t bssid[6] = {0};
};

struct SavedWifiCred {
    String ssid;
    String password;
    int32_t channel;

    SavedWifiCred() : ssid(""), password(""), channel(0) {}
    SavedWifiCred(const String& s, const String& p, int32_t c = 0)
        : ssid(s), password(p), channel(c) {}
};

class HalWifi {
public:
    static const int MAX_SAVED_NETWORKS = 8;

    static void begin(const char* ssid, const char* password, int32_t channel = 0, const uint8_t* bssid = nullptr);
    static bool isConnected();
    
    // Sync time using NTP. Returns true if successful.
    // IMPORTANT: Always pass gmtOffset_sec=0 to get true UTC from getUnixTime().
    // The system uses UTC internally; timezone is applied only at display time.
    static bool syncNTPTime(long gmtOffset_sec = 0, int daylightOffset_sec = 0);
    
    // Get current Unix time (seconds since epoch)
    // Returns 0 if time is not synced.
    static uint32_t getUnixTime();

    // Scan for available WiFi networks
    static std::vector<WiFiNetwork> scanNetworks();

    // NVS Multi-Credentials Management
    static std::vector<SavedWifiCred> getSavedNetworks();
    static bool hasCredential(const String& ssid);
    static bool getCredential(const String& ssid, String& outPassword, int32_t* outChannel = nullptr);
    static void saveCredentials(const String& ssid, const String& password, int32_t channel = 0);
    static bool forgetNetwork(const String& ssid);
    static bool loadCredentials(String& outSsid, String& outPassword, int32_t* outChannel = nullptr);
    
    // 智能自动漫游连接：匹配周围信号最强的已知网络自动连接
    static bool autoConnect();

    // Disconnect and stop WiFi
    static void disconnect();
    static bool isConnectAborted();

    // 在开机最早时刻预分配 WiFi STA 驱动与 DMA 缓冲区并立即射频休眠，杜绝内存碎片化引发分配失败
    static void preinit();
};


