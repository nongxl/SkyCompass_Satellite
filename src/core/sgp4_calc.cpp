#include "sgp4_calc.h"

SGP4Calc::SGP4Calc() {
    sat = new Sgp4();
}

SGP4Calc::~SGP4Calc() {
    delete sat;
}

SGP4Calc::SGP4Calc(const SGP4Calc& other) {
    sat = new Sgp4();
    if (other.sat) {
        sat->satrec = other.sat->satrec;
    }
}

SGP4Calc& SGP4Calc::operator=(const SGP4Calc& other) {
    if (this != &other) {
        if (sat && other.sat) {
            sat->satrec = other.sat->satrec;
        }
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
    
    return sat->init(name, line1, line2);
}

bool SGP4Calc::getTEME(uint32_t unix_ts, double& x, double& y, double& z) {
    double jd = CoordTransform::unixToJulian(unix_ts);
    double tsince = (jd - sat->satrec.jdsatepoch) * 24.0 * 60.0;
    
    double ro[3];
    double vo[3];
    
    // Copy the clean satrec state to stack to bypass in-place mutation and incremental cache pollution
    elsetrec temp_satrec = sat->satrec;
    bool success = sgp4(wgs72, temp_satrec, tsince, ro, vo);
    
    if (success) {
        x = ro[0];
        y = ro[1];
        z = ro[2];
        return true;
    }
    return false;
}
