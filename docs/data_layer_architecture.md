# 数据层与多级缓存架构 (Data Layer Architecture)

为了兼容 CelesTrak 自 2026 年起引入的 6 位 Catalog Number（100000+）且不再提供传统 TLE 格式的重大改变，本项目重构了底层数据架构，实现了**数据源格式与轨道物理动力学推演引擎的完全解耦**。

---

## 1. 架构设计与数据流向

```text
       ┌────────────────────────┐
       │   Network / WiFi Task  │
       └───────────┬────────────┘
                   │ HTTP GET
                   ▼
       ┌────────────────────────┐
       │   OrbitDataProvider    │
       └───────────┬────────────┘
                   │ Raw Payload
                   ▼
       ┌────────────────────────┐
       │      OrbitParser       │
       │ (TLEParser/JSONParser) │
       └───────────┬────────────┘
                   │ Parse & Map
                   ▼
       ┌────────────────────────┐
       │      OrbitRecord       │  ◄── 唯一可信数据对象 (Data Transfer Object)
       └───────────┬────────────┘
                   │ Pseudo TLE Bridge
                   ▼
       ┌────────────────────────┐
       │   SGP4 Physics Engine  │
       └───────────┬────────────┘
                   │ Orbit State (Az, El, Range, Lon, Lat)
                   ▼
       ┌────────────────────────┐
       │   Application Layers   │
       │ (3D Earth, GUI, HUD,   │
       │  Gimbal, RF Console)   │
       └────────────────────────┘
```

---

## 2. 支持的数据源格式 (Supported Formats)

### 2.1 CelesTrak GP JSON (Default 首选推荐)
- **标准来源**：基于 CCSDS OMM (Orbit Mean-Elements Message) 标准字段定义的 JSON 衍生格式。
- **解析机制**：由 [JSONParser](file:///d:/workspace/SkyCompass_Satellite/src/core/json_parser.h) 原生实现流式反序列化。
- **关键优势**：
  - 完全原生兼容 6 位以上的 NORAD Catalog ID（例如 `100001`）。
  - 支持直接携带空间物体的国际代号、历元信息、标准星等（Standard Visual Magnitude）与雷达散射截面（RCS）。

### 2.2 TLE (Legacy 传统双行元素法)
- **兼容策略**：由 [TLEParser](file:///d:/workspace/SkyCompass_Satellite/src/core/tle_parser.h) 提供支持，自动兼容已有的离线卫星历史缓存。
- **适用场景**：第三方小型地面站导出的经典 TLE 文本、开源社区离线合集等。

### 2.3 OMM (Reserved 预留规范)
- **规范背景**：CCSDS 原始轨道参数电文标准，包括 OMM XML 与 OMM KVN 格式。
- **架构考量**：
  - OMM XML 报文体相对冗长（体积较传统 TLE 膨胀约 7 倍以上），在 ESP32 嵌入式芯片上进行复杂的 DOM/SAX XML 树解析不仅内存开销极大，也容易引发堆碎片。
  - 架构上预留了 [OrbitParser](file:///d:/workspace/SkyCompass_Satellite/src/core/orbit_parser.h) 抽象基类，未来若出现纯 XML/KVN 的直收场景，可按需扩展派生解析器。

---

## 3. 六位 ID 兼容与 Pseudo TLE 桥接技术

底层成熟的 C++ 轨道动力学库（如 SGP4）大多基于定长字符串的 TLE 结构设计，无法直接识别 6 位以上的纯数字 NORAD ID。为兼顾极致的物理计算稳定性与现代数据标准，本项目采用了 **Pseudo TLE 桥接机制**：

1. **真实数据层保留**：业务展示层、卫星选择列表、UI HUD 以及持久化文件全部采用 64 位整数保存真实的 6 位 NORAD Catalog Number。
2. **要素逆向重构**：当需要调用 SGP4 初始化轨道状态时，由桥接适配器根据 `OrbitRecord` 中的半长轴、偏心率、倾角、近地点幅角、升交点赤经与平近点角等开普勒根数，动态重构出标准格式的内存 Pseudo TLE。
3. **安全透明**：对上层业务完全无感，既享受了 6 位新标准，又规避了重写整个空间动力学底层所带来的数值稳定性风险。

---

## 4. 智能分级多缓存时效策略

针对业余卫星追踪爱好者在野外无网环境下的使用需求，系统设计了多级缓存与按需更新机制：

| 数据类别 | 缓存载体 | 默认有效期 | 策略说明 |
| :--- | :--- | :--- | :--- |
| **卫星轨道根数 (TLE / JSON)** | LittleFS 本地闪存 | **48 小时** | 轨道摄动随时间积累，48小时内推演精度完全满足肉眼与手持八木天线通联；只要本地未过期，开机秒速加载，优先保障零网络延迟。 |
| **无线电频率与静态百科** | LittleFS (`frequencies.json`) | **7 天 (168 小时)** | 频率与硬件参数变动极低，延长缓存阈值以降低不必要的网络请求。 |
| **全球光害夜景点云** | Flash 代码段 (`light_points_data.h`) | **永久内置** | 离线预编译 12000 个采样点，零网络开销，开机立即可用。 |
| **全球离线时区网格** | Flash 代码段 (`timezone_grid.h`) | **永久内置** | $1^\circ \times 1^\circ$ 航海与陆地离线时区映射，无网 GPS 秒定本地时区。 |

### 惰性同步机制
仅在以下两种情况才会启动 WiFi 并拉取新数据：
1. 本地关键缓存时间戳超过设定阈值（如 TLE 超过 48 小时）；
2. 用户在主界面手动长按/按下 `W` 键主动发起强制刷新。

网络同步结束后，系统立刻自动关闭 WiFi 射频以保护有限的电池电量。
