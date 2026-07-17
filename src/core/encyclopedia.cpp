#include "encyclopedia.h"
#include "i18n.h"
#include <M5Cardputer.h> // 为了获得 TFT_ 颜色宏

// 预设轨道目标静态数据库（以推荐排序排列）
static const EncyclopediaEntry g_encyclopedia_data[] = {
    // 25544 ISS
    {
        25544,
        "ISS",
        Category::HUMAN_SPACEFLIGHT,
        ICON_STATION,
        "人类最大的载人空间站。自1998年以来持续有人类驻留，每天绕地球约16圈，是夜空中最容易观测到的目标。",
        "International Space Station. The largest human-made space station. Continuously inhabited since 1998, orbits Earth 16 times a day, very easy to spot.",
        FLAG_VISIBLE | FLAG_CREWED | FLAG_HISTORIC | FLAG_RADIO,
        TFT_YELLOW,
        2,
        -1.8,
        SAT_TYPE_SPACE_STATION,
        "145.800",
        "FM/SSTV",
        "",
        "",
        true  // defaultSelected
    },
    // 48274 Tiangong
    {
        48274,
        "Tiangong",
        Category::HUMAN_SPACEFLIGHT,
        ICON_STATION,
        "中国天宫空间站。低地球轨道上的国家太空实验室，呈三舱T字构型，常年有3名航天员驻留。",
        "China's Tiangong Space Station. A permanent space lab in low Earth orbit with a T-shape structure, hosting 3 crew members regularly.",
        FLAG_VISIBLE | FLAG_CREWED | FLAG_HISTORIC,
        TFT_GREEN,
        1,
        -0.5,
        SAT_TYPE_SPACE_STATION,
        "",
        "",
        "",
        "",
        true  // defaultSelected
    },
    // 20580 Hubble
    {
        20580,
        "Hubble",
        Category::ASTRONOMY,
        ICON_TELESCOPE,
        "哈勃空间望远镜。人类历史上最伟大的太空望远镜之一，彻底改变了我们对宇宙年龄、膨胀及暗能量的认识。",
        "Hubble Space Telescope. One of the greatest space observatories, revolutionizing our understanding of cosmic age, expansion, and dark energy.",
        FLAG_VISIBLE | FLAG_HISTORIC | FLAG_SCIENCE,
        TFT_CYAN,
        0,
        1.5,
        SAT_TYPE_VISUAL,
        "",
        "",
        "",
        "",
        false  // defaultSelected
    },
    // 33591 NOAA 19
    {
        33591,
        "NOAA 19",
        Category::WEATHER,
        ICON_WEATHER,
        "美国极轨气象卫星。在850公里轨道上运行，常年广播137MHz模拟实时图像（APT），无线电爱好者极易接收。",
        "NOAA weather satellite. Orbits at 850 km, broadcasting analog real-time cloud images (APT) on 137 MHz, very popular for DIY radio reception.",
        FLAG_WEATHER | FLAG_RADIO | FLAG_EARTH_OBS,
        TFT_ORANGE,
        0,
        3.5,
        SAT_TYPE_WEATHER,
        "137.100",
        "APT",
        "",
        "",
        false  // defaultSelected
    },
    // 50463 JWST
    {
        50463,
        "JWST",
        Category::ASTRONOMY,
        ICON_DEEPSPACE,
        "詹姆斯·韦布空间望远镜。目前最强大的红外太空望远镜，运行于距离地球150万公里的拉格朗日L2点。",
        "James Webb Space Telescope. The most powerful infrared observatory, operating at the Sun-Earth L2 Lagrange point 1.5 million km away.",
        FLAG_SCIENCE,
        TFT_GOLD,
        0,
        10.0,
        SAT_TYPE_VISUAL,
        "",
        "",
        "",
        "",
        false  // defaultSelected
    },
    // 53807 BlueWalker 3
    {
        53807,
        "BlueWalker 3",
        Category::COMMUNICATIONS,
        ICON_BLUEWALKER3,
        "大型直连手机通信试验卫星。天线面积达64平方米，其极高亮度曾在天文界引发巨大争议。",
        "AST SpaceMobile's direct-to-cell test satellite. Features a massive 64 sqm antenna array, causing controversy due to its high brightness.",
        FLAG_VISIBLE | FLAG_RADIO,
        TFT_WHITE,
        0,
        1.0,
        SAT_TYPE_VISUAL,
        "",
        "",
        "",
        "",
        false  // defaultSelected
    },
    // 118 Ablestar R/B
    {
        118,
        "Ablestar R/B",
        Category::ROCKET_BODY,
        ICON_ROCKET,
        "1960年发射的雷神-阿布尔星火箭残骸。它是目前在轨运行最古老的人造物体残骸之一。",
        "Thor-Ablestar rocket body launched in 1960. It is one of the oldest human-made debris objects still orbiting the Earth today.",
        FLAG_VISIBLE | FLAG_ROCKET_BODY | FLAG_DEBRIS,
        TFT_LIGHTGRAY,
        0,
        4.0,
        SAT_TYPE_VISUAL,
        "",
        "",
        "",
        "",
        false  // defaultSelected
    },
    // 25732 CZ-4B R/B
    {
        25732,
        "CZ-4B R/B",
        Category::ROCKET_BODY,
        ICON_ROCKET,
        "长征四号乙运载火箭的二级火箭残骸。呈圆柱形，阳光照射时在夜空中产生规律的闪烁。",
        "Long March 4B rocket upper stage. A cylindrical metallic body that exhibits predictable light flashes under solar illumination.",
        FLAG_VISIBLE | FLAG_ROCKET_BODY | FLAG_DEBRIS,
        TFT_ORANGE,
        0,
        4.0,
        SAT_TYPE_VISUAL,
        "",
        "",
        "",
        "",
        false  // defaultSelected
    },
    // 6155 Centaur R/B
    {
        6155,
        "Centaur R/B",
        Category::ROCKET_BODY,
        ICON_ROCKET,
        "半人马座火箭残骸。阿特拉斯运载火箭的高能上级，不锈钢壳体反射率高，是一个易于观测的目标。",
        "Centaur rocket upper stage. Made of highly reflective stainless steel, presenting an easily trackable reflective target.",
        FLAG_VISIBLE | FLAG_ROCKET_BODY | FLAG_DEBRIS,
        TFT_LIGHTGRAY,
        0,
        4.0,
        SAT_TYPE_VISUAL,
        "",
        "",
        "",
        "",
        false  // defaultSelected
    },
    // 28499 Ariane 5 R/B
    {
        28499,
        "Ariane 5 R/B",
        Category::ROCKET_BODY,
        ICON_ROCKET,
        "阿里安5号重型火箭残骸。体型巨大，是不活跃空间碎片的典型代表，反光极强。",
        "Ariane 5 rocket body. A massive upper stage element representing a prominent inactive space debris target with strong reflections.",
        FLAG_VISIBLE | FLAG_ROCKET_BODY | FLAG_DEBRIS,
        TFT_LIGHTGRAY,
        0,
        4.0,
        SAT_TYPE_VISUAL,
        "",
        "",
        "",
        "",
        false  // defaultSelected
    },
    // 41882 Fengyun-4A
    {
        41882,
        "Fengyun-4A",
        Category::WEATHER,
        ICON_WEATHER,
        "中国风云四号A气象卫星。运行于地球静止轨道，常年监视东半球大气变化与天气现象。",
        "Chinese Fengyun-4A weather satellite. Placed in geostationary orbit, continuously monitoring the atmospheric changes over the Eastern Hemisphere.",
        FLAG_WEATHER | FLAG_EARTH_OBS,
        TFT_BLUE,
        0,
        10.0,
        SAT_TYPE_VISUAL,
        "",
        "",
        "",
        "",
        false  // defaultSelected
    },
    // 43539 BeiDou-3
    {
        43539,
        "BeiDou-3",
        Category::NAVIGATION,
        ICON_NAVIGATION,
        "北斗三号中轨道导航卫星。中国北斗全球卫星导航系统（BDS）的核心成员，运行在约2万公里高的轨道。",
        "BeiDou-3 navigation satellite in MEO. A core component of China's BDS system, operating at an altitude of approximately 21,500 km.",
        FLAG_NAVIGATION,
        TFT_RED,
        0,
        10.0,
        SAT_TYPE_VISUAL,
        "",
        "",
        "",
        "",
        false  // defaultSelected
    },
    // 27386 Envisat
    {
        27386,
        "Envisat",
        Category::EARTH_OBSERVATION,
        ICON_SATELLITE,
        "欧洲巨型地球观测卫星。重达8吨，2012年突然失效，目前是轨道上最危险的巨大空间垃圾之一。",
        "European environmental satellite. Weighing 8 tons, it unexpectedly failed in 2012 and remains one of the largest debris threats in orbit.",
        FLAG_VISIBLE | FLAG_EARTH_OBS | FLAG_DEBRIS,
        TFT_LIGHTGRAY,
        0,
        2.5,
        SAT_TYPE_VISUAL,
        "",
        "",
        "",
        "",
        false  // defaultSelected
    },
    // 25576 FengYun-1C
    {
        25576,
        "FengYun-1C",
        Category::EARTH_OBSERVATION,
        ICON_DEBRIS,
        "风云一号C。中国于1999年发射的气象卫星，2007年在反卫星导弹试验中被摧毁，产生数千块碎片，是空间碎片观测的典型。",
        "FengYun-1C. A weather satellite launched in 1999, destroyed in a 2007 anti-satellite test. Created thousands of debris, a classic target for space junk observation.",
        FLAG_HISTORIC | FLAG_DEBRIS,
        TFT_LIGHTGRAY,
        0,
        4.0,
        SAT_TYPE_VISUAL,
        "",
        "",
        "",
        "",
        false  // defaultSelected
    },
    // 4382 DFH-1
    {
        4382,
        "DFH-1",
        Category::HISTORIC_EVENT,
        ICON_DFH1,
        "东方红一号。中国于1970年发射的首颗人造卫星，播送过《东方红》乐曲，目前已失效，在太空中无声运行。",
        "Dong Fang Hong I. China's first satellite launched in 1970, playing the patriotic song, now silent but historically monumental.",
        FLAG_VISIBLE | FLAG_HISTORIC,
        TFT_RED,
        0,
        6.0,
        SAT_TYPE_HISTORICAL,
        "20.009",
        "Beacon",
        "",
        "",
        false  // defaultSelected
    },
    // 25994 Terra
    {
        25994,
        "Terra",
        Category::EARTH_OBSERVATION,
        ICON_SATELLITE,
        "NASA旗舰级地球观测卫星。运行于太阳同步轨道，为研究地球气候变化、陆地覆盖等提供珍贵的观测数据。",
        "NASA's flagship Earth observation satellite. Operating in sun-synchronous orbit, providing key insights into climate and land-cover changes.",
        FLAG_EARTH_OBS | FLAG_SCIENCE,
        TFT_PINK,
        0,
        3.0,
        SAT_TYPE_VISUAL,
        "",
        "",
        "",
        "",
        false  // defaultSelected
    },
    // 27424 Aqua
    {
        27424,
        "Aqua",
        Category::EARTH_OBSERVATION,
        ICON_SATELLITE,
        "NASA主要地球观测卫星之一。重点监测全球水循环，包括海水表面温度、水汽、云层及降水变化。",
        "NASA's Earth Science satellite. Focuses on the global water cycle, including sea surface temperature, humidity, clouds, and precipitation.",
        FLAG_EARTH_OBS | FLAG_SCIENCE,
        TFT_MAGENTA,
        0,
        3.0,
        SAT_TYPE_VISUAL,
        "",
        "",
        "",
        "",
        false  // defaultSelected
    },
    // 42956 Iridium 127
    {
        42956,
        "Iridium 127",
        Category::COMMUNICATIONS,
        ICON_SATELLITE,
        "第二代铱星通信卫星。属于第二代铱星系统（Iridium NEXT），为全球提供语音与数据覆盖。",
        "Second-generation Iridium communication satellite. Part of the Iridium NEXT constellation, providing global voice and data coverage.",
        FLAG_VISIBLE | FLAG_RADIO,
        TFT_WHITE,
        0,
        4.0,
        SAT_TYPE_VISUAL,
        "",
        "",
        "",
        "",
        false  // defaultSelected
    },
    // 57165 Meteor-M2
    {
        57165,
        "Meteor-M2",
        Category::WEATHER,
        ICON_WEATHER,
        "俄罗斯极轨道气象卫星。在轨道上持续广播137MHz实时数字气象图（LRPT），能生成高分辨率可见光云图。",
        "Russian polar-orbiting weather satellite. Transmits real-time digital weather images (LRPT) on 137 MHz with high-resolution cloud cover detail.",
        FLAG_WEATHER | FLAG_RADIO | FLAG_EARTH_OBS,
        TFT_WHITE,
        0,
        3.5,
        SAT_TYPE_WEATHER,
        "137.100",
        "LRPT",
        "",
        "",
        false  // defaultSelected
    },
    // 27607 SO-50
    {
        27607,
        "SO-50",
        Category::COMMUNICATIONS,
        ICON_SATELLITE,
        "沙特之星1C。极受欢迎的寿命极长调频中继业余卫星，支持通过普通对讲机进行跨地区通信。",
        "SaudiSat 1C. An extremely popular, long-lived amateur FM voice repeater satellite, supporting contacts via handheld radios.",
        FLAG_RADIO | FLAG_SCIENCE,
        TFT_GREEN,
        0,
        6.5,
        SAT_TYPE_HAM,
        "145.850",
        "FM",
        "436.795",
        "67.0",
        false  // defaultSelected
    },
    // 43017 AO-91
    {
        43017,
        "AO-91",
        Category::COMMUNICATIONS,
        ICON_SATELLITE,
        "Fox-1B业余卫星。搭载U/V调频语音中继，体积非常小（1U立方星），为无线电爱好者提供语音转发服务。",
        "Fox-1B amateur satellite. Carries a U/V FM voice repeater in a 1U CubeSat form factor, serving ham operators globally.",
        FLAG_RADIO | FLAG_SCIENCE,
        TFT_MAGENTA,
        0,
        6.0,
        SAT_TYPE_HAM,
        "145.960",
        "FM",
        "435.250",
        "67.0",
        false  // defaultSelected
    },

    // ── 业余无线电 ──

    // 7530 OSCAR-7 (AO-7)
    {
        7530,
        "OSCAR-7",
        Category::COMMUNICATIONS,
        ICON_SATELLITE,
        "世界最长寿的业余无线电卫星之一。1974年发射，电池耗尽沉默多年，2002年太阳能板短路修复后奇迹般恢复，至今仍可通联。",
        "One of the world's longest-lived amateur satellites. Launched 1974, went silent for years, then miraculously revived in 2002 after a short circuit cleared. Still active today.",
        FLAG_RADIO | FLAG_HISTORIC,
        TFT_YELLOW,
        0,
        10.0,
        SAT_TYPE_HAM,
        "145.975",
        "USB/CW",
        "432.125",
        "",
        false  // defaultSelected
    },

    // 43700 QO-100 (Es'hail-2)
    {
        43700,
        "QO-100",
        Category::COMMUNICATIONS,
        ICON_COMMUNICATION,
        "全球首颗地球静止轨道业余无线电卫星。官方名称为Es'hail-2，覆盖从巴西到印度的广大区域。爱好者可通过小型天线实现跨洲通联。",
        "World's first geostationary amateur satellite (official name: Es'hail-2). Covers Brazil to India, letting hams communicate across continents with a small dish.",
        FLAG_RADIO | FLAG_HISTORIC,
        TFT_CYAN,
        0,
        10.0,
        SAT_TYPE_HAM,
        "10489.750",
        "SSB/CW",
        "2400.050",
        "",
        false  // defaultSelected
    },

    // ── 地球观测 ──

    // 39084 Landsat 8
    {
        39084,
        "Landsat 8",
        Category::EARTH_OBSERVATION,
        ICON_SATELLITE,
        "地球观测历史最悠久的系列卫星之一。Landsat 项目从1972年延续至今，其长达50年的连续影像是研究人类如何改变地球表面的最重要数据集。",
        "Part of the longest-running Earth observation program. The Landsat series, from 1972 to present, provides 50+ years of imagery essential for studying how humans reshape our planet.",
        FLAG_EARTH_OBS | FLAG_SCIENCE | FLAG_HISTORIC,
        TFT_GREEN,
        0,
        3.0,
        SAT_TYPE_VISUAL,
        "",
        "",
        "",
        "",
        false  // defaultSelected
    },

    // 40697 Sentinel-2A
    {
        40697,
        "Sentinel-2A",
        Category::EARTH_OBSERVATION,
        ICON_SATELLITE,
        "欧洲哨兵卫星。现在互联网上能看到的绝大多数高分辨率免费卫星图片都来自哨兵系列。13个光谱波段，10米分辨率，5天重访全球。",
        "European Sentinel satellite. The source of most free high-resolution satellite imagery on the internet. 13 spectral bands at 10 m resolution, with a 5-day global revisit.",
        FLAG_EARTH_OBS | FLAG_SCIENCE,
        TFT_GREEN,
        0,
        3.5,
        SAT_TYPE_VISUAL,
        "",
        "",
        "",
        "",
        false  // defaultSelected
    },

    // 62261 Sentinel-1C (launched Dec 5, 2024)
    {
        62261,
        "Sentinel-1C",
        Category::EARTH_OBSERVATION,
        ICON_SATELLITE,
        "欧洲哨兵雷达卫星。使用合成孔径雷达（SAR），无论白天黑夜、云层厚薄都能穿透拍摄地面，用于洪水、地震、海冰等灾害监测。",
        "European Sentinel radar satellite. Uses Synthetic Aperture Radar (SAR) to image Earth through clouds and darkness, invaluable for flood, earthquake, and sea-ice monitoring.",
        FLAG_EARTH_OBS | FLAG_SCIENCE,
        TFT_ORANGE,
        0,
        3.5,
        SAT_TYPE_VISUAL,
        "",
        "",
        "",
        "",
        false  // defaultSelected
    },

    // 43613 ICESat-2
    {
        43613,
        "ICESat-2",
        Category::EARTH_OBSERVATION,
        ICON_SATELLITE,
        "NASA激光测冰卫星。用绿色激光每秒发射一万次脉冲，通过测量光子飞行时间来精确测量极地冰盖高度变化，监测气候变暖。",
        "NASA's ice-measuring satellite. Fires 10,000 laser pulses per second, timing photon returns to precisely measure polar ice sheet elevation changes and track climate warming.",
        FLAG_EARTH_OBS | FLAG_SCIENCE,
        TFT_CYAN,
        0,
        4.0,
        SAT_TYPE_VISUAL,
        "",
        "",
        "",
        "",
        false  // defaultSelected
    },

    // ── 历史性首次 ──

    // 4 Sputnik 1 — 已于1958年陨落，无在轨数据，收录其火箭残骸
    // 用 DFH-1 前相邻位置放 Vanguard 1 和 Explorer 1 残骸

    // 5 Vanguard 1
    {
        5,
        "Vanguard 1",
        Category::HISTORIC_EVENT,
        ICON_SATELLITE,
        "迄今在轨运行时间最长的人造物体。1958年由美国发射，目前仍在约650公里高的轨道上运行，预计将继续在轨数百年。",
        "The oldest human-made object still in orbit. Launched in 1958 by the US, it continues to circle Earth at ~650 km and is expected to orbit for centuries to come.",
        FLAG_VISIBLE | FLAG_HISTORIC,
        TFT_LIGHTGRAY,
        0,
        7.5,
        SAT_TYPE_HISTORICAL,
        "",
        "",
        "",
        "",
        false  // defaultSelected
    },

    // ── 天文科学 ──

    // 39479 Gaia
    {
        39479,
        "Gaia",
        Category::ASTRONOMY,
        ICON_DEEPSPACE,
        "欧洲精密天体测量卫星。正在为银河系超过10亿颗恒星建立精确三维坐标图，其数据将彻底刷新人类对银河系结构的认知。",
        "ESA's stellar cartography mission. Mapping the precise 3D positions of over one billion stars in the Milky Way, transforming our understanding of galactic structure.",
        FLAG_SCIENCE | FLAG_HISTORIC,
        TFT_GOLD,
        0,
        10.0,
        SAT_TYPE_VISUAL,
        "",
        "",
        "",
        "",
        false  // defaultSelected
    },

    // ── 最快/最远 ──

    // 43592 Parker Solar Probe
    {
        43592,
        "Parker Solar Probe",
        Category::ASTRONOMY,
        ICON_SOLAR_PROBE,
        "人类制造的最快飞行器。在近日点时速度超过每秒690公里，已多次穿越太阳日冕，是探测太阳风起源的历史性任务。",
        "The fastest human-made object ever. Reaching speeds over 690 km/s at perihelion, it dives through the Sun's corona to study the origin of solar wind.",
        FLAG_SCIENCE | FLAG_HISTORIC,
        TFT_ORANGE,
        0,
        10.0,
        SAT_TYPE_VISUAL,
        "",
        "",
        "",
        "",
        false  // defaultSelected
    },

    // ── 行星防御 ──

    // 61449 HERA (ESA, launched Oct 7 2024)
    {
        61449,
        "HERA",
        Category::ASTRONOMY,
        ICON_DEEPSPACE,
        "欧洲行星防御任务。前往调查NASA的DART探测器2022年撞击Dimorphos小行星后留下的弹坑，验证人类首次主动改变天体轨道的效果。",
        "ESA's planetary defense mission. Investigating the crater left by NASA's DART impactor on asteroid Dimorphos, verifying humanity's first successful deflection of a celestial body.",
        FLAG_SCIENCE | FLAG_HISTORIC,
        TFT_PINK,
        0,
        10.0,
        SAT_TYPE_VISUAL,
        "",
        "",
        "",
        "",
        false  // defaultSelected
    },

    // ── 神秘与传奇 ──

    // 58666 X-37B OTV-7 (USA-349, launched Dec 28 2023, currently in orbit)
    {
        58666,
        "X-37B",
        Category::UNKNOWN,
        ICON_SPACEPLANE,
        "美国天军秘密太空飞机。外形类似小型航天飞机，可长时间在轨执行未公开任务，创造过908天连续在轨飞行纪录，任务内容至今成谜。",
        "US Space Force's secret spaceplane. Resembling a miniature shuttle, it holds a record of 908 continuous days in orbit on undisclosed missions. Its purpose remains classified.",
        FLAG_HISTORIC | FLAG_VISIBLE,
        TFT_LIGHTGRAY,
        0,
        4.0,
        SAT_TYPE_VISUAL,
        "",
        "",
        "",
        "",
        false  // defaultSelected
    },

    // ── 地球观测（最实用）──

    // 33591 NOAA 19 已收录，补充最新 NOAA-21
    // 43013 NOAA-20
    {
        43013,
        "NOAA-20",
        Category::WEATHER,
        ICON_WEATHER,
        "美国新一代极轨气象卫星。配备VIIRS成像仪，提供地球表面、大气温度和湿度的高分辨率数据，是天气预报的核心数据来源之一。",
        "NOAA's latest generation polar-orbiting satellite. Equipped with VIIRS imager, providing high-resolution data on surface, temperature, and humidity for weather forecasting.",
        FLAG_WEATHER | FLAG_EARTH_OBS,
        TFT_ORANGE,
        0,
        3.5,
        SAT_TYPE_WEATHER,
        "",
        "",
        "",
        "",
        false  // defaultSelected
    },

    // ── 导航 ──

    // 32260 NAVSTAR 60 (USA-196, GPS IIR-M 5)
    {
        32260,
        "NAVSTAR 60",
        Category::NAVIGATION,
        ICON_NAVIGATION,
        "美国GPS导航卫星（NAVSTAR 60，又称USA-196）。GPS星座是现代世界运转的基础设施，智能手机、航空、航海乃至金融系统都依赖GPS提供精确时间和位置。",
        "A GPS navigation satellite (NAVSTAR 60 / USA-196). The GPS constellation underpins modern civilisation — from smartphones to aviation, shipping, and financial timing systems.",
        FLAG_NAVIGATION | FLAG_HISTORIC,
        TFT_BLUE,
        0,
        10.0,
        SAT_TYPE_VISUAL,
        "",
        "",
        "",
        "",
        false  // defaultSelected
    }
};


const EncyclopediaEntry* Encyclopedia::getEntries() {
    return g_encyclopedia_data;
}

size_t Encyclopedia::getEntryCount() {
    return sizeof(g_encyclopedia_data) / sizeof(g_encyclopedia_data[0]);
}

const EncyclopediaEntry* Encyclopedia::getEntryByNorad(uint32_t norad) {
    for (size_t i = 0; i < getEntryCount(); i++) {
        if (g_encyclopedia_data[i].norad == norad) {
            return &g_encyclopedia_data[i];
        }
    }
    return nullptr;
}

const char* Encyclopedia::getDescription(uint32_t noradId) {
    const EncyclopediaEntry* entry = getEntryByNorad(noradId);
    if (!entry) return nullptr;
    
    if (I18N::getLanguage() == LANG_ZH) {
        return entry->description_zh;
    } else {
        return entry->description_en;
    }
}

String Encyclopedia::getCategoryName(Category category) {
    bool isZh = (I18N::getLanguage() == LANG_ZH);
    switch (category) {
        case Category::HUMAN_SPACEFLIGHT:
            return isZh ? "载人航天" : "Human Spaceflight";
        case Category::ASTRONOMY:
            return isZh ? "天文学" : "Astronomy";
        case Category::EARTH_OBSERVATION:
            return isZh ? "地球观测" : "Earth Observation";
        case Category::NAVIGATION:
            return isZh ? "导航" : "Navigation";
        case Category::WEATHER:
            return isZh ? "气象" : "Weather";
        case Category::COMMUNICATIONS:
            return isZh ? "通信" : "Communications";
        case Category::ROCKET_BODY:
            return isZh ? "火箭残骸" : "Rocket Body";
        case Category::HISTORIC_EVENT:
            return isZh ? "历史事件" : "Historic Event";
        default:
            return isZh ? "未知" : "Unknown";
    }
}

String Encyclopedia::getFlagName(uint32_t flag) {
    bool isZh = (I18N::getLanguage() == LANG_ZH);
    switch (flag) {
        case FLAG_VISIBLE:
            return isZh ? "肉眼可见" : "Visible";
        case FLAG_CREWED:
            return isZh ? "载人" : "Crewed";
        case FLAG_HISTORIC:
            return isZh ? "历史" : "Historic";
        case FLAG_ROCKET_BODY:
            return isZh ? "火箭残骸" : "Rocket Body";
        case FLAG_DEBRIS:
            return isZh ? "空间碎片" : "Debris";
        case FLAG_WEATHER:
            return isZh ? "气象" : "Weather";
        case FLAG_RADIO:
            return isZh ? "无线电" : "Radio";
        case FLAG_NAVIGATION:
            return isZh ? "导航" : "Navigation";
        case FLAG_SCIENCE:
            return isZh ? "科学" : "Science";
        case FLAG_EARTH_OBS:
            return isZh ? "地面观测" : "Earth Obs";
        default:
            return "";
    }
}
