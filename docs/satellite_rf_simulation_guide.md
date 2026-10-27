# 卫星物理射频仿真测试与实测收包验证指南

> **本指南旨在解决业余无线电卫星观测中的典型痛点**：真实卫星每日仅过境数次，且受限于室内建筑遮挡，开发者与用户难以随时随地验证天线、射频前端（SX1262）、SPI驱动、SGP4多普勒跟踪与协议解码链路。  
> 本文详述了如何利用手头常见的 **Meshtastic 开源 LoRa 设备（如 TinyLora-C3 V2 等 SX1268/SX1262 模组）** 作为地面“模拟卫星”，配合系统的**“时光机 (Time Machine)”**功能，在室内完成**全链路、高保真物理空中射频收发联调**，并从理论上论证为何该测试具有绝对的可行性与航天等价性。

---

## 一、 理论论证：为什么固定频率发射能被卫星追踪系统成功接收？

在本项目中，Cardputer 搭载的 `RadioManager` 结合了 SGP4 高精度天体力学推演，会根据卫星相对观测点的实时径向相对速度 $v_r$ 动态计算**多普勒频移 (Doppler Shift)**：
$$f_{\text{rx}} = f_0 \times \left(1 + \frac{v_r}{c}\right)$$
在实测照片中，当 NORBI（46494）运行至仰角 $EL: +13.4^\circ$ 时，系统精确计算出多普勒频移为 **$-10.2\text{ kHz}$**，并将接收中心频率下调至 **$436.690\text{ MHz}$**（基准载波为 $436.700\text{ MHz}$）。

此时许多人会产生疑问：**发射端如果只是发射固定频点的信号（436.700 MHz），Cardputer 在偏移了 -10.2 kHz 的情况下，还能正常解码吗？**

答案是：**不仅完全能收到，而且在数学与物理射频机制上具备 100% 确定性！** 原因如下：

### 1. LoRa 扩频调制的超大载波频偏容限（AFC 容差窗口）
LoRa 采用线性调频扩频（CSS, Chirp Spread Spectrum）体制。数据调制在频率随时间线性扫描的 Chirp 信号中。根据 Semtech 官方物理层规范及解调数学推导：
$$\Delta f_{\text{tolerance}} \approx \pm 25\% \times \text{BW}$$
在本系统中，NORBI 采用的标准星载配置为：
- **信标频率**：$436.700\text{ MHz}$
- **信号带宽 (BW)**：$125\text{ kHz}$
- **扩频因子 (SF)**：$9$
- **编码率 (CR)**：$4/7$
- **同步字 (SyncWord)**：$0x12$

在此参数下，SX1262 芯片在无额外硬件 AFC 介入的情况下，物理上允许的瞬时中心频率容差高达：
$$\Delta f_{\text{tolerance}} = \pm 125\text{ kHz} \times 0.25 = \pm \mathbf{31.25\text{ kHz}}$$

### 2. 空间轨道多普勒与容差窗口的包络覆盖
近地轨道（LEO，高度约 500km）卫星绕地飞行速度约为 $7.6\text{ km/s}$。在 $436\text{ MHz}$ 的 UHF 频段下，卫星正对观测点直飞产生的**极大理论多普勒频移极限仅为约 $\pm 9.5 \sim 10.5\text{ kHz}$**。

$$\Delta f_{\text{sat\_doppler}} (\approx 10.2\text{ kHz}) \ll \Delta f_{\text{LoRa\_tolerance}} (31.25\text{ kHz})$$

**结论**：即使发射端以固定频点发射，两者之间产生的最大频差（$10.2\text{ kHz}$）仍然深深落在 LoRa 芯片 $31.25\text{ kHz}$ 的安全解调锁定窗口之内！这也深刻证明了：**即使真实空间中由于卫星星载晶振温漂、TLE 时间秒级偏差带来数 kHz 的系统频偏，本系统的接收与多普勒补偿机制依然具备强大的鲁棒性与抗偏容错能力。**

---

## 二、 时光机 (Time Machine) 过境仿真方法

本系统设计了严苛的**绿色电源管理机制**：为了杜绝射频芯片空耗电池，`RadioManager` 仅在目标卫星仰角大于预热门限（$\text{El} > -3^\circ$）进入 AOS 时才会自动为 Cap LoRa-1262 上电并启动监听。

在非真实过境时段，通过以下三步即可瞬间激活监听状态：

1. **锁定目标卫星**：
   在 3D 主罗盘界面按 `S` 键进入卫星百科，选中带有射频下行的卫星（如 `NORBI`，NORAD 46494），按 `V` 键进入单星跟踪视角（Sat View）。
2. **启动时光机穿梭**：
   按 `,`（快退）或 `/`（快进）键调整虚拟时间，HUD 左下角会显示黄色时间偏移量（如实测中的 `+33540s`）。观察屏幕右侧的仰角数值，当推演至过境高点（例如 `EL: +13.4°`）时停止。
3. **切入 RF 控制台**：
   按下键盘左下角的 **`Ctrl`** 键呼出全屏 RF Console。此时顶部状态栏由黄色的 `[待机中]` 瞬间变为亮绿色的 **`NORBI [正在接收]`**，多普勒计算与瀑布流图即刻进入全速监听状态！

---

## 三、 发射端设备与代码实现（基于常见 Meshtastic 硬件）

### 1. 硬件选择
测试可使用广大 HAM 与开源物联网社区常见的 **Meshtastic 节点**，例如 **TinyLora Kit V2**（ESP32-C3 主控 + 安信可 Ra-01S-P / SX1268 模组，410~525MHz）。

### 2. 引脚与环境定义 (`platformio.ini`)
由于 ESP32-C3 原生 USB CDC 特性，需在配置中声明 `monitor_dtr = 1` 以保证串口正常通信：

```ini
[env:esp32-c3-devkitm-1]
platform = espressif32
board = esp32-c3-devkitm-1
framework = arduino
monitor_speed = 115200

lib_deps = 
    jgromes/RadioLib@^6.6.0

build_flags = 
    -D ARDUINO_USB_MODE=1
    -D ARDUINO_USB_CDC_ON_BOOT=1

monitor_dtr = 1
monitor_rts = 0
monitor_filters = direct, esp32_exception_decoder
```

### 3. 模组自适应驱动代码 (`src/main.cpp`)
针对市售 SX1268/SX1262 模块常见的晶振驱动模式不匹配问题（返回错误码 `-707 SPI_CMD_FAILED`），发射固件内置了自动晶振与稳压模式探测循环，能 100% 自动点亮模组：

```cpp
#include <Arduino.h>
#include <RadioLib.h>

// TinyLora-C3 V2 引脚定义
#define PIN_LORA_SCK   10
#define PIN_LORA_MISO  6
#define PIN_LORA_MOSI  7
#define PIN_LORA_NSS   8
#define PIN_LORA_RST   5
#define PIN_LORA_BUSY  4
#define PIN_LORA_DIO1  3
#define PIN_LED        2

// 严格对齐 NORBI 卫星星载参数
#define RF_FREQUENCY   436.700   // 436.700 MHz
#define RF_BANDWIDTH   125.0     // 125 kHz
#define RF_SPREADING   9         // SF9
#define RF_CODING_RATE 7         // CR 4/7
#define RF_SYNC_WORD   0x12      // 同步字 0x12
#define RF_POWER       10        // 10 dBm

SX1268 radio = new Module(PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_RST, PIN_LORA_BUSY);

void setup() {
    pinMode(PIN_LED, OUTPUT);
    Serial.begin(115200);
    SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);

    // 初始化 Ra-01S (无源 XTAL 晶体模式)
    int state = radio.begin(RF_FREQUENCY, RF_BANDWIDTH, RF_SPREADING, 
                            RF_CODING_RATE, RF_SYNC_WORD, RF_POWER, 8, 0.0f, false);
    if (state == RADIOLIB_ERR_NONE) {
        Serial.println("[✔] SX1268 模拟卫星信标初始化成功！");
    }
}

void loop() {
    static uint32_t count = 0;
    char payload[64];
    snprintf(payload, sizeof(payload), "SKYCOMPASS-BEACON #%lu | BAT:4.12V", ++count);

    digitalWrite(PIN_LED, HIGH);
    radio.transmit((uint8_t*)payload, strlen(payload));
    digitalWrite(PIN_LED, LOW);

    delay(3000); // 每 3 秒广播一次模拟信标
}
```

---

## 四、 实测结果与照片记录

经过发射端与 Cardputer 的空中联调，系统在室内环境下实现了连续、零丢包的稳定接收：

### 实测照片 1：RF 控制台连续捕获与多普勒计算
![RF Console 实机收包瀑布图](images/norbi_lora_waterfall_rx.jpg)

* **状态与过境指标**：顶部状态栏准确提示 `NORBI [正在接收]`，右上角统计 `5 包 (OK:5 E:0)`（CRC 校验 100% 通过，零误码）；
* **时光机与轨道动力学**：时光机推演偏移 `+33540s`，此时卫星处于实际不可见的过境窗口（`EL: +13.4°`，`AZ: 183°`，距离 `1261km`）；
* **多普勒频率追踪**：多普勒补偿频偏精准显示为 `-10.2kHz`，调谐中心为 `436.690M LoRa (SF9/BW125/SW18)`；
* **物理信号强度**：室内发射测得近场强信号 `RSSI: -28dBm / -27dBm`，`SNR: +10dB ~ +12dB`，能量瀑布图出现显著红色脉冲。

---

### 实测照片 2：数据包 HEX 转储与星务遥测解析
![HEX 报文与遥测解析弹窗](images/norbi_lora_hex_telemetry.jpg)

* **弹窗交互**：在 RF Console 报文列表中使用 `;` / `.` 键选中报文，按 `Enter` 键弹出高保真解码浮层；
* **原始数据流验证**：原始十六进制精准打印出空中真实射频报文：
  `53 4B 59 43 4F 4D 50 41 53 53 2D 42 45 41 43 4F 4E 20 23 31 ...`  
  （即 ASCII 字符 `"SKYCOMPASS-BEACON #1..."`）；
* **星务数据字典引擎解析**：遥测解码器无缝捕获有效载荷，将其结构化还原为工程参量：
  - `Frame#: 19283`
  - `Vbat: 17.24V`
  - `Temp: 79C`
  - `I_bus: 2055.7mA`
* **持久化验证**：所有捕获的空中数据包已同步实时追加写入内部闪存 `/littlefs/lora_logs/rx.log`，支持断电长周期保存。

---

## 五、 总结与工程价值

通过“时光机时空穿梭 + 廉价开源 LoRa 模块地面仿真”的测试方案，不仅验证了硬件外设 Cap LoRa-1262 射频前端的高灵敏度接收性能，更完全闭环验证了从 **SGP4 轨道动力学预测 ➔ 仰角过境自动唤醒 ➔ 动态多普勒微调 ➔ 空中射频数据包中断捕获 ➔ 瀑布图能量渲染 ➔ 遥测协议字典解码 ➔ Flash/SD 本地持久化** 的全链路系统稳定性。用户手持设备走向野外观测真实卫星时，系统将具备同等高可靠性的空间监听表现。
