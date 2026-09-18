# SkyCompass Satellite (人造天体观测罗盘)

**简体中文** | [English](README_EN.md)

> **🔥 固件已发布至 M5Burner！您可以在 M5Burner 官方烧录工具中直接搜索 `SkyCompass Satellite` 并一键免编译烧录体验。**

![SkyCompass Satellite Cover](cover.jpg)

## 项目概述

**SkyCompass Satellite** 是 SkyCompass 项目的扩展演化版本，运行在 **M5Stack Cardputer ADV (ESP32-S3)** 上。与原项目关注自然天体（太阳、月亮等）不同，Satellite 版本专注于**人造飞行器（如国际空间站 ISS、中国空间站天宫、哈勃望远镜等）**的实时空间轨道追踪与过境可视性预测。

**核心产品定位：** 向用户回答：**“今晚有哪些人造天体值得抬头观测？”**。它不仅是一个三维空间轨迹展示器，更是一个融合天文力学与光学评价的“今晚值得看”人造天体推荐系统。

---

## 核心功能亮点

- 🌍 **3D 极简天球地球仪**：内置矢量大陆轮廓、太阳昼夜晨昏线阴影、极地视觉锚点与 12000 点 NASA 夜景光害点云。
- ⭐ **智能观测推荐引擎**：综合地平仰角、太阳照明与地球本影遮挡，动态预测视星等，自动推荐最佳过境肉眼可见窗口。
- ⏳ **时光机时间穿梭 (Time Machine)**：支持随时快进/快退模拟任意未来时刻的过境轨迹，具备 120ms 计算冷却门限保证 60 FPS 流畅操作。
- 📻 **全天候业余无线电 (HAM) 通联辅助**：实时多普勒频移补偿计算、APRS/SSTV 双通道频移监听、空间遥测数据包抓取控制台。
- 🧭 **独创太阳日影物理对齐法 (Solar Shadow Alignment)**：在无内置电子罗盘硬件环境下，利用地表标针投影与现实影子平行重合实现三维空间 100% 精准对齐。
- 🪐 **三轴浑仪乐高云台物理联动 (Orbital Arch Gimbal)**：I2C 联动三轴机械拱门机构，物理复现卫星飞行航向、过境最大仰角与实时太空滑行轨迹。
- 🔋 **全天候离线生存与智能电源管理**：48 小时 TLE 本地闪存多级缓存，GNSS 搜星后自动休眠，断网野外秒级启动。

---

## 硬件支持与接线

- **主机平台**：M5Stack Cardputer (兼容 ADV 版与 v1.1 标准版，自适应引脚重映射)
- **定位授时**：内置/外置 GNSS 模块 (GPS/北斗/GLONASS，串口 115200bps@8N1)
- **姿态感应**：板载六轴 IMU (MPU6886 / SH200Q，独立 I2C 通道，与 GPS 并发运行互不干扰)
- **机械联动**：M5Stack Unit 8Servos / PCA9685 舵机驱动板 + 乐高三轴立体浑仪机构
- **无线电扩展**：M5Stack Cap LoRa-1262 射频通信单元 (SX1262)

![硬件接线示意图](docs/schematic_diagram.png)

---

## 实机交互与按键速查

| 按键 | 功能操作 | 说明 |
| :--- | :--- | :--- |
| **`Enter`** | 展开/收起推荐面板 | 在面板中按 `Enter` 查看过境详情或直跳 AOS 时刻 |
| **`S`** | 卫星选择与百科全书 | `f` 分类筛选，`p` 快速配置/清除射频频段，`d` 删除自选，`O` 发射单星 |
| **`W`** | WiFi 连接与数据更新 | 扫描热点配网，执行 NTP 对时与 TLE 轨道根数拉取 |
| **`H`** | 快捷键帮助菜单 | 屏幕中央悬浮呼出按键提示，支持多语言适配 |
| **`L`** | 切换多语言 (i18n) | 支持中文、English、日本語、Español 实时切换并持久化 |
| **`M`** | 硬件配置向导 (Wizard) | 自由检测并开关 WiFi、GNSS、地磁仪、浑仪云台外设 |
| **`V`** | 卫星跟随视角 (Sat View) | 进入目标跟踪视角；`;` / `.` 切换目标，投射 3D 激光视线与仰角 |
| **`Ctrl`** | 射频终端控制台 (RF Console) | 实时多普勒频移监控、信号瀑布能量图与空间抓包；`Tab` 切瀑布/报文，`T` 测注入 |
| **`Aa` (Shift)** | 浑仪舵机测试模式 | `0/1/2` 切换轴，`,`/`/` 微调角度，内置 30°~180° 防卡死保护 |
| **`C`** | 十字准星手动定位 | `;`/`.` 调纬度，`,`/`/` 调经度，`[`/`]` 调海拔高度 |
| **`,` / `/`** | 时光机快退 / 快进 | 短按步进 60 秒，长按高速穿梭（时间偏移时 HUD 呈黄色） |
| **`R`** | 重置时光机与坐标 | 瞬间恢复到真实物理时间与默认观测点 |
| **`Tab`** | 循环切换显示模式 | 主界面切换普通/极光/红光护眼模式；在 RF Console 切换瀑布图/报文列表 |
| **`Space`** | IMU 视角姿态锁定 | 锁定/释放当前体感跟踪视角 |
| **`Del`** | 切换 HUD 状态角标 | 隐藏/恢复四角遥测数据显示 |
| **`[` / `]`** | 调节屏幕背光亮度 | 硬件级调节 TFT 屏幕背光（16 ~ 255） |
| **`G0` (侧键)** | 触发高保真屏幕截图 | 原始 RGB565 像素流回传，PC 端脚本无损重构 24 位 BMP |
| **`Esc` / `~`** | 返回 / 退出当前浮层 | 退出当前对话框、推荐面板或跟随视角 |

---

## 工程目录与代码架构

本项目遵循分层解耦与高内聚的设计原则，核心代码划分清晰：

```text
SkyCompass_Satellite/
├── src/
│   ├── app/                      # 应用程序控制（时间机器、用户按键上下文）
│   ├── core/                     # 核心数学推算、天文力学与工具库
│   │   ├── orbit_utils.*         # SGP4 轨道采样推算、GEO 槽位、编队识别与视口焦点维护
│   │   ├── radio_tracking_pipeline.* # 离散多普勒频移微分、过境窗口探测与射频动态调谐
│   │   ├── coord_transform.*     # TEME / ECEF / 地心 / 站心拓扑坐标系精密转换
│   │   ├── earth_renderer.*      # 3D 地球仪矢量海岸线、卫星天球投影与昼夜晨昏线渲染
│   │   ├── observation_predictor.* # 7天过境事件预测、光学视星等与多属性评分仲裁引擎
│   │   ├── image_utils.*         # 夜视红光滤镜与串口 Base64 截图像素流传输
│   │   ├── text_utils.*          # UTF-8 字符宽度折行、受限高度文本绘制与平滑跑马灯
│   │   ├── encyclopedia.*        # 卫星百科全书静态数据库与分类标签
│   │   └── i18n.*                # 多语言国际化字典与字体字形支持
│   ├── gimbal/                   # 机械浑仪立体拱门云台驱动与自主巡天
│   │   ├── gimbal_tracking_pipeline.* # 全天自主巡天仲裁 (Sky Patrol) 与单星特写追踪 (Sat View)
│   │   └── gimbal_controller.*   # Unit 8Servos / PCA9685 底层 I2C 舵机角度驱动与限位保护
│   ├── hal/                      # 统一硬件抽象层 (HAL)
│   │   ├── hal_gnss.*            # GPS/北斗 串口驱动、NMEA 解包与低功耗待机
│   │   ├── hal_imu.*             # 六轴姿态传感器驱动 (MPU6886 / SH200Q)
│   │   ├── hal_wifi.*            # WiFi STA 扫描、连接管理与网络状态监听
│   │   └── hal_radio.*           # SX1262 (Cap LoRa-1262) 射频硬件 SPI 通讯抽象
│   ├── hardware/                 # 扩展硬件显示服务
│   │   └── chain_mono_view.*     # M5Chain 单色副屏刷新调度、倒计时与图标展示
│   ├── ui/                       # 独立页面与组件视图
│   │   ├── sat_select_view.*     # 卫星选择与百科大视图、多分类筛选弹窗
│   │   ├── recommendation_view.* # 左侧过境推荐卡片、折叠树视图与底部硬件双行状态栏
│   │   ├── rf_console_view.*     # 全屏射频控制台终端与原始报文 HEX 查看器
│   │   ├── wifi_setup_view.*     # WiFi 扫描分页热点列表与虚拟键盘配网
│   │   ├── servo_test_view.*     # 浑仪舵机三轴标定与微调调试视图
│   │   ├── hardware_wizard_view.*# 硬件外设配置向导 (按 M 键呼出)
│   │   ├── dialog_views.*        # 快捷键帮助浮窗（智能黄色高亮）与多语言切换对话框
│   │   └── startup_view.*        # 开机 3D 地球仪体感自转与初始化加载条
│   └── main.cpp                  # 系统初始化入口 setup()、主事件泵 loop() 与轨推后台任务
├── docs/                         # 专题技术白皮书与硬件设计文档
├── scripts/                      # PC 端配套工具（截图监听脚本、地图与点云生成脚本）
└── platformio.ini                # PlatformIO 编译与依赖库配置文件
```

---

## 详细技术专题与开发文档

深入研究底层原理、物理算法与拓展开发，请参阅 `docs/` 目录下的专题文档：

| 专题技术文档 | 核心内容概述 |
| :--- | :--- |
| 📐 [**轨道推演与可见性预测技术细节**](docs/orbital_calculation_details.md) | SGP4 摄动模型、TEME/ECEF/ENU 坐标转换、大气折射/消光、视星等相函数、多普勒频移物理推导与太阳日影对齐原理 |
| 💾 [**数据层与多级缓存架构**](docs/data_layer_architecture.md) | 兼容 6 位 NORAD Catalog ID 的 Pseudo TLE 桥接技术、GP JSON 格式解析、48h TLE/7d API 离线分级缓存策略 |
| 🛠️ [**固件编译与烧录指南**](docs/firmware_build_guide.md) | M5Burner 免编译体验、PlatformIO CLI 本地构建与烧录、环境依赖与常见故障排查 |
| 📸 [**高保真屏幕截图与辅助工具链**](docs/screenshot_and_tools_guide.md) | 无 PSRAM 架构下零压缩 RGB565 分段串口截图机制、PC 监听脚本及离线点云地图预处理工具 |
| 🔭 [**浑仪卫星过境指向功能设计与使用指南**](docs/浑仪卫星过境指向功能设计与使用指南.md) | 机械三轴立体拱门联动原理、舵机死区与活动角度标定、巡天仲裁机制 |
| 📡 [**卫星 LoRa+UHF 接收解析实现方案**](docs/卫星LoRa+UHF-GFSK数据接收解析与长期记录系统实现方案v2.md) | SX1262 射频前端、动态多普勒自动调谐与 AX.25 遥测报文解析架构 |

---

## 软件界面实机演示

| 3D 轨迹主视角与卫星环绕 | 卫星百科、特制图标与 HAM 频率 | 快捷键帮助悬浮框 |
| :---: | :---: | :---: |
| ![3D轨迹](screenshot/skycompass_20260621_142730.png) | ![卫星百科](screenshot/skycompass_20260621_142358.png) | ![快捷键帮助](screenshot/skycompass_20260621_142315.png) |

---

## 数据来源与致谢

- **TLE 轨道数据**：感谢 [CelesTrak](https://celestrak.org/) 提供高精度、实时更新的轨道根数服务。
- **3D 陆地轮廓矢量数据**：感谢 [Natural Earth](https://www.naturalearthdata.com/) 提供的 50m 分辨率全球陆地边界矢量数据。
- **全球夜景灯光点云**：感谢 [NASA GIBS](https://gibs.earthdata.nasa.gov/) 提供的 VIIRS Black Marble 全球夜景图源。
- **模型校准与仿真参考**：感谢 [天文通](https://laysky.com/) 与 [Heavens-Above](https://www.heavens-above.com/) 提供的模拟比对支持。

---

## 许可证

本项目采用 [GNU General Public License v3.0 (GPLv3)](LICENSE) 开源许可证。
