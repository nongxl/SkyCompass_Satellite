#include "hal_wifi.h"
#include "core/log_manager.h"
#include <esp_wifi.h>
#include <time.h>

void HalWifi::begin(const char* ssid, const char* password, int32_t channel, const uint8_t* bssid) {
    if (ssid == nullptr || strlen(ssid) == 0) {
        LOG_I("APP", "WiFi SSID is empty, skipping WiFi connection.");
        return;
    }
    
    // 内存安全防护：WiFi 驱动在开机 preinit() 已预先分配，启动射频仅需约 1.5KB 堆内存
    if (ESP.getFreeHeap() < 6000 || ESP.getMaxAllocHeap() < 1800) {
        LOG_W("APP", "WiFi begin aborted: insufficient memory (Free: %u, MaxBlock: %u)", 
              (unsigned int)ESP.getFreeHeap(), (unsigned int)ESP.getMaxAllocHeap());
        return;
    }
    
    if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == String(ssid)) {
        LOG_I("APP", "WiFi already connected: %s", WiFi.localIP().toString().c_str());
        return;
    }

    LOG_I("APP", "Connecting to WiFi: '%s' (Target Ch: %d, BSSID: %s)", 
          ssid, (int)channel, bssid ? "SPECIFIED" : "AUTO");
    
    // 1. 确保处于 STA 模式并设置中国区 1~13 全信道支持 (解决手机热点在 12/13 信道报 201 NO_AP_FOUND 故障)
    if (WiFi.getMode() != WIFI_STA) {
        WiFi.mode(WIFI_STA);
        delay(30);
    }
    wifi_country_t country = { "CN", 1, 13, 20, WIFI_COUNTRY_POLICY_AUTO };
    esp_wifi_set_country(&country);

    // 2. 唤醒 Wi-Fi 射频并禁用省电休眠以保证最大灵敏度
    esp_wifi_start();
    delay(20);
    WiFi.setSleep(false);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    
    // 3. 彻底清除 netif 上的残留静态 IP，确保纯净标准 DHCP Client 正常工作
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_dhcpc_stop(netif);
        esp_netif_ip_info_t ip_info;
        memset(&ip_info, 0, sizeof(ip_info));
        esp_netif_set_ip_info(netif, &ip_info);
        esp_netif_dhcpc_start(netif);
    }

    // 4. 调用 WiFi.begin 发起连接
    if (channel > 0 && bssid != nullptr) {
        WiFi.begin(ssid, password, channel, bssid);
    } else if (channel > 0) {
        WiFi.begin(ssid, password, channel);
    } else {
        WiFi.begin(ssid, password);
    }
    
    // 5. 关键增强：覆盖 WiFi.begin 内部的 memset 清零，强制注入 PMF 兼容模式与全信道扫描策略
    // （解决 Android 14 / 三星手机 WPA2/WPA3 混合热点报 201 NO_AP_FOUND 的核心根因）
    wifi_config_t conf;
    if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK) {
        conf.sta.scan_method = (channel > 0) ? WIFI_FAST_SCAN : WIFI_ALL_CHANNEL_SCAN;
        conf.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
        conf.sta.threshold.rssi = -127;
        conf.sta.threshold.authmode = WIFI_AUTH_OPEN;
        conf.sta.pmf_cfg.capable = true;   // 允许 PMF 协商，兼容现代 Android 14 / 三星 / iOS 热点
        conf.sta.pmf_cfg.required = false;  // 不强制要求 PMF
        esp_wifi_set_config(WIFI_IF_STA, &conf);
        esp_wifi_connect();
    }
    
    // 6. 平滑等待连接与 DHCP 分配，最多等 24 次（12 秒）
    int retries = 0;
    bool fallbackToAllChannel = false;
    while (WiFi.status() != WL_CONNECTED && retries < 24) {
        delay(500);
        LOG_I("APP", "[WiFi] Connecting to '%s'... (%d/24)", ssid, retries + 1);
        retries++;
        
        // 若指定了信道但前 6 次（3 秒）仍未搜到，说明手机热点可能更换了信道或 BSSID 发生了变化
        // 自动降级为全信道扫描（All Channel Scan）进行全频段搜寻
        if (channel > 0 && retries == 6 && !fallbackToAllChannel && WiFi.status() != WL_CONNECTED) {
            fallbackToAllChannel = true;
            LOG_I("APP", "[WiFi] Target channel missed, falling back to full-spectrum scan...");
            if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK) {
                conf.sta.channel = 0;
                conf.sta.bssid_set = 0;
                conf.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
                esp_wifi_set_config(WIFI_IF_STA, &conf);
                esp_wifi_connect();
            }
        }
        
        // 若中途遇到临时 NO_AP 状态，在第 12 次尝试重新触发关联
        if (WiFi.status() == WL_NO_SSID_AVAIL && retries == 12) {
            LOG_I("APP", "[WiFi] AP beacon probing... retrying association");
            esp_wifi_connect();
        }
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        LOG_I("APP", "\nWiFi Connected! IP: %s, GW: %s, DNS: %s (Channel: %d)", 
              WiFi.localIP().toString().c_str(), 
              WiFi.gatewayIP().toString().c_str(), 
              WiFi.dnsIP().toString().c_str(),
              (int)WiFi.channel());
        // 自动更新最新成功的信道至 NVS
        int32_t activeChannel = WiFi.channel();
        if (activeChannel > 0) {
            Preferences prefs;
            if (prefs.begin("wifi", false)) {
                prefs.putInt("ch", activeChannel);
                prefs.end();
            }
        }
    } else {
        LOG_I("APP", "\nWiFi Connection Failed (Timeout).");
        disconnect();
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
    
    if (now < 1704067200) {
        return 0;
    }
    return (uint32_t)now;
}

std::vector<WiFiNetwork> HalWifi::scanNetworks() {
    std::vector<WiFiNetwork> networks;
    LOG_I("APP", "Scanning WiFi networks...");
    
    if (WiFi.getMode() != WIFI_STA) {
        WiFi.mode(WIFI_STA);
        delay(30);
    }
    esp_wifi_start();
    delay(20);
    WiFi.setSleep(false);
    
    int n = WiFi.scanNetworks(false, true);
    LOG_I("APP", "Found %d networks", n);
    
    if (n > 0) {
        for (int i = 0; i < n; ++i) {
            WiFiNetwork net;
            net.ssid = WiFi.SSID(i);
            net.rssi = WiFi.RSSI(i);
            net.encryptionType = WiFi.encryptionType(i);
            net.channel = WiFi.channel(i);
            const uint8_t* bssidPtr = WiFi.BSSID(i);
            if (bssidPtr) {
                memcpy(net.bssid, bssidPtr, 6);
            }
            networks.push_back(net);
            LOG_I("APP", "[SCAN] %d: '%s' (RSSI: %ddBm, Ch: %d, Enc: %d)", 
                  i, net.ssid.c_str(), (int)net.rssi, (int)net.channel, (int)net.encryptionType);
        }
    }
    WiFi.scanDelete(); // 必须无条件释放 ESP32 内部扫描结果内存缓冲区
    
    LOG_I("APP", "WiFi scan complete.");
    return networks;
}

void HalWifi::saveCredentials(const String& ssid, const String& password, int32_t channel) {
    Preferences prefs;
    prefs.begin("wifi", false); // false = read/write
    prefs.putString("ssid", ssid);
    prefs.putString("pass", password);
    if (channel > 0) {
        prefs.putInt("ch", channel);
    }
    prefs.end();
    LOG_I("APP", "WiFi credentials saved to NVS (SSID: %s, Ch: %d).", ssid.c_str(), (int)channel);
}

bool HalWifi::loadCredentials(String& outSsid, String& outPassword, int32_t* outChannel) {
    Preferences prefs;
    prefs.begin("wifi", true); // true = read-only
    outSsid = prefs.getString("ssid", "");
    outPassword = prefs.getString("pass", "");
    if (outChannel) {
        *outChannel = prefs.getInt("ch", 0);
    }
    prefs.end();
    
    return outSsid.length() > 0;
}

void HalWifi::disconnect() {
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);
    delay(20);
    // 彻底停闭射频硬件（0发射、0功耗），但保留 STA 模式与 DMA 接收缓冲池，杜绝内存碎片化引发 esp_wifi_init 257
    esp_wifi_stop();
    WiFi.setSleep(true);
    LOG_I("APP", "WiFi RF hardware stopped (STA retained). Free Heap: %u", (unsigned int)ESP.getFreeHeap());
}

void HalWifi::preinit() {
    WiFi.mode(WIFI_STA);
    delay(30);
    wifi_country_t country = { "CN", 1, 13, 20, WIFI_COUNTRY_POLICY_AUTO };
    esp_wifi_set_country(&country);
    esp_wifi_stop();
    WiFi.setSleep(true);
    LOG_I("APP", "WiFi pre-initialized in clean SRAM (CN 1-13 channels enabled). Free Heap: %u", (unsigned int)ESP.getFreeHeap());
}

