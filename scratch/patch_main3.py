import re

with open('src/main.cpp', 'r', encoding='utf-8') as f:
    lines = f.read().splitlines()

# Find networkTask
networkTaskIdx = -1
for i, line in enumerate(lines):
    if "void networkTask(void* parameter)" in line:
        networkTaskIdx = i
        break

if networkTaskIdx != -1:
    fetch_code = """
void fetchFrequencies() {
    WiFiClientSecure *client = new WiFiClientSecure;
    if (!client) return;
    client->setInsecure();
    
    HTTPClient http;
    http.begin(*client, "https://raw.githubusercontent.com/nongxl/SkyCompass_Satellite/main/data/frequencies.json");
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
        if (httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
            String newUrl = http.getLocation();
            http.end();
            http.begin(*client, newUrl);
            httpCode = http.GET();
        }
    }
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (!error) {
            for (int i = 0; i < NUM_SATELLITES; i++) {
                String idStr = String(g_satellites[i].noradId);
                if (doc.containsKey(idStr)) {
                    g_satellites[i].downlinkFreq = doc[idStr]["freq"].as<String>();
                    g_satellites[i].radioMode = doc[idStr]["mode"].as<String>();
                }
            }
        }
    }
    http.end();
    delete client;
}
"""
    lines.insert(networkTaskIdx, fetch_code)

# Now find where to call fetchFrequencies() inside networkTask
fetchCallIdx = -1
for i, line in enumerate(lines):
    if "bool updated = false;" in line and "4. Fetch TLEs" in lines[i-1]:
        fetchCallIdx = i - 1
        break

if fetchCallIdx != -1:
    lines.insert(fetchCallIdx, "        // 3.5 Fetch Frequencies\n        fetchFrequencies();\n")

with open('src/main.cpp', 'w', encoding='utf-8') as f:
    f.write('\n'.join(lines) + '\n')
