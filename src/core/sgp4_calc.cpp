#include "sgp4_calc.h"

SGP4Calc::SGP4Calc() {
}

SGP4Calc::~SGP4Calc() {
}

SGP4Calc::SGP4Calc(const SGP4Calc& other) {
    extern portMUX_TYPE satMutex;
    portENTER_CRITICAL(&satMutex);
    sat.satrec = other.sat.satrec;
    portEXIT_CRITICAL(&satMutex);
}

SGP4Calc& SGP4Calc::operator=(const SGP4Calc& other) {
    if (this != &other) {
        extern portMUX_TYPE satMutex;
        portENTER_CRITICAL(&satMutex);
        sat.satrec = other.sat.satrec;
        portEXIT_CRITICAL(&satMutex);
    }
    return *this;
}

bool SGP4Calc::init(const TLEData& tle) {
    char name[30];
    char line1[80];
    char line2[80];
    tle.name.toCharArray(name, sizeof(name));
    tle.line1.toCharArray(line1, sizeof(line1));
    tle.line2.toCharArray(line2, sizeof(line2));
    
    return sat.init(name, line1, line2);
}

bool SGP4Calc::getTEME(uint32_t unix_ts, double& x, double& y, double& z) {
    double jd = CoordTransform::unixToJulian(unix_ts);
    
    extern portMUX_TYPE satMutex;
    portENTER_CRITICAL(&satMutex);
    elsetrec temp_satrec = sat.satrec;
    portEXIT_CRITICAL(&satMutex);
    
    double tsince = (jd - temp_satrec.jdsatepoch) * 24.0 * 60.0;
    
    double ro[3];
    double vo[3];
    
    bool success = sgp4(wgs72, temp_satrec, tsince, ro, vo);
    
    if (success) {
        x = ro[0];
        y = ro[1];
        z = ro[2];
        return true;
    }
    return false;
}

bool SGP4Calc::init(const OrbitRecord& record) {
    String l1 = "";
    String l2 = "";
    buildPseudoTle(record, l1, l2);
    
    char name[30];
    char line1[80];
    char line2[80];
    record.name.toCharArray(name, sizeof(name));
    l1.toCharArray(line1, sizeof(line1));
    l2.toCharArray(line2, sizeof(line2));
    
    return sat.init(name, line1, line2);
}

void SGP4Calc::buildPseudoTle(const OrbitRecord& record, String& outL1, String& outL2) {
    uint32_t pseudoCat = record.catalogNumber % 100000;
    
    String desig = formatIntlDesig(record.internationalDesignator);
    String bstarStr = formatBStar(record.bstar);
    
    // Safety clamp double inputs to reasonable ranges to prevent formatting buffer overflow
    double inclination = record.inclination;
    if (std::isnan(inclination) || std::isinf(inclination)) inclination = 0.0;
    else if (inclination < -360.0 || inclination > 360.0) inclination = fmod(inclination, 360.0);

    double raan = record.raan;
    if (std::isnan(raan) || std::isinf(raan)) raan = 0.0;
    else if (raan < -360.0 || raan > 360.0) raan = fmod(raan, 360.0);

    double argumentOfPerigee = record.argumentOfPerigee;
    if (std::isnan(argumentOfPerigee) || std::isinf(argumentOfPerigee)) argumentOfPerigee = 0.0;
    else if (argumentOfPerigee < -360.0 || argumentOfPerigee > 360.0) argumentOfPerigee = fmod(argumentOfPerigee, 360.0);

    double meanAnomaly = record.meanAnomaly;
    if (std::isnan(meanAnomaly) || std::isinf(meanAnomaly)) meanAnomaly = 0.0;
    else if (meanAnomaly < -360.0 || meanAnomaly > 360.0) meanAnomaly = fmod(meanAnomaly, 360.0);

    double meanMotion = record.meanMotion;
    if (std::isnan(meanMotion) || std::isinf(meanMotion) || meanMotion < 0.0) meanMotion = 0.0;
    else if (meanMotion > 99.99999999) meanMotion = 99.99999999;

    double epochDays = record.epochDays;
    if (std::isnan(epochDays) || std::isinf(epochDays) || epochDays < 0.0) epochDays = 1.0;
    else if (epochDays > 367.0) epochDays = 367.0;

    int epochYr = record.epochYr;
    if (epochYr < 0 || epochYr > 99) epochYr = 26;

    char l1_buf[128];
    snprintf(l1_buf, sizeof(l1_buf), "1 %05uU %-8.8s %02d%12.8f  .00000000  00000-0 %s 0  999",
            (unsigned int)pseudoCat,
            desig.c_str(),
            epochYr,
            epochDays,
            bstarStr.c_str());
            
    String l1 = String(l1_buf);
    while (l1.length() < 68) l1 += " ";
    if (l1.length() > 68) l1 = l1.substring(0, 68);
    l1 += String(calculateTleChecksum(l1));
    outL1 = l1;
    
    char l2_buf[128];
    long pseudoEcc = 0;
    if (!std::isnan(record.eccentricity) && !std::isinf(record.eccentricity)) {
        pseudoEcc = round(record.eccentricity * 10000000.0);
    }
    if (pseudoEcc > 9999999) pseudoEcc = 9999999;
    if (pseudoEcc < 0) pseudoEcc = 0;
    
    snprintf(l2_buf, sizeof(l2_buf), "2 %05u %8.4f %8.4f %07ld %8.4f %8.4f %11.8f00000",
            (unsigned int)pseudoCat,
            inclination,
            raan,
            pseudoEcc,
            argumentOfPerigee,
            meanAnomaly,
            meanMotion);
            
    String l2 = String(l2_buf);
    while (l2.length() < 68) l2 += " ";
    if (l2.length() > 68) l2 = l2.substring(0, 68);
    l2 += String(calculateTleChecksum(l2));
    outL2 = l2;
}

String SGP4Calc::formatBStar(double bstar) {
    if (std::isnan(bstar) || std::isinf(bstar) || abs(bstar) < 1e-9) {
        return " 00000-0";
    }
    int exponent = 0;
    double mantissa = bstar;
    if (abs(mantissa) >= 1.0) {
        while (abs(mantissa) >= 1.0 && exponent < 99) {
            mantissa /= 10.0;
            exponent++;
        }
    } else {
        while (abs(mantissa) < 0.1 && exponent > -99) {
            mantissa *= 10.0;
            exponent--;
        }
    }
    long val = round(mantissa * 100000.0);
    if (abs(val) >= 100000) {
        val /= 10;
        exponent++;
    }
    char buf[16];
    char valSign = (val < 0) ? '-' : ' ';
    char expSign = (exponent < 0) ? '-' : '+';
    snprintf(buf, sizeof(buf), "%c%05ld%c%d", valSign, abs(val), expSign, abs(exponent));
    return String(buf);
}

String SGP4Calc::formatIntlDesig(const String& raw) {
    String clean = raw;
    clean.trim();
    if (clean.length() >= 9 && clean[4] == '-') {
        clean = clean.substring(2, 4) + clean.substring(5);
    }
    while (clean.length() < 8) {
        clean += " ";
    }
    if (clean.length() > 8) {
        clean = clean.substring(0, 8);
    }
    return clean;
}

int SGP4Calc::calculateTleChecksum(const String& line) {
    int sum = 0;
    int len = min((int)line.length(), 68);
    for (int i = 0; i < len; ++i) {
        char c = line[i];
        if (c >= '0' && c <= '9') {
            sum += (c - '0');
        } else if (c == '-') {
            sum += 1;
        }
    }
    return sum % 10;
}
