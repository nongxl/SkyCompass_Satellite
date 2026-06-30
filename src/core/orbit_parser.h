#pragma once
#include "orbit_record.h"

class OrbitParser {
public:
    virtual ~OrbitParser() {}
    
    // Parse input string (e.g. TLE multi-line or JSON block) into OrbitRecord
    virtual bool parse(const String& input, OrbitRecord& record) = 0;
};
