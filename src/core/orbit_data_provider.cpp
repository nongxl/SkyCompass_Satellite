#include "orbit_data_provider.h"
#include "json_parser.h"
#include "tle_parser.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <LittleFS.h>

// Helper to streamingly read a single JSON object from stream
static String readNextJsonObject(WiFiClient* stream) {
    String json = "";
    int braceCount = 0;
    bool inString = false;
    bool escaped = false;
    bool foundStart = false;
    
    uint32_t startMs = millis();
    while (millis() - startMs < 8000) { 
        if (!stream->available()) {
            delay(10);
            if (!stream->connected() && !stream->available()) {
                break;
            }
            continue;
        }
        char c = stream->read();
        if (c == -1) break;
        
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
bool OrbitDataProvider::loadByCatalogNumber(uint32_t catNum, OrbitRecord& record) {
    char path[32];
    sprintf(path, "/cat_%u.json", (unsigned int)catNum);
    
    if (LittleFS.exists(path)) {
        File f = LittleFS.open(path, "r");
        if (f) {
            String content = f.readString();
            f.close();
            JSONParser parser;
            if (parser.parse(content, record)) {
                return true;
            }
        }
    }
    
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(15000);
    char url[128];
    sprintf(url, "http://celestrak.org/NORAD/elements/gp.php?CATNR=%u&FORMAT=json", (unsigned int)catNum);
    
    http.begin(client, url);
    int httpCode = http.GET();
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
bool OrbitDataProvider::downloadRecentLaunches(std::vector<RecentLaunchItem>& tempLaunches) {
    WiFiClient client;
    client.setTimeout(30000);
    
    HTTPClient http;
    http.setTimeout(60000);
    http.setConnectTimeout(30000);
    
    String url = "http://celestrak.org/NORAD/elements/gp.php?GROUP=last-30-days&FORMAT=json";
    http.begin(client, url);
    int httpCode = http.GET();
    
    if (httpCode != HTTP_CODE_OK) {
        http.end();
        return false;
    }
    
    WiFiClient* stream = http.getStreamPtr();
    File f = LittleFS.open("/json_recent_raw.jsonl", "w");
    
    tempLaunches.clear();
    tempLaunches.reserve(30);
    
    JSONParser parser;
    int rawCount = 0;
    
    while (stream->connected() || stream->available()) {
        String singleJson = readNextJsonObject(stream);
        if (singleJson.length() == 0) {
            if (!stream->available()) break;
            continue;
        }
        
        OrbitRecord record;
        if (parser.parse(singleJson, record)) {
            if (f) {
                singleJson.replace("\r", "");
                singleJson.replace("\n", ""); 
                f.println(singleJson);
                rawCount++;
            }
            processRecentLaunchItem(tempLaunches, record);
        }
    }
    
    if (f) {
        f.close();
    }
    http.end();
    
    return rawCount > 0;
}

// Page load level 3 objects from jsonl file
extern std::vector<LazyObjectItem> g_level3Objects;
bool OrbitDataProvider::loadLevel3ObjectsPage(const RecentLaunchItem& item, int page) {
    g_level3Objects.clear();
    File f = LittleFS.open("/json_recent_raw.jsonl", "r");
    if (!f) return false;
    
    int skipCount = page * 5;
    int loadCount = 0;
    int matchIndex = 0;
    
    JSONParser parser;
    
    while (f.available() && loadCount < 5) {
        String singleLine = f.readStringUntil('\n');
        singleLine.trim();
        if (singleLine.length() == 0) continue;
        
        // Fast pre-filter using substring search to avoid expensive JSON deserialization
        bool match = false;
        if (singleLine.indexOf(item.batchId) != -1) {
            match = true;
        } else if (item.batchId.length() == 5 && isdigit(item.batchId[0]) && isdigit(item.batchId[1])) {
            String cosparForm = "20" + item.batchId.substring(0, 2) + "-" + item.batchId.substring(2);
            if (singleLine.indexOf(cosparForm) != -1) {
                match = true;
            }
        }
        if (!match) continue;
        
        OrbitRecord record;
        if (parser.parse(singleLine, record)) {
            if (record.getBatchId() == item.batchId) {
                if (matchIndex >= skipCount) {
                    LazyObjectItem obj;
                    obj.name = record.name;
                    obj.orbit = record; 
                    obj.calc.init(obj.orbit);
                    obj.lastGeoValid = false;
                    obj.isVisible = false;
                    g_level3Objects.push_back(obj);
                    loadCount++;
                }
                matchIndex++;
            }
        }
    }
    f.close();
    return loadCount > 0;
}
