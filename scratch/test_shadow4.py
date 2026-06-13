from sgp4.api import Satrec, jday
import math

s = '1 25544U 98067A   24162.77259259  .00014761  00000-0  26658-3 0  9997'
t = '2 25544  51.6406  69.4140 0004584  57.8184  83.8966 15.49887754457636'

satellite = Satrec.twoline2rv(s, t)

unix = 1781386840 # 2026-06-13 21:40:40 UTC

jd = (unix / 86400.0) + 2440587.5
fr = jd - int(jd)
jd_int = int(jd)

e, r, v = satellite.sgp4(jd_int, fr)
print("SGP4 TEME x,y,z:", r)

# Get GMST
jd0 = math.floor(jd + 0.5) - 0.5
T0 = (jd0 - 2451545.0) / 36525.0
gmst_0h_sec = 24110.54841 + 8640184.812866 * T0 + 0.093104 * T0 * T0 - 6.2e-6 * T0 * T0 * T0
time_of_day_sec = (jd - jd0) * 86400.0
gmst_sec = gmst_0h_sec + time_of_day_sec * 1.002737909350795
gmst_rad = (gmst_sec % 86400.0) * 2.0 * math.pi / 86400.0

ecef_x = r[0] * math.cos(gmst_rad) + r[1] * math.sin(gmst_rad)
ecef_y = r[0] * -math.sin(gmst_rad) + r[1] * math.cos(gmst_rad)
ecef_z = r[2]

print("ECEF x,y,z:", ecef_x, ecef_y, ecef_z)

r_delta = math.sqrt(ecef_x**2 + ecef_y**2)
lon = math.atan2(ecef_y, ecef_x)
WGS84_A = 6378.137
WGS84_E2 = 0.00669437999014
lat = math.atan2(ecef_z, r_delta * (1.0 - WGS84_E2))
N = WGS84_A
alt = 0.0

for _ in range(5):
    sin_lat = math.sin(lat)
    N = WGS84_A / math.sqrt(1.0 - WGS84_E2 * sin_lat * sin_lat)
    alt = r_delta / math.cos(lat) - N
    lat = math.atan2(ecef_z, r_delta * (1.0 - WGS84_E2 * (N / (N + alt))))

lat_deg = math.degrees(lat)
lon_deg = math.degrees(lon)
print(f"Lat: {lat_deg}, Lon: {lon_deg}, Alt: {alt}")
