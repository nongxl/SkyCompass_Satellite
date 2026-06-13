import math

unix_time = 1781386830
viewLat = 22.85
viewLon = 108.33

def get_julian(unix):
    return (unix / 86400.0) + 2440587.5

def get_sun(unix):
    jd = get_julian(unix)
    n = jd - 2451545.0
    L = (280.460 + 0.9856474 * n) % 360.0
    g = (357.528 + 0.9856003 * n) % 360.0
    
    L_rad = math.radians(L)
    g_rad = math.radians(g)
    
    ecliptic_lon = L + 1.915 * math.sin(g_rad) + 0.020 * math.sin(2 * g_rad)
    ecliptic_lon_rad = math.radians(ecliptic_lon)
    
    obliquity = 23.439 - 0.0000004 * n
    obliquity_rad = math.radians(obliquity)
    
    ra_rad = math.atan2(math.cos(obliquity_rad) * math.sin(ecliptic_lon_rad), math.cos(ecliptic_lon_rad))
    dec_rad = math.asin(math.sin(obliquity_rad) * math.sin(ecliptic_lon_rad))
    
    jd0 = math.floor(jd + 0.5) - 0.5
    T0 = (jd0 - 2451545.0) / 36525.0
    gmst_0h_sec = 24110.54841 + 8640184.812866 * T0 + 0.093104 * T0 * T0 - 6.2e-6 * T0 * T0 * T0
    time_of_day_sec = (jd - jd0) * 86400.0
    gmst_sec = gmst_0h_sec + time_of_day_sec * 1.002737909350795
    gmst_rad = (gmst_sec % 86400.0) * 2.0 * math.pi / 86400.0
    
    subsolarLon_rad = ra_rad - gmst_rad
    if subsolarLon_rad > math.pi: subsolarLon_rad -= 2 * math.pi
    if subsolarLon_rad < -math.pi: subsolarLon_rad += 2 * math.pi
    
    return math.degrees(dec_rad), math.degrees(subsolarLon_rad)

sunLat, sunLon = get_sun(unix_time)

uLatR = math.radians(viewLat)
uLonR = math.radians(viewLon)
subLatR = math.radians(sunLat)
subLonR = math.radians(sunLon)

cos_dist = math.sin(uLatR)*math.sin(subLatR) + math.cos(uLatR)*math.cos(subLatR)*math.cos(uLonR - subLonR)
sun_alt = math.asin(cos_dist) * 180.0 / math.pi

print(f"Sun Lat: {sunLat}, Sun Lon: {sunLon}")
print(f"Sun Altitude at Nanning: {sun_alt}")
