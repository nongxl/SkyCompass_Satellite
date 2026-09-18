#pragma once
#include <Arduino.h>
#include <vector>
#include "../hal/hal_radio.h"
#include "telemetry_decoder.h"

struct ReceivedLogItem {
    RadioPacket raw;
    DecodedTelemetry decoded;
};

struct RadioTrackingInfo {
    bool hasPass = false;
    uint32_t satNorad = 0;
    String satName = "";
    float currentEl = -90.0f;
    float currentAz = 0.0f;
    float maxEl = 0.0f;
    uint32_t aosTime = 0;
    uint32_t tcaTime = 0;
    uint32_t losTime = 0;
    float dopplerHz = 0.0f;       // 多普勒频移 (Hz)
    float baseFreqMHz = 0.0f;     // 发射中心频率 (MHz)
    int32_t timeOffsetSec = 0;    // 当前时间校准偏移量 (秒)
    bool isRising = false;        // 是否处于上升期 (AOS -> TCA)
};

class RadioManager {
public:
    static RadioManager& getInstance() {
        static RadioManager instance;
        return instance;
    }

    // 初始化 Radio 模块与管理系统
    void init();

    // 每帧在主循环中调用：执行自动过境探测、射频收发、数据处理与休眠逻辑
    // focalNoradId: 当前主界面聚焦的卫星 NORAD ID（若有）
    // focalElevation: 当前主界面聚焦卫星的仰角（度）
    // hasFocalRadio: 当前聚焦卫星是否配置了射频参数
    // focalFreqMHz: 聚焦卫星的下行频率
    // focalMode: 聚焦卫星的调制方式（如 "LoRa", "FSK", "436.700" 等）
    void update(uint32_t focalNoradId, const String& focalName, float focalElevation,
                bool hasFocalRadio, float focalFreqMHz, const String& focalMode);

    // 更新过境追踪遥测与多普勒信息
    void updateTracking(const RadioTrackingInfo& info) { _trackingInfo = info; }
    const RadioTrackingInfo& getTrackingInfo() const { return _trackingInfo; }

    // 查询当前工作状态
    bool isHardwareReady() const;
    bool isListening() const { return _isListening; }
    uint32_t getActiveSatNorad() const { return _activeSatNorad; }
    String getActiveSatName() const { return _activeSatName; }
    float getActiveFreq() const { return _activeFreq; }

    // 历史接收数据包列表访问与管理
    const std::vector<ReceivedLogItem>& getRecentPackets() const { return _recentPackets; }
    size_t getTotalPacketsCount() const { return _totalPacketsReceived; }
    size_t getValidPacketsCount() const { return _validPackets; }
    size_t getCrcErrorCount() const { return _crcErrorPackets; }
    float getPacketLossRate() const {
        size_t total = _validPackets + _crcErrorPackets;
        return (total > 0) ? ((float)_crcErrorPackets * 100.0f / (float)total) : 0.0f;
    }
    bool deletePacket(size_t index);

    // 测试数据包注入 (用于在无卫星过境或室内无信号时调试确认界面与解码器)
    void injectTestPacket();
    void injectTestCrcError();

    // Toast 提示状态
    bool hasActiveToast() const;
    String getToastText() const;
    float getToastAlpha() const; // 0.0 ~ 1.0 用于透明度淡出

    // 信号辐射波纹动画状态 (用于在天空罗盘卫星旁绘制)
    bool isEmittingWaves() const { return _isListening; }
    float getWavePhase() const; // 0.0 ~ 1.0

private:
    RadioManager();
    ~RadioManager();

    void logPacketToFile(const ReceivedLogItem& item);

    bool _initialized = false;
    bool _isListening = false;
    uint32_t _activeSatNorad = 0;
    String _activeSatName = "";
    float _activeFreq = 0.0f;
    uint32_t _passEndedTime = 0;

    RadioTrackingInfo _trackingInfo;

    std::vector<ReceivedLogItem> _recentPackets;
    size_t _totalPacketsReceived = 0;
    size_t _validPackets = 0;
    size_t _crcErrorPackets = 0;

    // Toast 提示
    String _toastText = "";
    uint32_t _toastStartTime = 0;

    // 波纹动画
    uint32_t _waveBaseTime = 0;
};
