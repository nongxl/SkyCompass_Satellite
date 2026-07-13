#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>
#include "orbit_record.h"
#include "json_parser.h"
#include "recent_launch_item.h"

enum class OrbitState {
    STABLE = 0,
    SLOW_DECAY = 1,
    DECAYING = 2,
    RAPID_DECAY = 3,
    REENTRY_SOON = 4,
    REENTERED = 5
};

struct DecayInfo {
    bool isDecaying = false;
    float altitudeDropPerDay = 0.0f;
    float meanMotionChange = 0.0f;
    float bstarScore = 0.0f;
    float decayScore = 0.0f;
    OrbitState level = OrbitState::STABLE;
    uint32_t lastEpoch = 0;
    float lastAlt = 0.0f;
};

struct DisplayStatus {
    String title;
    String subtitle;
    uint16_t color;
};

class DisplayStatusFormatter {
private:
    static TrainState getTrainState(float occupancy) {
        if (occupancy < 10.0f) return TrainState::VERY_TIGHT;
        if (occupancy < 30.0f) return TrainState::TIGHT;
        if (occupancy < 90.0f) return TrainState::EXPANDING;
        return TrainState::OPERATIONAL;
    }
public:
    static DisplayStatus format(OrbitState state, OrbitControlCapability cap, const String& name, bool isGroup = false, float occupancy = 360.0f) {
        DisplayStatus ds;
        ds.color = TFT_GREEN;
        
        String uName = name;
        uName.toUpperCase();
        
        bool isRB = (uName.indexOf("R/B") != -1 || uName.indexOf("ROCKET") != -1);
        bool isDebris = (uName.indexOf("DEB") != -1 || uName.indexOf("DEBRIS") != -1 || uName.indexOf("COLLISION") != -1 || uName.indexOf("FRAGMENT") != -1 || uName.indexOf("WESTFORD NEEDLES") != -1);
        bool isStation = (uName.indexOf("ISS") != -1 || uName.indexOf("CSS") != -1 || uName.indexOf("TIANGONG") != -1);
        
        // 1. Constellation Group & Active Control check
        if (isGroup && cap == OrbitControlCapability::ACTIVE_CONTROL && state != OrbitState::RAPID_DECAY && state != OrbitState::REENTRY_SOON) {
            TrainState ts = getTrainState(occupancy);
            switch (ts) {
                case TrainState::VERY_TIGHT:
                case TrainState::TIGHT:
                    ds.title = "Tight Train";
                    ds.subtitle = "The satellites are clustered closely in a spectacular train.";
                    ds.color = TFT_GREEN;
                    break;
                case TrainState::EXPANDING:
                    ds.title = "Train Expanding";
                    ds.subtitle = "The satellites are gradually spreading into their operational positions.";
                    ds.color = TFT_GREEN;
                    break;
                case TrainState::OPERATIONAL:
                default:
                    ds.title = "Operational";
                    ds.subtitle = "The satellites have spread out and are operating normally.";
                    ds.color = TFT_GREEN;
                    break;
            }
            return ds;
        }
        
        // 2. Normal active payload / Space station
        if (cap == OrbitControlCapability::ACTIVE_CONTROL) {
            switch (state) {
                case OrbitState::STABLE:
                    ds.title = "Operational";
                    ds.subtitle = "This satellite is operating in a stable orbit.";
                    ds.color = TFT_GREEN;
                    break;
                case OrbitState::SLOW_DECAY:
                case OrbitState::DECAYING:
                    ds.title = "Operational";
                    if (isStation) {
                        ds.subtitle = "Its orbit is routinely maintained by periodic reboost maneuvers.";
                    } else {
                        ds.subtitle = "This satellite is operating in a stable orbit.";
                    }
                    ds.color = TFT_GREEN;
                    break;
                case OrbitState::RAPID_DECAY:
                    ds.title = "Decaying Orbit";
                    ds.subtitle = "This satellite is experiencing significant orbit decay.";
                    ds.color = TFT_RED;
                    break;
                case OrbitState::REENTRY_SOON:
                    ds.title = "Low Orbit";
                    ds.subtitle = "This object is in a very low orbit and may reenter in the future.";
                    ds.color = TFT_MAGENTA;
                    break;
                case OrbitState::REENTERED:
                    ds.title = "Reentered";
                    ds.subtitle = "This spacecraft has entered the atmosphere and burned up.";
                    ds.color = TFT_DARKGREY;
                    break;
                default:
                    ds.title = "Operational";
                    ds.subtitle = "This satellite is operating in a stable orbit.";
                    ds.color = TFT_GREEN;
                    break;
            }
        } else { // PASSIVE
            if (isRB) {
                switch (state) {
                    case OrbitState::STABLE:
                        ds.title = "Rocket Body";
                        ds.subtitle = "This rocket body remains in a stable passive orbit.";
                        ds.color = TFT_GREEN;
                        break;
                    case OrbitState::SLOW_DECAY:
                        ds.title = "Rocket Body";
                        ds.subtitle = "This rocket body is gradually losing altitude due to atmospheric drag.";
                        ds.color = TFT_YELLOW;
                        break;
                    case OrbitState::DECAYING:
                        ds.title = "Rocket Body";
                        ds.subtitle = "This rocket body is losing altitude due to atmospheric drag.";
                        ds.color = TFT_ORANGE;
                        break;
                    case OrbitState::RAPID_DECAY:
                        ds.title = "Decaying Orbit";
                        ds.subtitle = "The orbit is shrinking more rapidly than before.";
                        ds.color = TFT_RED;
                        break;
                    case OrbitState::REENTRY_SOON:
                        ds.title = "Low Orbit";
                        ds.subtitle = "This object is in a very low orbit and may reenter in the future.";
                        ds.color = TFT_MAGENTA;
                        break;
                    case OrbitState::REENTERED:
                        ds.title = "Reentered";
                        ds.subtitle = "This object has reentered the atmosphere and burned up.";
                        ds.color = TFT_DARKGREY;
                        break;
                    default:
                        ds.title = "Rocket Body";
                        ds.subtitle = "This rocket body remains in a stable passive orbit.";
                        ds.color = TFT_GREEN;
                        break;
                }
            } else { // Debris / Other passive
                switch (state) {
                    case OrbitState::STABLE:
                        ds.title = "Space Debris";
                        ds.subtitle = "This object is no longer operational and remains in orbit.";
                        ds.color = TFT_GREEN;
                        break;
                    case OrbitState::SLOW_DECAY:
                    case OrbitState::DECAYING:
                        ds.title = "Space Debris";
                        ds.subtitle = "Its orbit is gradually decaying due to atmospheric drag.";
                        ds.color = TFT_YELLOW;
                        break;
                    case OrbitState::RAPID_DECAY:
                        ds.title = "Decaying Orbit";
                        ds.subtitle = "The orbit is shrinking more rapidly than before.";
                        ds.color = TFT_RED;
                        break;
                    case OrbitState::REENTRY_SOON:
                        ds.title = "Low Orbit";
                        ds.subtitle = "This object is in a very low orbit and may reenter in the future.";
                        ds.color = TFT_MAGENTA;
                        break;
                    case OrbitState::REENTERED:
                        ds.title = "Reentered";
                        ds.subtitle = "This object has reentered the atmosphere and burned up.";
                        ds.color = TFT_DARKGREY;
                        break;
                    default:
                        ds.title = "Space Debris";
                        ds.subtitle = "This object is no longer operational and remains in orbit.";
                        ds.color = TFT_GREEN;
                        break;
                }
            }
        }
        return ds;
    }
};

class DecayAnalyzer {
public:
    static OrbitControlCapability autoAssignControlCapability(const String& name) {
        String n = name;
        n.toUpperCase();
        if (n.indexOf("R/B") != -1 || n.indexOf("ROCKET") != -1 ||
            n.indexOf("DEB") != -1 || n.indexOf("DEBRIS") != -1 ||
            n.indexOf("COLLISION") != -1 || n.indexOf("FRAGMENT") != -1 ||
            n.indexOf("WESTFORD NEEDLES") != -1) {
            return OrbitControlCapability::PASSIVE;
        }
        return OrbitControlCapability::ACTIVE_CONTROL; 
    }

    // Core analysis between prev and curr record
    static DecayInfo analyze(const OrbitRecord& prev, const OrbitRecord& curr) {
        DecayInfo info;
        info.lastEpoch = curr.epochUnix;
        
        // 1. Calculate Altitudes
        // a = 42241.0979 * n^(-2/3)
        double a1 = 42241.0979 * pow(prev.meanMotion, -2.0 / 3.0);
        double a2 = 42241.0979 * pow(curr.meanMotion, -2.0 / 3.0);
        double h1 = a1 - 6378.137;
        double h2 = a2 - 6378.137;
        info.lastAlt = h2;
        
        // 2. Time Difference in days
        double dt = (double)(curr.epochUnix - prev.epochUnix) / 86400.0;
        if (dt <= 0.001) {
            // Epoch not updated or same, try to load cached info to avoid resetting rate
            if (loadDecayInfo(curr.catalogNumber, info)) {
                return info;
            }
            // Fallback heuristics based on current record
            info.altitudeDropPerDay = 0.0f;
            info.meanMotionChange = 0.0f;
        } else {
            info.altitudeDropPerDay = (h1 - h2) / dt;
            info.meanMotionChange = (curr.meanMotion - prev.meanMotion) / dt;
        }
        
        info.bstarScore = curr.bstar;
        
        // 3. Compute Decay Score (0..100)
        double score = 0.0;
        
        // Altitude contribution
        if (h2 > 500.0) {
            score = 0.0;
        } else if (h2 > 400.0) {
            score = (500.0 - h2) * 0.2; // 0..20
        } else if (h2 > 300.0) {
            score = 20.0 + (400.0 - h2) * 0.3; // 20..50
        } else if (h2 > 200.0) {
            score = 50.0 + (300.0 - h2) * 0.3; // 50..80
        } else {
            score = 80.0 + (200.0 - h2) * 0.2; // 80..100
        }
        
        // BSTAR contribution
        if (curr.bstar > 0.0) {
            score += curr.bstar * 4000.0; // e.g. bstar of 0.002 adds 8 points
        }
        
        // Altitude drop rate contribution
        if (info.altitudeDropPerDay > 0.0f) {
            score += info.altitudeDropPerDay * 6.0f; // e.g. 2 km/day adds 12 points
        }
        
        if (score < 0.0) score = 0.0;
        if (score > 100.0) score = 100.0;
        info.decayScore = score;
        
        // 4. Map to OrbitState
        if (h2 < 185.0 || (h2 < 200.0 && info.altitudeDropPerDay > 3.0f)) {
            info.level = OrbitState::REENTRY_SOON;
        } else if (score >= 70.0 || h2 < 250.0 || info.altitudeDropPerDay > 1.5f) {
            info.level = OrbitState::RAPID_DECAY;
        } else if (score >= 40.0 || info.altitudeDropPerDay > 0.4f) {
            info.level = OrbitState::DECAYING;
        } else if (score >= 12.0 || info.altitudeDropPerDay > 0.03f) {
            info.level = OrbitState::SLOW_DECAY;
        } else {
            info.level = OrbitState::STABLE;
        }
        
        info.isDecaying = (info.level != OrbitState::STABLE);
        return info;
    }
    
    // Load cached DecayInfo from file
    static bool loadDecayInfo(uint32_t catNum, DecayInfo& info) {
        char path[32];
        sprintf(path, "/dec_%u.txt", (unsigned int)catNum);
        if (!LittleFS.exists(path)) return false;
        
        File f = LittleFS.open(path, "r");
        if (!f) return false;
        
        info.isDecaying = f.readStringUntil('\n').toInt() != 0;
        info.altitudeDropPerDay = f.readStringUntil('\n').toFloat();
        info.meanMotionChange = f.readStringUntil('\n').toFloat();
        info.bstarScore = f.readStringUntil('\n').toFloat();
        info.decayScore = f.readStringUntil('\n').toFloat();
        info.level = (OrbitState)f.readStringUntil('\n').toInt();
        info.lastEpoch = f.readStringUntil('\n').toInt();
        info.lastAlt = f.readStringUntil('\n').toFloat();
        
        f.close();
        return true;
    }
    
    // Save DecayInfo to file
    static bool saveDecayInfo(uint32_t catNum, const DecayInfo& info) {
        char path[32];
        sprintf(path, "/dec_%u.txt", (unsigned int)catNum);
        File f = LittleFS.open(path, "w");
        if (!f) return false;
        
        f.println(info.isDecaying ? 1 : 0);
        f.println(info.altitudeDropPerDay, 4);
        f.println(info.meanMotionChange, 6);
        f.println(info.bstarScore, 6);
        f.println(info.decayScore, 2);
        f.println((int)info.level);
        f.println(info.lastEpoch);
        f.println(info.lastAlt, 2);
        
        f.close();
        return true;
    }
    
    // Format status strings
    static const char* getLevelStr(OrbitState level) {
        switch (level) {
            case OrbitState::STABLE:       return "Stable";
            case OrbitState::SLOW_DECAY:   return "Slow Decay";
            case OrbitState::DECAYING:     return "Decaying";
            case OrbitState::RAPID_DECAY:  return "Rapid Decay";
            case OrbitState::REENTRY_SOON: return "Reentry Soon";
            case OrbitState::REENTERED:    return "Reentered";
            default:                       return "Unknown";
        }
    }
    
    static const char* getLevelEmoji(OrbitState level) {
        switch (level) {
            case OrbitState::STABLE:       return "G";
            case OrbitState::SLOW_DECAY:   return "Y";
            case OrbitState::DECAYING:     return "O";
            case OrbitState::RAPID_DECAY:  return "R";
            case OrbitState::REENTRY_SOON: return "F";
            case OrbitState::REENTERED:    return "C";
            default:                       return "U";
        }
    }
    
    // Helper to get or calculate DecayInfo on-demand
    static DecayInfo getDecayInfo(const OrbitRecord& curr) {
        DecayInfo cached;
        // 1. Try to load matching cached decay info
        if (loadDecayInfo(curr.catalogNumber, cached)) {
            if (cached.lastEpoch == curr.epochUnix) {
                return cached;
            }
        }
        
        // 2. If epoch doesn't match or no cache, try to find previous record
        OrbitRecord prev;
        bool foundPrev = false;
        
        // A. Is it custom satellite (look in /cat_<catNum>_prev.json)?
        char pathPrev[32];
        sprintf(pathPrev, "/cat_%u_prev.json", (unsigned int)curr.catalogNumber);
        if (LittleFS.exists(pathPrev)) {
            File f = LittleFS.open(pathPrev, "r");
            if (f) {
                String content = f.readString();
                f.close();
                JSONParser parser;
                if (parser.parse(content, prev)) {
                    foundPrev = true;
                }
            }
        }
        
        // B. If not found in custom, scan /json_recent_raw_prev.jsonl
        if (!foundPrev && LittleFS.exists("/json_recent_raw_prev.jsonl")) {
            File f = LittleFS.open("/json_recent_raw_prev.jsonl", "r");
            if (f) {
                JSONParser parser;
                while (f.available()) {
                    String line = f.readStringUntil('\n');
                    line.trim();
                    if (line.length() == 0) continue;
                    
                    // Quick check if the line contains our catalogNumber
                    char idStr[32];
                    sprintf(idStr, "\"NORAD_CAT_ID\":%u", (unsigned int)curr.catalogNumber);
                    char idStr2[32];
                    sprintf(idStr2, "\"NORAD_CAT_ID\":\"%u\"", (unsigned int)curr.catalogNumber);
                    
                    if (line.indexOf(idStr) != -1 || line.indexOf(idStr2) != -1) {
                        OrbitRecord tempRecord;
                        if (parser.parse(line, tempRecord)) {
                            if (tempRecord.catalogNumber == curr.catalogNumber) {
                                prev = tempRecord;
                                foundPrev = true;
                                break;
                            }
                        }
                    }
                }
                f.close();
            }
        }
        
        DecayInfo result;
        if (foundPrev) {
            // Compute from comparison
            result = analyze(prev, curr);
        } else {
            // No previous record available - use static heuristics (Orbital Life Cycle first run)
            double a = 42241.0979 * pow(curr.meanMotion, -2.0 / 3.0);
            double h = a - 6378.137;
            result.lastEpoch = curr.epochUnix;
            result.lastAlt = h;
            result.bstarScore = curr.bstar;
            result.altitudeDropPerDay = 0.0f;
            result.meanMotionChange = 0.0f;
            
            // Map solely by current altitude and BSTAR
            if (h < 185.0) {
                result.level = OrbitState::REENTRY_SOON;
            } else if (h < 250.0 || curr.bstar > 0.01) {
                result.level = OrbitState::RAPID_DECAY;
            } else if (h < 350.0 || curr.bstar > 0.002) {
                result.level = OrbitState::DECAYING;
            } else if (h < 450.0 || curr.bstar > 0.0005) {
                result.level = OrbitState::SLOW_DECAY;
            } else {
                result.level = OrbitState::STABLE;
            }
            result.isDecaying = (result.level != OrbitState::STABLE);
        }
        
        saveDecayInfo(curr.catalogNumber, result);
        return result;
    }
    
    static String getDecayStatusWithSuffix(OrbitState level, const String& worstName) {
        String baseStr = getLevelStr(level);
        if (level == OrbitState::STABLE) return baseStr;
        
        if (worstName.length() > 0) {
            if (worstName.indexOf("R/B") != -1 || worstName.indexOf("ROCKET") != -1) {
                return baseStr + " (RB)";
            } else if (worstName.indexOf("DEB") != -1 || worstName.indexOf("DEBRIS") != -1) {
                return baseStr + " (DEB)";
            } else {
                return baseStr + " (SAT)";
            }
        }
        return baseStr;
    }
    
    static DecayInfo getRecentLaunchWorstDecay(const RecentLaunchItem& item, String& worstName) {
        DecayInfo worstDecay;
        worstDecay.level = OrbitState::STABLE;
        worstDecay.decayScore = 0.0f;
        worstDecay.lastAlt = item.avgAlt; // fallback
        worstName = item.repSatName; // default fallback
        
        if (!LittleFS.exists("/json_recent_raw.jsonl")) return worstDecay;
        
        File f = LittleFS.open("/json_recent_raw.jsonl", "r");
        if (f) {
            JSONParser parser;
            int yieldCounter = 0;
            while (f.available()) {
                String line = f.readStringUntil('\n');
                line.trim();
                if (line.length() == 0) continue;
                
                String cosparForm = "";
                if (item.batchId.length() == 5 && isdigit(item.batchId[0]) && isdigit(item.batchId[1])) {
                    cosparForm = "20" + item.batchId.substring(0, 2) + "-" + item.batchId.substring(2);
                }
                
                bool match = false;
                if (line.indexOf(item.batchId) != -1) {
                    match = true;
                } else if (cosparForm.length() > 0 && line.indexOf(cosparForm) != -1) {
                    match = true;
                }
                
                if (match) {
                    OrbitRecord curr;
                    if (parser.parse(line, curr)) {
                        DecayInfo d = getDecayInfo(curr);
                        
                        auto getPriority = [](OrbitState l) {
                            if (l == OrbitState::REENTRY_SOON) return 5;
                            if (l == OrbitState::RAPID_DECAY) return 4;
                            if (l == OrbitState::DECAYING) return 3;
                            if (l == OrbitState::SLOW_DECAY) return 2;
                            if (l == OrbitState::REENTERED) return 1;
                            return 0; // STABLE
                        };
                        
                        int currentPri = getPriority(d.level);
                        int worstPri = getPriority(worstDecay.level);
                        
                        if (currentPri > worstPri || (currentPri == worstPri && d.altitudeDropPerDay > worstDecay.altitudeDropPerDay)) {
                            worstDecay = d;
                            worstName = curr.name;
                        }
                    }
                }
                if (yieldCounter++ % 15 == 0) {
                    esp_task_wdt_reset(); // Feed watchdog for SetupLoader task
                }
            }
            f.close();
        }
        return worstDecay;
    }
};
