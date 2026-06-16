import math

def project(y, z, pitch):
    pitchRad = math.radians(pitch)
    z_pitched = y * math.sin(pitchRad) + z * math.cos(pitchRad)
    y_pitched = y * math.cos(pitchRad) - z * math.sin(pitchRad)
    return z_pitched, y_pitched

r = 137
# Center satellite
print("Center Sat (pitch=45):", project(0, r, 45))
# North pole
print("North Pole (pitch=45):", project(r, 0, 45))
# South pole
print("South Pole (pitch=45):", project(-r, 0, 45))
