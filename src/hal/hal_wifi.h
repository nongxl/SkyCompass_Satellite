#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <vector>
#include <Preferences.h>

struct WiFiNetwork {
    String ssid;
    int32_t rssi;
    uint8_t encryptionType;
};

class HalWifi {
public:
    static void begin(const char* ssid, const char* password);
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

    // NVS Credentials Management
    static void saveCredentials(const String& ssid, const String& password);
    static bool loadCredentials(String& outSsid, String& outPassword);
    
    // Disconnect and stop WiFi
    static void disconnect();
};
