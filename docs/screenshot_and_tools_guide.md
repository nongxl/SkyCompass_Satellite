# 高保真屏幕截图与辅助工具指南 (Screenshot Tool & Utilities)

本项目为在 ESP32 极度受限硬件资源（无 PSRAM）下实现流畅运行与高清调试，开发了一套独创的**零压缩串口原始像素截图传输系统**，并在 `scripts/` 目录下配套了多项离线数据预处理和自动化工具。

---

## 一、高保真屏幕截图功能指南

Cardputer 搭载的 ESP32-S3 内部 SRAM 极为紧张，无法容纳完整的 PNG/JPEG 图像编码器及大块动态内存缓冲区。为此，本项目绕过传统单片机图片压缩思路，首创了**零压缩原始像素分段串口传输机制**。

### 1.1 设备端触发方式
- 在任意主界面下，按下 Cardputer 侧面的 **BtnA（即 GPIO0 物理按键，位于电源开关右侧）**。
- 屏幕右上角会即刻浮现 `Capturing screen...` 提示。
- 设备将屏幕画布（Sprite）中 240x135 像素的 16 位原始 RGB565 帧缓冲区切分为固定大小的数据段，通过 Base64 编码并附加魔术报头 `==SKYCOMPASS_DATA==` 发往 USB 虚拟串口通道。
- 传输过程不阻塞主推演线程，完成后提示自动消隐。

### 1.2 电脑端监听与无损重构

在电脑端运行配套的 Python 脚本来监听并实时还原截图：

1. **安装 Python 依赖库**：
   ```bash
   pip install pyserial Pillow
   ```
2. **启动监听服务**：
   在项目根目录下执行：
   ```bash
   python scripts/get_screenshot.py
   ```
   *该脚本具备串口自动探测与热插拔扫描机制，能动态剥离 ANSI 颜色控制字符和并发调试日志干扰。*
3. **自动捕获与保存**：
   当在 Cardputer 上按下截图按键时，电脑端终端会自动捕获 Base64 数据流并重构为 24 位高保真的 `.bmp` 原始图片，存放于项目的 `screenshot/` 文件夹中。

---

## 二、离线数据预处理与辅助工具链 (`scripts/`)

为了将繁重的几何拓扑、大地测量与天文三角函数运算移至编译期或云端，并实现开机免网即用与网络防限流保护，项目配备了以下预处理与自动化脚本：

### 2.1 [update_builtin_tles.py](file:///d:/workspace/SkyCompass_Satellite/scripts/update_builtin_tles.py) (出厂 TLE 自动同步与固件头文件烘焙)
* **功能**：
  1. 静态遍历解析 [encyclopedia.cpp](file:///d:/workspace/SkyCompass_Satellite/src/core/encyclopedia.cpp)，自动收集全部 59 颗预置人造天体的 NORAD ID；
  2. 自动化发起网络抓取，优先请求 CelesTrak 官方 GP API，遭遇 403 限流或超时时无缝切换至备用开源卫星数据镜像；深空望远镜（JWST、Roman 等）应用地日 L2 轨道动力学拟合，确保 100% 覆盖率；
  3. 格式化生成包含 59 颗卫星最新历元 TLE 的 C++ 只读头文件 [builtin_tles.h](file:///d:/workspace/SkyCompass_Satellite/src/core/builtin_tles.h)。
* **防封机制与离线建表**：解决首次烧录设备因本地无缓存连续触发云端高频限制（HTTP 403）的痛点；固件启动时会自动将出厂烘焙数据写入 LittleFS 文件系统（Cache Seeding），确保即刻离线精确预测，开机联网直接复用有效缓存，零冗余网络请求。

### 2.2 [gen_light_points.py](file:///d:/workspace/SkyCompass_Satellite/scripts/gen_light_points.py) (全球光害夜景点云采样)
* **功能**：从 NASA GIBS 官方服务下载最新的 VIIRS Black Marble 全球夜景瓦片地图，采用分层重要性采样（Stratified Importance Sampling）提取出 12000 个高对比度夜景地标点。
* **产出**：输出预计算好球面三维坐标的 [light_points_data.h](file:///d:/workspace/SkyCompass_Satellite/src/core/light_points_data.h)，使设备在 3D 渲染时免去实时三角函数解算，帧率稳固在 30 FPS。

### 2.3 [generate_timezone_grid.py](file:///d:/workspace/SkyCompass_Satellite/scripts/generate_timezone_grid.py) (离线全球时区网格生成)
* **功能**：生成全球 $1^\circ \times 1^\circ$ 精度的离线时区映射网格（180 行 × 360 列），公海等区域采用航海时区自动对齐。
* **产出**：生成 [timezone_grid.h](file:///d:/workspace/SkyCompass_Satellite/src/core/timezone_grid.h)，实现设备在纯 GPS 信号断网环境下秒级获取本地时区偏移量。

### 2.4 [optimize_earth_data.py](file:///d:/workspace/SkyCompass_Satellite/scripts/optimize_earth_data.py) (大陆轮廓线离线三角函数预计算)
* **功能**：对全球陆地边界经纬度线段数据进行离线三角函数预计算（$\sin Lat, \cos Lat, \sin Lon, \cos Lon$ 等）。
* **产出**：更新 [earth_data.h](file:///d:/workspace/SkyCompass_Satellite/src/core/earth_data.h)，消除旋转变换矩阵中的重复三角函数开销。

### 2.5 [update_frequencies.py](file:///d:/workspace/SkyCompass_Satellite/scripts/update_frequencies.py) (云端业余无线电频率同步)
* **功能**：结合 GitHub Actions 自动化流水线，定期从卫星通联数据库拉取最新的 NORAD 卫星下行通联频率和调制模式。
* **产出**：更新生成 `data/frequencies.json`，供设备在联网时一键更新。

### 2.6 [generate_schematic.py](file:///d:/workspace/SkyCompass_Satellite/scripts/generate_schematic.py) (硬件接线与电气拓扑示意图生成)
* **功能**：采用 PIL/Pillow 矢量排版引擎，将 Cardputer 主控、Cap LoRa-1262、Unit 8Servos 浑仪云台、Chain Mono 副屏与 Unit GPS 的端口电气定义、引脚线序与 3 大预设硬件拓扑方案自动绘制为高清示意图。
* **产出**：生成 [docs/schematic_diagram.png](file:///d:/workspace/SkyCompass_Satellite/docs/schematic_diagram.png) 硬件接线白皮书图表。

---

## 三、算法原型验证与仿真目录 (`scratch/`)

开发过程中编写的算法验证、仿真对比脚本均归档在 `scratch/` 目录下，供有深度研究需求者查阅：

* **轨道力学与真值校验**：
  - `test_sgp4_2024.py` / `test_sgp4_2026.py`：调用 Python `sgp4` 库对比验证单片机 C++ SGP4 数值推演精度，防止单精度浮点溢出。
  - `test_gmst.py` / `verify_coord.py`：格林尼治平恒星时计算与 ENU 站心转换精度验证。
* **本影与光照物理仿真**：
  - `test_shadow4.py` / `test_shadow5.py`：地球本影圆锥遮挡判断数学模型仿真。
  - `test_sun_alt.py` / `test_sun_alt_exact.py`：太阳直射点与高度角计算对比。
* **点云与星表提取**：
  - `generate_stars.py`：从耶鲁亮星表中提取关键导航星体三维坐标。
  - `gen_map.py`：基于 Natural Earth 50m 分辨率 GeoJSON 轮廓自适应稀疏滤波重采样。
