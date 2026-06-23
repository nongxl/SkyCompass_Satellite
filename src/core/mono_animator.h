/*
 * SPDX-FileCopyrightText: 2026 Antigravity Agent
 * SPDX-License-Identifier: MIT
 */

#ifndef _MONO_ANIMATOR_H_
#define _MONO_ANIMATOR_H_

#include <Arduino.h>

/**
 * @brief 8x8 LED screen visualization states
 */
enum MonoVisualState {
    MONO_VIS_IDLE,
    MONO_VIS_GPS_SEARCHING,
    MONO_VIS_WIFI_CONNECTING,
    MONO_VIS_ORBIT_CALCULATING,
    MONO_VIS_DATA_UPDATING,
    MONO_VIS_READY
};

// Global volatile state tracking flags, updated by network, predictor, and GNSS modules
extern volatile bool g_wifiConnecting;
extern volatile bool g_dataUpdating;
extern volatile bool g_orbitCalculating;
extern volatile uint32_t g_readyStartTime;

/**
 * @brief Initialize state flags for the mono animator
 */
void initMonoAnimator();

/**
 * @brief Draw the real-time mathematical animation into the 8-byte frame buffer
 *        using 4-bit grayscale and space-temporal dithering.
 * 
 * @param buffer Reference to the 8-byte display buffer of Chain Mono.
 */
void drawMonoVisualAnimation(uint8_t* buffer);

#endif // _MONO_ANIMATOR_H_
