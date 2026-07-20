#include "orbit_data_provider.h"
#include "json_parser.h"
#include "log_manager.h"
#include "tle_parser.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>  // Plain HTTP — no TLS heap cost
#include <LittleFS.h>
#include <esp_task_wdt.h>

// Helper to streamingly read a single JSON object from stream
static String readNextJsonObject(WiFiClient* stream, int& totalReadBytes) {
    String json = "";
    int braceCount = 0;
    bool inString = false;
    bool escaped = false;
    bool foundStart = false;
    
    uint32_t waitMs = 0;
    while (waitMs < 5000) { 
        if (!stream->available()) {
            delay(10);
            waitMs += 10;
            if (!stream->connected() && !stream->available()) {
                break;
            }
            if (totalReadBytes > 0 && waitMs >= 5000) {
                break; // Stream idle timeout (likely completed but Keep-Alive is active)
            }
            continue;
        }
        char c = stream->read();
        if (c == -1) break;
        totalReadBytes++;
        waitMs = 0; // Reset wait timer on successfully reading a byte
        
        if (c == '\r' || c == '\n') continue;
        
        if (!foundStart) {
            if (c == '{') {
                foundStart = true;
                braceCount = 1;
                json += c;
                inString = false;
                escaped = false;
            }
            continue;
        }
        
        json += c;
        
        if (escaped) {
            escaped = false;
            continue;
        }
        
        if (c == '\\') {
            escaped = true;
            continue;
        }
        
        if (c == '"') {
            inString = !inString;
            continue;
        }
        
        if (!inString) {
            if (c == '{') {
                braceCount++;
            } else if (c == '}') {
                braceCount--;
                if (braceCount == 0) {
                    return json;
                }
            }
        }
    }
    return json;
}

// Load single satellite from cache or network
bool OrbitDataProvider::loadByCatalogNumber(uint32_t catNum, OrbitRecord& record, bool forceRefresh, int* outHttpCode) {
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
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(15000);
    char url[128];
    sprintf(url, "http://celestrak.org/NORAD/elements/gp.php?CATNR=%u&FORMAT=json", (unsigned int)catNum);
    
    http.begin(client, url);
    int httpCode = http.GET();
    if (outHttpCode) *outHttpCode = httpCode;
    bool success = false;
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        payload.trim();
        if (payload.startsWith("[") && payload.endsWith("]")) {
            payload = payload.substring(1, payload.length() - 1);
            payload.trim();
        }
        
        JSONParser parser;
        if (parser.parse(payload, record)) {
            File f = LittleFS.open(path, "w");
            if (f) {
                f.print(payload);
                f.close();
            }
            success = true;
        }
    }
    http.end();
    return success;
}

// Batch-fetch GP data for multiple satellites in a single HTTP request.
// Results are parsed individually and cached per-satellite.
// Returns number of satellites successfully fetched (0 = total failure).
int OrbitDataProvider::loadByCatalogNumbers(const std::vector<uint32_t>& catNums, std::vector<OrbitRecord>& records, int* outHttpCode) {
    if (catNums.empty()) return 0;
    if (outHttpCode) *outHttpCode = 0;
    
    // Build comma-separated CATNR list
    String catnrList = "";
    for (size_t i = 0; i < catNums.size(); i++) {
        if (i > 0) catnrList += ",";
        catnrList += String(catNums[i]);
    }
    
    String url = "http://celestrak.org/NORAD/elements/gp.php?CATNR=" + catnrList + "&FORMAT=json";
    LOG_I("APP", "[Batch] Loading %d satellites", (int)catNums.size());
    
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(30000);
    http.setConnectTimeout(15000);
    http.begin(client, url);
    int httpCode = http.GET();
    if (outHttpCode) *outHttpCode = httpCode;
    
    if (httpCode != HTTP_CODE_OK) {
        http.end();
        LOG_I("APP", "[Batch] HTTP Error: %d", httpCode);
        return 0;
    }
    
    // Stream parse the JSON array of GP objects
    WiFiClient* stream = http.getStreamPtr();
    int totalRead = 0;
    int fetched = 0;
    
    while (stream->connected() || stream->available()) {
        String obj = readNextJsonObject(stream, totalRead);
        if (obj.length() == 0) {
            if (!stream->connected() && !stream->available()) break;
            continue;
        }
        JSONParser parser;
        OrbitRecord rec;
        if (parser.parse(obj, rec)) {
            // Cache individual result
            char path[32];
            sprintf(path, "/cat_%u.json", (unsigned int)rec.catalogNumber);
            File f = LittleFS.open(path, "w");
            if (f) { f.print(obj); f.close(); }
            records.push_back(rec);
            fetched++;
        }
    }
    
    http.end();
    LOG_I("APP", "[Batch] Fetched %d/%d satellites", fetched, (int)catNums.size());
    return fetched;
}

static void processRecentLaunchItem(std::vector<RecentLaunchItem>& tempLaunches, const OrbitRecord& record) {
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
        tempLaunches.push_back(item);
    }
}

// Download Recent Launches and save to JSONL
bool OrbitDataProvider::downloadRecentLaunches(std::vector<RecentLaunchItem>& tempLaunches, int* outHttpCode) {
    if (outHttpCode) *outHttpCode = 0;
    // Use plain HTTP to save TLS heap. CelesTrak supports HTTP.
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(60000);
    http.setConnectTimeout(30000);
    
    String url = "http://celestrak.org/NORAD/elements/gp.php?GROUP=last-30-days&FORMAT=json";
    http.begin(client, url);
    int httpCode = http.GET();
    if (outHttpCode) *outHttpCode = httpCode;
    
    if (httpCode != HTTP_CODE_OK) {
        http.end();
        return false;
    }
    
    int expectedSize = http.getSize();
    WiFiClient* stream = http.getStreamPtr();
    File f = LittleFS.open("/json_recent_raw.jsonl", "w");
    
    int rawCount = 0;
    int totalReadBytes = 0;
    
    while (stream->connected() || stream->available()) {
        int prevReadBytes = totalReadBytes;
        String singleJson = readNextJsonObject(stream, totalReadBytes);
        if (singleJson.length() == 0) {
            if (!stream->connected() && !stream->available()) break;
            if (expectedSize > 0 && totalReadBytes >= expectedSize) {
                break;
            }
            if (totalReadBytes == prevReadBytes) {
                LOG_I("RECENT_LAUNCH", "Stream read timed out (%d bytes read)", totalReadBytes);
                if (outHttpCode) *outHttpCode = -11; // HTTPC_ERROR_READ_TIMEOUT
                break;
            }
            continue;
        }
        
        if (f) {
            f.println(singleJson);
            rawCount++;
        }
        
        if (expectedSize > 0 && totalReadBytes >= expectedSize) {
            LOG_I("RECENT_LAUNCH", "Stream completed successfully via size checking (%d/%d bytes)", totalReadBytes, expectedSize);
            break;
        }
    }
    
    if (f) {
        f.close();
    }
    http.end();
    
    bool completed = true;
    if (expectedSize > 0 && totalReadBytes < expectedSize) {
        completed = false;
        if (outHttpCode && *outHttpCode == 0) {
            *outHttpCode = -5; // HTTPC_ERROR_CONNECTION_LOST
        }
    }
    
    LOG_I("RECENT_LAUNCH", "Download finished. Raw json lines saved: %d. Expected size: %d, actual read size: %d", rawCount, expectedSize, totalReadBytes);
    if (completed && rawCount > 0) {
        return true;
    }
    return false;
}

bool OrbitDataProvider::loadRecentLaunchesFromCache(std::vector<RecentLaunchItem>& tempLaunches) {
    File f = LittleFS.open("/json_recent_raw.jsonl", "r");
    if (!f) {
        LOG_I("DEBUG", "loadRecentLaunchesFromCache: Failed to open /json_recent_raw.jsonl");
        return false;
    }
    
    tempLaunches.clear();
    tempLaunches.reserve(30);
    
    JSONParser parser;
    int rawCount = 0;
    int parseSuccessCount = 0;
    int lineCount = 0;
    
    while (f.available()) {
        lineCount++;
        String singleJson = f.readStringUntil('\n');
        singleJson.trim();
        if (singleJson.length() == 0) continue;
        
        if (lineCount <= 5) {
            LOG_I("DEBUG", "JSON Line %d (len %d): %s", lineCount, (int)singleJson.length(), singleJson.c_str());
        }
        
        OrbitRecord record;
        if (parser.parse(singleJson, record)) {
            parseSuccessCount++;
            rawCount++;
            processRecentLaunchItem(tempLaunches, record);
        } else {
            if (lineCount <= 5) {
                LOG_I("DEBUG", "JSON Parse Failed for Line %d", lineCount);
            }
        }
        
        // Feed watchdog every 5 lines to prevent WDT timeout during heavy parsing
        if (lineCount % 5 == 0) {
            esp_task_wdt_reset();
            vTaskDelay(2);
        }
    }
    
    f.close();
    LOG_I("DEBUG", "loadRecentLaunchesFromCache finished: Total Lines: %d, Parse Success: %d, Launches Created: %d", lineCount, parseSuccessCount, (int)tempLaunches.size());
    return rawCount > 0;
}

// Page load level 3 objects from jsonl file
// Page load level 3 objects from jsonl file
extern std::vector<LazyObjectItem> g_level3Objects;
extern volatile bool recentLaunchDownloading;
bool OrbitDataProvider::loadLevel3ObjectsPage(const RecentLaunchItem& item, int page) {
    if (recentLaunchDownloading) return false;
    g_level3Objects.clear();
    File f = LittleFS.open("/json_recent_raw.jsonl", "r");
    if (!f) return false;
    
    int skipCount = page * 5;
    int loadCount = 0;
    int matchIndex = 0;
    
    JSONParser parser;
    
    String cosparForm = "";
    if (item.batchId.length() == 5 && isdigit(item.batchId[0]) && isdigit(item.batchId[1])) {
        cosparForm = "20" + item.batchId.substring(0, 2) + "-" + item.batchId.substring(2);
    }
    
    while (f.available() && loadCount < 5) {
        String singleLine = f.readStringUntil('\n');
        singleLine.trim();
        if (singleLine.length() == 0) continue;
        
        // Cheap text pre-filtering using index search
        bool match = false;
        if (singleLine.indexOf(item.batchId) != -1) {
            match = true;
        } else if (cosparForm.length() > 0 && singleLine.indexOf(cosparForm) != -1) {
            match = true;
        }
        
        if (match) {
            if (matchIndex >= skipCount) {
                OrbitRecord record;
                if (parser.parse(singleLine, record)) {
                    LazyObjectItem obj;
                    obj.name = record.name;
                    obj.orbit = record; 
                    obj.calc.init(obj.orbit); // Heavy calculations done only for visible 5 items
                    obj.lastGeoValid = false;
                    obj.isVisible = false;
                    g_level3Objects.push_back(obj);
                    loadCount++;
                }
            }
            matchIndex++;
        }
    }
    f.close();
    return loadCount > 0;
}
