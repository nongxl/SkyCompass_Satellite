#ifndef I18N_H
#define I18N_H

#include <Arduino.h>

enum Language {
    LANG_EN = 0,
    LANG_ZH = 1,
    LANG_JA = 2,
    LANG_ES = 3
};

enum TextId {
    // Startup
    TXT_LOADING_MODELS,
    
    // WiFi Setup Page
    TXT_WIFI_SETUP,
    TXT_SCANNING_NETWORKS,
    TXT_NO_NETWORKS_FOUND,
    TXT_PRESS_R_RESCAN,
    TXT_CONNECT_TO,
    TXT_PASSWORD,
    TXT_WIFI_HELP_CONN,
    TXT_SELECT_NETWORK,
    TXT_WIFI_HELP_SEL,
    TXT_WIFI_CONNECTING,
    
    // Tab Headers
    TXT_TAB_ENCYCLOPEDIA,
    TXT_TAB_RECENT_LAUNCH,
    
    // Satellite / Encyclopedia View
    TXT_DOWNLOADING,
    TXT_ID,
    TXT_GP_AGE,
    TXT_GP_AGE_NA,
    TXT_NO_DESCRIPTION,
    TXT_PRESS_D_DELETE,
    TXT_WEATHER_IMAGING,
    TXT_MODE,
    TXT_CUSTOM_ADDED_SAT,
    TXT_AOS,
    TXT_LOS,
    
    // Recent Launch Page
    TXT_DOWNLOADING_GP_JSONS,
    TXT_RL_ONLINE_FEATURE,
    TXT_PRESS_W_CONNECT_WIFI,
    TXT_DOWNLOAD_LATEST_GROUPS,
    TXT_RL_AGE,
    TXT_RL_EPOCH,
    TXT_RL_REP,
    TXT_RL_OBJECTS,
    TXT_RL_ORBIT,
    TXT_RL_STATUS,
    
    // Formation State Text
    TXT_FORM_OPERATIONAL,
    TXT_FORM_TIGHT_TRAIN,
    TXT_FORM_TRAIN_FORMATION,
    TXT_FORM_EXPANDING,
    
    // Coordinate HUD
    TXT_HUD_ALT,
    
    // Compass Mode
    TXT_CAMERA_VIEW_MODE,
    TXT_PRESS_S_REFERENCE,
    
    // Help Panel
    TXT_HELP_TITLE,
    TXT_HELP_BRIGHT,
    TXT_HELP_GNSS,
    TXT_HELP_HELP,
    TXT_HELP_HUD,
    TXT_HELP_LOCK,
    TXT_HELP_PASSLIST,
    TXT_HELP_SATS,
    TXT_HELP_TIME,
    TXT_HELP_VIEW,
    TXT_HELP_WIFI,
    TXT_HELP_CONFIG,
    TXT_HELP_REALTIME,
    TXT_HELP_TAB,
    
    // Recommended Passes Panel
    TXT_RECOMMENDED_PASSES,
    TXT_NO_PASSES_7D,
    TXT_PASS_CALCULATING,
    TXT_WAITING_TIME_SYNC,
    TXT_PASS_SCORE,
    TXT_PASS_MAG,
    TXT_PASS_REASON,
    TXT_PASS_REASON_DARK,
    TXT_PASS_REASON_BRIGHT,
    TXT_PASS_REASON_ZENITH,
    TXT_PASS_REASON_LONG,
    TXT_PASS_NAME,
    TXT_PASS_AOS,
    TXT_PASS_MAX_EL,
    TXT_PASS_LOS,
    TXT_PASS_DURATION,
    TXT_PASS_MIN_EL,
    TXT_PASS_TRACKABLE,
    TXT_PASS_YES,
    TXT_PASS_NO,
    TXT_PASS_DETAIL_HELP,
    
    // Sun Data Page
    TXT_SUN_DATA,
    TXT_SUN_AZIMUTH,
    TXT_SUN_ELEVATION,
    TXT_SUN_DIST,
    TXT_SUN_RA,
    TXT_SUN_DEC,
    TXT_SUN_DEC_DEG,
    TXT_SUN_SUNRISE,
    TXT_SUN_SUNSET,
    TXT_SUN_NOON,
    TXT_PRESS_ESC_RETURN,
    
    // Time Machine Page
    TXT_TM_TITLE,
    TXT_TM_ADJUST,
    TXT_TM_CONFIRM,
    TXT_TM_CANCEL,
    TXT_TM_CURRENT_TIME,
    
    // Quick Setup Dialog
    TXT_SETTINGS_TITLE,
    TXT_LON,
    TXT_LAT,
    TXT_ALT,
    TXT_OK,
    TXT_SETTINGS_HELP,
    
    // Position Settings Page
    TXT_POS_SETTINGS_TITLE,
    TXT_LONGITUDE,
    TXT_LATITUDE,
    TXT_ALTITUDE_M,
    TXT_POS_HELP_VAL,
    TXT_POS_HELP_FIELD,
    TXT_POS_HELP_SAVE,
    
    // Language Dialog
    TXT_SELECT_LANGUAGE,
    TXT_LANGUAGE_MENU,
    
    // Custom NORAD ADD Text
    TXT_ENTER_NORAD_ADD,
    TXT_SOURCE_CELESTRAK,
    
    // Extra Recent Launch Page Detail Specs
    TXT_RL_VISIBILITY,
    TXT_VIS_EXCELLENT,
    TXT_VIS_MODERATE,
    TXT_VIS_NA,
    TXT_PRESS_O_OBJECTS,
    TXT_NO_OBJECTS_FOUND,
    
    // Status Feedback / Error Msg / Banner info
    TXT_SYS_BUSY,
    TXT_TASK_INIT_FAILED,
    TXT_CONNECTING_WIFI,
    TXT_WIFI_DISCONNECTED,
    TXT_REFRESHING_GP,
    TXT_PARSE_CACHE_FAILED,
    TXT_UPDATE_SUCCESS_CACHE,
    TXT_RL_CACHED_LIMIT,
    TXT_UPDATE_FAILED,
    
    // Tree categories in passes panel
    TXT_CAT_TONIGHT,
    TXT_CAT_NEXT_7D,
    TXT_CAT_HIGHLY_REC,
    TXT_CAT_ALL_PASSES,
    
    // Max index marker
    TXT_MAX
};

class I18N {
private:
    static Language _currentLang;
    static bool _initialized;

public:
    /**
     * @brief 初始化I18N管理器，加载NVS中的语言配置
     */
    static void begin();

    /**
     * @brief 获取当前设置的语言
     */
    static Language getLanguage();

    /**
     * @brief 设置语言，并存入NVS
     */
    static void setLanguage(Language lang);

    /**
     * @brief 根据文本ID获取当前语言的字符串
     */
    static const char* get(TextId id);

    /**
     * @brief 获取百科卫星的本地化简介。如果是未知或动态添加的返回 nullptr。
     */
    static const char* getSatDescription(int noradId);

    /**
     * @brief 判断是否为首次启动（即NVS中未设置语言）
     */
    static bool isFirstStart();

    /**
     * @brief 将首次启动标记设为已完成
     */
    static void setFirstStartDone();
};

#endif // I18N_H
