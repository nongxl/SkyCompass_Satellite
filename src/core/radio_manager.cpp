#include "radio_manager.h"
#include "hardware_config.h"
#include <LittleFS.h>
#include <FS.h>

#define MAX_LOG_PACKETS 30

RadioManager::RadioManager() {
}

RadioManager::~RadioManager() {
}

void RadioManager::init() {
    if (_initialized) return;

    if (HardwareConfig::getInstance().isEnabled(HW_MOD_CAP_LORA1262)) {
        bool ok = HalRadio::getInstance().init();
        if (ok) {
            Serial.println("[RadioManager] Cap LoRa-1262 initialized successfully.");
        } else {
            Serial.println("[RadioManager] Cap LoRa-1262 not detected on hardware bus.");
        }
    }

    // 确保日志目录存在
    if (LittleFS.begin()) {
        if (!LittleFS.exists("/lora_logs")) {
            LittleFS.mkdir("/lora_logs");
        }
    }

    _initialized = true;
}

static void parseLoRaParams(const String& modeStr, uint32_t noradId, float& outBw, uint8_t& outSf, uint8_t& outCr, uint8_t& outSyncWord) {
    outBw = 125.0f;
    outSf = 9;
    outCr = 7; // 默认 4/7
    outSyncWord = 0x12;

    String s = modeStr;
    s.toUpperCase();

    // 1. 扩频因子 SF (支持 SF5 ~ SF12)
    int idxSf = s.indexOf("SF");
    if (idxSf != -1) {
        int val = s.substring(idxSf + 2).toInt();
        if (val >= 5 && val <= 12) {
            outSf = (uint8_t)val;
        }
    }

    // 2. 带宽 BW (kHz)
    int idxBw = s.indexOf("BW");
    if (idxBw != -1) {
        float val = s.substring(idxBw + 2).toFloat();
        if (val > 10.0f && val < 600.0f) {
            outBw = val;
        }
    }

    // 3. 编码率 CR (支持 CR4/5, CR4:5, CR4/7, CR4:7, CR4/8 等)
    int idxCr = s.indexOf("CR");
    if (idxCr != -1) {
        String crPart = s.substring(idxCr + 2);
        if (crPart.startsWith("4/5") || crPart.startsWith("4:5") || crPart.startsWith("5")) {
            outCr = 5;
        } else if (crPart.startsWith("4/6") || crPart.startsWith("4:6") || crPart.startsWith("6")) {
            outCr = 6;
        } else if (crPart.startsWith("4/7") || crPart.startsWith("4:7") || crPart.startsWith("7")) {
            outCr = 7;
        } else if (crPart.startsWith("4/8") || crPart.startsWith("4:8") || crPart.startsWith("8")) {
            outCr = 8;
        }
    }

    // 4. 同步字 SyncWord (例如 SW18 或 SW12)
    int idxSw = s.indexOf("SW");
    if (idxSw != -1) {
        long val = strtol(s.substring(idxSw + 2).c_str(), nullptr, 16);
        if (val > 0 && val <= 0xFF) {
            outSyncWord = (uint8_t)val;
        }
    }

    // 5. 针对已知卫星 NORAD ID 兜底补充校准
    if (noradId == 69795) { // PROVES-Electra
        outSf = 8;
        outCr = 5; // 4/5
        outBw = 125.0f;
        outSyncWord = 0x12;
    } else if (noradId == 46494) { // NORBI
        outSf = 9;
        outCr = 7;
    } else if (noradId == 61751) { // Vladivostok-1
        outSf = 10;
        outCr = 7;
    } else if (noradId == 62676) { // FOSSASAT-2E
        outSf = 10;
        outCr = 7;
    } else if (noradId == 57172) { // UMKA-1
        outSf = 8;
        outCr = 5;
    }
}

bool RadioManager::isHardwareReady() const {
    return HardwareConfig::getInstance().isEnabled(HW_MOD_CAP_LORA1262) &&
           HalRadio::getInstance().isHardwareDetected();
}

void RadioManager::update(uint32_t focalNoradId, const String& focalName, float focalElevation,
                          bool hasFocalRadio, float focalFreqMHz, const String& focalMode) {
    if (!isHardwareReady()) {
        if (_isListening) {
            HalRadio::getInstance().sleep();
            _isListening = false;
        }
        return;
    }

    uint32_t now = millis();

    // 1. 自动过境触发判断 (仰角 > -3.0 度视为过境窗口)
    bool shouldListen = false;
    uint32_t targetNorad = 0;
    String targetName = "";
    float targetFreq = 0.0f;
    String targetMode = "";

    // 优先监听用户当前在天空罗盘上选中的卫星
    if (focalNoradId > 0 && hasFocalRadio && focalFreqMHz > 0.0f) {
        _isTargetFreqSupported = HalRadio::isFrequencySupported(focalFreqMHz);
        if (_isTargetFreqSupported && focalElevation > -3.0f) {
            shouldListen = true;
            targetNorad = focalNoradId;
            targetName = focalName;
            targetFreq = focalFreqMHz;
            targetMode = focalMode;
        }
    } else {
        _isTargetFreqSupported = true;
    }

    // 2. 状态机切换
    if (shouldListen) {
        // 计算当前带多普勒频移的目标实际调谐频率
        float dopplerMHz = 0.0f;
        if (_trackingInfo.satNorad == targetNorad && abs(_trackingInfo.dopplerHz) < 100000.0f) {
            dopplerMHz = _trackingInfo.dopplerHz / 1e6f;
        }
        float targetTunedFreq = targetFreq + dopplerMHz;

        // 如果尚未监听，或者监听目标变更，重新配置射频
        if (!_isListening || _activeSatNorad != targetNorad || fabs(_activeBaseFreq - targetFreq) > 0.005f) {
            _activeSatNorad = targetNorad;
            _activeSatName = targetName;
            _activeBaseFreq = targetFreq;
            _activeFreq = targetFreq;
            _currentTunedFreq = targetTunedFreq;
            _lastDopplerTuneTime = now;

            bool configOk = false;
            // 判断调制方式 (LoRa 还是 FSK)
            String mUpper = targetMode;
            mUpper.toUpperCase();
            if (mUpper.indexOf("FSK") != -1 || mUpper.indexOf("GFSK") != -1) {
                // 默认 4.8kbps, dev 5kHz, bw 50kHz
                configOk = HalRadio::getInstance().configFSK(targetTunedFreq, 4.8f, 5.0f, 50.0f);
            } else {
                // 自适应解析 LoRa 调制参数 (BW, SF, CR, SyncWord)
                float bw = 125.0f;
                uint8_t sf = 9;
                uint8_t cr = 7;
                uint8_t syncWord = 0x12;
                parseLoRaParams(targetMode, targetNorad, bw, sf, cr, syncWord);

                configOk = HalRadio::getInstance().configLoRa(targetTunedFreq, bw, sf, cr, syncWord);
            }

            if (configOk) {
                _isListening = true;
                _passEndedTime = 0;
                Serial.printf("[RadioManager] Auto-started listening to %s (NORAD %u) at %.4f MHz (Base: %.3f MHz, Doppler: %+.1f kHz)\n",
                              targetName.c_str(), targetNorad, targetTunedFreq, targetFreq, dopplerMHz * 1000.0f);
            } else {
                _isListening = false;
                Serial.printf("[RadioManager] Failed to configure radio for %s at %.4f MHz\n",
                              targetName.c_str(), targetTunedFreq);
            }
        } else {
            // 已处于持续监听状态：执行多普勒连续自动微调 (Doppler Continuous AFC Auto-Tuning)
            // 采用 1.0 kHz 步进门限 + 500ms 最小调谐间隔，确保锁频紧密且不打断前导码接收
            if (fabs(targetTunedFreq - _currentTunedFreq) >= 0.001f && (now - _lastDopplerTuneTime >= 500)) {
                if (HalRadio::getInstance().setFrequency(targetTunedFreq)) {
                    _currentTunedFreq = targetTunedFreq;
                    _lastDopplerTuneTime = now;
                }
            }
        }
    } else {
        // 无过境卫星 或 频段不支持
        if (_isListening) {
            if (_passEndedTime == 0) {
                _passEndedTime = now;
            } else if (!_isTargetFreqSupported || (now - _passEndedTime > 120000)) { // 若频段不支持立即休眠，或2分钟无过境休眠
                HalRadio::getInstance().sleep();
                _isListening = false;
                _activeSatNorad = 0;
                _activeSatName = "";
                _activeBaseFreq = 0.0f;
                _currentTunedFreq = 0.0f;
                _passEndedTime = 0;
                Serial.println("[RadioManager] Pass ended or freq unsupported. Radio entered sleep.");
            }
        }
    }

    // 3. 轮询接收数据包
    if (_isListening) {
        RadioPacket pkt;
        RadioPollResult res = HalRadio::getInstance().pollPacket(pkt);
        if (res == RADIO_POLL_PACKET_OK) {
            ReceivedLogItem item;
            item.raw = pkt;
            item.decoded = TelemetryDecoder::decode(pkt, _activeSatNorad);
            if (item.decoded.satName.length() == 0 || item.decoded.satName == "Unknown Sat") {
                item.decoded.satName = _activeSatName;
                item.decoded.noradId = _activeSatNorad;
            }

            _recentPackets.insert(_recentPackets.begin(), item);
            if (_recentPackets.size() > MAX_LOG_PACKETS) {
                _recentPackets.pop_back();
            }
            _totalPacketsReceived++;
            _validPackets++;

            // 触发居中顶部 Toast 弹窗
            _toastText = "[RX] " + item.decoded.satName + " (" + String(pkt.length) + "B) RSSI:" + String(pkt.rssi, 0);
            _toastStartTime = now;

            // 异步保存到本地文件
            logPacketToFile(item);

            Serial.printf("[RadioManager] Received packet: %s, RSSI: %.1f dBm\n",
                          _toastText.c_str(), pkt.rssi);
        } else if (res == RADIO_POLL_CRC_ERROR) {
            _crcErrorPackets++;
            Serial.println("[RadioManager] CRC Error detected on incoming packet!");
        }
    }
}

void RadioManager::injectTestPacket() {
    RadioPacket pkt;
    pkt.timestamp = millis();
    pkt.rssi = -82.0f;
    pkt.snr = 8.5f;

    uint32_t injectNorad = _activeSatNorad;
    String injectName = _activeSatName;
    float injectFreq = _activeFreq;

    // 根据当前选中的卫星智能适配模拟报文；若无或非已知 LoRa 星，默认使用 PROVES-Electra 示范
    bool isNorbi = (injectNorad == 46494 || injectName.indexOf("NORBI") != -1);
    bool isProves = (injectNorad == 69795 || injectName.indexOf("PROVES") != -1);
    if (!isNorbi && !isProves) {
        // 默认示范星：PROVES-Electra
        injectNorad = 69795;
        injectName = "PROVES-Electra";
        injectFreq = 437.400f;
        isProves = true;
    }

    pkt.freqMHz = injectFreq > 100.0f ? injectFreq : (isNorbi ? 436.700f : 437.400f);

    if (isProves) {
        // PROVES PySquared 真实信标格式
        // byte 0: 0x03 (Spacecraft ID: Electra)
        // byte 1-2: Vbat = 3970 mV (3.97 V) -> 0x82, 0x0F
        // byte 3: Temp = 22 C -> 0x16
        // byte 4-5: I_bus = 54.6 mA (546) -> 0x22, 0x02
        // byte 6-7: Reboot counter = 4 -> 0x04, 0x00
        // byte 8-10: "P1P"
        uint8_t provesPayload[48] = {
            0x03,
            0x82, 0x0F,
            0x16,
            0x22, 0x02,
            0x04, 0x00,
            'P', '1', 'P', 0x00
        };
        for (int i = 12; i < 48; i++) provesPayload[i] = (uint8_t)(0x20 + i);
        pkt.length = 48;
        memcpy(pkt.payload, provesPayload, 48);
    } else {
        // NORBI LoRa 真实信标格式
        // byte 0-1: Frame# = 1042 -> 0x12, 0x04
        // byte 2-3: Vbat = 3920 mV (3.92 V) -> 0x50, 0x0F
        // byte 4: Temp = 21 C -> 0x15
        // byte 5-6: I_bus = 58.7 mA (587) -> 0x4B, 0x02
        uint8_t norbiPayload[48] = {
            0x12, 0x04,
            0x50, 0x0F,
            0x15,
            0x4B, 0x02,
            0x01, 0x00,
            'N', 'O', 'R', 'B'
        };
        for (int i = 12; i < 48; i++) norbiPayload[i] = (uint8_t)(0x10 + i);
        pkt.length = 48;
        memcpy(pkt.payload, norbiPayload, 48);
    }

    ReceivedLogItem item;
    item.raw = pkt;
    item.decoded = TelemetryDecoder::decode(pkt, injectNorad);
    if (item.decoded.satName.length() == 0 || item.decoded.satName == "Unknown Sat") {
        item.decoded.satName = injectName;
        item.decoded.noradId = injectNorad;
    }

    _recentPackets.insert(_recentPackets.begin(), item);
    if (_recentPackets.size() > MAX_LOG_PACKETS) {
        _recentPackets.pop_back();
    }
    _totalPacketsReceived++;
    _validPackets++;

    _toastText = "[RX TEST] " + item.decoded.satName + " (" + String(pkt.length) + "B) RSSI:" + String((int)pkt.rssi);
    _toastStartTime = millis();

    logPacketToFile(item);
    Serial.printf("[RadioManager] Injected test packet for %s (NORAD %u)\n", item.decoded.satName.c_str(), injectNorad);
}

void RadioManager::injectTestCrcError() {
    _crcErrorPackets++;
    _toastText = "[RX CRC ERR] Checksum Failed";
    _toastStartTime = millis();
    Serial.println("[RadioManager] Injected simulated CRC error.");
}

void RadioManager::logPacketToFile(const ReceivedLogItem& item) {
    File f = LittleFS.open("/lora_logs/rx.log", "a");
    if (f) {
        f.printf("[%u] NORAD:%u %s | Freq:%.4f RSSI:%.1f SNR:%.1f Len:%d | Hex: %s\n",
                 item.raw.timestamp, item.decoded.noradId, item.decoded.satName.c_str(),
                 item.raw.freqMHz, item.raw.rssi, item.raw.snr, item.raw.length,
                 item.decoded.rawHex.c_str());
        f.close();
    }
}

bool RadioManager::hasActiveToast() const {
    if (_toastStartTime == 0) return false;
    return (millis() - _toastStartTime < 5000);
}

String RadioManager::getToastText() const {
    return _toastText;
}

float RadioManager::getToastAlpha() const {
    if (_toastStartTime == 0) return 0.0f;
    uint32_t elapsed = millis() - _toastStartTime;
    if (elapsed >= 5000) return 0.0f;
    if (elapsed < 4000) return 1.0f;
    // 4000ms ~ 5000ms: 平滑淡出
    return 1.0f - (float)(elapsed - 4000) / 1000.0f;
}

float RadioManager::getWavePhase() const {
    // 1 秒循环一次
    return (float)(millis() % 1000) / 1000.0f;
}

bool RadioManager::deletePacket(size_t index) {
    if (index < _recentPackets.size()) {
        _recentPackets.erase(_recentPackets.begin() + index);
        if (_totalPacketsReceived > 0) {
            _totalPacketsReceived--;
        }
        return true;
    }
    return false;
}
