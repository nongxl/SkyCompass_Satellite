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

std::vector<SavedWifiCred> HalWifi::getSavedNetworks() {
    std::vector<SavedWifiCred> list;
    Preferences prefs;
    
    // 优先读取多网络配置列表
    if (prefs.begin("wifi_list", true)) {
        int count = prefs.getInt("count", 0);
        if (count > 0) {
            if (count > MAX_SAVED_NETWORKS) count = MAX_SAVED_NETWORKS;
            for (int i = 0; i < count; ++i) {
                String sKey = "s" + String(i);
                String pKey = "p" + String(i);
                String cKey = "c" + String(i);
                String s = prefs.getString(sKey.c_str(), "");
                String p = prefs.getString(pKey.c_str(), "");
                int32_t c = prefs.getInt(cKey.c_str(), 0);
                if (s.length() > 0) {
                    list.push_back({s, p, c});
                }
            }
        }
        prefs.end();
    }
    
    // 若多网络列表为空，尝试从历史单网络配置（"wifi" 命名空间）无缝迁移
    if (list.empty()) {
        if (prefs.begin("wifi", true)) {
            String oldSsid = prefs.getString("ssid", "");
            String oldPass = prefs.getString("pass", "");
            int32_t oldCh = prefs.getInt("ch", 0);
            prefs.end();
            
            if (oldSsid.length() > 0) {
                list.push_back({oldSsid, oldPass, oldCh});
                // 自动固化迁移至 wifi_list
                if (prefs.begin("wifi_list", false)) {
                    prefs.putInt("count", 1);
                    prefs.putString("s0", oldSsid);
                    prefs.putString("p0", oldPass);
                    prefs.putInt("c0", oldCh);
                    prefs.end();
                    LOG_I("APP", "[WiFi] Migrated legacy credential to multi-network storage: '%s'", oldSsid.c_str());
                }
            }
        }
    }
    
    return list;
}

bool HalWifi::hasCredential(const String& ssid) {
    if (ssid.length() == 0) return false;
    auto list = getSavedNetworks();
    for (const auto& item : list) {
        if (item.ssid == ssid) return true;
    }
    return false;
}

bool HalWifi::getCredential(const String& ssid, String& outPassword, int32_t* outChannel) {
    if (ssid.length() == 0) return false;
    auto list = getSavedNetworks();
    for (const auto& item : list) {
        if (item.ssid == ssid) {
            outPassword = item.password;
            if (outChannel) {
                *outChannel = item.channel;
            }
            return true;
        }
    }
    return false;
}

void HalWifi::saveCredentials(const String& ssid, const String& password, int32_t channel) {
    if (ssid.length() == 0) return;
    
    auto list = getSavedNetworks();
    // 检查是否已有记录并移除（后续置顶为最新记录）
    int foundIdx = -1;
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].ssid == ssid) {
            foundIdx = (int)i;
            break;
        }
    }
    
    SavedWifiCred newEntry = {ssid, password, channel};
    if (foundIdx >= 0) {
        list.erase(list.begin() + foundIdx);
    }
    list.insert(list.begin(), newEntry); // 置顶为 Most-Recently-Used
    
    // 超出最大容量时截断末尾最久未用的配置
    if ((int)list.size() > MAX_SAVED_NETWORKS) {
        list.resize(MAX_SAVED_NETWORKS);
    }
    
    // 保存至 wifi_list 命名空间
    Preferences prefs;
    if (prefs.begin("wifi_list", false)) {
        prefs.clear();
        prefs.putInt("count", (int)list.size());
        for (size_t i = 0; i < list.size(); ++i) {
            prefs.putString(("s" + String(i)).c_str(), list[i].ssid);
            prefs.putString(("p" + String(i)).c_str(), list[i].password);
            prefs.putInt(("c" + String(i)).c_str(), list[i].channel);
        }
        prefs.end();
    }
    
    // 同步更新兼容旧版单一记录（"wifi" 命名空间）
    if (prefs.begin("wifi", false)) {
        prefs.putString("ssid", ssid);
        prefs.putString("pass", password);
        if (channel > 0) {
            prefs.putInt("ch", channel);
        }
        prefs.end();
    }
    
    LOG_I("APP", "[WiFi] Saved credentials for '%s' (Total saved: %d, Ch: %d)", 
          ssid.c_str(), (int)list.size(), (int)channel);
}

bool HalWifi::forgetNetwork(const String& ssid) {
    if (ssid.length() == 0) return false;
    auto list = getSavedNetworks();
    bool removed = false;
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (it->ssid == ssid) {
            list.erase(it);
            removed = true;
            break;
        }
    }
    if (!removed) return false;
    
    Preferences prefs;
    if (prefs.begin("wifi_list", false)) {
        prefs.clear();
        prefs.putInt("count", (int)list.size());
        for (size_t i = 0; i < list.size(); ++i) {
            prefs.putString(("s" + String(i)).c_str(), list[i].ssid);
            prefs.putString(("p" + String(i)).c_str(), list[i].password);
            prefs.putInt(("c" + String(i)).c_str(), list[i].channel);
        }
        prefs.end();
    }
    LOG_I("APP", "[WiFi] Forgot network: '%s' (Remaining: %d)", ssid.c_str(), (int)list.size());
    return true;
}

bool HalWifi::loadCredentials(String& outSsid, String& outPassword, int32_t* outChannel) {
    auto list = getSavedNetworks();
    if (list.empty()) {
        outSsid = "";
        outPassword = "";
        if (outChannel) *outChannel = 0;
        return false;
    }
    outSsid = list[0].ssid;
    outPassword = list[0].password;
    if (outChannel) {
        *outChannel = list[0].channel;
    }
    return true;
}

bool HalWifi::autoConnect() {
    if (isConnected()) {
        return true;
    }
    
    auto saved = getSavedNetworks();
    if (saved.empty()) {
        LOG_I("APP", "[WiFi] AutoConnect aborted: No saved networks in NVS.");
        return false;
    }
    
    // 如果只保存了 1 个网络，直接快速直连，无需额外耗时全信道扫描
    if (saved.size() == 1) {
        LOG_I("APP", "[WiFi] AutoConnect: Connecting to single saved network '%s'...", saved[0].ssid.c_str());
        begin(saved[0].ssid.c_str(), saved[0].password.c_str(), saved[0].channel);
        return isConnected();
    }
    
    // 保存了多个已知网络：先扫描周围 AP，自动漫游连接匹配到且信号最好的已知网络
    LOG_I("APP", "[WiFi] AutoConnect: %d saved networks found. Scanning environment...", (int)saved.size());
    auto scanned = scanNetworks();
    
    int bestSavedIdx = -1;
    int bestScanIdx = -1;
    int32_t bestRssi = -999;
    
    for (size_t s = 0; s < scanned.size(); ++s) {
        for (size_t k = 0; k < saved.size(); ++k) {
            if (scanned[s].ssid == saved[k].ssid) {
                // 优先挑选信号最好的已知网络；若信号相近（差在5dB以内），优先尊重最近使用排序
                if (scanned[s].rssi > bestRssi) {
                    bestRssi = scanned[s].rssi;
                    bestSavedIdx = (int)k;
                    bestScanIdx = (int)s;
                }
                break;
            }
        }
    }
    
    if (bestSavedIdx >= 0 && bestScanIdx >= 0) {
        LOG_I("APP", "[WiFi] AutoConnect: Best match '%s' (RSSI: %ddBm, Ch: %d)",
              saved[bestSavedIdx].ssid.c_str(), (int)bestRssi, (int)scanned[bestScanIdx].channel);
        begin(saved[bestSavedIdx].ssid.c_str(),
              saved[bestSavedIdx].password.c_str(),
              scanned[bestScanIdx].channel,
              scanned[bestScanIdx].bssid);
        if (isConnected()) {
            // 将此连通的网络置顶刷新为最近使用
            saveCredentials(saved[bestSavedIdx].ssid, saved[bestSavedIdx].password, scanned[bestScanIdx].channel);
            return true;
        }
    }
    
    // 若扫描未能命中（例如隐藏 SSID、弱信号漏搜），回退尝试最近一次使用的网络
    LOG_W("APP", "[WiFi] AutoConnect: No saved AP matched in scan. Fallback to '%s'", saved[0].ssid.c_str());
    begin(saved[0].ssid.c_str(), saved[0].password.c_str(), saved[0].channel);
    return isConnected();
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

