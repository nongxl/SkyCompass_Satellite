#include "orbit_data_provider.h"
#include "json_parser.h"
#include "log_manager.h"
#include "tle_parser.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>  // Plain HTTP — no TLS heap cost
#include <LittleFS.h>
#include <esp_task_wdt.h>
#include <lwip/sockets.h>
#include <errno.h>
#include <memory>

// 共享静态 I/O 缓冲区（仅占 2KB SRAM，替代原先分散的 7KB 静态缓冲，永久归还 5KB 连续堆内存给 WiFi/lwIP）
static uint8_t s_sharedIoBuf[1024];
static char s_sharedLineBuf[1024];

// Load single satellite from cache or network (plain HTTP — no TLS memory cost)
bool OrbitDataProvider::loadByCatalogNumber(uint32_t catNum, OrbitRecord& record, bool forceRefresh, WiFiClient* sharedClient, int* outHttpCode) {
    if (outHttpCode) *outHttpCode = 0;
    char path[32];
    sprintf(path, "/cat_%u.json", (unsigned int)catNum);
    
    if (!forceRefresh && LittleFS.exists(path)) {
        File f = LittleFS.open(path, "r");
        if (f) {
            String content = f.readString();
            f.close();
            JSONParser parser;
            if (parser.parse(content, record)) {
                if (outHttpCode) *outHttpCode = 200;
                return true;
            }
        }
    }
    
    // Use plain HTTP — CelesTrak supports HTTP and this saves ~40KB TLS heap
    // 注意：超时必须严格 < 5000ms，防止 HTTPClient::handleHeaderResponse 内 Stream::timedRead 忙等触发 Core 0 IDLE0 看门狗崩溃
    std::unique_ptr<HTTPClient> http(new HTTPClient());
    if (!http) return false;
    http->setTimeout(3500);
    http->setConnectTimeout(3500);
    http->setUserAgent("Mozilla/5.0 (ESP32-Cardputer; SkyCompass Satellite Tracker)");
    http->setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    char url[128];
    sprintf(url, "http://celestrak.org/NORAD/elements/gp.php?CATNR=%u&FORMAT=json", (unsigned int)catNum);
    
    std::unique_ptr<WiFiClient> client(new WiFiClient());
    if (!client) return false;
    
    if (sharedClient) {
        http->begin(*sharedClient, url);
    } else {
        http->begin(*client, url);
    }
    
    int httpCode = http->GET();
    if (httpCode < 0) {
        http->end();
        delay(1000);
        if (sharedClient) {
            http->begin(*sharedClient, url);
        } else {
            http->begin(*client, url);
        }
        httpCode = http->GET();
    }
    if (outHttpCode) *outHttpCode = httpCode;
    bool success = false;
    if (httpCode == HTTP_CODE_OK) {
        String payload = http->getString();
        payload.trim();
        if (payload.startsWith("[") && payload.endsWith("]")) {
            payload = payload.substring(1, payload.length() - 1);
            payload.trim();
        }
        
        JSONParser parser;
        if (parser.parse(payload, record)) {
            File f = LittleFS.open(path, "w", true);
            if (f) {
                f.print(payload);
                f.close();
            }
            success = true;
        }
    }
    http->end();
    return success;
}

static void processRecentLaunchItem(std::vector<RecentLaunchItem>& tempLaunches, const OrbitRecord& record, std::vector<std::vector<float>>* outPhases = nullptr) {
    String batchId = record.getBatchId();
    if (batchId.length() == 0) return;
    
    int foundIdx = -1;
    for (size_t i = 0; i < tempLaunches.size(); i++) {
        if (tempLaunches[i].batchId == batchId) {
            foundIdx = i;
            break;
        }
    }
    
    if (foundIdx != -1) {
        if (tempLaunches[foundIdx].satelliteCount < 60) {
            tempLaunches[foundIdx].satelliteCount++;
        }
        if (outPhases && (size_t)foundIdx < outPhases->size()) {
            (*outPhases)[foundIdx].push_back(record.meanAnomaly);
        }
    } else {
        RecentLaunchItem item;
        item.batchId = batchId;
        String rawName = record.name;
        int sepIdx = rawName.indexOf('-');
        if (sepIdx == -1) sepIdx = rawName.indexOf('_');
        if (sepIdx == -1) sepIdx = rawName.indexOf(' ');
        if (sepIdx != -1) {
            item.displayName = rawName.substring(0, sepIdx);
        } else {
            int lastAlpha = rawName.length() - 1;
            while (lastAlpha >= 0 && rawName[lastAlpha] >= '0' && rawName[lastAlpha] <= '9') {
                lastAlpha--;
            }
            item.displayName = rawName.substring(0, lastAlpha + 1);
        }
        item.displayName.trim();
        
        item.isGroup = true;
        if (item.displayName.indexOf("OBJECT") != -1 || 
            item.displayName.indexOf("DEBRIS") != -1 ||
            item.displayName.indexOf("R/B") != -1 ||
            item.displayName.length() == 0) {
            item.isGroup = false;
            item.displayName = "Miscellaneous / Deb";
        }
        
        item.satelliteCount = 1;
        item.selected = false;
        item.epoch = record.epochUnix;
        item.inclination = record.inclination;
        
        if (record.meanMotion > 0) {
            double n = record.meanMotion * 2.0 * 3.141592653589793 / 86400.0;
            double mu = 3.986004418e14;
            double a = pow(mu / (n * n), 1.0 / 3.0) / 1000.0;
            item.avgAlt = a - 6378.137;
        }
        item.repSatName = record.name;
        item.iconType = ICON_SATELLITE;
        item.repTLE.name = record.name;
        item.repTLE.baseScore = 0;
        SGP4Calc::buildPseudoTle(record, item.repTLE.line1, item.repTLE.line2);
        tempLaunches.push_back(item);
        if (outPhases) {
            std::vector<float> pList;
            pList.push_back(record.meanAnomaly);
            outPhases->push_back(pList);
        }
    }
}

// 不区分大小写查找子串（用于轻量解析 HTTP 响应头 Content-Length）
static const char* findHeaderValueIgnoreCase(const char* buf, const char* key) {
    size_t keyLen = strlen(key);
    for (const char* p = buf; *p != '\0'; ++p) {
        if (strncasecmp(p, key, keyLen) == 0) {
            const char* v = p + keyLen;
            while (*v == ' ' || *v == '\t') ++v;
            return v;
        }
    }
    return nullptr;
}

// Download Recent Launches via non-blocking lwIP socket + compact CSV stream (~40KB vs ~110KB JSON)
bool OrbitDataProvider::downloadRecentLaunches(std::vector<RecentLaunchItem>& tempLaunches, int* outHttpCode) {
    if (outHttpCode) *outHttpCode = 0;

    // 再次确认 Wi-Fi DNS 服务器是否分配完成
    if (WiFi.status() == WL_CONNECTED && WiFi.dnsIP() == IPAddress(0, 0, 0, 0)) {
        int waitDns = 0;
        while (WiFi.dnsIP() == IPAddress(0, 0, 0, 0) && waitDns < 15) {
            delay(200);
            waitDns++;
        }
    }

    static const char* CELESTRAK_REQ =
        "GET /NORAD/elements/gp.php?GROUP=last-30-days&FORMAT=csv HTTP/1.1\r\n"
        "Host: celestrak.org\r\n"
        "User-Agent: Mozilla/5.0 (ESP32-Cardputer; SkyCompass Satellite Tracker)\r\n"
        "Accept: text/csv,*/*\r\n"
        "Connection: close\r\n\r\n";

    extern String recentLaunchErrorMsg;
    esp_task_wdt_reset();

    static const char* RAW_JSONL_TMP = "/json_recent_raw.tmp";
    static const char* RAW_JSONL_PATH = "/json_recent_raw.jsonl";

    // 解析 DNS（在创建 Socket 前完成）
    IPAddress hostIp;
    if (!WiFi.hostByName("celestrak.org", hostIp)) {
        LOG_W("RECENT_LAUNCH", "DNS resolution failed for celestrak.org");
        if (outHttpCode) *outHttpCode = -1;
        return false;
    }
    LOG_I("RECENT_LAUNCH", "DNS resolved celestrak.org -> %s (Free Heap: %u, MaxBlock: %u)",
          hostIp.toString().c_str(), (unsigned int)ESP.getFreeHeap(), (unsigned int)ESP.getMaxAllocHeap());

    auto closeSocketFast = [](int& s) {
        if (s >= 0) {
            struct linger sl = {1, 0}; // 立即发送 RST 释放 tcp_pcb 与 ooseq 乱序缓冲队列，避免 FIN_WAIT 占用堆内存
            lwip_setsockopt(s, SOL_SOCKET, SO_LINGER, &sl, sizeof(sl));
            lwip_close(s);
            s = -1;
        }
    };

    int totalReadBytes = 0;
    int expectedSize = 0;
    int rawCount = 0;
    bool downloadOk = false;

    for (int attempt = 0; attempt < 3 && !downloadOk; ++attempt) {
        if (attempt > 0) {
            LOG_I("RECENT_LAUNCH", "Retrying CSV download (attempt %d/3, Free Heap: %u, MaxBlock: %u)...",
                  attempt + 1, (unsigned int)ESP.getFreeHeap(), (unsigned int)ESP.getMaxAllocHeap());
            vTaskDelay(pdMS_TO_TICKS(350));
            esp_task_wdt_reset();
        }

        // 1. 在发起 TCP 连接前预先打开目标文件，消除 HTTP 200 返回后的 Flash 擦写空窗期
        File fOut = LittleFS.open(RAW_JSONL_TMP, "w", true);
        if (!fOut) {
            LOG_E("RECENT_LAUNCH", "Failed to open %s for writing!", RAW_JSONL_TMP);
            if (outHttpCode) *outHttpCode = -100;
            return false;
        }

        // 2. 创建原生非阻塞 lwIP Socket（彻底省去 HTTPClient + WiFiClient 约 3KB 堆开销，并根除 Stream::timedRead 看门狗崩溃）
        int sock = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock < 0) {
            LOG_E("RECENT_LAUNCH", "lwip_socket failed: errno=%d", errno);
            fOut.close();
            if (outHttpCode) *outHttpCode = -2;
            continue;
        }

        int fl = lwip_fcntl(sock, F_GETFL, 0);
        lwip_fcntl(sock, F_SETFL, fl | O_NONBLOCK);
        int nodelay = 1;
        lwip_setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

        struct sockaddr_in servAddr;
        memset(&servAddr, 0, sizeof(servAddr));
        servAddr.sin_family = AF_INET;
        servAddr.sin_port = htons(80);
        uint32_t rawIp = (uint32_t)hostIp;
        memcpy(&servAddr.sin_addr.s_addr, &rawIp, 4);

        int connRes = lwip_connect(sock, (struct sockaddr*)&servAddr, sizeof(servAddr));
        if (connRes < 0 && errno != EINPROGRESS) {
            LOG_W("RECENT_LAUNCH", "lwip_connect immediate error: errno=%d", errno);
            closeSocketFast(sock);
            fOut.close();
            if (outHttpCode) *outHttpCode = -1;
            continue;
        }

        // 3. 使用 200ms 分片 select 等待连接完成（阻塞期间让出 Core 0 给 IDLE0，绝对不触发 TWDT）
        bool connected = (connRes == 0);
        uint32_t connStartMs = millis();
        while (!connected && (millis() - connStartMs < 7000)) {
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(sock, &wfds);
            struct timeval tv = {0, 200000}; // 200ms
            int sres = lwip_select(sock + 1, nullptr, &wfds, nullptr, &tv);
            esp_task_wdt_reset();
            if (sres > 0 && FD_ISSET(sock, &wfds)) {
                int sockErr = 0;
                socklen_t errLen = sizeof(sockErr);
                if (lwip_getsockopt(sock, SOL_SOCKET, SO_ERROR, &sockErr, &errLen) == 0 && sockErr == 0) {
                    connected = true;
                } else {
                    LOG_W("RECENT_LAUNCH", "TCP connect failed: sockErr=%d", sockErr);
                    break;
                }
            } else if (sres < 0 && errno != EINTR) {
                break;
            }
        }

        if (!connected) {
            closeSocketFast(sock);
            fOut.close();
            if (outHttpCode) *outHttpCode = -1;
            continue;
        }

        // 4. 发送 HTTP GET 请求
        size_t reqLen = strlen(CELESTRAK_REQ);
        size_t reqSent = 0;
        uint32_t sendStartMs = millis();
        while (reqSent < reqLen && (millis() - sendStartMs < 5000)) {
            int s = lwip_send(sock, CELESTRAK_REQ + reqSent, reqLen - reqSent, MSG_DONTWAIT);
            if (s > 0) {
                reqSent += (size_t)s;
            } else if (s < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
                fd_set wfds;
                FD_ZERO(&wfds);
                FD_SET(sock, &wfds);
                struct timeval tv = {0, 200000};
                lwip_select(sock + 1, nullptr, &wfds, nullptr, &tv);
            } else {
                break;
            }
            esp_task_wdt_reset();
        }

        if (reqSent < reqLen) {
            LOG_W("RECENT_LAUNCH", "Failed to send full HTTP GET request (%u/%u)", (unsigned int)reqSent, (unsigned int)reqLen);
            closeSocketFast(sock);
            fOut.close();
            if (outHttpCode) *outHttpCode = -3;
            continue;
        }

        // 5. 非阻塞 select + recv 高速流式接收 HTTP 头与 CSV 正文
        bool headerDone = false;
        size_t hdrLen = 0;
        int httpStatus = 0;
        expectedSize = -1;
        totalReadBytes = 0;
        rawCount = 0;
        size_t outLen = 0;
        int curLineLen = 0;
        bool isFirstNonEmptyLine = true;
        bool curLineIsHeader = false;
        int lastReportedKb = -1;
        bool timedOut = false;
        bool httpError = false;
        bool eofReached = false;

        auto flushOut = [&]() {
            if (outLen > 0) {
                fOut.write((const uint8_t*)s_sharedLineBuf, outLen);
                outLen = 0;
                esp_task_wdt_reset();
            }
        };

        auto feedBodyChar = [&](char c) {
            if (c == '\r') return;
            if (curLineLen == 0 && c != '\n') {
                // 仅首行可能是 CSV 表头 OBJECT_NAME,...；避免误过滤名称为 OBJECT A / ONEWEB 的卫星
                curLineIsHeader = isFirstNonEmptyLine && (c == 'O' || c == 'o');
            }
            if (c == '\n') {
                if (curLineLen > 0) {
                    isFirstNonEmptyLine = false;
                    if (!curLineIsHeader && curLineLen > 20) {
                        rawCount++;
                    }
                    if (!curLineIsHeader) {
                        s_sharedLineBuf[outLen++] = '\n';
                        if (outLen >= sizeof(s_sharedLineBuf)) {
                            flushOut();
                        }
                    }
                    curLineLen = 0;
                    curLineIsHeader = false;
                }
            } else {
                curLineLen++;
                if (!curLineIsHeader) {
                    s_sharedLineBuf[outLen++] = c;
                    if (outLen >= sizeof(s_sharedLineBuf)) {
                        flushOut();
                    }
                }
            }
        };

        uint32_t lastReadMs = millis();
        const uint32_t STALL_TIMEOUT_MS = 8000;

        while (!eofReached && !httpError && !timedOut) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(sock, &rfds);
            struct timeval tv = {0, 200000}; // 200ms 内核休眠等待，自动唤醒且完美喂狗 IDLE0
            int sres = lwip_select(sock + 1, &rfds, nullptr, nullptr, &tv);
            esp_task_wdt_reset();

            if (sres > 0 && FD_ISSET(sock, &rfds)) {
                // 一次性循环抽干所有已到达的 TCP 分段，立即归还 lwIP pbuf 堆块并更新接收窗口
                while (true) {
                    int r = lwip_recv(sock, s_sharedIoBuf, sizeof(s_sharedIoBuf), MSG_DONTWAIT);
                    if (r > 0) {
                        lastReadMs = millis();
                        int bodyStartIdx = 0;

                        if (!headerDone) {
                            for (int i = 0; i < r; ++i) {
                                if (hdrLen < sizeof(s_sharedLineBuf) - 1) {
                                    s_sharedLineBuf[hdrLen++] = (char)s_sharedIoBuf[i];
                                }
                                if ((hdrLen >= 4 &&
                                     s_sharedLineBuf[hdrLen - 4] == '\r' && s_sharedLineBuf[hdrLen - 3] == '\n' &&
                                     s_sharedLineBuf[hdrLen - 2] == '\r' && s_sharedLineBuf[hdrLen - 1] == '\n') ||
                                    (hdrLen >= 2 &&
                                     s_sharedLineBuf[hdrLen - 2] == '\n' && s_sharedLineBuf[hdrLen - 1] == '\n')) {
                                    s_sharedLineBuf[hdrLen] = '\0';
                                    headerDone = true;
                                    bodyStartIdx = i + 1;

                                    // 解析 HTTP 状态码: "HTTP/1.1 200 OK"
                                    const char* sp = strchr(s_sharedLineBuf, ' ');
                                    if (sp) httpStatus = atoi(sp + 1);
                                    if (outHttpCode) *outHttpCode = httpStatus;

                                    const char* clVal = findHeaderValueIgnoreCase(s_sharedLineBuf, "Content-Length:");
                                    if (clVal) expectedSize = atoi(clVal);

                                    LOG_I("RECENT_LAUNCH", "HTTP %d, Content-Length: %d (Free Heap: %u, MaxBlock: %u)",
                                          httpStatus, expectedSize, (unsigned int)ESP.getFreeHeap(), (unsigned int)ESP.getMaxAllocHeap());

                                    outLen = 0; // 复用 s_sharedLineBuf 作为 1KB 正文写盘缓冲
                                    if (httpStatus != 200) {
                                        httpError = true;
                                    }
                                    break;
                                }
                            }
                            if (!headerDone) {
                                continue;
                            }
                            if (httpError) {
                                break;
                            }
                        }

                        int bodyBytes = r - bodyStartIdx;
                        if (bodyBytes > 0) {
                            totalReadBytes += bodyBytes;
                            for (int i = bodyStartIdx; i < r; ++i) {
                                feedBodyChar((char)s_sharedIoBuf[i]);
                            }
                        }

                        if (expectedSize > 0 && totalReadBytes >= expectedSize) {
                            eofReached = true;
                            break;
                        }
                    } else if (r == 0) {
                        eofReached = true;
                        break;
                    } else {
                        if (errno != EWOULDBLOCK && errno != EAGAIN) {
                            LOG_W("RECENT_LAUNCH", "lwip_recv error: errno=%d (read %d/%d)", errno, totalReadBytes, expectedSize);
                            eofReached = true;
                        }
                        break;
                    }
                }

                if (headerDone && !httpError) {
                    int currKb = totalReadBytes / 1024;
                    if (currKb != lastReportedKb) {
                        lastReportedKb = currKb;
                        char progBuf[16];
                        if (expectedSize > 0) {
                            int pct = (int)((int64_t)totalReadBytes * 100 / expectedSize);
                            if (pct > 100) pct = 100;
                            snprintf(progBuf, sizeof(progBuf), "%dKB (%d%%)", currKb, pct);
                        } else {
                            snprintf(progBuf, sizeof(progBuf), "%dKB", currKb);
                        }
                        recentLaunchErrorMsg = progBuf;
                    }
                }
            } else if (sres < 0 && errno != EINTR) {
                LOG_W("RECENT_LAUNCH", "lwip_select error: errno=%d", errno);
                break;
            } else {
                if (millis() - lastReadMs > STALL_TIMEOUT_MS) {
                    LOG_W("RECENT_LAUNCH", "Socket stall timeout (%u ms, read %d/%d bytes, Free Heap: %u, MaxBlock: %u)",
                          (unsigned int)STALL_TIMEOUT_MS, totalReadBytes, expectedSize,
                          (unsigned int)ESP.getFreeHeap(), (unsigned int)ESP.getMaxAllocHeap());
                    if (outHttpCode) *outHttpCode = -11;
                    timedOut = true;
                    break;
                }
            }
        }

        // 若末尾最后一行无换行符，补齐换行并计入
        if (headerDone && !httpError && curLineLen > 20 && !curLineIsHeader) {
            if (outLen >= sizeof(s_sharedLineBuf)) {
                flushOut();
            }
            s_sharedLineBuf[outLen++] = '\n';
            rawCount++;
        }
        flushOut();
        fOut.close();
        closeSocketFast(sock);

        if (!httpError && !timedOut && headerDone && (expectedSize <= 0 || totalReadBytes >= expectedSize) && rawCount > 0) {
            downloadOk = true;
        } else {
            LOG_W("RECENT_LAUNCH", "Attempt %d incomplete (HTTP %d, %d/%d bytes, %d objects). Discarded temp file.",
                  attempt + 1, httpStatus, totalReadBytes, expectedSize, rawCount);
            LittleFS.remove(RAW_JSONL_TMP);
            if (!timedOut && outHttpCode && *outHttpCode == 200) {
                *outHttpCode = -5;
            }
            if (httpStatus == 403 || httpStatus == 404 || httpStatus == 429) {
                break; // 服务端明确拒绝或限流时不重复重试
            }
        }
    }

    if (!downloadOk) {
        return false;
    }

    LOG_I("RECENT_LAUNCH", "CSV stream complete! Valid satellite records: %d, Total bytes: %d", rawCount, totalReadBytes);

    LittleFS.remove(RAW_JSONL_PATH);
    LittleFS.rename(RAW_JSONL_TMP, RAW_JSONL_PATH);
    LOG_I("RECENT_LAUNCH", "Atomically updated %s with %d records!", RAW_JSONL_PATH, rawCount);
    return true;
}

static const char* META_CACHE_PATH = "/recent_launches_meta.bin";
static const uint32_t META_MAGIC = 0x534B594C; // "SKYL"
static const uint8_t META_VERSION = 2;

static void writeBinStr(File& f, const String& s) {
    uint8_t len = (uint8_t)min((size_t)s.length(), (size_t)255);
    f.write(len);
    if (len > 0) {
        f.write((const uint8_t*)s.c_str(), len);
    }
}

static String readBinStr(File& f) {
    uint8_t len = 0;
    if (f.read(&len, 1) != 1) return "";
    if (len == 0) return "";
    char buf[256];
    int r = f.read((uint8_t*)buf, len);
    if (r <= 0) return "";
    buf[r] = '\0';
    return String(buf);
}

bool OrbitDataProvider::saveRecentLaunchesMeta(const std::vector<RecentLaunchItem>& items) {
    File f = LittleFS.open(META_CACHE_PATH, "w", true);
    if (!f) {
        LOG_E("RECENT_LAUNCH", "Failed to open %s for write", META_CACHE_PATH);
        return false;
    }
    
    uint32_t magic = META_MAGIC;
    uint8_t ver = META_VERSION;
    uint16_t count = (uint16_t)items.size();
    
    f.write((const uint8_t*)&magic, sizeof(magic));
    f.write(&ver, sizeof(ver));
    f.write((const uint8_t*)&count, sizeof(count));
    
    for (const auto& item : items) {
        writeBinStr(f, item.batchId);
        writeBinStr(f, item.displayName);
        writeBinStr(f, item.shortName);
        writeBinStr(f, item.repSatName);
        
        int32_t satCount = item.satelliteCount;
        uint8_t isGrp = item.isGroup ? 1 : 0;
        uint8_t sel = item.selected ? 1 : 0;
        uint32_t ep = item.epoch;
        float inc = item.inclination;
        float alt = item.avgAlt;
        float occ = item.occupancy;
        float occStart = item.occupancyStartPhase;
        float occEnd = item.occupancyEndPhase;
        float repPhase = item.repAlongTrackPhase;
        uint8_t icon = (uint8_t)item.iconType;
        
        f.write((const uint8_t*)&satCount, sizeof(satCount));
        f.write(&isGrp, 1);
        f.write(&sel, 1);
        f.write((const uint8_t*)&ep, sizeof(ep));
        f.write((const uint8_t*)&inc, sizeof(inc));
        f.write((const uint8_t*)&alt, sizeof(alt));
        f.write((const uint8_t*)&occ, sizeof(occ));
        f.write((const uint8_t*)&occStart, sizeof(occStart));
        f.write((const uint8_t*)&occEnd, sizeof(occEnd));
        f.write((const uint8_t*)&repPhase, sizeof(repPhase));
        f.write(&icon, 1);
        
        uint8_t pCount = (uint8_t)item.proxyFormation.size();
        f.write(&pCount, 1);
        for (const auto& pt : item.proxyFormation) {
            f.write((const uint8_t*)&pt.AlongTrackPhase, sizeof(pt.AlongTrackPhase));
            f.write((const uint8_t*)&pt.brightness, sizeof(pt.brightness));
        }

        // Version 2: Write repTLE
        writeBinStr(f, item.repTLE.line1);
        writeBinStr(f, item.repTLE.line2);
    }
    
    f.close();
    LOG_I("RECENT_LAUNCH", "Saved %d launch items to meta snapshot (%s, v%d).", (int)items.size(), META_CACHE_PATH, (int)ver);
    return true;
}

bool OrbitDataProvider::loadRecentLaunchesMeta(std::vector<RecentLaunchItem>& items) {
    if (!LittleFS.exists(META_CACHE_PATH)) return false;
    File f = LittleFS.open(META_CACHE_PATH, "r");
    if (!f) return false;
    
    uint32_t magic = 0;
    uint8_t ver = 0;
    uint16_t count = 0;
    
    if (f.read((uint8_t*)&magic, sizeof(magic)) != sizeof(magic) || magic != META_MAGIC) {
        f.close();
        return false;
    }
    if (f.read(&ver, sizeof(ver)) != sizeof(ver) || (ver != 1 && ver != 2)) {
        f.close();
        return false;
    }
    if (f.read((uint8_t*)&count, sizeof(count)) != sizeof(count) || count > 200) {
        f.close();
        return false;
    }
    
    items.clear();
    items.reserve(count);
    
    for (uint16_t i = 0; i < count; i++) {
        RecentLaunchItem item;
        item.batchId = readBinStr(f);
        item.displayName = readBinStr(f);
        item.shortName = readBinStr(f);
        item.repSatName = readBinStr(f);
        
        int32_t satCount = 0;
        uint8_t isGrp = 0, sel = 0, icon = 0;
        
        f.read((uint8_t*)&satCount, sizeof(satCount));
        f.read(&isGrp, 1);
        f.read(&sel, 1);
        f.read((uint8_t*)&item.epoch, sizeof(item.epoch));
        f.read((uint8_t*)&item.inclination, sizeof(item.inclination));
        f.read((uint8_t*)&item.avgAlt, sizeof(item.avgAlt));
        f.read((uint8_t*)&item.occupancy, sizeof(item.occupancy));
        f.read((uint8_t*)&item.occupancyStartPhase, sizeof(item.occupancyStartPhase));
        f.read((uint8_t*)&item.occupancyEndPhase, sizeof(item.occupancyEndPhase));
        f.read((uint8_t*)&item.repAlongTrackPhase, sizeof(item.repAlongTrackPhase));
        f.read(&icon, 1);
        
        item.satelliteCount = satCount;
        item.isGroup = (isGrp != 0);
        item.selected = (sel != 0);
        item.iconType = (SatIconType)icon;
        
        uint8_t pCount = 0;
        f.read(&pCount, 1);
        item.proxyFormation.clear();
        for (uint8_t p = 0; p < pCount; p++) {
            FormationPoint pt;
            f.read((uint8_t*)&pt.AlongTrackPhase, sizeof(pt.AlongTrackPhase));
            f.read((uint8_t*)&pt.brightness, sizeof(pt.brightness));
            item.proxyFormation.push_back(pt);
        }

        if (ver >= 2) {
            item.repTLE.line1 = readBinStr(f);
            item.repTLE.line2 = readBinStr(f);
            item.repTLE.name = item.repSatName.length() > 0 ? item.repSatName : item.displayName;
        }
        
        items.push_back(item);
    }
    
    f.close();
    LOG_I("RECENT_LAUNCH", "Fast-loaded %d launch items from meta snapshot (v%d)!", (int)items.size(), (int)ver);
    return !items.empty();
}

bool OrbitDataProvider::loadRecentLaunchesFromCache(std::vector<RecentLaunchItem>& tempLaunches, std::vector<std::vector<float>>* outPhases) {
    File f = LittleFS.open("/json_recent_raw.jsonl", "r");
    if (!f) {
        LOG_I("DEBUG", "loadRecentLaunchesFromCache: Failed to open /json_recent_raw.jsonl");
        return false;
    }
    
    tempLaunches.clear();
    tempLaunches.reserve(30);
    if (outPhases) {
        outPhases->clear();
        outPhases->reserve(30);
    }
    
    JSONParser parser;
    int rawCount = 0;
    int parseSuccessCount = 0;
    int lineCount = 0;

    size_t bufLen = 0;
    size_t bufPos = 0;
    size_t linePos = 0;
    bool lineOverflow = false;

    while (f.available() || bufPos < bufLen) {
        if (bufPos >= bufLen) {
            bufLen = f.read(s_sharedIoBuf, sizeof(s_sharedIoBuf));
            bufPos = 0;
            if (bufLen == 0) break;
        }

        char c = (char)s_sharedIoBuf[bufPos++];
        if (c == '\n' || c == '\r') {
            if (linePos > 0) {
                s_sharedLineBuf[linePos] = '\0';
                lineCount++;

                if (!lineOverflow) {
                    // 内存熔断保护：CSV 原生解析零堆分配，阈值放宽至 6000B 仅防 vector 扩容耗尽系统内存
                    if (ESP.getFreeHeap() < 6000) {
                        LOG_W("RECENT_LAUNCH", "Low heap memory during cache load (%u bytes), terminating parse early.", (unsigned int)ESP.getFreeHeap());
                        linePos = 0;
                        break;
                    }

                    OrbitRecord record;
                    if (parser.parse(s_sharedLineBuf, record)) {
                        parseSuccessCount++;
                        rawCount++;
                        processRecentLaunchItem(tempLaunches, record, outPhases);
                    }
                }

                linePos = 0;
                lineOverflow = false;
                if (lineCount % 20 == 0) {
                    esp_task_wdt_reset();
                    taskYIELD();
                }
            } else {
                lineOverflow = false;
            }
        } else {
            if (!lineOverflow) {
                if (linePos < sizeof(s_sharedLineBuf) - 1) {
                    s_sharedLineBuf[linePos++] = c;
                } else {
                    lineOverflow = true;
                }
            }
        }
    }
    if (linePos > 0 && !lineOverflow && ESP.getFreeHeap() >= 6000) {
        s_sharedLineBuf[linePos] = '\0';
        lineCount++;
        OrbitRecord record;
        if (parser.parse(s_sharedLineBuf, record)) {
            parseSuccessCount++;
            rawCount++;
            processRecentLaunchItem(tempLaunches, record, outPhases);
        }
    }

    f.close();
    esp_task_wdt_reset();
    LOG_I("DEBUG", "loadRecentLaunchesFromCache finished: Total Lines: %d, Parse Success: %d, Launches Created: %d", lineCount, parseSuccessCount, (int)tempLaunches.size());
    return rawCount > 0;
}

// Page load level 3 objects from jsonl file
extern Level3ObjectList g_level3Objects;
extern volatile bool recentLaunchDownloading;
bool OrbitDataProvider::loadLevel3ObjectsPage(const RecentLaunchItem& item, int page) {
    if (recentLaunchDownloading) return false;
    g_level3Objects.clear();
    File f = LittleFS.open("/json_recent_raw.jsonl", "r");
    if (!f) return false;
    
    int skipCount = page * 5;
    int loadCount = 0;
    int matchIndex = 0;
    int lineCount = 0;
    
    String cosparForm = "";
    if (item.batchId.length() == 5 && isdigit(item.batchId[0]) && isdigit(item.batchId[1])) {
        int yr = item.batchId.substring(0, 2).toInt();
        String century = (yr >= 50) ? "19" : "20";
        cosparForm = century + item.batchId.substring(0, 2) + "-" + item.batchId.substring(2);
    }
    
    const char* batchIdC = item.batchId.c_str();
    const char* cosparC = cosparForm.c_str();
    bool hasCospar = (cosparForm.length() > 0);
    
    // 复用文件级共享缓冲区 s_sharedIoBuf / s_sharedLineBuf（0 字节栈开销，再省 1.5KB 静态 SRAM）
    static JSONParser parser;
    static OrbitRecord record;

    size_t bufLen = 0;
    size_t bufPos = 0;
    size_t linePos = 0;

    while ((f.available() || bufPos < bufLen) && loadCount < 5) {
        if (bufPos >= bufLen) {
            bufLen = f.read(s_sharedIoBuf, sizeof(s_sharedIoBuf));
            bufPos = 0;
            if (bufLen == 0) break;
        }

        char c = (char)s_sharedIoBuf[bufPos++];
        if (c == '\n' || c == '\r') {
            if (linePos > 0) {
                s_sharedLineBuf[linePos] = '\0';
                lineCount++;

                // 零堆内存开销极速子串匹配
                bool match = false;
                if (strstr(s_sharedLineBuf, batchIdC) != nullptr) {
                    match = true;
                } else if (hasCospar && strstr(s_sharedLineBuf, cosparC) != nullptr) {
                    match = true;
                }

                if (match) {
                    if (matchIndex >= skipCount) {
                        if (parser.parse(s_sharedLineBuf, record)) {
                            if (g_level3Objects.count < Level3ObjectList::MAX_ITEMS) {
                                auto& obj = g_level3Objects.items[g_level3Objects.count++];
                                obj.name = record.name;
                                obj.orbit = record; 
                                obj.calc.init(obj.orbit);
                                obj.lastGeoValid = false;
                                obj.isVisible = false;
                                obj.cache.lastCalcTime = 0;
                                obj.cache.past.clear();
                                obj.cache.future.clear();
                                loadCount++;
                            }
                        }
                    }
                    matchIndex++;
                }
                linePos = 0;

                if (lineCount % 50 == 0) {
                    esp_task_wdt_reset();
                    taskYIELD();
                }
            }
        } else {
            if (linePos < sizeof(s_sharedLineBuf) - 1) {
                s_sharedLineBuf[linePos++] = c;
            }
        }
    }
    
    f.close();
    return loadCount > 0;
}
