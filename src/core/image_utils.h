#ifndef IMAGE_UTILS_H
#define IMAGE_UTILS_H

#include <Arduino.h>
#include <M5GFX.h>

class EarthRenderer; // 前置声明

class ImageUtils {
public:
    // Base64 编码辅助函数
    static String base64Encode(const uint8_t* data, size_t len);

    // 对 Sprite 画布应用夜视红光滤镜
    static void applyNightVisionFilter(LGFX_Sprite* canvas);

    // 根据 EarthRenderer 当前的视觉模式推送带滤镜的 Canvas
    static void pushCanvasWithFilter(EarthRenderer* earthRenderer);

    // 截屏并通过串口输出 Base64 编码的 RGB565 图像
    static void captureScreenshot(EarthRenderer* earthRenderer);
};

#endif // IMAGE_UTILS_H
