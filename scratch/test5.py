import math

_zoom = 2.5
_earthRadius = 150.0
alt = 400.0
_cameraPitch = -45.0

visualAlt = min(alt, 20000.0)
r = _earthRadius + math.sqrt(visualAlt) * 0.4 * _zoom
_cameraFocusR = r

y = 0.0
z = r

pitchRad = _cameraPitch * 0.017453292519943295769236907684886

z_pitched = y * math.sin(pitchRad) + z * math.cos(pitchRad)
y_pitched = y * math.cos(pitchRad) - z * math.sin(pitchRad)

anchorBlend = max(0.0, min(1.0, (_zoom - 1.0) / 1.5))
focusOffset = _cameraFocusR * math.sin(pitchRad) * anchorBlend
y_pitched += focusOffset

print(f"y_pitched: {y_pitched}")
print(f"z_pitched: {z_pitched}")
print(f"focusOffset: {focusOffset}")
