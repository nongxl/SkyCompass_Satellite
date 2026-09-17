#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>

// Cardputer Cap LoRa-1262 硬件引脚定义
#define LORA_CS_PIN    5
#define LORA_RST_PIN   3
#define LORA_IRQ_PIN   4
#define LORA_BUSY_PIN  6
#define LORA_SCK_PIN   40
#define LORA_MISO_PIN  39
#define LORA_MOSI_PIN  14
#define LORA_TCXO_VOLT 1.6f

enum RadioModulationMode {
    RADIO_MODE_NONE = 0,
    RADIO_MODE_LORA = 1,
    RADIO_MODE_FSK  = 2
};

struct RadioPacket {
    uint32_t timestamp = 0;
    float rssi = 0.0f;
    float snr = 0.0f;
    float freqMHz = 0.0f;
    size_t length = 0;
    uint8_t payload[256];
};

class HalRadio {
public:
    static HalRadio& getInstance() {
        static HalRadio instance;
        return instance;
    }

    // 初始化 SPI 并探测 SX1262 硬件是否存在
    bool init();

    // 检查硬件是否已成功检测并就绪
    bool isHardwareDetected() const { return _isDetected; }

    // 配置为 LoRa 模式并进入接收
    bool configLoRa(float freqMHz, float bwKHz = 125.0f, uint8_t sf = 10, uint8_t cr = 5, uint8_t syncWord = 0x12, int8_t power = 10);

    // 配置为 FSK / GFSK 模式并进入接收
    bool configFSK(float freqMHz, float brKbps = 4.8f, float freqDevKHz = 5.0f, float rxBwKHz = 50.0f, const uint8_t* syncWord = nullptr, size_t syncWordLen = 0);

    // 动态微调频率（多普勒频移补偿，不重置射频引擎）
    bool setFrequency(float freqMHz);

    // 开始非阻塞接收
    bool startReceive();

    // 射频芯片进入低功耗休眠
    bool sleep();

    // 轮询检查是否有新数据包接收完成
    bool pollPacket(RadioPacket& outPacket);

    // 获取当前配置信息
    RadioModulationMode getCurrentMode() const { return _currentMode; }
    float getCurrentFrequency() const { return _currentFreq; }
    float getBW() const { return _currentBw; }
    uint8_t getSF() const { return _currentSf; }
    uint8_t getSyncWord() const { return _currentSyncWord; }

private:
    HalRadio();
    ~HalRadio();

    bool _isDetected = false;
    bool _isReceiving = false;
    RadioModulationMode _currentMode = RADIO_MODE_NONE;
    float _currentFreq = 0.0f;
    float _currentBw = 125.0f;
    uint8_t _currentSf = 10;
    uint8_t _currentCr = 5;
    uint8_t _currentSyncWord = 0x12;

    Module* _mod = nullptr;
    SX1262* _radio = nullptr;
};
