#include "hal_wifi.h"
#include "core/log_manager.h"
#include <esp_wifi.h>
#include <time.h>

static IPAddress s_cachedIp(0, 0, 0, 0);
static IPAddress s_cachedGateway(0, 0, 0, 0);
static IPAddress s_cachedSubnet(0, 0, 0, 0);
static IPAddress s_cachedDns(0, 0, 0, 0);

void HalWifi::begin(const char* ssid, const char* password) {
    if (ssid == nullptr || strlen(ssid) == 0) {
        LOG_I("APP", "WiFi SSID is empty, skipping WiFi connection.");
        return;
    }
    
    // 内存安全防护
    if (ESP.getFreeHeap() < 8000 || ESP.getMaxAllocHeap() < 4000) {
        LOG_W("APP", "WiFi begin aborted: insufficient memory (Free: %u, MaxBlock: %u)", 
              (unsigned int)ESP.getFreeHeap(), (unsigned int)ESP.getMaxAllocHeap());
        return;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        LOG_I("APP", "WiFi already connected: %s", WiFi.localIP().toString().c_str());
        return;
    }

    LOG_I("APP", "Connecting to WiFi: %s", ssid);
    
    // 1. 启动并置为 STA 模式（系统瘦身后拥有充沛的连续 DMA 内存）
    WiFi.mode(WIFI_STA);
    delay(50);
    
    // 2. 关键：彻底禁用 Wi-Fi 休眠（Modem Sleep），避免 TCP SYN-ACK 握手包被丢弃导致超时！
    WiFi.setSleep(false);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    
    // 3. 微调发射功率至 15dBm，避免 Cardputer 紧凑天线近距离信号饱和失真
    WiFi.setTxPower(WIFI_POWER_15dBm);
    
    WiFi.begin(ssid, password);
    
    // 平滑等待连接与 DHCP 分配，最多等 10 次（5秒）
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 10) {
        delay(500);
        log_i(".");
        retries++;
    }
    
    // 4. 如果物理层已连上 AP，但路由器端 DHCP 响应缓慢（企业 AP / 租约未释放），
    // 且我们之前在同一个网络下已经成功获取并缓存过有效 IP 租约，立即快速应用该已知有效租约！
    if (WiFi.status() != WL_CONNECTED && s_cachedIp != IPAddress(0, 0, 0, 0)) {
        LOG_I("APP", "Activating active DHCP lease: IP %s, GW %s", 
              s_cachedIp.toString().c_str(), s_cachedGateway.toString().c_str());
        WiFi.config(s_cachedIp, s_cachedGateway, s_cachedSubnet, s_cachedDns);
        delay(200);
    }
    
    while (WiFi.status() != WL_CONNECTED && retries < 30) {
        delay(500);
        log_i(".");
        retries++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        // 保存当前有效租约，便于下次重连快速激活
        s_cachedIp = WiFi.localIP();
        s_cachedGateway = WiFi.gatewayIP();
        s_cachedSubnet = WiFi.subnetMask();
        s_cachedDns = WiFi.dnsIP();
        LOG_I("APP", "\nWiFi Connected! IP: %s, DNS: %s", s_cachedIp.toString().c_str(), s_cachedDns.toString().c_str());
    } else {
        LOG_I("APP", "\nWiFi Connection Failed (Timeout).");
        WiFi.disconnect(true, true);
        delay(50);
        WiFi.mode(WIFI_OFF);
    }
}

bool HalWifi::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

bool HalWifi::syncNTPTime(long gmtOffset_sec, int daylightOffset_sec) {
    if (!isConnected()) {
        return false;
    }
    
    LOG_I("APP", "Syncing NTP time...");
    configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org", "time.nist.gov");
    
    struct tm timeinfo;
    int retries = 0;
    // Reduced from 10 (5s) to 6 (3s) to shorten boot time.
    // Pool NTP servers usually respond within 1~2 retries on a healthy network.
    while (!getLocalTime(&timeinfo) && retries < 6) {
        delay(500);
        log_i(".");
        retries++;
    }
    
    if (retries < 6) {
        LOG_I("APP", "\nTime synced successfully!");
        log_i("Current time: %s", asctime(&timeinfo));
        return true;
    } else {
        LOG_I("APP", "\nFailed to sync NTP time (timeout).");
        return false;
    }
}

uint32_t HalWifi::getUnixTime() {
    time_t now;
    time(&now);
    
    // If year is before 2024, it means time is not synced yet (epoch starts at 1970)
    // 1704067200 is 2024-01-01
    if (now < 1704067200) {
        return 0;
    }
    return (uint32_t)now;
}

std::vector<WiFiNetwork> HalWifi::scanNetworks() {
    std::vector<WiFiNetwork> networks;
    LOG_I("APP", "Scanning WiFi networks...");
    
    WiFi.mode(WIFI_STA);
    esp_wifi_start();
    delay(50);
    
    int n = WiFi.scanNetworks(false, true);
    LOG_I("APP", "Found %d networks", n);
    
    if (n > 0) {
        for (int i = 0; i < n; ++i) {
            WiFiNetwork net;
            net.ssid = WiFi.SSID(i);
            net.rssi = WiFi.RSSI(i);
            net.encryptionType = WiFi.encryptionType(i);
            networks.push_back(net);
        }
    }
    WiFi.scanDelete(); // 必须无条件释放 ESP32 内部扫描结果内存缓冲区
    
    // 扫描完成后停止 RF 射频发射器省电
    WiFi.disconnect(false, false);
    delay(50);
    esp_wifi_stop();
    LOG_I("APP", "WiFi scan complete. RF stopped for power saving.");
    
    return networks;
}

void HalWifi::saveCredentials(const String& ssid, const String& password) {
    Preferences prefs;
    prefs.begin("wifi", false); // false = read/write
    prefs.putString("ssid", ssid);
    prefs.putString("pass", password);
    prefs.end();
    LOG_I("APP", "WiFi credentials saved to NVS.");
}

bool HalWifi::loadCredentials(String& outSsid, String& outPassword) {
    Preferences prefs;
    prefs.begin("wifi", true); // true = read-only
    outSsid = prefs.getString("ssid", "");
    outPassword = prefs.getString("pass", "");
    prefs.end();
    
    return outSsid.length() > 0;
}

void HalWifi::disconnect() {
    WiFi.disconnect(true, true);
    delay(50);
    WiFi.mode(WIFI_OFF);
    LOG_I("APP", "WiFi disconnected, turned OFF to return memory to heap.");
}
