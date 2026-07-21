# M5Stack StickS3 开发参考手册

> 本文档整理自 M5Stack 官方文档:https://docs.m5stack.com/en/core/StickS3(唯一官方数据来源)。
> 用途:作为后续 StickS3 软件与外设开发的基础资料。
> 整理日期:2026-07-12(官方原理图版本 V0.6,2025-11-11)

---

## 目录

1. [产品概述](#1-产品概述)
2. [核心规格](#2-核心规格)
3. [完整管脚映射](#3-完整管脚映射)
4. [I2C / I2S 总线拓扑与设备地址](#4-i2c--i2s-总线拓扑与设备地址)
5. [电源管理(M5PM1)](#5-电源管理m5pm1)
6. [扩展接口](#6-扩展接口)
7. [按键与下载模式](#7-按键与下载模式)
8. [开发平台与工具链](#8-开发平台与工具链)
9. [官方注意事项与坑点汇总](#9-官方注意事项与坑点汇总)
10. [参考资料与链接](#10-参考资料与链接)
11. [实战踩坑记录(固件开发实测)](#11-实战踩坑记录固件开发实测)

---

## 1. 产品概述

**StickS3(SKU: K150)** 是 M5Stack 推出的紧凑型高性能可编程控制器,面向遥控与 IoT 应用。主控采用 **ESP32-S3-PICO-1-N8R8**(2.4GHz Wi-Fi,内置 8MB Flash + 8MB PSRAM),集成:

- 1.14 英寸 ST7789P3 LCD(135×240)
- BMI270 六轴姿态传感器
- 可编程按键(KEY1/KEY2)
- ES8311 单声道音频编解码器 + 高灵敏度 MEMS 麦克风 + AW8737 功放 + 8Ω@1W 腔体扬声器
- **红外发射管 + 红外接收管**(收发俱全,接收需用 RMT 外设)
- **M5PM1 电源管理芯片**(多级低功耗状态、充电管理、电源按键逻辑)
- 250mAh 锂电池
- 磁吸背面设计
- 扩展接口:**Hat2 总线(2.54-16P)** + HY2.0-4P(Grove)

适用场景:智能家居控制、AI 语音助手(官方提供小智 Xiaozhi / ESP-Claw 固件教程)、IoT 项目开发。

**官方特性清单(Features)**:ESP32-S3-PICO-1-N8R8 主控;8MB Flash + 8MB PSRAM;ES8311 单声道音频编解码;MEMS 麦克风 + 扬声器;集成 IR 发射 + IR 接收;磁吸背面;Hat2 总线(2.54-16P)与 HY2.0-4P 扩展口;支持 Arduino / UiFlow2 / ESP-IDF / PlatformIO。

**包装内含**:1 × StickS3。

---

## 2. 核心规格

| 规格 | 参数 |
| :--: | :--: |
| SoC | ESP32-S3-PICO-1-N8R8 @双核 Xtensa LX7,主频高达 240MHz |
| Flash | 8MB |
| PSRAM | **8MB Octal(八线)** |
| IMU | BMI270(6 轴) |
| 无线 | 2.4GHz Wi-Fi |
| 屏幕 | ST7789P3,135 × 240 分辨率,1.14" |
| 输入电源 | USB Type-C DC 5V |
| 音频编解码 | ES8311:24-bit,I2S 协议 |
| 麦克风 | MEMS 麦克风,信噪比(SNR)65dB |
| 扬声器 | AW8737 功放 + 8Ω@1W 2011 腔体扬声器 |
| 工作温度 | 0 ~ 40°C |
| 电池容量 | 250mAh |
| Grove 负载能力 | 空载:5V;最大:4.88V @ 0.38A |
| 功耗(关机) | 4.2V @ 14.02µA |
| 功耗(L1 状态) | 4.2V @ 52.47µA |
| 功耗(L2 状态) | 4.2V @ 102.40µA |
| 功耗(L3A 状态) | 4.2V @ 36.69mA |
| 功耗(满载) | 4.2V @ 519.02mA |
| 产品尺寸 | 48.0 × 24.0 × 15.0mm |
| 产品重量 | 20.0g |
| 包装尺寸 | 65.0 × 25.0 × 15.0mm |
| 毛重 | 22.4g |

### ESP32-S3-PICO-1-N8R8 要点

- 双核 Xtensa LX7 @240MHz;**与 Cardputer Adv(FN8 无 PSRAM)不同,本机有 8MB Octal PSRAM**,全屏帧缓冲、音频缓冲、大 JSON 等大内存场景宽裕得多。
- 原生 USB-OTG(USB-C 即原生 USB,支持 USB CDC 串口烧录/日志、HID 模拟等)。
- 注意(通用常识,非官方页面内容):ESP32-S3 使用 Octal PSRAM 时 GPIO35/36/37 被占用,不可作普通 IO。

---

## 3. 完整管脚映射

以下各表均为官方 PinMap 原文数据。

### LCD(ST7789P3)

| ESP32-S3 | G39 | G40 | G45 | G41 | G21 | G38 |
| :------: | :--: | :--: | :--: | :--: | :--: | :--: |
| ST7789P3 | MOSI | SCK | RS | CS | RST | BL |

### IMU & M5PM1(I2C)

| ESP32-S3 | G48 | G47 |
| :------------: | :--: | :--: |
| BMI270 (0x68) | SCL | SDA |
| M5PM1 (0x6e) | SCL | SDA |

### M5PM1 自身 GPIO 分配

| M5PM1 引脚 | 信号 | 功能 |
| :--: | :--: | :-- |
| G0 | PYG0_CHG_STAT | 电池充电状态 |
| G1 | PYG1_IRQ | 中断输出 → ESP32-S3 |
| G2 | PYG2_L3B_EN | L3B 电源域使能 |
| G3 | PYG3_SPK_Pulse | 扬声器(脉冲控制) |
| G4 | PYG4_IMU_INT | IMU 中断输入 |

### Audio(ES8311)

| ESP32-S3 | G18 | G14 | G17 | G15 | G16 | G48 | G47 |
| :----------: | :--: | :--: | :--: | :--: | :--: | :--: | :--: |
| ES8311 (0x18) | MCLK | DOUT | BCLK | LRCK | DIN | SCL | SDA |

> 与 Cardputer Adv 不同,**MCLK 有独立 GPIO(G18)**。按 ES8311 命名习惯,DOUT=录音数据(ES8311→MCU)、DIN=播放数据(MCU→ES8311)——方向为惯例推断,官方页面仅给出信号名。

### 按键

| ESP32-S3 | G11 | G12 |
| :------: | :---: | :---: |
| KEY1 | Input | |
| KEY2 | | Input |

### IR(红外收发)

| ESP32-S3 | G46 | G42 |
| :------: | :---: | :---: |
| IR | IR_TX | IR_RX |

### HY2.0-4P(Grove,PORT.A)

| HY2.0-4P | Black | Red | Yellow | White |
| :---------: | :---: | :--: | :----: | :---: |
| PORT.CUSTOM | GND | 5V | G9 | G10 |

### Hat2-Bus(2.54-16P,机身顶部)

| FUNC(左) | PIN | LEFT | RIGHT | PIN | FUNC(右) |
| :----: | :--: | :--: | :---: | :--: | :--: |
| GND | — | 1 | 2 | — | G5 |
| EXT_5V | — | 3 | 4 | — | G4 |
| Boot | — | 5 | 6 | — | G6 |
| G1 | — | 7 | 8 | — | G7 |
| G8 | — | 9 | 10 | — | G43 |
| BAT | — | 11 | 12 | — | G44 |
| 3V3_L2 | — | 13 | 14 | — | G2 |
| 5V_IN | — | 15 | 16 | — | G3 |

即:奇数脚(左列)自上而下为 GND / EXT_5V / Boot / G1 / G8 / BAT / 3V3_L2 / 5V_IN;偶数脚(右列)为 G5 / G4 / G6 / G7 / G43 / G44 / G2 / G3。

### GPIO 汇总表(按 GPIO 排序)

| GPIO | 功能 | 说明 |
| :--: | :-- | :-- |
| G1 | Hat2-Bus Pin7 | 通用扩展 IO |
| G2 | Hat2-Bus Pin14 | 通用扩展 IO |
| G3 | Hat2-Bus Pin16 | 通用扩展 IO |
| G4 | Hat2-Bus Pin4 | 通用扩展 IO |
| G5 | Hat2-Bus Pin2 | 通用扩展 IO |
| G6 | Hat2-Bus Pin6 | 通用扩展 IO |
| G7 | Hat2-Bus Pin8 | 通用扩展 IO |
| G8 | Hat2-Bus Pin9 | 通用扩展 IO |
| G9 | Grove 黄线 | PORT.A |
| G10 | Grove 白线 | PORT.A |
| G11 | KEY1 | 用户按键,输入 |
| G12 | KEY2 | 用户按键,输入 |
| G14 | ES8311 I2S DOUT | 录音数据(惯例推断方向) |
| G15 | ES8311 I2S LRCK | 帧时钟 |
| G16 | ES8311 I2S DIN | 播放数据(惯例推断方向) |
| G17 | ES8311 I2S BCLK | 位时钟 |
| G18 | ES8311 I2S MCLK | 主时钟 |
| G21 | LCD RST | ST7789P3 |
| G38 | LCD BL | 背光 |
| G39 | LCD MOSI | ST7789P3 |
| G40 | LCD SCK | ST7789P3 |
| G41 | LCD CS | ST7789P3 |
| G42 | IR_RX | 红外接收(必须用 RMT 外设解码) |
| G43 | Hat2-Bus Pin10 | 通用扩展 IO |
| G44 | Hat2-Bus Pin12 | 通用扩展 IO |
| G45 | LCD RS(DC) | ST7789P3 |
| G46 | IR_TX | 红外发射 |
| G47 | I2C SDA | BMI270 / M5PM1 / ES8311 共用 |
| G48 | I2C SCL | 同上 |

> Hat2-Bus 上的 "Boot"(Pin5)为启动信号脚,官方页面未标注对应 GPIO 编号。

---

## 4. I2C / I2S 总线拓扑与设备地址

### 4.1 内部 I2C 总线(G47=SDA,G48=SCL)

板上三个外设共用一条 I2C 总线:

| 设备 | 7 位地址 | 用途 |
| :--: | :--: | :-- |
| ES8311 | **0x18** | 音频编解码控制 |
| BMI270 | **0x68** | 六轴 IMU(注意:Cardputer Adv 上是 0x69,本机为 0x68) |
| M5PM1 | **0x6e** | 电源管理 |

外接 I2C 设备(经 Grove 或 Hat2-Bus)时注意避开 0x18 / 0x68 / 0x6e。

### 4.2 I2S(音频)

单一 I2S 链路连接 ES8311:MCLK=G18、BCLK=G17、LRCK=G15、DOUT=G14、DIN=G16。麦克风(模拟 MEMS)经 ES8311 ADC 采集;播放经 ES8311 DAC → AW8737 功放 → 扬声器。

### 4.3 其他总线

- LCD 走独立 SPI(G39/G40/G41/G45/G21/G38),与扩展口无共享。
- 官方页面未定义板载 SD 卡或额外 SPI/UART 外设;Hat2-Bus / Grove 上的 GPIO 可自由配置为 SPI/UART/I2C/ADC 等(ESP32-S3 IO MUX 任意映射)。

---

## 5. 电源管理(M5PM1)

StickS3 采用 M5Stack 自研 **M5PM1 电源管理芯片**(I2C 地址 0x6e),负责充电管理(CHG_STAT)、按键开关机逻辑、多级电源状态与中断(IRQ→ESP32-S3),并接管 IMU 中断(PYG4_IMU_INT)与扬声器脉冲控制(PYG3_SPK_Pulse)、L3B 电源域使能(PYG2_L3B_EN)。

### 5.1 电源状态与功耗(官方实测,4.2V)

| 状态 | 电流 |
| :--: | :--: |
| 关机(Power off) | 14.02µA |
| L1 | 52.47µA |
| L2 | 102.40µA |
| L3A | 36.69mA |
| 满载(Full load) | 519.02mA |

> 各状态(L1/L2/L3A/L3B)的具体电源域定义详见官方 M5PM1 文档:
> - M5PM1 数据手册:https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/M5PM1_Datasheet_EN.pdf
> - Arduino M5PM1 电源管理教程:https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1
> - Hat2-Bus 的 3V3_L2 脚即 L2 电源域的 3.3V 输出。

### 5.2 EXT_5V_EN(5V 口方向控制)—— 重要

设备的 5V 电源接口可配置为 **DC 5V 输出 / 输入**两种模式:

- **默认(M5Unified 初始化后)为输入模式,EXT_5V_EN 被禁用**:关闭对 Grove、Hat EXT_5V 接口以及 **IR TX/RX** 的供电。此状态下可经 Grove 口、顶部 Hat2-Bus 的 EXT_5V 或 5VIN 接口向设备供 5V 电。
- **输出模式**:向 Grove 与 Hat 接口供电;此时**只允许**经 USB 或顶部 Hat2-Bus 的 5V_IN 输入电源。**不要经其他输出接口供电,否则有短路和损坏设备的风险。**
- **IR 收发依赖此 5V 电源**:无外部 5V 供电时,必须开启输出模式红外才能工作:

```cpp
M5.Power.setExtOutput(true);   // EXT_5V 输出模式(恢复 IR TX/RX 与 Grove/Hat 5V 供电)
// M5.Power.setExtOutput(false); // EXT_5V 输入模式
```

### 5.3 电池

- 250mAh 锂电池,USB Type-C DC 5V 输入充电,充电状态由 M5PM1(PYG0_CHG_STAT)管理。
- Grove 5V 负载能力:空载 5V,最大 4.88V @ 0.38A。

---

## 6. 扩展接口

### 6.1 HY2.0-4P(Grove,PORT.A)

- 引脚:GND(黑)、5V(红)、G9(黄)、G10(白)。
- 5V 脚受 EXT_5V_EN 模式控制(见 5.2):默认输入模式(可从此口给设备供电),`setExtOutput(true)` 后对外输出。

### 6.2 Hat2-Bus(2.54-16P)

- 位于机身顶部,2.54mm 间距 16P,是 Hat2 系列扩展模块的安装总线。
- 提供:GND、EXT_5V(受 5.2 模式控制)、Boot、BAT(电池)、3V3_L2(L2 电源域 3.3V)、5V_IN(输入),以及 10 个 GPIO:G1/G2/G3/G4/G5/G6/G7/G8/G43/G44。
- **结构不兼容**以下旧款 Hat 产品:Hat Mini JoyC(U156)、Hat Mini EncoderC(U157)、Hat 18650C(U080)。

### 6.3 其他

- USB-C:供电/充电 + 原生 USB(CDC 串口、烧录、HID 模拟)。
- 磁吸背面设计:机构固定。
- **无 microSD 卡槽**(官方规格与 PinMap 均未列出)。

---

## 7. 按键与下载模式

### 7.1 侧面电源/复位按键(M5PM1 管理)

| 操作 | 功能 |
| :--: | :-- |
| 单击 | 开机 / 复位 |
| 双击 | 关机 |
| 长按 | 进入下载模式 |

### 7.2 用户按键

- KEY1 = G11,KEY2 = G12,均为输入,可编程。

### 7.3 进入下载(烧录)模式

1. 用 USB 线连接设备与电脑;
2. **按住侧面复位(reset)按键不放**;
3. 当机身内部**绿色 LED 闪烁**时,即已成功进入下载模式。

> 正常运行时 USB CDC 也可直接烧录;硬件下载模式用于固件损坏或 USB CDC 不可用时兜底。

---

## 8. 开发平台与工具链

支持 **Arduino / UiFlow2 / ESP-IDF / PlatformIO** 四种开发方式。

### 8.1 Arduino

- 官方快速上手:https://docs.m5stack.com/en/arduino/m5sticks3/program
- 驱动库:**M5Unified**(https://github.com/m5stack/M5Unified)+ **M5GFX**(https://github.com/m5stack/M5GFX)+ **M5PM1**(https://github.com/m5stack/M5PM1,电源管理库)
- M5PM1 电源管理教程(含扬声器功放开关方法,IR 接收前需要):https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1

### 8.2 UiFlow2

- 官方快速上手:https://docs.m5stack.com/en/uiflow2/sticks3/program
- 通过 M5Burner 烧录 StickS3 的 UiFlow2 固件后,使用 Blockly / MicroPython 图形化编程。

### 8.3 PlatformIO(官方配置原文)

```ini
[env:m5stack-sticks3]
platform = espressif32@6.12.0
board = esp32-s3-devkitc-1
framework = arduino
board_build.arduino.partitions = default_8MB.csv
board_build.arduino.memory_type = qio_opi
build_flags =
    -DESP32S3
    -DBOARD_HAS_PSRAM
    -mfix-esp32-psram-cache-issue
    -DCORE_DEBUG_LEVEL=5
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
lib_deps =
    M5Unified=https://github.com/m5stack/M5Unified
    M5PM1=https://github.com/m5stack/M5PM1
```

> 注意 `memory_type = qio_opi`(QIO Flash + Octal PSRAM),与 N8R8 芯片对应。

### 8.4 官方现成固件

- **小智(Xiaozhi)语音助手**:经 M5Burner 烧录,教程 https://docs.m5stack.com/en/guide/realtime/xiaozhi/sticks3
- **ESP-Claw 固件**:将 StickS3 配置为支持 AI 交互、硬件编程与自动化控制的智能终端,教程 https://docs.m5stack.com/en/guide/agent/esp_claw/sticks3

---

## 9. 官方注意事项与坑点汇总

1. **EXT_5V_EN 默认关闭(输入模式)**:M5Unified 初始化后 Grove/Hat 5V 与 **IR TX/RX 均断电**。无外部 5V 时必须 `M5.Power.setExtOutput(true)` 才能用红外;输出模式下严禁再从 Grove 等输出口倒灌供电,有短路损坏风险。
2. **红外接收三条规则**:① 必须用 ESP32 **RMT 外设**接收解码,不支持 GPIO 方式;② 使用红外接收时**必须关闭扬声器功放**,否则无法正常接收(方法见 M5PM1 教程 spk amp 章节);③ 收发端尽量正对,**距离不小于 30cm**,过近会接收异常。
3. **电池供电时扬声器音量保持在 75% 以下**,否则功耗过大可能导致设备意外重启。
4. **I2C 地址占用**:0x18(ES8311)、0x68(BMI270)、0x6e(M5PM1),外接 I2C 设备需避开;且 BMI270 地址与 Cardputer Adv(0x69)不同,移植代码时注意。
5. **早期批次开机后可能有轻微异响**,不影响功能使用。
6. **请勿擅自拆解外壳**:可能损坏天线 FPC 电路,影响设备正常工作。
7. **Hat 兼容性**:结构上不兼容 Hat Mini JoyC(U156)、Hat Mini EncoderC(U157)、Hat 18650C(U080)。
8. **Octal PSRAM 占用 GPIO35/36/37**(ESP32-S3 通用限制),扩展口引脚列表中也确实不含这三个脚。
9. **烧录救砖**:插 USB → 按住侧面复位键 → 内部绿色 LED 闪烁即进入下载模式。
10. **工作温度 0~40°C**,电池充电注意环境温度。
11. **电源状态非标准 ESP32 深睡**:低功耗由 M5PM1 的 L1/L2/L3A/L3B 状态机管理,做低功耗设计前先读 M5PM1 数据手册与教程,不要只按 ESP-IDF deep sleep 思路设计。

---

## 10. 参考资料与链接

### 官方文档

- 产品页(规格/管脚,本文唯一数据来源):https://docs.m5stack.com/en/core/StickS3
- Arduino 快速上手:https://docs.m5stack.com/en/arduino/m5sticks3/program
- M5PM1 电源管理(Arduino):https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1
- UiFlow2 快速上手:https://docs.m5stack.com/en/uiflow2/sticks3/program
- Xiaozhi 语音助手教程:https://docs.m5stack.com/en/guide/realtime/xiaozhi/sticks3
- ESP-Claw 固件烧录教程:https://docs.m5stack.com/en/guide/agent/esp_claw/sticks3

### 原理图 / 硬件

- StickS3 原理图 PDF(V0.6,2025-11-11):https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/K150_Stick_S3_PRJ_V0.6_20251111_2025_11_17_16_10_24.pdf
- 尺寸图 PDF:https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/K150-sticks3.pdf
- 结构文件:https://github.com/m5stack/M5_Hardware/tree/master/Products/K150_StickS3/Structures
- 硬件设计文件总仓库:https://github.com/m5stack/M5_Hardware

### 数据手册

- ESP32-S3 技术参考手册:https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/477/esp32-s3_technical_reference_manual_en.pdf
- ES8311:https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/products/atom/Atomic%20Echo%20Base/ES8311.pdf
- BMI270:https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/core/K128%20CoreS3/BMI270.PDF
- M5PM1 电源管理芯片:https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/M5PM1_Datasheet_EN.pdf

### 代码仓库

- M5Unified:https://github.com/m5stack/M5Unified
- M5GFX:https://github.com/m5stack/M5GFX
- M5PM1 库:https://github.com/m5stack/M5PM1

### 关键芯片数据手册关键词

ESP32-S3-PICO-1-N8R8 / ST7789P3 / ES8311 / AW8737 / BMI270 / M5PM1

---

## 11. 实战踩坑记录(固件开发实测)

> 本章不是官方资料,而是 `ble-page-turner` 固件(ESP-IDF v6.0.1 原生开发)在真机上踩过的坑,
> 逐条附结论与解决方法,供后续开发直接规避。记录日期:2026-07-15。

### 11.1 设备其实有三个物理按键,左侧是 M5PM1 电源键(最大的坑)

官方产品页只列出 KEY1(G11)/KEY2(G12)两个"用户按键",但真机上有**三个**按键:

| 位置 | 身份 | 与 ESP32 的关系 |
| :-- | :-- | :-- |
| 中间 / 右侧 | KEY1 = G11、KEY2 = G12,普通用户按键 | 直连 ESP32 GPIO,固件可读 |
| **左侧** | **M5PM1 的专用电源键(PWR_BTN)** | **不连 ESP32**,单击开机/复位、双击关机、长按 2s 进下载模式 |

后果:开发时若误把左侧键当成用户键测试,设备会"莫名其妙"复位
(串口表现为 USB 重新枚举 + 日志时间戳归零),极易误判为固件崩溃。
排查方向:串口断连且**无 panic 回溯**、时间戳从 0 重来 → 几乎可以肯定是 PM1 硬件复位,而不是软件问题。

### 11.2 电源键复位后屏幕永远黑屏:PM1 复位会清空自身 GPIO 寄存器(L3B 断电)

M5PM1 数据手册(V1.9)复位流程图:**Reset = 清空 power/GPIO 寄存器 + HOLD_CFG → 内部关机 → 500ms → 重新上电**。
其中 PM1 的 GPIO2 就是 `PYG2_L3B_EN`——LCD 所在 L3B 电源域的使能脚。
复位后该寄存器被清零,LCD 供电域不再使能,于是**之后每次启动屏幕都点不亮**
(固件日志一切正常,esp_lcd 初始化"成功",因为 SPI 写入无从校验面板是否有电)。

解决:固件每次启动、在初始化 LCD 之前,通过 I2C 把 PM1 GPIO2 配成推挽输出并拉高
(顺序:FUNC=GPIO → 先写 OUT=1 再切输出方向,避免使能脚瞬间拉低)。
参考实现:`ble-page-turner/components/m5pm1/m5pm1.c` 的 `m5pm1_enable_l3b_domain()`。

### 11.3 M5PM1 寄存器在有电期间持久:改过就"赖着不走"

PM1 的按键配置(BTN_CFG_1/2 @ 0x49/0x4A)等寄存器**在 PM1 有电期间一直保持**
(装着电池就算 ESP32 重刷固件、PM1 复位/关机也大多不恢复默认)。
某个旧固件改过这些寄存器后,换刷新固件、甚至换一个工程,行为仍是被改过的。
解决:固件启动时**显式写入**自己期望的完整配置(不要假设是出厂默认值);
出厂默认:BTN_CFG_1 = 0x2A(单击复位使能、长按 2s),BTN_CFG_2 = 0x00(双击关机使能)。

### 11.4 `gpio_wakeup_enable()` 会覆盖引脚中断类型(边沿中断变电平中断)

ESP32 的 GPIO 唤醒与 GPIO 中断共用同一个 INT_TYPE 硬件字段。
按键驱动若配置 `GPIO_INTR_NEGEDGE` 中断、再调用 `gpio_wakeup_enable(pin, GPIO_INTR_LOW_LEVEL)`
(auto light-sleep 唤醒只支持电平触发),后者会**把中断类型改写成低电平触发**
——按住按键期间 ISR 无限重入,轻则事件风暴,重则看门狗复位。
IDF 驱动源码可证:`gpio_wakeup_enable()` 内部直接 `gpio_hal_set_intr_type(...)`。

解决:干脆按电平中断设计——ISR 里先 `gpio_intr_disable()` 屏蔽本引脚(该函数无临界区,ISR 安全),
通知任务处理,任务等按键释放、去抖后再 `gpio_intr_enable()`。
额外好处:light sleep 中按下的按键唤醒后电平仍然有效,**绝不丢按键**。

### 11.5 auto light sleep 会冻结 LEDC PWM(背光失控)

开了 `CONFIG_PM_ENABLE` + tickless idle 后,芯片一空闲就进 light sleep,
LEDC 时钟随之停摆——背光 PWM 输出被冻结在随机电平(亮屏页面会闪烁或直接黑掉)。
解决:屏幕点亮期间持有 `ESP_PM_APB_FREQ_MAX` 电源锁(顺带锁 APB 频率,PWM 频率也不漂),
熄屏时释放。翻页器亮屏只有秒级,功耗代价可忽略。

### 11.6 Arduino/M5Unified 路线做不了低功耗常连设备

arduino-esp32 的系统库是预编译的,出厂 sdkconfig 没开 `CONFIG_PM_ENABLE` /
`CONFIG_FREERTOS_USE_TICKLESS_IDLE`,运行时调 `esp_pm_configure()` 直接返回
`ESP_ERR_NOT_SUPPORTED`。BLE 保持连接的空闲电流被钉死在 ~37mA(即 L3A 整机水平),
250mAh 电池撑不过 7 小时。需要 mA 级连接待机的项目必须走 **ESP-IDF 原生**
(实测选 NimBLE + esp_hid 组件,idle 期由 tickless + modem sleep 接管)。

### 11.7 USB 供电时 M5PM1 的 VBAT 读数不可靠(可能读到 0)

插着 USB 时读 VBAT(0x22/0x23)可能得到 0,直接换算成"电量 0%"会吓到用户,
推给 BLE 电池服务(BAS)还会让手机端显示电量告急。
解决:外部供电时 UI 显示"充电中"而非百分比;读数为 0 时跳过 BAS 上报(保留上次有效值)。

### 11.8 M5GFX 不支持 ESP-IDF v6

M5GFX 官方支持"以 ESP-IDF 组件方式使用",但截至 IDF v6.0.1 无法编译。
小 UI 直接用 IDF 自带 `esp_lcd`(内置 ST7789 驱动)即可:135×240、偏移 (52, 40)、
需要 `esp_lcd_panel_invert_color(panel, true)`,全帧缓冲 63KB 放内部 SRAM 绰绰有余
(这也是敢关 PSRAM 的原因之一)。

### 11.9 其他一句话坑

- **熄屏要做两件事**:背光关 + 面板 `SLPIN`(esp_lcd 的 `esp_lcd_panel_disp_sleep`),只关背光驱动 IC 仍耗 ~1mA。
- **NimBLE 上用 esp_hid 组件**需在 sdkconfig 打开 `CONFIG_BT_NIMBLE_HID_SERVICE` 与
  `CONFIG_BT_NIMBLE_BAS_SERVICE`(默认关),否则链接期才报错。
- **IDF v6 新 I2C 驱动**函数名是 `i2c_new_master_bus()`(不是 `i2c_master_new_bus`),
  老代码/老教程的函数名照抄会编译失败。
- **连接参数要连接后再协商**:Android 对加密/发现期间的参数更新请求常直接拒绝,
  加密完成后延时 ~2s 再请求(latency=24),被拒后退避 5s 重试一次。
