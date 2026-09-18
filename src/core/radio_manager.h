#pragma once
#include <Arduino.h>
#include <vector>
#include "../hal/hal_radio.h"
#include "telemetry_decoder.h"

struct ReceivedLogItem {
    RadioPacket raw;
    DecodedTelemetry decoded;
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

    // 查询当前工作状态
    bool isHardwareReady() const;
    bool isListening() const { return _isListening; }
    uint32_t getActiveSatNorad() const { return _activeSatNorad; }
    String getActiveSatName() const { return _activeSatName; }
    float getActiveFreq() const { return _activeFreq; }

    // 历史接收数据包列表访问
    const std::vector<ReceivedLogItem>& getRecentPackets() const { return _recentPackets; }
    size_t getTotalPacketsCount() const { return _totalPacketsReceived; }

    // 测试数据包注入 (用于在无卫星过境或室内无信号时调试确认界面与解码器)
    void injectTestPacket();

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

    std::vector<ReceivedLogItem> _recentPackets;
    size_t _totalPacketsReceived = 0;

    // Toast 提示
    String _toastText = "";
    uint32_t _toastStartTime = 0;

    // 波纹动画
    uint32_t _waveBaseTime = 0;
};
