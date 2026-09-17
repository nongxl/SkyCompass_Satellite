#include "tle_data.h"

// TLE data for testing (from recent dates)
// Note: TLEs change frequently, these are for Phase 0 validation.

TLEData TLEManager::getISS_TLE() {
    TLEData iss;
    iss.name = "ISS (ZARYA)";
    // Example recent TLE for ISS (2026)
    iss.line1 = "1 25544U 98067A   26163.80312907  .00008495  00000+0  16106-3 0  9990";
    iss.line2 = "2 25544  51.6335 321.7912 0004900 178.5917 181.5086 15.49196054571074";
    iss.baseScore = 2; // ISS is huge and very bright
    return iss;
}

TLEData TLEManager::getTiangong_TLE() {
    TLEData tiangong;
    tiangong.name = "CSS (TIANGONG)";
    // Example recent TLE for Tiangong (2026)
    tiangong.line1 = "1 48274U 21035A   26163.81770925  .00021976  00000+0  26094-3 0  9991";
    tiangong.line2 = "2 48274  41.4694 348.5894 0007968  41.6006 318.5438 15.60618766292486";
    tiangong.baseScore = 1; // Tiangong is large but smaller than ISS
    return tiangong;
}

TLEData TLEManager::getHubble_TLE() {
    TLEData hubble;
    hubble.name = "HST (HUBBLE)";
    // Real Hubble TLE from mid 2026
    hubble.line1 = "1 20580U 90037B   26163.25344175  .00006001  00000+0  18892-3 0  9990";
    hubble.line2 = "2 20580  28.4709 114.2921 0001952  91.9422 268.1398 15.30693075787818";
    hubble.baseScore = 0; // Standard satellite brightness
    return hubble;
}

#include <time.h>

TLEData TLEManager::getJWST_TLE() {
    TLEData jwst;
    jwst.name = "JWST";
    
    // 动态生成接近当前系统时间的历元（Epoch），避免 dsspace 解析器死循环卡看门狗，并使 GP Age 显示为 0d 左右。
    time_t now = time(nullptr);
    if (now < 1700000000) {
        // 时钟未同步时，默认使用 2026 年（对应模拟启动锚点时间 2026-07-27）
        now = 1785096183;
    }
    struct tm* tm_utc = gmtime(&now);
    int year = tm_utc->tm_year % 100; // 两位年份，例如 26
    int yday = tm_utc->tm_yday + 1;   // 一年中的天数，1~366
    
    char l1_buf[80];
    snprintf(l1_buf, sizeof(l1_buf), "1 50463U 21130A   %02d%03d.00000000  .00000000  00000-0  00000-0 0  999", year, yday);
    
    // 计算 Line 1 校验和（只累加数字和减号，取个位数）
    int sum = 0;
    for (int i = 0; i < 68; i++) {
        char c = l1_buf[i];
        if (c >= '0' && c <= '9') sum += (c - '0');
        else if (c == '-') sum += 1;
    }
    l1_buf[68] = '0' + (sum % 10);
    l1_buf[69] = '\0';
    
    jwst.line1 = String(l1_buf);
    
    // Line 2 原始串（末尾保留空出给校验和）
    jwst.line2 = "2 50463  23.4392   0.0000 0000000   0.0000   0.0000  0.00273700    00";
    
    // 计算 Line 2 校验和并补齐
    sum = 0;
    for (int i = 0; i < 68; i++) {
        char c = jwst.line2[i];
        if (c >= '0' && c <= '9') sum += (c - '0');
        else if (c == '-') sum += 1;
    }
    char l2_buf[80];
    strncpy(l2_buf, jwst.line2.c_str(), 68);
    l2_buf[68] = '0' + (sum % 10);
    l2_buf[69] = '\0';
    jwst.line2 = String(l2_buf);
    
    jwst.baseScore = 0;
    return jwst;
}

TLEData TLEManager::getNGRST_TLE() {
    TLEData ngrst;
    ngrst.name = "NGRST (Roman)";
    
    // 动态生成接近当前系统时间的历元（Epoch），避免 dsspace 解析器卡看门狗，并使 GP Age 显示为 0d 左右。
    time_t now = time(nullptr);
    if (now < 1700000000) {
        now = 1785096183;
    }
    struct tm* tm_utc = gmtime(&now);
    int year = tm_utc->tm_year % 100; // 两位年份，例如 26
    int yday = tm_utc->tm_yday + 1;   // 一年中的天数，1~366
    
    char l1_buf[80];
    // TLE 标准要求编号严格占用 5 位列宽，100532 映射为 00532（与 SGP4 规范一致），国际标识符 2026-199A -> 26199A
    snprintf(l1_buf, sizeof(l1_buf), "1 00532U 26199A   %02d%03d.00000000  .00000000  00000-0  00000-0 0  999", year, yday);
    
    // 计算 Line 1 校验和
    int sum = 0;
    for (int i = 0; i < 68; i++) {
        char c = l1_buf[i];
        if (c >= '0' && c <= '9') sum += (c - '0');
        else if (c == '-') sum += 1;
    }
    l1_buf[68] = '0' + (sum % 10);
    l1_buf[69] = '\0';
    
    ngrst.line1 = String(l1_buf);
    
    // Line 2 原始串（日-地 L2 晕轮轨道模拟，周期约 1 年，与 JWST 相同）
    ngrst.line2 = "2 00532  23.4392   0.0000 0000000   0.0000   0.0000  0.00273700    00";
    
    // 计算 Line 2 校验和并补齐
    sum = 0;
    for (int i = 0; i < 68; i++) {
        char c = ngrst.line2[i];
        if (c >= '0' && c <= '9') sum += (c - '0');
        else if (c == '-') sum += 1;
    }
    char l2_buf[80];
    strncpy(l2_buf, ngrst.line2.c_str(), 68);
    l2_buf[68] = '0' + (sum % 10);
    l2_buf[69] = '\0';
    ngrst.line2 = String(l2_buf);
    
    ngrst.baseScore = 0;
    return ngrst;
}

TLEData TLEManager::getHerschel_TLE() {
    TLEData herschel;
    herschel.name = "HERSCHEL";
    
    // 动态生成接近当前系统时间的历元（Epoch），避免 dsspace 解析器卡看门狗，并使 GP Age 显示为 0d 左右。
    time_t now = time(nullptr);
    if (now < 1700000000) {
        now = 1785096183;
    }
    struct tm* tm_utc = gmtime(&now);
    int year = tm_utc->tm_year % 100; // 两位年份，例如 26
    int yday = tm_utc->tm_yday + 1;   // 一年中的天数，1~366
    
    char l1_buf[80];
    // 目录号 34937，国际标识符 2009-026A -> 09026A
    snprintf(l1_buf, sizeof(l1_buf), "1 34937U 09026A   %02d%03d.00000000  .00000000  00000-0  00000-0 0  999", year, yday);
    
    // 计算 Line 1 校验和
    int sum = 0;
    for (int i = 0; i < 68; i++) {
        char c = l1_buf[i];
        if (c >= '0' && c <= '9') sum += (c - '0');
        else if (c == '-') sum += 1;
    }
    l1_buf[68] = '0' + (sum % 10);
    l1_buf[69] = '\0';
    
    herschel.line1 = String(l1_buf);
    
    // Line 2 原始串（日-地 L2 利萨如轨道模拟，周期约 1 年，与 JWST 相同）
    herschel.line2 = "2 34937  23.4392   0.0000 0000000   0.0000   0.0000  0.00273700    00";
    
    // 计算 Line 2 校验和并补齐
    sum = 0;
    for (int i = 0; i < 68; i++) {
        char c = herschel.line2[i];
        if (c >= '0' && c <= '9') sum += (c - '0');
        else if (c == '-') sum += 1;
    }
    char l2_buf[80];
    strncpy(l2_buf, herschel.line2.c_str(), 68);
    l2_buf[68] = '0' + (sum % 10);
    l2_buf[69] = '\0';
    herschel.line2 = String(l2_buf);
    
    herschel.baseScore = 0;
    return herschel;
}

TLEData TLEManager::getSO50_TLE() {
    TLEData so50;
    so50.name = "SO-50";
    so50.line1 = "1 27607U 02058C   26163.78561234  .00000084  00000-0  56106-4 0  9990";
    so50.line2 = "2 27607  64.5512 284.1234 0081234 274.5612  85.1234 14.7561234571234";
    so50.baseScore = 0;
    return so50;
}

TLEData TLEManager::getAO91_TLE() {
    TLEData ao91;
    ao91.name = "AO-91";
    ao91.line1 = "1 43017U 17073B   26163.78561234  .00000123  00000-0  12345-3 0  9990";
    ao91.line2 = "2 43017  97.6512 184.1234 0241234 274.5612  85.1234 14.8561234571234";
    ao91.baseScore = 0;
    return ao91;
}

TLEData TLEManager::getNORBI_TLE() {
    TLEData d;
    d.name = "NORBI";
    d.line1 = "1 46494U 20068J   26260.00274634  .00024463  00000+0  40463-3 0  9993";
    d.line2 = "2 46494  97.8589 284.3188 0003402 277.1031  82.9833 15.52207859330439";
    d.baseScore = 0;
    return d;
}

TLEData TLEManager::getFOSSASAT2E_TLE() {
    TLEData d;
    d.name = "FOSSASAT-2E";
    d.line1 = "1 62676U 25009BV  26258.67848957  .00002452  00000+0  11186-3 0  9992";
    d.line2 = "2 62676  97.3898 335.3350 0005870  37.8087 322.3560 15.21547755 92451";
    d.baseScore = 0;
    return d;
}

TLEData TLEManager::getLilacSat2_TLE() {
    TLEData d;
    d.name = "LilacSat-2";
    d.line1 = "1 40908U 15049K   26259.90225052  .00019258  00000+0  29536-3 0  9991";
    d.line2 = "2 40908  97.4557 300.3766 0004616 276.1708  83.9020 15.54345131609989";
    d.baseScore = 0;
    return d;
}

TLEData TLEManager::getXW3_TLE() {
    TLEData d;
    d.name = "XW-3 (CAS-9)";
    d.line1 = "1 50466U 21131B   26259.96327671  .00000225  00000+0  82910-4 0  9996";
    d.line2 = "2 50466  98.4973 351.8533 0004215 166.7909 193.3392 14.41425787248334";
    d.baseScore = 0;
    return d;
}

TLEData TLEManager::getSONATE2_TLE() {
    TLEData d;
    d.name = "SONATE-2";
    d.line1 = "1 59112U 24043Q   26259.41493109  .00053443  00000+0  52160-3 0  9991";
    d.line2 = "2 59112  97.5628  55.9655 0005362 220.4611 139.6251 15.66030541142133";
    d.baseScore = 0;
    return d;
}

TLEData TLEManager::getVladivostok1_TLE() {
    TLEData d;
    d.name = "Vladivostok-1";
    d.line1 = "1 61751U 24199S   26259.91966800  .00008556  00000+0  22911-3 0  9999";
    d.line2 = "2 61751  97.2835 128.7194 0008702 348.4947  11.6099 15.38226030151232";
    d.baseScore = 0;
    return d;
}

TLEData TLEManager::getNORBY2_TLE() {
    TLEData d;
    d.name = "NORBY-2";
    d.line1 = "1 57179U 23091P   26259.91641061  .00003558  00000+0  18261-3 0  9997";
    d.line2 = "2 57179  97.4995 314.9911 0015151 161.1096 199.0702 15.17081935177697";
    d.baseScore = 0;
    return d;
}

TLEData TLEManager::getUMKA1_TLE() {
    TLEData d;
    d.name = "UMKA-1";
    d.line1 = "1 57172U 23091G   26259.91749094  .00005592  00000+0  24966-3 0  9994";
    d.line2 = "2 57172  97.4950 318.2717 0013213 152.9670 207.2257 15.21694931177952";
    d.baseScore = 0;
    return d;
}

uint32_t TLEManager::getMockTimeAnchor() {
    // 2026-06-14 00:00:00 UTC = 1781395200
    // MUST match the 2026 TLE epoch
    return 1781395200;
}
