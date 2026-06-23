/*
 * SPDX-FileCopyrightText: 2026 Antigravity Agent
 * SPDX-License-Identifier: MIT
 */

#include "mono_animator.h"
#include "hal/hal_gnss.h"
#include <math.h>

volatile bool g_wifiConnecting = false;
volatile bool g_dataUpdating = false;
volatile bool g_orbitCalculating = false;
volatile uint32_t g_readyStartTime = 0;

void initMonoAnimator() {
    g_wifiConnecting = false;
    g_dataUpdating = false;
    g_orbitCalculating = false;
    g_readyStartTime = 0;
}

void drawMonoVisualAnimation(uint8_t* buffer) {
    extern HalGnss* gnss;
    extern bool gnssLocationFixed;
    
    MonoVisualState visState = MONO_VIS_IDLE;
    uint32_t now = millis();
    
    if (g_readyStartTime > 0 && (now - g_readyStartTime < 2000)) {
        visState = MONO_VIS_READY;
    } else if (g_dataUpdating) {
        visState = MONO_VIS_DATA_UPDATING;
    } else if (g_orbitCalculating) {
        visState = MONO_VIS_ORBIT_CALCULATING;
    } else if (g_wifiConnecting) {
        visState = MONO_VIS_WIFI_CONNECTING;
    } else if (gnss && !gnss->isInStandbyMode() && !gnssLocationFixed) {
        visState = MONO_VIS_GPS_SEARCHING;
    }
    
    memset(buffer, 0, 8);
    
    switch (visState) {
        case MONO_VIS_IDLE: {
            // 心脏呼吸状态：平稳且节奏分明的同心方圈呼吸
            // 周期：1200ms
            uint32_t t_mod = now % 1200;
            
            for (int r = 0; r < 8; r++) {
                float dy = r - 3.5f;
                for (int c = 0; c < 8; c++) {
                    float dx = c - 3.5f;
                    float dist = fmaxf(fabsf(dx), fabsf(dy));
                    
                    // R0 (中心 2x2) IDLE 期间恒亮
                    if (dist < 1.0f) {
                        buffer[r] |= (1 << (7 - c));
                    }
                    // R1 (中内环 4x4) 在第 2、3 阶段亮起 (400 - 1200ms)
                    else if (dist >= 1.0f && dist < 2.0f) {
                        if (t_mod >= 400) {
                            buffer[r] |= (1 << (7 - c));
                        }
                    }
                    // R2 环的 4 向十字对齐点在第 3 阶段高峰亮起 (800 - 1200ms)
                    else if (dist >= 2.0f && dist < 3.0f) {
                        if (t_mod >= 800) {
                            // Row 1: (1,3), (1,4)
                            // Row 6: (6,3), (6,4)
                            // Col 1: (3,1), (4,1)
                            // Col 6: (3,6), (4,6)
                            if ((r == 1 && (c == 3 || c == 4)) ||
                                (r == 6 && (c == 3 || c == 4)) ||
                                (c == 1 && (r == 3 || r == 4)) ||
                                (c == 6 && (r == 3 || r == 4))) {
                                buffer[r] |= (1 << (7 - c));
                            }
                        }
                    }
                }
            }
            break;
        }
        case MONO_VIS_GPS_SEARCHING: {
            // GPS 搜频雷达：同心方圈由内向外依次单独点亮扩散，形成水波
            // 周期：800ms，每 200ms 依次点亮 R0, R1, R2, R3
            uint32_t phase = (now % 800) / 200;
            
            for (int r = 0; r < 8; r++) {
                float dy = r - 3.5f;
                for (int c = 0; c < 8; c++) {
                    float dx = c - 3.5f;
                    float dist = fmaxf(fabsf(dx), fabsf(dy));
                    
                    // 基于切比雪夫距离直接转换获取环索引 (0, 1, 2, 3)
                    int ringIdx = (int)(dist + 0.01f);
                    if (ringIdx == (int)phase) {
                        buffer[r] |= (1 << (7 - c));
                    }
                }
            }
            break;
        }
        case MONO_VIS_WIFI_CONNECTING: {
            // WiFi 连接状态：经典 WiFi 扇形电波弧由下而上层层递进点亮
            // 周期：800ms，每 200ms 增加一层
            uint32_t phase = (now % 800) / 200;
            
            for (int r = 0; r < 8; r++) {
                for (int c = 0; c < 8; c++) {
                    // 第 0 层：发射底点 (3,7), (4,7)
                    if (r == 7 && (c == 3 || c == 4)) {
                        buffer[r] |= (1 << (7 - c));
                    }
                    // 第 1 层：第一道弧
                    else if (r == 5 && (c >= 2 && c <= 5) && phase >= 1) {
                        buffer[r] |= (1 << (7 - c));
                    }
                    // 第 2 层：第二道弧
                    else if (r == 3 && (c >= 1 && c <= 6) && phase >= 2) {
                        buffer[r] |= (1 << (7 - c));
                    }
                    // 第 3 层：第三道弧
                    else if (r == 1 && (c >= 0 && c <= 7) && phase >= 3) {
                        buffer[r] |= (1 << (7 - c));
                    }
                }
            }
            break;
        }
        case MONO_VIS_ORBIT_CALCULATING: {
            // 轨道计算：中心 2x2 恒亮（恒星），外圈一个 2x2 的“卫星”大方块顺时针平稳绕行
            // 周期：1200ms，每 150ms 移动一格，共 8 个离散位置
            uint32_t step = (now % 1200) / 150;
            
            // 绘制中心 2x2 (R0)
            for (int r = 3; r <= 4; r++) {
                for (int c = 3; c <= 4; c++) {
                    buffer[r] |= (1 << (7 - c));
                }
            }
            
            // 绘制外圈 2x2 卫星方块在不同轨道的投影
            int sat_r_start = 0, sat_r_end = 0;
            int sat_c_start = 0, sat_c_end = 0;
            
            switch (step) {
                case 0: sat_r_start = 0; sat_r_end = 1; sat_c_start = 0; sat_c_end = 1; break; // 左上角
                case 1: sat_r_start = 0; sat_r_end = 1; sat_c_start = 3; sat_c_end = 4; break; // 中上
                case 2: sat_r_start = 0; sat_r_end = 1; sat_c_start = 6; sat_c_end = 7; break; // 右上角
                case 3: sat_r_start = 3; sat_r_end = 4; sat_c_start = 6; sat_c_end = 7; break; // 中右
                case 4: sat_r_start = 6; sat_r_end = 7; sat_c_start = 6; sat_c_end = 7; break; // 右下角
                case 5: sat_r_start = 6; sat_r_end = 7; sat_c_start = 3; sat_c_end = 4; break; // 中下
                case 6: sat_r_start = 6; sat_r_end = 7; sat_c_start = 0; sat_c_end = 1; break; // 左下角
                case 7: sat_r_start = 3; sat_r_end = 4; sat_c_start = 0; sat_c_end = 1; break; // 中左
            }
            
            for (int r = sat_r_start; r <= sat_r_end; r++) {
                for (int c = sat_c_start; c <= sat_c_end; c++) {
                    buffer[r] |= (1 << (7 - c));
                }
            }
            break;
        }
        case MONO_VIS_DATA_UPDATING: {
            // 数据更新：同心方圈由外向内依次点亮收缩，象征数据被吸入或写入中心
            // 周期：800ms，每 200ms 收缩一圈 (R3 -> R2 -> R1 -> R0)
            uint32_t phase = (now % 800) / 200;
            
            for (int r = 0; r < 8; r++) {
                float dy = r - 3.5f;
                for (int c = 0; c < 8; c++) {
                    float dx = c - 3.5f;
                    float dist = fmaxf(fabsf(dx), fabsf(dy));
                    
                    int ringIdx = (int)(dist + 0.01f);
                    if (ringIdx == (3 - (int)phase)) {
                        buffer[r] |= (1 << (7 - c));
                    }
                }
            }
            break;
        }
        case MONO_VIS_READY: {
            // 准备就绪：前 400ms 超快向心凝聚，后 1.6 秒发生极高对比度的 100ms 周期全屏黑白爆闪
            uint32_t elapsed = now - g_readyStartTime;
            
            if (elapsed < 400) {
                // 超快向内收缩阶段 (0-400ms)：每 100ms 收缩一圈 (R3 -> R0)
                uint32_t phase = elapsed / 100;
                for (int r = 0; r < 8; r++) {
                    float dy = r - 3.5f;
                    for (int c = 0; c < 8; c++) {
                        float dx = c - 3.5f;
                        float dist = fmaxf(fabsf(dx), fabsf(dy));
                        int ringIdx = (int)(dist + 0.01f);
                        if (ringIdx == (3 - (int)phase)) {
                            buffer[r] |= (1 << (7 - c));
                        }
                    }
                }
            } else {
                // 全白与全灭交替高频爆闪阶段 (400-2000ms)：
                // 每 100ms 进行一次翻转，视觉冲击力极强
                uint32_t flashPhase = (elapsed - 400) / 100;
                if (flashPhase % 2 == 0) {
                    memset(buffer, 0xFF, 8); // 全白点亮
                } else {
                    memset(buffer, 0x00, 8); // 全黑熄灭
                }
            }
            break;
        }
    }
}
