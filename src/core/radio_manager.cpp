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
    if (focalNoradId > 0 && hasFocalRadio && focalElevation > -3.0f && focalFreqMHz > 100.0f) {
        shouldListen = true;
        targetNorad = focalNoradId;
        targetName = focalName;
        targetFreq = focalFreqMHz;
        targetMode = focalMode;
    }

    // 2. 状态机切换
    if (shouldListen) {
        // 如果尚未监听，或者监听目标变更，重新配置射频
        if (!_isListening || _activeSatNorad != targetNorad || fabs(_activeFreq - targetFreq) > 0.005f) {
            _activeSatNorad = targetNorad;
            _activeSatName = targetName;
            _activeFreq = targetFreq;

            // 判断调制方式 (LoRa 还是 FSK)
            String mUpper = targetMode;
            mUpper.toUpperCase();
            if (mUpper.indexOf("FSK") != -1 || mUpper.indexOf("GFSK") != -1) {
                // 默认 4.8kbps, dev 5kHz, bw 50kHz
                HalRadio::getInstance().configFSK(targetFreq, 4.8f, 5.0f, 50.0f);
            } else {
                // 默认 LoRa SF9 / SF10, 125kHz
                uint8_t sf = 9;
                if (targetNorad == 46494) sf = 9;  // NORBI
                else if (targetNorad == 61751) sf = 10; // Vladivostok-1
                HalRadio::getInstance().configLoRa(targetFreq, 125.0f, sf, 7, 0x12);
            }

            _isListening = true;
            _passEndedTime = 0;
            Serial.printf("[RadioManager] Auto-started listening to %s (NORAD %u) at %.3f MHz\n",
                          targetName.c_str(), targetNorad, targetFreq);
        }
    } else {
        // 无过境卫星
        if (_isListening) {
            if (_passEndedTime == 0) {
                _passEndedTime = now;
            } else if (now - _passEndedTime > 120000) { // 2 分钟无过境信号，休眠
                HalRadio::getInstance().sleep();
                _isListening = false;
                _activeSatNorad = 0;
                _activeSatName = "";
                _passEndedTime = 0;
                Serial.println("[RadioManager] Pass ended. Radio entered sleep.");
            }
        }
    }

    // 3. 轮询接收数据包
    if (_isListening) {
        RadioPacket pkt;
        if (HalRadio::getInstance().pollPacket(pkt)) {
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

            // 触发居中顶部 Toast 弹窗
            _toastText = "[RX] " + item.decoded.satName + " (" + String(pkt.length) + "B) RSSI:" + String(pkt.rssi, 0);
            _toastStartTime = now;

            // 异步保存到本地文件
            logPacketToFile(item);

            Serial.printf("[RadioManager] Received packet: %s, RSSI: %.1f dBm\n",
                          _toastText.c_str(), pkt.rssi);
        }
    }
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
