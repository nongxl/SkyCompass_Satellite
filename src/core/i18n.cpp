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
    "+Bright",
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
    "Update Success: Data updated!",
    "Data is fresh (<2h old). Press C to force.",
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
    "理由:",
    "夜空暗",
    "+高亮",
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
    "更新成功：数据已最新！",
    "数据已最新（2小时内已同步），按 C 键强刷。",
    "更新失败：",
    
    // Tree categories in passes panel
    "今晚",
    "未来7天",
    "强烈推荐",
    "所有过境"
};

// 日文文本资源表
static const char* const t_ja[TXT_MAX] = {
    // Startup
    "衛星軌道モデルを読み込み中...",
    
    // WiFi Setup Page
    "WiFi設定",
    "ネットワークをスキャン中...",
    "WiFiが見つかりません。",
    "[R]キーで再スキャン",
    "接続先:",
    "パスワード:",
    "[Enter] 接続     [ESC] キャンセル",
    "ネットワークを選択:",
    "[^/v] 選択 [Enter] 入力 [R] スキャン [ESC] 終了",
    "接続中...",
    
    // Tab Headers
    "衛星図鑑",
    "最新打上げ",
    
    // Satellite / Encyclopedia View
    "ダウンロード中...",
    "ID: ",
    "GP経過時間: ",
    "GP経過時間: N/A",
    "説明はありません。",
    "'d'キーで削除",
    "気象観測",
    "モード: ",
    "カスタム追加衛星。\n\n",
    "AOS: ",
    "LOS: ",
    
    // Recent Launch Page
    "GP JSONをダウンロード中...",
    "最新打上げはオンライン機能です。",
    "'w'キーでWiFiに接続して",
    "最新の打上げグループを取得します。",
    "経過時間: ",
    "元Epoch: ",
    "代表: ",
    "物体数: ",
    "軌道: ",
    "ステータス: ",
    
    // Formation State Text
    "運用中",
    "密接トレイン",
    "編隊飛行",
    "拡散中",
    
    // Coordinate HUD
    "高度",
    
    // Compass Mode
    "カメラビューモード",
    "'S'キーで参照方向を設定",
    
    // Help Panel
    "--- ヘルプ＆ショートカット ---",
    "画面明るさ[ [/] ]",
    "GNSS位置[G]",
    "ヘルプメニュー[H]",
    "HUD表示切り替え[Back]",
    "視点ロック[Spc]",
    "通過予測一覧[Enter]",
    "衛星図鑑[S]",
    "タイムマシン[ , / . ]",
    "衛星追尾視点[V]",
    "WiFi切り替え[W]",
    "手動位置設定[C]",
    "時刻リセット[R]",
    "カラーフィルター[Tab]",
    
    // Recommended Passes Panel
    " 推奨の通過イベント",
    "7日以内に通過なし",
    "計算中...",
    "時刻同期待ち...",
    "スコア:",
    "等級:",
    "理由:",
    "暗い夜空",
    "+高輝度",
    "+天頂近く",
    "+長時間",
    "名称: ",
    "AOS: ",
    "最大仰角: ",
    "LOS: ",
    "継続時間: ",
    "最小仰角: ",
    "追尾可能: ",
    "はい",
    "いいえ",
    "OKキーでAOS時刻へ移動",
    
    // Sun Data Page
    "太陽データ",
    "方位角: ",
    "仰角: ",
    "距離: ",
    "赤経: ",
    "赤緯: ",
    "赤緯: ",
    "日の出: ",
    "日の入: ",
    "正午: ",
    "ESCキーで戻る",
    
    // Time Machine Page
    "タイムマシン",
    "左右キーで時間を調整",
    "OKキーで確定",
    "ESCキーでキャンセル",
    "現在設定時刻:",
    
    // Quick Setup Dialog
    "位置設定",
    "経度:",
    "緯度:",
    "標高(m):",
    "OK",
    "Tab: 項目切替, Del: 削除, OK: 保存",
    
    // Position Settings Page
    "位置パラメータ設定",
    "経度:",
    "緯度:",
    "標高 (m):",
    "UP/DOWN: 値の増減",
    "LEFT/RIGHT: 項目選択",
    "OK: 保存  ESC: キャンセル",
    
    // Language Dialog
    "言語を選択",
    "言語設定",
    
    // Custom NORAD ADD Text
    "カスタム衛星を追加するには5桁または6桁のNORAD IDを入力してください。",
    "データソース: celestrak.org",
    
    // Extra Recent Launch Page Detail Specs
    "視認性:",
    "極めて良好",
    "中程度",
    "データなし",
    "'O'キーで衛星一覧表示",
    "衛星が見つかりません。",
    
    // Status Feedback / Error Msg / Banner info
    "システム処理中...",
    "タスク初期化失敗！",
    "WiFi接続中...",
    "WiFi切断。",
    "GP JSON更新中...",
    "キャッシュ解析失敗！",
    "更新成功：最新データです！",
    "データは最新です(<2時間)。Cキーで強制更新。",
    "更新失敗: ",
    
    // Tree categories in passes panel
    "今夜",
    "今後7日間",
    "強く推奨",
    "すべての通過"
};

// 西班牙文文本资源表 (Standard ASCII rendering)
static const char* const t_es[TXT_MAX] = {
    // Startup
    "Cargando modelos orbitales...",
    
    // WiFi Setup Page
    "Configuracion WiFi",
    "Buscando redes...",
    "No se encontraron redes.",
    "Presione [R] para buscar de nuevo",
    "Conectar a:",
    "Contrasena:",
    "[Enter] Conectar   [ESC] Cancelar",
    "Seleccionar red:",
    "[^/v] Sel [Enter] Ingresar [R] Buscar [ESC] Salir",
    "Conectando...",
    
    // Tab Headers
    "Enciclopedia",
    "Lanzamientos recientes",
    
    // Satellite / Encyclopedia View
    "Descargando...",
    "ID: ",
    "Edad GP: ",
    "Edad GP: N/A",
    "Sin descripcion.",
    "Presione 'd' para eliminar",
    "Imagenes meteorologicas",
    "Modo: ",
    "Satelite personalizado.\n\n",
    "AOS: ",
    "LOS: ",
    
    // Recent Launch Page
    "Descargando JSONs GP...",
    "Lanzamientos recientes requiere conexion.",
    "Presione 'w' para conectar WiFi",
    "y descargar los grupos mas recientes.",
    "Edad: ",
    "Epoca: ",
    "Rep: ",
    "Objetos: ",
    "Orbita: ",
    "Estado: ",
    
    // Formation State Text
    "Operacional",
    "Tren compacto",
    "Formacion en tren",
    "Expandiendose",
    
    // Coordinate HUD
    "Alt",
    
    // Compass Mode
    "Modo de vista de camara",
    "Presione 'S' para fijar referencia",
    
    // Help Panel
    "--- Ayuda y atajos ---",
    "Brillo pantalla[ [/] ]",
    "Ubicacion GNSS[G]",
    "Menu de ayuda[H]",
    "Alternar HUD[Back]",
    "Bloquear vista[Spc]",
    "Panel de pases[Enter]",
    "Lista de satelites[S]",
    "Maquina del tiempo[ , / . ]",
    "Vista satelite[V]",
    "Alternar WiFi[W]",
    "Posicion manual[C]",
    "Reiniciar hora[R]",
    "Filtro de color[Tab]",
    
    // Recommended Passes Panel
    " PASES RECOMENDADOS",
    "Sin pases en 7 dias",
    "Calculando...",
    "Esperando sinc. de hora...",
    "Puntuacion:",
    "Mag:",
    "Razon:",
    "Cielo oscuro",
    "+Brillante",
    "+Zenit",
    "+Largo",
    "Nombre: ",
    "AOS: ",
    "El Max: ",
    "LOS: ",
    "Duracion: ",
    "El Min: ",
    "Rastreable: ",
    "Si",
    "No",
    "Presione OK para ir a hora AOS",
    
    // Sun Data Page
    "Datos del Sol",
    "Azimut: ",
    "Elevacion: ",
    "Distancia: ",
    "AR: ",
    "Dec: ",
    "Dec: ",
    "Amanecer: ",
    "Atardecer: ",
    "Mediodia: ",
    "Presione ESC para volver",
    
    // Time Machine Page
    "Maquina del tiempo",
    "Use las flechas para ajustar la hora",
    "Presione OK para confirmar",
    "Presione ESC para cancelar",
    "Hora actual:",
    
    // Quick Setup Dialog
    "Configuracion",
    "Long:",
    "Lat:",
    "Alt(m):",
    "OK",
    "Tab: campo, Del: borrar, OK: guardar",
    
    // Position Settings Page
    "Ajuste de posicion",
    "Longitud:",
    "Latitud:",
    "Altitud (m):",
    "ARRIBA/ABAJO: +/- valor",
    "IZQ/DER: seleccionar campo",
    "OK: guardar  ESC: cancelar",
    
    // Language Dialog
    "Seleccionar idioma",
    "Idioma",
    
    // Custom NORAD ADD Text
    "Ingrese ID NORAD de 5 o 6 digitos para agregar satelite.",
    "Fuente: celestrak.org",
    
    // Extra Recent Launch Page Detail Specs
    "Visibilidad:",
    "Excelente",
    "Moderada",
    "N/A",
    "Presione 'O' para objetos",
    "No se encontraron objetos.",
    
    // Status Feedback / Error Msg / Banner info
    "Sistema ocupado... Espere.",
    "Error al iniciar tarea!",
    "Conectando a WiFi...",
    "WiFi desconectado.",
    "Actualizando JSON GP...",
    "Error al leer cache!",
    "Actualizacion exitosa!",
    "Datos actualizados (<2h). Presione C para forzar.",
    "Error al actualizar: ",
    
    // Tree categories in passes panel
    "Esta noche",
    "Proximos 7 dias",
    "Muy recomendado",
    "Todos los pases"
};

const lgfx::IFont* I18N::getFont() {
    if (getLanguage() == LANG_JA) {
        return &fonts::efontJA_12;
    }
    return &fonts::efontCN_12;
}

void I18N::begin() {
    if (_initialized) return;
    i18nPrefs.begin("i18n", false);
    int langCode = i18nPrefs.getInt("lang", -1);
    
    if (langCode >= 0 && langCode <= 3) {
        _currentLang = (Language)langCode;
    } else {
        _currentLang = LANG_EN;
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
    switch (_currentLang) {
        case LANG_ZH: return t_zh[id];
        case LANG_JA: return t_ja[id];
        case LANG_ES: return t_es[id];
        case LANG_EN:
        default:      return t_en[id];
    }
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
