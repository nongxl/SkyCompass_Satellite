import sys

with open('src/main.cpp', 'r', encoding='utf-8') as f:
    content = f.read()

# 1. Add basePitch and baseRoll
old1 = "bool isManualLocationMode = false;\nbool isSatViewMode = false;"
new1 = "bool isManualLocationMode = false;\nbool isSatViewMode = false;\nfloat basePitch = 0.0f;\nfloat baseRoll = 0.0f;"
content = content.replace(old1, new1)

# 2. Fix handleContinuousKey
old2 = """            auto handleContinuousKey = [&](char key) {
                if (isSatViewMode || (!isManualLocationMode && !showRecommendations)) {
                    if (key == ',') current_unix -= 60;
                    else if (key == '/') current_unix += 60;
                } else if (isManualLocationMode) {
                    // Step size based on zoom level, finer control when zoomed in
                    float step = 1.0;
                    if (key == ';') { baseUserLat += step; if (baseUserLat > 90) baseUserLat = 90; }
                    else if (key == '.') { baseUserLat -= step; if (baseUserLat < -90) baseUserLat = -90; }
                    else if (key == ',') { baseUserLon -= step; if (baseUserLon < -180) baseUserLon += 360; }
                    else if (key == '/') { baseUserLon += step; if (baseUserLon > 180) baseUserLon -= 360; }
                    else if (key == '[') { baseUserAlt -= 10.0; if (baseUserAlt < -500) baseUserAlt = -500; }
                    else if (key == ']') { baseUserAlt += 10.0; if (baseUserAlt > 9000) baseUserAlt = 9000; }
                } else if (showRecommendations && selectedPassIndex == -1) {
                    if (key == ';') { if (passScrollIndex > 0) passScrollIndex--; }
                    else if (key == '.') { if (passScrollIndex < (int)displayTree.size() - 1) passScrollIndex++; }
                }
            };"""

new2 = """            auto handleContinuousKey = [&](char key) {
                if (showRecommendations) {
                    if (selectedPassIndex == -1) {
                        if (key == ';') { if (passScrollIndex > 0) passScrollIndex--; }
                        else if (key == '.') { if (passScrollIndex < (int)displayTree.size() - 1) passScrollIndex++; }
                    }
                } else if (isSatViewMode || !isManualLocationMode) {
                    if (key == ',') current_unix -= 60;
                    else if (key == '/') current_unix += 60;
                } else if (isManualLocationMode) {
                    // Step size based on zoom level, finer control when zoomed in
                    float step = 1.0;
                    if (key == ';') { baseUserLat += step; if (baseUserLat > 90) baseUserLat = 90; }
                    else if (key == '.') { baseUserLat -= step; if (baseUserLat < -90) baseUserLat = -90; }
                    else if (key == ',') { baseUserLon -= step; if (baseUserLon < -180) baseUserLon += 360; }
                    else if (key == '/') { baseUserLon += step; if (baseUserLon > 180) baseUserLon -= 360; }
                    else if (key == '[') { baseUserAlt -= 10.0; if (baseUserAlt < -500) baseUserAlt = -500; }
                    else if (key == ']') { baseUserAlt += 10.0; if (baseUserAlt > 9000) baseUserAlt = 9000; }
                }
            };"""
content = content.replace(old2, new2)

# 3. Update basePitch on SatView
old3 = """                        if (!found) isSatViewMode = false;
                    }
                } else if (M5Cardputer.Keyboard.isKeyPressed('m') || M5Cardputer.Keyboard.isKeyPressed('M')) {"""

new3 = """                        if (!found) isSatViewMode = false;
                        else {
                            if (attitude && imu) {
                                AttitudeData att = attitude->getAttitude();
                                basePitch = att.pitch;
                                baseRoll = att.roll;
                            }
                        }
                    }
                } else if (M5Cardputer.Keyboard.isKeyPressed('m') || M5Cardputer.Keyboard.isKeyPressed('M')) {"""
content = content.replace(old3, new3)

# 4. Use basePitch in SatView attitude
old4 = """            if (attitude && imu) {
                if (!isImuLocked) {
                    AttitudeData att = attitude->getAttitude();
                    lockedPitch = att.pitch;
                    lockedRoll = att.roll;
                }
                // Option A: Pass real pitch and roll to camera (inverted as requested by user)
                earth_renderer->setCameraAttitude(-lockedPitch, -lockedRoll, 0);
            } else {"""

new4 = """            if (attitude && imu) {
                if (!isImuLocked) {
                    AttitudeData att = attitude->getAttitude();
                    lockedPitch = att.pitch - basePitch;
                    lockedRoll = att.roll - baseRoll;
                }
                // Option A: Pass real pitch and roll to camera (inverted as requested by user)
                earth_renderer->setCameraAttitude(-lockedPitch, -lockedRoll, 0);
            } else {"""
content = content.replace(old4, new4)

with open('src/main.cpp', 'w', encoding='utf-8') as f:
    f.write(content)
