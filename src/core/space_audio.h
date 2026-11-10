#ifndef SPACE_AUDIO_H
#define SPACE_AUDIO_H

#include <Arduino.h>
#include <M5Cardputer.h>
#include "earth_renderer.h"

// 航天电台与下行无线电信标音效模拟器
class SpaceAudio {
public:
    struct ToneNote {
        uint16_t freq;      // 声音频率 (Hz)，0 表示静音间隔
        uint16_t duration;  // 持续时间 (ms)
    };

    static void begin();
    static void update();
    static bool hasBeacon(uint32_t noradId, const String& name, SatIconType icon);
    static const char* getBeaconTitle(uint32_t noradId, const String& name, SatIconType icon);
    static void playBeacon(uint32_t noradId, const String& name, SatIconType icon);
    static void stop();
    static bool isPlaying();

private:
    static const ToneNote* s_currentSequence;
    static size_t s_sequenceLength;
    static size_t s_currentStep;
    static uint32_t s_stepEndTime;
    static bool s_isPlaying;

    static void startSequence(const ToneNote* seq, size_t len);
};

#endif // SPACE_AUDIO_H

