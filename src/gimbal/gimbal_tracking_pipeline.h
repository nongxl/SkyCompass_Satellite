#ifndef GIMBAL_TRACKING_PIPELINE_H
#define GIMBAL_TRACKING_PIPELINE_H

#include <Arduino.h>

class GimbalTrackingPipeline {
public:
    static void update(uint32_t currentSimTime);
};

#endif // GIMBAL_TRACKING_PIPELINE_H
