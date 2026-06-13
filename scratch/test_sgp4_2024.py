from sgp4.api import Satrec, jday
import math

# Using the OLD 2024 TLE
s = '1 25544U 98067A   24162.77259259  .00014761  00000-0  26658-3 0  9997'
t = '2 25544  51.6406  69.4140 0004584  57.8184  83.8966 15.49887754457636'
satellite = Satrec.twoline2rv(s, t)

viewLat = 22.85
viewLon = 108.33
WGS84_A = 6378.137
WGS84_E2 = 0.00669437999014

# June 14, 2024
# UTC time: June 13, 2024, 21:35 to 21:45 UTC
unix_start = 1718314500

for i in range(0, 10*60, 10):
    unix = unix_start + i
    jd = (unix / 86400.0) + 2440587.5
    fr = jd - int(jd)
    e, r, v = satellite.sgp4(int(jd), fr)
    
    jd0 = math.floor(jd + 0.5) - 0.5
    T0 = (jd0 - 2451545.0) / 36525.0
    gmst_0h_sec = 24110.54841 + 8640184.812866 * T0 + 0.093104 * T0 * T0 - 6.2e-6 * T0 * T0 * T0
    gmst_sec = gmst_0h_sec + (jd - jd0) * 86400.0 * 1.002737909350795
    gmst_rad = (gmst_sec % 86400.0) * 2.0 * math.pi / 86400.0
    
    ecef_x = r[0] * math.cos(gmst_rad) + r[1] * math.sin(gmst_rad)
    ecef_y = r[0] * -math.sin(gmst_rad) + r[1] * math.cos(gmst_rad)
    ecef_z = r[2]
    
    r_delta = math.sqrt(ecef_x**2 + ecef_y**2)
    lon = math.atan2(ecef_y, ecef_x)
    lat = math.atan2(ecef_z, r_delta * (1.0 - WGS84_E2))
    alt = 0.0
    for _ in range(5):
        N = WGS84_A / math.sqrt(1.0 - WGS84_E2 * math.sin(lat)**2)
        alt = r_delta / math.cos(lat) - N
        lat = math.atan2(ecef_z, r_delta * (1.0 - WGS84_E2 * (N / (N + alt))))
        
    lat_deg, lon_deg = math.degrees(lat), math.degrees(lon)
    
    # Calculate elevation
    import numpy as np
    uLatR, uLonR = math.radians(viewLat), math.radians(viewLon)
    u_N = WGS84_A / math.sqrt(1.0 - WGS84_E2 * math.sin(uLatR)**2)
    ux = (u_N + 0) * math.cos(uLatR) * math.cos(uLonR)
    uy = (u_N + 0) * math.cos(uLatR) * math.sin(uLonR)
    uz = (u_N * (1 - WGS84_E2) + 0) * math.sin(uLatR)
    
    dx, dy, dz = ecef_x - ux, ecef_y - uy, ecef_z - uz
    
    sinL, cosL = math.sin(uLatR), math.cos(uLatR)
    sino, coso = math.sin(uLonR), math.cos(uLonR)
    
    xNorth = -sinL * coso * dx - sinL * sino * dy + cosL * dz
    yEast = -sino * dx + coso * dy
    zUp = cosL * coso * dx + cosL * sino * dy + sinL * dz
    
    el = math.degrees(math.atan2(zUp, math.sqrt(xNorth**2 + yEast**2)))
    
    time_str = f"{21 + (35 + i//60)//60 % 24:02d}:{(35 + i//60)%60:02d}:{i%60:02d} UTC"
    if el > 0:
        print(f"{time_str} | Lat: {lat_deg:.2f}, Lon: {lon_deg:.2f}, El: {el:.2f}")

