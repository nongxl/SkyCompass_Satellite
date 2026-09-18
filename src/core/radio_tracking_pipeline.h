#ifndef RADIO_TRACKING_PIPELINE_H
#define RADIO_TRACKING_PIPELINE_H

#include <Arduino.h>

class RadioTrackingPipeline {
public:
    static void update(uint32_t currentSimTime, int32_t tmOffset);
};

#endif // RADIO_TRACKING_PIPELINE_H
