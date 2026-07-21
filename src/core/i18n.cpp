#include "i18n.h"
#include <Preferences.h>
#include "encyclopedia.h"

Language I18N::_currentLang = LANG_EN;
bool I18N::_initialized = false;
static Preferences i18nPrefs;

// 英文文本资源表
static const char* const t_en[TXT_MAX] = {
    // Startup
    "Loading Satellite Orbit Models...",
    
    // WiFi Setup Page
    "WiFi Setup",
    "Scanning for networks...",
    "No networks found.",
    "Press [R] to rescan",
    "Connect to:",
    "Password:",
    "[Enter] Connect   [ESC] Cancel",
    "Select Network:",
    "[^/v] Sel [Enter] Input [R] Scan [ESC] Exit",
    "Connecting...",
    
    // Tab Headers
    "Encyclopedia",
    "Recent Launch",
    
    // Satellite / Encyclopedia View
    "Downloading...",
    "ID: ",
    "GP Age: ",
    "GP Age:N/A",
    "No description.",
    "Press 'd' to delete",
    "Weather Imaging",
    "Mode: ",
    "Custom added satellite.\n\n",
    "AOS: ",
    "LOS: ",
    
    // Recent Launch Page
    "Downloading GP JSONs...",
    "Recent Launch is an online feature.",
    "Press 'w' to connect WiFi",
    "& download latest launcher groups.",
    "Age: ",
    "Epoch: ",
    "Rep: ",
    "Objects: ",
    "Orbit: ",
    "Status: ",
    
    // Formation State Text
    "Operational",
    "Tight Train",
    "Train Formation",
    "Expanding",
    
    // Coordinate HUD
    "alt",
    
    // Compass Mode
    "Camera View Mode",
    "Press 'S' to set reference",
    
    // Help Panel
    "--- Help & Shortcuts ---",
    "Brightness[ [/] ]",
    "GNSS Location[G]",
    "Help Menu[H]",
    "HUD Toggle[Back]",
    "View Lock[Spc]",
    "Passes Panel[Enter]",
    "Satellite List[S]",
    "Time Machine[ , / . ]",
    "Sat View[V]",
    "WiFi Toggle[W]",
    "Manual Pos[C]",
    "Reset Time[R]",
    "Color Filter[Tab]",
    
    // Recommended Passes Panel
    " RECOMMENDED PASSES",
    "No passes in 7 days",
    "Calculating...",
    "Waiting for time sync...",
    "Score:",
    "Mag:",
    "Reason:",
    "Dark sky",
    "+Zenith",
    "+Long",
    "Name: ",
    "AOS: ",
    "Max El: ",
    "LOS: ",
    "Duration: ",
    "Min El: ",
    "Trackable: ",
    "Yes",
    "No",
    "Press OK to lock AOS time",
    
    // Sun Data Page
    "Sun Data",
    "Azimuth: ",
    "Elevation: ",
    "Distance: ",
    "RA: ",
    "Dec: ",
    "Dec: ",
    "Sunrise: ",
    "Sunset: ",
    "Noon: ",
    "Press ESC to return",
    
    // Time Machine Page
    "Time Machine",
    "Use arrow keys to adjust time",
    "Press OK to confirm",
    "Press ESC to cancel",
    "Current Time:",
    
    // Quick Setup Dialog
    "Settings",
    "Lon:",
    "Lat:",
    "Alt(m):",
    "OK",
    "Tab: field, Del: delete, OK: save",
    
    // Position Settings Page
    "Position Settings",
    "Longitude:",
    "Latitude:",
    "Altitude (m):",
    "UP/DOWN: +/- value",
    "LEFT/RIGHT: select field",
    "OK: save  ESC: cancel",
    
    // Language Dialog
    "Select Language",
    "Language",
    
    // Custom NORAD ADD Text
    "Enter 5 or 6-digit NORAD ID to add custom satellite.",
    "Source: celestrak.org",
    
    // Extra Recent Launch Page Detail Specs
    "Visibility:",
    "Excellent",
    "Moderate",
    "N/A",
    "Press 'O' for Objects",
    "No Objects Found.",
    
    // Status Feedback / Error Msg / Banner info
    "System Busy... Wait.",
    "Task Init Failed!",
    "Connecting WiFi...",
    "WiFi Disconnected.",
    "Refreshing GP JSON...",
    "Parse Cache Failed!",
    "Update Success: Cache overwritten!",
    "Cached (<2h old). Press C to force.",
    "Update Failed: ",
    
    // Tree categories in passes panel
    "Tonight",
    "Next 7 Days",
    "Highly Recommended",
    "All Passes"
};

// 中文文本资源表
static const char* const t_zh[TXT_MAX] = {
    // Startup
    "正在加载卫星轨道模型...",
    
    // WiFi Setup Page
    "无线网络设置",
    "正在扫描网络...",
    "未找到任何无线网络。",
    "按 [R] 键重新扫描",
    "连接至:",
    "密码:",
    "[Enter] 连接     [ESC] 取消",
    "选择无线网络:",
    "[^/v] 选择  [Enter] 输入密码  [R] 扫描  [ESC] 退出",
    "正在连接...",
    
    // Tab Headers
    "卫星百科",
    "最新发射",
    
    // Satellite / Encyclopedia View
    "正在下载...",
    "编号: ",
    "GP Age: ",
    "GP Age: N/A",
    "无简介描述。",
    "按 'd' 键删除",
    "气象成像",
    "模式: ",
    "自定义添加的卫星。\n\n",
    "过境开始: ",
    "过境结束: ",
    
    // Recent Launch Page
    "正在下载发射数据...",
    "最新发射是网络功能。",
    "按 'w' 键连接无线网络",
    "并下载最新的卫星发射组数据。",
    "Age: ",
    "历元: ",
    "代表卫星: ",
    "卫星数: ",
    "可见: ",
    "状态: ",
    
    // Formation State Text
    "运行中",
    "紧密链",
    "编队中",
    "扩散中",
    
    // Coordinate HUD
    "高度",
    
    // Compass Mode
    "相机观察模式",
    "按 'S' 键设置参考方向",
    
    // Help Panel
    "--- 帮助与快捷键 ---",
    "屏幕亮度[ [/] ]",
    "卫星定位[G]",
    "帮助菜单[H]",
    "界面开关[Back]",
    "视角校准[Spc]",
    "过境推荐[Enter]",
    "卫星百科[S]",
    "时间机器[ , / . ]",
    "追焦视角[V]",
    "无线网络[W]",
    "手工位置[C]",
    "重置时间[R]",
    "色彩滤镜[Tab]",
    
    // Recommended Passes Panel
    " 推荐过境事件",
    "7天内无过境事件",
    "正在计算...",
    "等待时间同步...",
    "推荐指数:",
    "亮度:",
    "原因:",
    "夜空暗",
    "+天顶",
    "+持续长",
    "名称: ",
    "进入(AOS): ",
    "最大仰角: ",
    "离开(LOS): ",
    "持续时间: ",
    "最小仰角: ",
    "可观测性: ",
    "是",
    "否",
    "按 OK 键跳转到 AOS 时间",
    
    // Sun Data Page
    "太阳数据",
    "方位角: ",
    "仰角: ",
    "距离: ",
    "赤经: ",
    "赤纬: ",
    "赤纬: ",
    "日出: ",
    "日落: ",
    "正午: ",
    "按 ESC 键返回",
    
    // Time Machine Page
    "时间机器",
    "使用左右方向键调整时间",
    "按 OK 键确认",
    "按 ESC 键取消",
    "当前设置时间:",
    
    // Quick Setup Dialog
    "位置配置",
    "经度:",
    "纬度:",
    "海拔(米):",
    "确定",
    "Tab: 切换字段, Del: 删除, OK: 保存",
    
    // Position Settings Page
    "位置参数设置",
    "经度:",
    "纬度:",
    "海拔 (米):",
    "UP/DOWN: 增/减数值",
    "LEFT/RIGHT: 切换输入项",
    "OK: 保存  ESC: 取消",
    
    // Language Dialog
    "选择语言",
    "语言设置",
    
    // Custom NORAD ADD Text
    "输入5位或6位 NORAD 编号以添加自定义卫星。",
    "数据来源: celestrak.org",
    
    // Extra Recent Launch Page Detail Specs
    "可观测性:",
    "极佳",
    "中等",
    "无数据",
    "按 'O' 键查看卫星列表",
    "未找到任何卫星。",
    
    // Status Feedback / Error Msg / Banner info
    "系统繁忙，请稍候...",
    "任务初始化失败！",
    "正在连接无线网络...",
    "无线网络已断开。",
    "正在刷新轨道数据...",
    "解析本地缓存失败！",
    "更新成功：缓存已被覆写！",
    "检测到2小时内缓存，按 C 键强制更新。",
    "更新失败：",
    
    // Tree categories in passes panel
    "今晚",
    "未来7天",
    "强烈推荐",
    "所有过境"
};

void I18N::begin() {
    if (_initialized) return;
    i18nPrefs.begin("i18n", false);
    int langCode = i18nPrefs.getInt("lang", -1);
    
    if (langCode == -1) {
        // 未设置过语言（首次启动），默认设为 LANG_EN
        _currentLang = LANG_EN;
    } else {
        _currentLang = (langCode == 1) ? LANG_ZH : LANG_EN;
    }
    _initialized = true;
}

Language I18N::getLanguage() {
    if (!_initialized) begin();
    return _currentLang;
}

void I18N::setLanguage(Language lang) {
    if (!_initialized) begin();
    _currentLang = lang;
    i18nPrefs.putInt("lang", (int)lang);
}

const char* I18N::get(TextId id) {
    if (id < 0 || id >= TXT_MAX) return "";
    return (_currentLang == LANG_ZH) ? t_zh[id] : t_en[id];
}

bool I18N::isFirstStart() {
    if (!_initialized) begin();
    // 如果 lang 还没被写入，即为首次启动
    return !i18nPrefs.isKey("lang");
}

void I18N::setFirstStartDone() {
    if (!_initialized) begin();
    // 写入当前语言即可消除首次启动标记
    i18nPrefs.putInt("lang", (int)_currentLang);
}

const char* I18N::getSatDescription(int noradId) {
    return Encyclopedia::getDescription(noradId);
}
