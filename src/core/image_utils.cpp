#include "image_utils.h"
#include "earth_renderer.h"

static const char b64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

String ImageUtils::base64Encode(const uint8_t* data, size_t len) {
    String ret;
    ret.reserve((len * 4 / 3) + 4);
    int i = 0;
    uint8_t a3[3], a4[4];
    while (len--) {
        a3[i++] = *(data++);
        if (i == 3) {
            a4[0] = (a3[0] & 0xfc) >> 2;
            a4[1] = ((a3[0] & 0x03) << 4) + ((a3[1] & 0xf0) >> 4);
            a4[2] = ((a3[1] & 0x0f) << 2) + ((a3[2] & 0xc0) >> 6);
            a4[3] = a3[2] & 0x3f;
            for (i = 0; i < 4; i++) ret += b64_chars[a4[i]];
            i = 0;
        }
    }
    if (i) {
        for (int j = i; j < 3; j++) a3[j] = '\0';
        a4[0] = (a3[0] & 0xfc) >> 2;
        a4[1] = ((a3[0] & 0x03) << 4) + ((a3[1] & 0xf0) >> 4);
        a4[2] = ((a3[1] & 0x0f) << 2) + ((a3[2] & 0xc0) >> 6);
        a4[3] = a3[2] & 0x3f;
        for (int j = 0; j < i + 1; j++) ret += b64_chars[a4[j]];
        while (i++ < 3) ret += '=';
    }
    return ret;
}

void ImageUtils::applyNightVisionFilter(LGFX_Sprite* canvas) {
    if (!canvas) return;
    int w = canvas->width();
    int h = canvas->height();
    uint16_t* buf = (uint16_t*)canvas->getBuffer();
    if (!buf) return;
    
    int totalPixels = w * h;
    for (int i = 0; i < totalPixels; i++) {
        uint16_t rawCol = buf[i];
        // Byte swap from Big Endian (display format) to Little Endian (CPU format)
        uint16_t pixel = (rawCol >> 8) | (rawCol << 8);
        
        // Extract RGB565 channels
        uint16_t r = (pixel >> 11) & 0x1F;
        uint16_t g = (pixel >> 5) & 0x3F;
        uint16_t b = pixel & 0x1F;
        
        // Calculate average brightness
        uint16_t gray = (r * 3 + (g >> 1) * 6 + b) / 10;
        if (gray > 0x1F) gray = 0x1F;
        
        // Form new pixel (Red channel only)
        uint16_t new_pixel = (gray << 11);
        
        // Byte swap back to Big Endian
        buf[i] = (new_pixel >> 8) | (new_pixel << 8);
    }
}

void ImageUtils::pushCanvasWithFilter(EarthRenderer* earthRenderer) {
    if (!earthRenderer) return;
    LGFX_Sprite* canvas = earthRenderer->getCanvas();
    if (!canvas) return;
    if (earthRenderer->getVisualMode() == 1) {
        applyNightVisionFilter(canvas);
    }
    canvas->pushSprite(0, 0);
}

void ImageUtils::captureScreenshot(EarthRenderer* earthRenderer) {
    log_i("[Screenshot] Capturing screen...");
    if (!earthRenderer || !earthRenderer->getCanvas()) {
        log_e("[Screenshot] Canvas not ready!");
        return;
    }
    auto* canvas = earthRenderer->getCanvas();
    int w = canvas->width();
    int h = canvas->height();
    
    // Use log_i for markers (same output channel as other logs)
    log_i("==SKYCOMPASS_RAW_START==%d,%d", w, h);
    delay(10); // Let marker flush
    
    const uint8_t* buf = (const uint8_t*)canvas->getBuffer();
    size_t total = w * h * 2; // RGB565 = 2 bytes per pixel
    size_t offset = 0;
    const size_t CHUNK = 768; // Must be multiple of 3 for clean Base64
    while (offset < total) {
        size_t n = (total - offset > CHUNK) ? CHUNK : (total - offset);
        String b64 = base64Encode(buf + offset, n);
        log_i("==SKYCOMPASS_DATA==%s", b64.c_str());
        offset += n;
        delay(15); // Add delay to prevent serial transmit buffer overflow
    }
    log_i("==SKYCOMPASS_RAW_END==");
    log_i("[Screenshot] Done. Sent %d bytes raw RGB565 (%dx%d)", total, w, h);
}
