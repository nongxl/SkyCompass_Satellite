from sgp4.api import Satrec, jday
import math

s = '1 25544U 98067A   26163.80312907  .00008495  00000+0  16106-3 0  9990'
t = '2 25544  51.6335 321.7912 0004900 178.5917 181.5086 15.49196054571074'
satellite = Satrec.twoline2rv(s, t)

unix_start = 1781386500

for i in range(0, 15*60, 30):
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
    lat = math.atan2(ecef_z, r_delta * (1.0 - 0.00669437999014))
    alt = 0.0
    for _ in range(5):
        N = 6378.137 / math.sqrt(1.0 - 0.00669437999014 * math.sin(lat)**2)
        alt = r_delta / math.cos(lat) - N
        lat = math.atan2(ecef_z, r_delta * (1.0 - 0.00669437999014 * (N / (N + alt))))
        
    lat_deg, lon_deg = math.degrees(lat), math.degrees(math.atan2(ecef_y, ecef_x))
    
    n = jd - 2451545.0
    L = (280.460 + 0.9856474 * n) % 360.0
    g = (357.528 + 0.9856003 * n) % 360.0
    L_rad, g_rad = math.radians(L), math.radians(g)
    ecliptic_lon = L + 1.915 * math.sin(g_rad) + 0.020 * math.sin(2 * g_rad)
    ecliptic_lon_rad = math.radians(ecliptic_lon)
    obliquity = 23.439 - 0.0000004 * n
    obliquity_rad = math.radians(obliquity)
    ra_rad = math.atan2(math.cos(obliquity_rad) * math.sin(ecliptic_lon_rad), math.cos(ecliptic_lon_rad))
    dec_rad = math.asin(math.sin(obliquity_rad) * math.sin(ecliptic_lon_rad))
    subsolarLon_rad = ra_rad - gmst_rad
    if subsolarLon_rad > math.pi: subsolarLon_rad -= 2 * math.pi
    if subsolarLon_rad < -math.pi: subsolarLon_rad += 2 * math.pi
    
    sunLat, sunLon = math.degrees(dec_rad), math.degrees(subsolarLon_rad)
    
    latR, lonR = math.radians(lat_deg), math.radians(lon_deg)
    subLatR, subLonR = math.radians(sunLat), math.radians(sunLon)
    cos_theta = math.sin(subLatR)*math.sin(latR) + math.cos(subLatR)*math.cos(latR)*math.cos(lonR - subLonR)
    
    inShadow = False
    if cos_theta < 0:
        dist_sq = (6371.0 + alt)**2 * (1.0 - cos_theta**2)
        inShadow = dist_sq < 6371.0**2
        
    time_str = f"{21 + (35 + i//60)//60 % 24:02d}:{(35 + i//60)%60:02d}:{i%60:02d} UTC"
    print(f"{time_str} | Lat: {lat_deg:.2f}, Lon: {lon_deg:.2f}, Shadow: {inShadow}")

