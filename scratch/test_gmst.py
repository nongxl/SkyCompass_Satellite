import math
unix = 1781222400
jd = (unix / 86400.0) + 2440587.5
print("JD:", jd)

def getGMST_wrong(julian_date):
    T = (julian_date - 2451545.0) / 36525.0
    gmst_sec = 24110.54841 + 8640184.812866 * T + 0.093104 * T * T - 6.2e-6 * T * T * T
    gmst_rad = (gmst_sec % 86400.0) * 2.0 * math.pi / 86400.0
    return gmst_rad

def getGMST_correct(julian_date):
    jd0 = math.floor(julian_date + 0.5) - 0.5
    T0 = (jd0 - 2451545.0) / 36525.0
    gmst_0h_sec = 24110.54841 + 8640184.812866 * T0 + 0.093104 * T0 * T0 - 6.2e-6 * T0 * T0 * T0
    time_of_day_sec = (julian_date - jd0) * 86400.0
    gmst_sec = gmst_0h_sec + time_of_day_sec * 1.002737909350795
    gmst_rad = (gmst_sec % 86400.0) * 2.0 * math.pi / 86400.0
    return gmst_rad

print("Wrong GMST:", getGMST_wrong(jd))
print("Correct GMST:", getGMST_correct(jd))

unix2 = 1781222400 + 3600 * 14 # 14 hours later
jd2 = (unix2 / 86400.0) + 2440587.5
print("Wrong GMST at +14h:", getGMST_wrong(jd2))
print("Correct GMST at +14h:", getGMST_correct(jd2))
