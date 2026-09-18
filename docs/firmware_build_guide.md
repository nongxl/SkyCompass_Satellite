# 固件编译与烧录指南 (Firmware Build & Flash Guide)

本项目基于 **PlatformIO** 构建，面向 **M5Stack Cardputer** (ESP32-S3) 硬件平台。您可以根据需要选择“免编译一键烧录”或“本地源码编译烧录”。

---

## 方式一：M5Burner 官方工具一键烧录 (推荐普通用户)

无需配置复杂的 C++ 开发环境，直接使用 M5Stack 官方烧录工具即可完成体验：

1. 下载并安装官方烧录工具：[M5Burner 官网下载](https://docs.m5stack.com/en/download)；
2. 打开 M5Burner，在左侧设备列表中选择 **Cardputer**；
3. 在搜索栏中输入 **`SkyCompass Satellite`**；
4. 将 Cardputer 通过 Type-C 数据线连接至电脑，点击 **Burn** 按钮即可一键下载并烧录最新稳定固件。

---

## 方式二：本地源码编译烧录 (面向开发者)

### 1. 环境准备

推荐使用以下两种开发环境之一：
- **VS Code + PlatformIO IDE 插件** (推荐图形化操作)
- **PlatformIO Core (CLI 命令行)** (适合高级开发者与自动化 CI/CD)

#### 安装 PlatformIO CLI (可选命令行工具)
```bash
pip install -U platformio
```

---

### 2. 获取代码与依赖检查

```bash
# 克隆仓库
git clone https://github.com/nongxl/SkyCompass_Satellite.git
cd SkyCompass_Satellite
```

项目的依赖库均已在 [platformio.ini](file:///d:/workspace/SkyCompass_Satellite/platformio.ini) 中声明，PlatformIO 在首次编译时会自动下载并缓存以下核心组件：
* `espressif32@6.7.0` (ESP32-S3 Arduino Core)
* `M5Unified#0.2.13` & `M5Cardputer#1.1.1` & `M5Gfx#0.2.19`
* `TinyGPSPlus` (GNSS NMEA 协议解析)
* `Sgp4-Library` (NORAD SGP4/SDP4 空间轨道传播力学模型)
* `ArduinoJson` (CelesTrak GP JSON 流式反序列化)
* `RadioLib` (Cap LoRa-1262 / SX1262 射频驱动)
* `M5Chain` (单色副屏展示)

---

### 3. 编译与烧录命令

在项目根目录下执行：

#### 仅编译检查语法
```bash
pio run
```

#### 编译并上传烧录至设备
```bash
pio run -t upload
```

> **提示**：若电脑连接了多个串口设备，可通过 `--upload-port` 指定端口号，例如：
> ```bash
> pio run -t upload --upload-port COM3   # Windows
> pio run -t upload --upload-port /dev/ttyACM0 # Linux / macOS
> ```

#### 打开串口监视器
```bash
pio device monitor -b 115200
```

---

### 4. 常见问题与排查指南

| 现象 | 可能原因 | 解决办法 |
| :--- | :--- | :--- |
| **设备未识别 (找不到 COM 口)** | 缺少 USB 转串口驱动 | 检查电脑是否安装 **CH9102** 或 **CP210x** VCP 虚拟串口驱动。 |
| **上传卡在 `Connecting...`** | 串口通信未进入 Bootloader | 拔插 USB 线，或长按 Cardputer 侧面的 **G0 (BtnA) 键**后再按电源复位键进入强制下载模式。 |
| **串口监视器输出乱码** | 波特率不匹配 | 确保串口监视器波特率设定为 `115200`。 |
| **烧录后开机白屏或重启循环** | LittleFS 闪存未格式化或损坏 | 初次烧录若闪存异常，代码中具备自检与格式化机制，亦可执行 `pio run -t uploadfs` 擦除并初始化文件系统分区。 |
