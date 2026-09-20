#include "orbit_data_provider.h"
#include "json_parser.h"
#include "log_manager.h"
#include "tle_parser.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>  // Plain HTTP — no TLS heap cost
#include <LittleFS.h>
#include <esp_task_wdt.h>
#include <memory>



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
    std::unique_ptr<HTTPClient> http(new HTTPClient());
    if (!http) return false;
    http->setTimeout(10000);
    http->setConnectTimeout(5000);
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

// Download Recent Launches and save to JSONL via high-speed chunk-buffered stream
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

    static const char* CELESTRAK_URL = "http://celestrak.org/NORAD/elements/gp.php?GROUP=last-30-days&FORMAT=json";
    
    // 静态大缓冲区，彻底消除任务栈负担（0 字节栈开销）
    static const size_t IN_BUF_SIZE = 2048;
    static const size_t OUT_BUF_SIZE = 2048;
    static uint8_t inBuf[IN_BUF_SIZE];
    static char outBuf[OUT_BUF_SIZE];

    extern String recentLaunchErrorMsg;
    esp_task_wdt_reset();

    // 诊断 DNS 解析
    IPAddress hostIp;
    if (WiFi.hostByName("celestrak.org", hostIp)) {
        LOG_I("RECENT_LAUNCH", "DNS resolved celestrak.org -> %s", hostIp.toString().c_str());
    } else {
        LOG_W("RECENT_LAUNCH", "DNS resolution failed for celestrak.org");
    }

    std::unique_ptr<WiFiClient> client(new WiFiClient());
    if (!client) {
        if (outHttpCode) *outHttpCode = -2;
        return false;
    }
    
    std::unique_ptr<HTTPClient> http(new HTTPClient());
    if (!http) {
        if (outHttpCode) *outHttpCode = -2;
        return false;
    }
    
    // 超时时间严格控制在 12 秒内，确保绝对不触发系统任务看门狗(TWDT)复位
    client->setTimeout(12);
    http->setTimeout(12000);
    http->setConnectTimeout(6000);
    http->setUserAgent("Mozilla/5.0 (ESP32-Cardputer; SkyCompass Satellite Tracker)");
    http->setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    
    http->begin(*client, CELESTRAK_URL);
    esp_task_wdt_reset();
    
    int httpCode = http->GET();
    esp_task_wdt_reset();
    if (outHttpCode) *outHttpCode = httpCode;
    
    if (httpCode != HTTP_CODE_OK) {
        LOG_W("RECENT_LAUNCH", "Celestrak HTTP GET returned error code %d", httpCode);
        http->end();
        return false;
    }
    
    int expectedSize = http->getSize();
    WiFiClient* stream = http->getStreamPtr();
        
    static const char* RAW_DOWNLOAD_PATH = "/recent_raw.tmp";
    static const char* RAW_JSONL_TMP = "/json_recent_raw.tmp";
    static const char* RAW_JSONL_PATH = "/json_recent_raw.jsonl";
    
    // 阶段 1: 高速网络流直写 Flash，杜绝逐字符计算阻塞 TCP ACK 窗口
    if (LittleFS.exists(RAW_DOWNLOAD_PATH)) {
        LittleFS.remove(RAW_DOWNLOAD_PATH);
    }
    File fDown = LittleFS.open(RAW_DOWNLOAD_PATH, "w", true);
    if (!fDown) {
        LOG_E("RECENT_LAUNCH", "Failed to open %s for writing!", RAW_DOWNLOAD_PATH);
        if (outHttpCode) *outHttpCode = -100;
        http->end();
        return false;
    }
    
    int totalReadBytes = 0;
    uint32_t lastReadMs = millis();
    const uint32_t TIMEOUT_MS = 25000;
    
    while (stream->connected() || stream->available()) {
        int avail = stream->available();
        if (avail > 0) {
            int toRead = avail > (int)sizeof(inBuf) ? (int)sizeof(inBuf) : avail;
            int r = stream->read(inBuf, toRead);
            if (r > 0) {
                fDown.write(inBuf, r);
                totalReadBytes += r;
                lastReadMs = millis();
                
                if (expectedSize > 0) {
                    int pct = (int)((int64_t)totalReadBytes * 100 / expectedSize);
                    recentLaunchErrorMsg = String(totalReadBytes / 1024) + "KB (" + String(pct) + "%)";
                } else {
                    recentLaunchErrorMsg = String(totalReadBytes / 1024) + "KB";
                }
            }
        } else {
            if (!stream->connected()) break;
            if (millis() - lastReadMs > TIMEOUT_MS) {
                LOG_W("RECENT_LAUNCH", "Stream chunk read timed out (no data for %u ms, total read %d bytes)", 
                      (unsigned int)TIMEOUT_MS, totalReadBytes);
                if (outHttpCode) *outHttpCode = -11;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        esp_task_wdt_reset();
        if (expectedSize > 0 && totalReadBytes >= expectedSize) {
            LOG_I("RECENT_LAUNCH", "Stream completed successfully (%d/%d bytes)", totalReadBytes, expectedSize);
            break;
        }
    }
    
    fDown.close();
    http->end();
    
    if (expectedSize > 0 && totalReadBytes < expectedSize) {
        LOG_W("RECENT_LAUNCH", "Incomplete download (%d/%d bytes). Discarded raw file.", totalReadBytes, expectedSize);
        LittleFS.remove(RAW_DOWNLOAD_PATH);
        if (outHttpCode && *outHttpCode == 0) *outHttpCode = -5;
        return false;
    }
    
    // 阶段 2: 本地离线流式转换，将 JSON 数组拆分为每行一条的 JSONL 格式
    File fRaw = LittleFS.open(RAW_DOWNLOAD_PATH, "r");
    if (!fRaw) {
        LittleFS.remove(RAW_DOWNLOAD_PATH);
        return false;
    }
    
    if (LittleFS.exists(RAW_JSONL_TMP)) {
        LittleFS.remove(RAW_JSONL_TMP);
    }
    File fJsonl = LittleFS.open(RAW_JSONL_TMP, "w", true);
    if (!fJsonl) {
        fRaw.close();
        LittleFS.remove(RAW_DOWNLOAD_PATH);
        return false;
    }
    
    int rawCount = 0;
    size_t outLen = 0;
    auto flushOut = [&]() {
        if (outLen > 0) {
            fJsonl.write((const uint8_t*)outBuf, outLen);
            outLen = 0;
        }
    };
    auto writeChar = [&](char ch) {
        outBuf[outLen++] = ch;
        if (outLen >= OUT_BUF_SIZE) {
            flushOut();
        }
    };
    
    bool inString = false;
    bool escaped = false;
    int braceDepth = 0;
    
    while (fRaw.available()) {
        int r = fRaw.read(inBuf, sizeof(inBuf));
        if (r <= 0) break;
        
        for (int i = 0; i < r; ++i) {
            char c = (char)inBuf[i];
            if (c == '\r' || c == '\n') continue;
            
            if (escaped) {
                escaped = false;
                if (braceDepth > 0) writeChar(c);
                continue;
            }
            if (c == '\\') {
                if (inString) escaped = true;
                if (braceDepth > 0) writeChar(c);
                continue;
            }
            if (c == '"') {
                inString = !inString;
                if (braceDepth > 0) writeChar(c);
                continue;
            }
            if (inString) {
                if (braceDepth > 0) writeChar(c);
                continue;
            }
            
            if (c == '{') {
                braceDepth++;
                writeChar(c);
            } else if (c == '}') {
                braceDepth--;
                writeChar(c);
                if (braceDepth == 0) {
                    writeChar('\n');
                    rawCount++;
                }
            } else {
                if (braceDepth > 0) {
                    writeChar(c);
                }
            }
        }
        esp_task_wdt_reset();
    }
    
    flushOut();
    fJsonl.close();
    fRaw.close();
    LittleFS.remove(RAW_DOWNLOAD_PATH); // 清理原始中间文件
    
    LOG_I("RECENT_LAUNCH", "Offline parsing complete. Raw json lines: %d. Total bytes: %d", rawCount, totalReadBytes);
    
    if (rawCount > 0) {
        if (LittleFS.exists(RAW_JSONL_PATH)) {
            LittleFS.remove(RAW_JSONL_PATH);
        }
        LittleFS.rename(RAW_JSONL_TMP, RAW_JSONL_PATH);
        LOG_I("RECENT_LAUNCH", "Atomically updated %s with %d objects!", RAW_JSONL_PATH, rawCount);
        return true;
    } else {
        LittleFS.remove(RAW_JSONL_TMP);
        LOG_W("RECENT_LAUNCH", "No valid JSON objects parsed from downloaded raw data.");
        return false;
    }
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
    
    while (f.available()) {
        lineCount++;
        String singleJson = f.readStringUntil('\n');
        singleJson.trim();
        if (singleJson.length() == 0) continue;
        if (singleJson.length() > 2048) {
            // 异常超长损坏行，直接跳过，防止堆内存耗尽
            continue;
        }
        
        // 内存熔断保护：若剩余堆内存极低，提前终止加载并保留已有对象，防止系统 panic
        if (ESP.getFreeHeap() < 24000) {
            LOG_W("RECENT_LAUNCH", "Low heap memory during cache load (%u bytes), terminating parse early.", (unsigned int)ESP.getFreeHeap());
            break;
        }
        
        OrbitRecord record;
        if (parser.parse(singleJson, record)) {
            parseSuccessCount++;
            rawCount++;
            processRecentLaunchItem(tempLaunches, record, outPhases);
        }
        
        if (lineCount % 20 == 0) {
            esp_task_wdt_reset();
            taskYIELD();
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
    
    // 将缓冲区与解析器声明为静态存储，彻底避免在 loopTask 栈上占用空间（0 字节栈开销）
    static uint8_t buffer[1024];
    static char lineBuf[512];
    static JSONParser parser;
    static OrbitRecord record;
    
    size_t bufLen = 0;
    size_t bufPos = 0;
    size_t linePos = 0;
    
    while ((f.available() || bufPos < bufLen) && loadCount < 5) {
        if (bufPos >= bufLen) {
            bufLen = f.read(buffer, sizeof(buffer));
            bufPos = 0;
            if (bufLen == 0) break;
        }
        
        char c = (char)buffer[bufPos++];
        if (c == '\n' || c == '\r') {
            if (linePos > 0) {
                lineBuf[linePos] = '\0';
                lineCount++;
                
                // 零堆内存开销极速子串匹配
                bool match = false;
                if (strstr(lineBuf, batchIdC) != nullptr) {
                    match = true;
                } else if (hasCospar && strstr(lineBuf, cosparC) != nullptr) {
                    match = true;
                }
                
                if (match) {
                    if (matchIndex >= skipCount) {
                        if (parser.parse(lineBuf, record)) {
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
            if (linePos < sizeof(lineBuf) - 1) {
                lineBuf[linePos++] = c;
            }
        }
    }
    
    f.close();
    return loadCount > 0;
}
