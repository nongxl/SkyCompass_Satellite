#include <iostream>
#include <cmath>

int main() {
    float _zoom = 2.5f;
    float _earthRadius = 150.0f;
    float alt = 400.0f;
    float _cameraPitch = -45.0f;
    
    float visualAlt = alt;
    float r = _earthRadius + sqrtf(visualAlt) * 0.4f * _zoom;
    
    float _cameraFocusR = r; // assuming exact match
    
    float y = 0;
    float z = r;
    
    float pitchRad = _cameraPitch * 0.017453292519943295769236907684886f;
    
    float z_pitched = y * sinf(pitchRad) + z * cosf(pitchRad);
    float y_pitched = y * cosf(pitchRad) - z * sinf(pitchRad);
    
    float anchorBlend = (_zoom - 1.0f) / 1.5f;
    if (anchorBlend < 0.0f) anchorBlend = 0.0f;
    if (anchorBlend > 1.0f) anchorBlend = 1.0f;
    
    float focusOffset = _cameraFocusR * sinf(pitchRad) * anchorBlend;
    y_pitched += focusOffset;
    
    std::cout << "y_pitched: " << y_pitched << std::endl;
    std::cout << "z_pitched: " << z_pitched << std::endl;
    std::cout << "focusOffset: " << focusOffset << std::endl;
    return 0;
}
