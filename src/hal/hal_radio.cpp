#include "hal_radio.h"
#include <SPI.h>

#define CARDPUTER_SD_CS_PIN 12

HalRadio::HalRadio() {
}

HalRadio::~HalRadio() {
    if (_radio) {
        delete _radio;
        _radio = nullptr;
    }
    if (_mod) {
        delete _mod;
        _mod = nullptr;
    }
}

bool HalRadio::init() {
    if (_isDetected) return true;

    // 防止共用 SPI 的 MicroSD 卡占用总线，先拉高 SD CS
    pinMode(CARDPUTER_SD_CS_PIN, OUTPUT);
    digitalWrite(CARDPUTER_SD_CS_PIN, HIGH);

    // 初始化 SPI 引脚
    SPI.begin(LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN, LORA_CS_PIN);

    // 创建 RadioLib 模块实例 (CS, IRQ, RST, BUSY, SPI)
    if (!_mod) {
        _mod = new Module(LORA_CS_PIN, LORA_IRQ_PIN, LORA_RST_PIN, LORA_BUSY_PIN, SPI);
    }
    if (!_radio) {
        _radio = new SX1262(_mod);
    }

    Serial.println("[HalRadio] Initializing SX1262 on Cap LoRa-1262...");

    // 尝试进行硬件初始化与探测
    // 默认参数：436.7 MHz, BW 125.0 kHz, SF9, CR7, SyncWord 0x12, Pwr 10dBm, Preamble 8, TCXO 1.6V
    int16_t state = _radio->begin(436.7f, 125.0f, 9, 7, 0x12, 10, 8, LORA_TCXO_VOLT, false);

    if (state == RADIOLIB_ERR_NONE) {
        // Cap LoRa-1262 使用 DIO2 控制 RF 开关
        _radio->setDio2AsRfSwitch(true);

        _isDetected = true;
        _currentMode = RADIO_MODE_LORA;
        _currentFreq = 436.7f;
        _currentBw = 125.0f;
        _currentSf = 9;
        _currentCr = 7;
        _currentSyncWord = 0x12;

        Serial.println("[HalRadio] SX1262 detected and initialized successfully!");
        
        // 初始化后先进入睡眠模式以省电，等待调度器唤醒
        sleep();
        return true;
    } else {
        Serial.printf("[HalRadio] SX1262 not detected or init failed, code: %d\n", state);
        _isDetected = false;
        return false;
    }
}

bool HalRadio::configLoRa(float freqMHz, float bwKHz, uint8_t sf, uint8_t cr, uint8_t syncWord, int8_t power) {
    if (!_isDetected || !_radio) return false;

    _radio->standby();

    int16_t state = _radio->setFrequency(freqMHz);
    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("[HalRadio] setFrequency failed: %d\n", state);
        return false;
    }

    state = _radio->setBandwidth(bwKHz);
    if (state != RADIOLIB_ERR_NONE) return false;

    state = _radio->setSpreadingFactor(sf);
    if (state != RADIOLIB_ERR_NONE) return false;

    state = _radio->setCodingRate(cr);
    if (state != RADIOLIB_ERR_NONE) return false;

    state = _radio->setSyncWord(syncWord);
    if (state != RADIOLIB_ERR_NONE) return false;

    state = _radio->setOutputPower(power);
    if (state != RADIOLIB_ERR_NONE) return false;

    _radio->setDio2AsRfSwitch(true);

    _currentMode = RADIO_MODE_LORA;
    _currentFreq = freqMHz;
    _currentBw = bwKHz;
    _currentSf = sf;
    _currentCr = cr;
    _currentSyncWord = syncWord;

    Serial.printf("[HalRadio] Configured LoRa: %.4f MHz, BW: %.1f kHz, SF%d, CR4/%d, Sync: 0x%02X\n",
                  freqMHz, bwKHz, sf, cr, syncWord);

    return startReceive();
}

bool HalRadio::configFSK(float freqMHz, float brKbps, float freqDevKHz, float rxBwKHz, const uint8_t* syncWord, size_t syncWordLen) {
    if (!_isDetected || !_radio) return false;

    _radio->standby();

    // 切换到 FSK 调制
    int16_t state = _radio->setFrequency(freqMHz);
    if (state != RADIOLIB_ERR_NONE) return false;

    state = _radio->setBitRate(brKbps);
    if (state != RADIOLIB_ERR_NONE) return false;

    state = _radio->setFrequencyDeviation(freqDevKHz);
    if (state != RADIOLIB_ERR_NONE) return false;

    state = _radio->setRxBandwidth(rxBwKHz);
    if (state != RADIOLIB_ERR_NONE) return false;

    if (syncWord && syncWordLen > 0) {
        uint8_t swCopy[8] = {0};
        size_t len = syncWordLen > 8 ? 8 : syncWordLen;
        memcpy(swCopy, syncWord, len);
        _radio->setSyncWord(swCopy, len);
    }

    _radio->setDio2AsRfSwitch(true);

    _currentMode = RADIO_MODE_FSK;
    _currentFreq = freqMHz;
    _currentBw = rxBwKHz;

    Serial.printf("[HalRadio] Configured FSK: %.4f MHz, BR: %.1f kbps, Dev: %.1f kHz, RxBW: %.1f kHz\n",
                  freqMHz, brKbps, freqDevKHz, rxBwKHz);

    return startReceive();
}

bool HalRadio::setFrequency(float freqMHz) {
    if (!_isDetected || !_radio) return false;
    _currentFreq = freqMHz;
    return (_radio->setFrequency(freqMHz) == RADIOLIB_ERR_NONE);
}

bool HalRadio::startReceive() {
    if (!_isDetected || !_radio) return false;
    // 使用非阻塞异步接收模式，等待 DIO1 中断触发或轮询
    int16_t state = _radio->startReceive();
    if (state == RADIOLIB_ERR_NONE) {
        _isReceiving = true;
        return true;
    }
    Serial.printf("[HalRadio] startReceive failed: %d\n", state);
    return false;
}

bool HalRadio::sleep() {
    if (!_isDetected || !_radio) return false;
    _isReceiving = false;
    // SX1262 保持配置休眠 (warm start)
    return (_radio->sleep(true) == RADIOLIB_ERR_NONE);
}

bool HalRadio::pollPacket(RadioPacket& outPacket) {
    if (!_isDetected || !_radio || !_isReceiving) return false;

    // 检查是否有数据包接收完成
    uint16_t irqFlags = _radio->getIrqStatus();
    if (irqFlags & RADIOLIB_SX126X_IRQ_RX_DONE) {
        // 数据包已接收
        size_t len = _radio->getPacketLength();
        if (len > 0 && len <= sizeof(outPacket.payload)) {
            int16_t state = _radio->readData(outPacket.payload, len);
            if (state == RADIOLIB_ERR_NONE) {
                outPacket.length = len;
                outPacket.timestamp = millis();
                outPacket.rssi = _radio->getRSSI();
                outPacket.snr = _radio->getSNR();
                outPacket.freqMHz = _currentFreq;

                // 重新进入接收状态
                _radio->startReceive();
                return true;
            }
        }
        // 若读取失败或超长，重置接收
        _radio->standby();
        _radio->startReceive();
    }
    return false;
}
