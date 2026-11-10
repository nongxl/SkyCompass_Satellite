#include "space_audio.h"

// 1. 东方红一号经典下行乐曲电子合成音节 (D大调经典旋律)
static const SpaceAudio::ToneNote SEQ_DFH1[] = {
    {294, 240}, {0, 35}, // D4
    {294, 240}, {0, 35}, // D4
    {392, 360}, {0, 35}, // G4
    {440, 240}, {0, 35}, // A4
    {494, 240}, {0, 35}, // B4
    {440, 240}, {0, 35}, // A4
    {392, 240}, {0, 35}, // G4
    {330, 480}, {0, 60}  // E4
};

// 2. 业余无线电卫星 800Hz CW 莫尔斯电码信标 (播发 "CAS": -.-.  .-  ...)
static const SpaceAudio::ToneNote SEQ_MORSE_HAM[] = {
    // C: - . - .
    {800, 180}, {0, 60},
    {800, 60},  {0, 60},
    {800, 180}, {0, 60},
    {800, 60},  {0, 180}, // 字母间隔
    // A: . -
    {800, 60},  {0, 60},
    {800, 180}, {0, 180}, // 字母间隔
    // S: . . .
    {800, 60},  {0, 60},
    {800, 60},  {0, 60},
    {800, 60},  {0, 250}
};

// 3. 空间站下行通信导频音 (经典 Quindar Beep)
static const SpaceAudio::ToneNote SEQ_STATION_QUINDAR[] = {
    {2525, 220}, {0, 70},
    {2475, 140}, {0, 100}
};

// 4. 常规航天器下行遥测多普勒频移扫频脉冲 (Telemetry Chirp)
static const SpaceAudio::ToneNote SEQ_CHIRP[] = {
    {1100, 60},
    {1350, 60},
    {1600, 60},
    {1900, 80},
    {2250, 120},
    {0, 100}
};

const SpaceAudio::ToneNote* SpaceAudio::s_currentSequence = nullptr;
size_t SpaceAudio::s_sequenceLength = 0;
size_t SpaceAudio::s_currentStep = 0;
uint32_t SpaceAudio::s_stepEndTime = 0;
bool SpaceAudio::s_isPlaying = false;

void SpaceAudio::begin() {
    M5Cardputer.Speaker.begin();
    M5Cardputer.Speaker.setVolume(100);
    s_isPlaying = false;
    s_currentSequence = nullptr;
    s_sequenceLength = 0;
    s_currentStep = 0;
}

bool SpaceAudio::hasBeacon(uint32_t noradId, const String& name, SatIconType icon) {
    if (noradId == 431 || icon == ICON_DFH1 || name.indexOf("DFH") != -1 || name.indexOf("DongFangHong") != -1) {
        return true;
    }
    if (noradId == 25544 || noradId == 48274 || icon == ICON_STATION) {
        return true;
    }
    if (icon == ICON_COMMUNICATION || name.indexOf("CAS-") != -1 || name.indexOf("OSCAR") != -1 || name.indexOf("XW-") != -1) {
        return true;
    }
    return true; // 所有入库卫星均支持特色或遥测 Chirp 试听
}

const char* SpaceAudio::getBeaconTitle(uint32_t noradId, const String& name, SatIconType icon) {
    if (noradId == 431 || icon == ICON_DFH1 || name.indexOf("DFH") != -1 || name.indexOf("DongFangHong") != -1) {
        return "东方红乐曲信标";
    }
    if (noradId == 25544 || noradId == 48274 || icon == ICON_STATION) {
        return "空间站VHF下行导频音";
    }
    if (icon == ICON_COMMUNICATION || name.indexOf("CAS-") != -1 || name.indexOf("OSCAR") != -1 || name.indexOf("XW-") != -1) {
        return "业余CW莫尔斯电码信标";
    }
    return "多普勒遥测扫频信标";
}

void SpaceAudio::startSequence(const ToneNote* seq, size_t len) {
    stop();
    if (!seq || len == 0) return;

    M5Cardputer.Speaker.begin();
    M5Cardputer.Speaker.setVolume(120);

    s_currentSequence = seq;
    s_sequenceLength = len;
    s_currentStep = 0;
    s_isPlaying = true;

    // 播放第一步
    const ToneNote& note = s_currentSequence[0];
    if (note.freq > 0) {
        M5Cardputer.Speaker.tone(note.freq, note.duration);
    }
    s_stepEndTime = millis() + note.duration;
}

void SpaceAudio::playBeacon(uint32_t noradId, const String& name, SatIconType icon) {
    if (noradId == 431 || icon == ICON_DFH1 || name.indexOf("DFH") != -1 || name.indexOf("DongFangHong") != -1) {
        startSequence(SEQ_DFH1, sizeof(SEQ_DFH1) / sizeof(SEQ_DFH1[0]));
    } else if (icon == ICON_COMMUNICATION || name.indexOf("CAS-") != -1 || name.indexOf("OSCAR") != -1 || name.indexOf("XW-") != -1) {
        startSequence(SEQ_MORSE_HAM, sizeof(SEQ_MORSE_HAM) / sizeof(SEQ_MORSE_HAM[0]));
    } else if (noradId == 25544 || noradId == 48274 || icon == ICON_STATION) {
        startSequence(SEQ_STATION_QUINDAR, sizeof(SEQ_STATION_QUINDAR) / sizeof(SEQ_STATION_QUINDAR[0]));
    } else {
        startSequence(SEQ_CHIRP, sizeof(SEQ_CHIRP) / sizeof(SEQ_CHIRP[0]));
    }
}

void SpaceAudio::stop() {
    if (s_isPlaying) {
        M5Cardputer.Speaker.stop();
        s_isPlaying = false;
        s_currentSequence = nullptr;
        s_sequenceLength = 0;
        s_currentStep = 0;
    }
}

bool SpaceAudio::isPlaying() {
    return s_isPlaying;
}

void SpaceAudio::update() {
    if (!s_isPlaying || !s_currentSequence) return;

    uint32_t now = millis();
    if (now >= s_stepEndTime) {
        s_currentStep++;
        if (s_currentStep >= s_sequenceLength) {
            // 播放完毕，自动静音待命
            stop();
            return;
        }

        const ToneNote& note = s_currentSequence[s_currentStep];
        if (note.freq > 0) {
            M5Cardputer.Speaker.tone(note.freq, note.duration);
        } else {
            M5Cardputer.Speaker.stop();
        }
        s_stepEndTime = now + note.duration;
    }
}
