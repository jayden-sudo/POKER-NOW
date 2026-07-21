# M5Stack Cardputer Adv 开发参考手册

> 本文档整理自 M5Stack 官方文档、Cardputer-Adv v1.0 原理图(2025-06-20)、M5Cardputer Arduino 库源码、M5Cardputer-UserDemo(CardputerADV 分支)出厂固件源码,以及 Cap LoRa1262 官方文档。
> 用途:作为后续 Cardputer Adv 软件与外设开发的基础资料。
> 整理日期:2026-07-10

---

## 目录

1. [产品概述](#1-产品概述)
2. [核心规格](#2-核心规格)
3. [硬件架构详解(基于原理图)](#3-硬件架构详解基于原理图)
4. [完整管脚映射](#4-完整管脚映射)
5. [I2C / SPI / UART 总线拓扑与设备地址](#5-i2c--spi--uart-总线拓扑与设备地址)
6. [扩展接口](#6-扩展接口)
7. [Cap LoRa1262 外接模块(LoRa + GNSS)](#7-cap-lora1262-外接模块lora--gnss)
8. [开发平台与工具链](#8-开发平台与工具链)
9. [Arduino 驱动库(M5Cardputer)详解](#9-arduino-驱动库m5cardputer详解)
10. [出厂固件(ESP-IDF UserDemo)结构](#10-出厂固件esp-idf-userdemo结构)
11. [键盘布局与键值映射](#11-键盘布局与键值映射)
12. [开发注意事项与坑点汇总](#12-开发注意事项与坑点汇总)
13. [Launcher 多固件启动器基础知识](#13-launcher-多固件启动器基础知识)
14. [参考资料与链接](#14-参考资料与链接)

---

## 1. 产品概述

**Cardputer-Adv(SKU: K132-Adv)** 是 M5Stack 推出的卡片电脑,是初代 Cardputer 的升级版。主控采用 **Stamp-S3A 模块**(基于 ESP32-S3FN8),集成:

- 1.14 英寸 ST7789V2 LCD(240×135)
- 56 键(4×14)物理键盘,由 **TCA8418 键盘扫描芯片**管理(初代 Cardputer 为 GPIO 矩阵扫描,这是两代最大的硬件差异之一)
- ES8311 音频编解码器 + NS4150B 功放 + 1W 扬声器 + MEMS 模拟麦克风 + **3.5mm 耳机接口(带插入检测)**
- BMI270 六轴姿态传感器(初代为 BMI8563/其他,Adv 升级为 BMI270)
- 红外发射管
- 1750mAh 锂电池(初代仅 120/1400mAh,大幅升级)
- microSD 卡槽
- Grove HY2.0-4P 接口 + EXT 2.54-14P 扩展总线(即 "Cap-Bus",用于安装官方 Cap 系列扩展模块,如 Cap LoRa1262)
- 磁吸底座设计,兼容乐高孔

适用场景:快速功能验证与原型设计、工业控制、嵌入式学习、IoT/无线通信项目、便携终端(LoRa 通信、Meshtastic 类应用)。

本机附带外设:**Cap LoRa1262 [LoRa+GNSS] 模块 + 外接 RP-SMA 天线**(详见第 7 章)。

---

## 2. 核心规格

| 规格 | 参数 |
| :--: | :--: |
| SoC | ESP32-S3FN8 @双核 Xtensa LX7,主频高达 240MHz |
| Flash | 8MB(**注意:FN8 型号无 PSRAM**) |
| 无线 | Wi-Fi 2.4GHz (802.11 b/g/n)、BLE 5(ESP32-S3 内置) |
| 外部存储扩展 | microSD(SPI 接口) |
| 屏幕 | ST7789V2 @1.14",240 × 135px |
| 键盘 | 56 按键(4 × 14),TCA8418RTWR 扫描芯片 |
| 按键操作力 | 160gf |
| 拓展接口 | HY2.0-4P(Grove)+ EXT 2.54-14P(Cap-Bus) |
| IR | 1x 红外发射管(G44) |
| 音频编解码芯片 | ES8311(I2S + I2C 控制) |
| 扬声器 | NS4150B 功放芯片 + 8Ω@1W 扬声器 |
| 耳机口 | 3.5mm(PJ-342,带插入检测,插入自动切断扬声器) |
| 麦克风 | MEMS 模拟麦克风(MSM381A3729H9BPC),SNR 65dB,经 ES8311 ADC 采集 |
| 姿态传感器 | BMI270(6 轴:3 轴加速度 + 3 轴陀螺仪) |
| 电池容量 | 1750mAh(充电芯片 TP4057,充电电流约 300mA) |
| 工作温度 | 0 ~ 40°C |
| 待机电流(电源开关 OFF) | DC4.2V @ 0.23µA |
| 工作电流 | DC4.2V @ 120.2mA |
| Wi-Fi 工作电流 | DC4.2V @ 132.3mA |
| BLE 工作电流 | DC4.2V @ 154.6mA |
| 产品尺寸 | 84.0 × 54.0 × 19.6mm |
| 产品重量 | 81.0g |
| 包装尺寸 | 145.7 × 95.0 × 20.7mm |
| 毛重 | 98.5g |

### ESP32-S3FN8 要点

- 双核 Xtensa LX7 @240MHz,512KB SRAM,384KB ROM
- **8MB 内置 Flash,无 PSRAM** —— 大缓冲(全屏帧缓冲、长音频缓冲、大 JSON)需精打细算内存
- 原生 USB-OTG(USB-C 口即原生 USB,支持 USB CDC 串口、HID 设备模拟等)
- ADC、RMT(可用于 IR 发送)、I2S、SPI、I2C、UART 等常规外设

---

## 3. 硬件架构详解(基于原理图)

原理图版本:`Sch_M5CardputerAdv_v1.0_2025_06_20`(共 4 页:电源/接口、键盘、音频/IMU、EXT 扩展)。

### 3.1 电源系统

```
USB-C 5V (+5VIN) ──┬── TP4057 充电管理 ── 锂电池 BAT (1750mAh)
                   │        (PROG=3.3kΩ → 充电电流约 300mA)
                   │
电池 VBAT ── SW1(侧面电源开关, P-MOS 切断) ── VBAT_OUT
                   │
                   ├── SY7088 升压 → +5VOUT (给 Grove / EXT-14P 的 5VOUT 供电)
                   └── SY8089 降压 → +3.3V (主控与所有板载外设)
```

- **充电**:TP4057,由 USB 5V 供电,PROG 电阻 3.3kΩ,充电电流约 300mA。
- **电源开关 SW1**(侧面拨动开关):通过双 P-MOSFET(LP3218)物理切断电池输出,因此 OFF 待机电流仅 0.23µA。
- **复位监控**:CN809J 电压监控芯片 + BTN1(接 EN,复位键);BTN2 接 G0(BOOT 键,即 M5 库中的 `BtnA`)。
- **电池电量检测**:VBAT 经 **100kΩ/100kΩ 分压**接到 **G10**(ADC),实际电池电压 = ADC 读数 × 2。分压电路上还有 100nF 滤波电容。
- **Grove 5V**:经一颗 SPDT 开关(SW2)在 +5VOUT(电池升压)与 +5VIN(USB 输入)之间选择后输出到 Grove 的 5V 脚。
- 板上另有 FPC-8P 内部排线座(GND/3V3/5V/G7/G8/G9),用于内部连接。

### 3.2 键盘(原理图第 2 页)

- 扫描芯片:**TI TCA8418RTWR**,I2C 接口,**7 位地址 0x34**。
- 挂在内部 I2C 总线:SDA=G8、SCL=G9,中断 **INT=G11**(低有效,芯片侧接 3.3kΩ 上拉)。
- 56 个按键组成 **ROW0~ROW6 × COL0~COL7(7 行 × 8 列)电气矩阵**,逻辑上呈现为 4 行 × 14 列布局;列线上串 22Ω 电阻。
- TCA8418 内置去抖、FIFO 事件队列、按下/释放事件,支持"幽灵键"更少的多键检测,相比初代 GPIO 扫描省引脚且更可靠。

### 3.3 音频(原理图第 3 页)

- **ES8311** 编解码器:
  - 控制接口 I2C:CCLK=G9(SCL)、CDATA=G8(SDA),CE 脚接地 → **I2C 地址 0x18**。
  - 数字音频接口 I2S:**SCLK=G41、ASDOUT=G46(录音数据,ES8311→MCU)、LRCK=G43、DSDIN=G42(播放数据,MCU→ES8311)**。
  - 原理图中 **MCLK 未接主控 GPIO**,需将 ES8311 配置为以 SCLK 为时钟源的模式(M5Unified/出厂固件已处理)。
- **播放链路**:ES8311 DAC 输出(DAC_P)→ NS4150B 单声道 D 类功放 → 8Ω/1W 扬声器。
- **耳机口 PJ-342(3.5mm)**:DAC 输出同时经 100Ω 送耳机;插座带检测脚 **HP_DET**,通过 2N7002 MOS 管控制 NS4150B 的 CTRL(AMP_EN)——**插入耳机自动关闭扬声器功放**(纯硬件逻辑,软件无需干预,也无法从软件强制同时输出)。
- **麦克风**:MSM381A3729H9BPC 模拟 MEMS 麦克风,差分(MIC_P/MIC_N)接 ES8311 的 MIC1P/MIC1N,由 ES8311 内部 ADC 采样(**不是 PDM 麦克风**)。

### 3.4 IMU(原理图第 3 页)

- **BMI270** 六轴传感器,I2C 接口(SDX=G8、SCX=G9),**7 位地址 0x69**。
- INT1/INT2 **未连接**到主控 → 无法用中断唤醒,只能轮询。

### 3.5 红外发射(原理图第 1 页)

- IR 发射管接 **G44**,串 22Ω 限流电阻,高电平点亮。
- **注意:G44 是 ESP32-S3 默认的 UART0 RXD**。使用 IR 时避免同时占用 UART0 引脚功能;日志/烧录请走 USB CDC。
- 仅发射,无红外接收管。

### 3.6 microSD(原理图第 1 页)

- SPI 接口:**CS=G12、MOSI=G14、CLK=G40、MISO=G39**,四线均串 33Ω 电阻并带 ESD 保护管。
- **此 SPI 总线与 EXT-14P(Cap-Bus)共享**(SCK/MOSI/MISO 相同,CS 不同),详见 5.2。

### 3.7 LCD

- ST7789V2,SPI 接口,引脚在 Stamp-S3A 模块内部引出(G33~G38,不经过 DIP 排针):

| 信号 | GPIO |
| :--: | :--: |
| DISP_BL(背光)/ RGB LED PWR_EN | G38 |
| RST | G33 |
| RS(DC) | G34 |
| DAT(MOSI) | G35 |
| SCK | G36 |
| CS | G37 |

- G38 同时是屏幕背光使能和 Stamp-S3A 板载 RGB LED 的电源使能(PWR_EN)。
- 分辨率 240×135,横屏使用时 M5GFX 会自动处理偏移(offset)与旋转。

---

## 4. 完整管脚映射

### 汇总表(按 GPIO 排序)

| GPIO | 功能 | 说明 |
| :--: | :-- | :-- |
| G0 | BOOT 按键(BtnA) | 下载模式按键;运行时可作用户按键 |
| G1 | Grove 白线 | PORT.CUSTOM,带 ESD 保护 + 10kΩ 上拉至 3.3V(原理图标注 IIC_SCL) |
| G2 | Grove 黄线 | PORT.CUSTOM,带 ESD 保护 + 10kΩ 上拉至 3.3V(原理图标注 IIC_SDA) |
| G3 | EXT-14P Pin1 RESET | → Cap LoRa1262 的 SX1262 RST |
| G4 | EXT-14P Pin3 INT | → Cap LoRa1262 的 SX1262 DIO1/IRQ |
| G5 | EXT-14P Pin13 CS | → Cap LoRa1262 的 SX1262 NSS |
| G6 | EXT-14P Pin5 BUSY | → Cap LoRa1262 的 SX1262 BUSY |
| G7 | 内部 FPC-8P IO1 | 内部预留 |
| G8 | **内部 I2C SDA** | 键盘 TCA8418 / IMU BMI270 / 音频 ES8311 共用;亦引出到 EXT-14P Pin8 |
| G9 | **内部 I2C SCL** | 同上;亦引出到 EXT-14P Pin10 |
| G10 | 电池电压 ADC | 100k/100k 分压,Vbat = 读数 × 2 |
| G11 | 键盘中断 | TCA8418 INT,低有效 |
| G12 | microSD CS | |
| G13 | EXT-14P Pin12 UART_RX | ← Cap LoRa1262 GNSS 的 GPS-TX |
| G14 | 共享 SPI MOSI | microSD + EXT-14P Pin9 |
| G15 | EXT-14P Pin14 UART_TX | → Cap LoRa1262 GNSS 的 GPS-RX |
| G33 | LCD RST | 模块内部 |
| G34 | LCD RS(DC) | 模块内部 |
| G35 | LCD DAT(MOSI) | 模块内部 |
| G36 | LCD SCK | 模块内部 |
| G37 | LCD CS | 模块内部 |
| G38 | LCD 背光 / RGB LED PWR_EN | 模块内部 |
| G39 | 共享 SPI MISO | microSD + EXT-14P Pin11(MTCK) |
| G40 | 共享 SPI CLK | microSD + EXT-14P Pin7(MTDO) |
| G41 | ES8311 I2S SCLK(BCLK) | (MTDI) |
| G42 | ES8311 I2S DSDIN(播放数据) | (MTMS) |
| G43 | ES8311 I2S LRCK(WS) | (默认 U0TXD) |
| G44 | IR 发射 | (默认 U0RXD,注意冲突) |
| G46 | ES8311 I2S ASDOUT(录音数据) | |

### 官方分类表

**LCD**

| Stamp-S3A | G38 | G33 | G34 | G35 | G36 | G37 |
| :-------: | :-----: | :--: | :--: | :--: | :--: | :--: |
| ST7789V2 | DISP_BL | RST | RS | DAT | SCK | CS |
| RGB LED | PWR_EN | | | | | |

**Audio(ES8311)**

| Stamp-S3A | G8 | G9 | G41 | G46 | G43 | G42 |
| :-------: | :--: | :--: | :--: | :----: | :--: | :---: |
| ES8311 | SDA | SCL | SCLK | ASDOUT | LRCK | DSDIN |

**IMU(BMI270)**:SDA=G8,SCL=G9
**IR 发射**:G44
**电池 ADC**:G10
**键盘(TCA8418RTWR)**:SDA=G8,SCL=G9,INT=G11
**microSD**:CS=G12,MOSI=G14,CLK=G40,MISO=G39

**HY2.0-4P(Grove)**

| HY2.0-4P | Black | Red | Yellow | White |
| :---------: | :---: | :--: | :----: | :---: |
| PORT.CUSTOM | GND | 5V | G2 | G1 |

**EXT 2.54-14P(Cap-Bus)**

| FUNC | PIN | LEFT | RIGHT | PIN | FUNC |
| :---: | :--: | :--: | :---: | :---: | :-----: |
| RESET | G3 | 1 | 2 | 5VIN | |
| INT | G4 | 3 | 4 | GND | |
| BUSY | G6 | 5 | 6 | 5VOUT | |
| SCK | G40 | 7 | 8 | G8 | I2C_SDA |
| MOSI | G14 | 9 | 10 | G9 | I2C_SCL |
| MISO | G39 | 11 | 12 | G13 | UART_RX |
| CS | G5 | 13 | 14 | G15 | UART_TX |

> 原理图中 EXT-14P 的 Pin8/Pin9 信号名直接标为 **GPS-TX(G15)/GPS-RX(G13)**,印证该接口是为 LoRa+GNSS Cap 设计的 Cap-Bus。

---

## 5. I2C / SPI / UART 总线拓扑与设备地址

### 5.1 内部 I2C 总线(G8=SDA,G9=SCL)

板上三个外设共用一条 I2C 总线,并引出到 EXT-14P 和(经由 M5Unified 的 In_I2C)供扩展模块使用:

| 设备 | 7 位地址 | 用途 |
| :--: | :--: | :-- |
| TCA8418RTWR | **0x34** | 键盘扫描 |
| ES8311 | **0x18** | 音频编解码控制(CE 接地) |
| BMI270 | **0x69** | 六轴 IMU |

总线上拉:3.3kΩ 至 3.3V(键盘页 R35/R36;IMU 页另有 R43 3.3kΩ 上拉 SDO/地址脚)。

> Cap LoRa1262 上还有一颗 I2C 扩展芯片挂在此总线上(官方文档提及 SDA=G8/SCL=G9),开发外接 I2C 设备时注意避开以上地址。

### 5.2 共享 SPI 总线(G40=SCK,G14=MOSI,G39=MISO)

| 设备 | CS | 其他控制脚 |
| :--: | :--: | :-- |
| microSD | G12 | — |
| EXT-14P / Cap LoRa1262 的 SX1262 | G5 | RST=G3,DIO1=G4,BUSY=G6 |

两设备通过不同 CS 分时共享总线;同时使用 SD 卡与 LoRa 时注意总线互斥(RadioLib + SD 库混用时留意事务锁 / `SPI.beginTransaction`)。LCD 使用模块内部独立 SPI(G35/G36),互不影响。

### 5.3 UART

| 用途 | TX | RX | 备注 |
| :--: | :--: | :--: | :-- |
| USB CDC(日志/烧录) | USB-C 原生 USB | — | 推荐的调试口 |
| EXT-14P UART(GNSS) | G15 | G13 | Cap LoRa1262 GNSS 默认 115200 8N1 |
| UART0 默认引脚 | G43 | G44 | **已被占用**:G43=I2S LRCK,G44=IR;不要使用默认 UART0 引脚 |

---

## 6. 扩展接口

### 6.1 HY2.0-4P(Grove,PORT.CUSTOM)

- 引脚:GND(黑)、5V(红)、G2(黄)、G1(白)。
- G1/G2 带 ESD 保护和 10kΩ 上拉至 3.3V,原理图网络名为 IIC_SCL/IIC_SDA → 适合作外部 I2C(M5Unified 的 `Ex_I2C`),也可作普通 GPIO / UART / ADC 使用。
- 5V 脚由板载 SPDT 在"电池升压 5VOUT"与"USB 5VIN"之间选择供电。

### 6.2 EXT 2.54-14P(Cap-Bus)

- 2.54mm 间距 14P 母座,位于机身顶部,是官方 Cap 系列模块(如 Cap LoRa1262)的安装总线。
- 提供:SPI(含独立 CS/RST/INT/BUSY 控制线)、I2C(G8/G9,与内部总线共享!)、UART(G13/G15)、5VIN、5VOUT、GND。
- 自研扩展板可直接利用该接口,注意 I2C 与内部设备共线、SPI 与 SD 卡共线。

### 6.3 其他

- USB-C:供电/充电 + 原生 USB(CDC 串口、烧录、USB HID 模拟等)。
- microSD 卡槽:数据存储、固件分发。
- 磁吸底座 + 乐高兼容孔:机构固定。

---

## 7. Cap LoRa1262 外接模块(LoRa + GNSS)

**Cap LoRa 1262 for Cardputer Adv(SKU: U214?,SX1262 + ATGM336H)**,通过 EXT 2.54-14P(Cap-Bus)安装在 Cardputer Adv 顶部。

### 7.1 LoRa 部分(Semtech SX1262)

| 项目 | 规格 |
| :--: | :-- |
| 芯片 | SX1262 |
| 频段 | 868 ~ 923 MHz(按地区法规选择具体频点) |
| 发射功率 | 最高 +22 dBm |
| 接收灵敏度 | -147 dBm(低速率长距离模式) |
| 调制方式 | LoRa、FSK、GFSK、MSK、GMSK、OOK |
| 数据速率 | 最高 300 kbps |
| 接口 | SPI |
| 天线 | 外接 RP-SMA 天线(108×9.3mm,3dBi) |

**与主机的引脚连接(经 Cap-Bus):**

| SX1262 信号 | Cardputer Adv GPIO |
| :--: | :--: |
| NSS(CS) | G5 |
| SCK | G40 |
| MOSI | G14 |
| MISO | G39 |
| RST | G3 |
| DIO1(IRQ) | G4 |
| BUSY | G6 |

> 出厂固件使用 **RadioLib** 驱动 SX1262,Arduino 下同样推荐 RadioLib:
> `SX1262 radio = new Module(/*cs*/5, /*irq*/4, /*rst*/3, /*busy*/6);`(共享 SPI 记得先 `SPI.begin(40, 39, 14)`)。

### 7.2 GNSS 部分(ATGM336H-6N,AT6668 方案)

| 项目 | 规格 |
| :--: | :-- |
| 模组/芯片 | ATGM336H-6N @ AT6668 |
| 卫星系统 | GPS / BD2 / BD3 / GLONASS / GALILEO / QZSS |
| 定位精度 | <1.5m(CEP50) |
| 更新率 | 最高 10Hz |
| 通道 | 50 |
| 冷启动 | 约 23s |
| 接口 | UART,**115200bps 8N1**,NMEA 输出 |
| 天线 | 内置陶瓷天线 |

**UART 连接**:模块 GPS-TX → 主机 **G13(RX)**;模块 GPS-RX ← 主机 **G15(TX)**。
Arduino 示例:`Serial1.begin(115200, SERIAL_8N1, /*RX*/13, /*TX*/15);` 搭配 TinyGPS++ 等 NMEA 解析库。

### 7.3 电气与机械

- 供电:DC 5V(经 Cap-Bus 的 5VOUT/5VIN)
- 功耗:LoRa 工作 163.4mA;GNSS 33.1mA
- 尺寸:84.0 × 24.0 × 15.2mm;重量 22.1g(不含天线)
- 模块上另有一颗 I2C 芯片挂在 G8/G9 总线(扩展/识别用)

---

## 8. 开发平台与工具链

支持 **Arduino / UiFlow2 / PlatformIO / ESP-IDF** 四种开发方式。

### 8.1 进入下载(烧录)模式 —— 通用步骤

1. 将侧面电源开关拨到 **OFF**;
2. **按住 G0(BOOT)键**;
3. 用 USB-C 连接电脑后松开按键,即进入下载模式。

> 正常运行时 USB CDC 也可直接烧录;上述硬件下载模式用于固件砖了或 USB CDC 不可用时兜底。

### 8.2 Arduino

1. 安装 Arduino IDE,添加 M5Stack 板管理(Boards Manager URL),选择开发板 **M5Cardputer**;
2. 库管理器安装 **M5Cardputer** 库及全部依赖(M5Unified、M5GFX、IRremote);
3. 打开示例(如 `Basic -> display`),选择端口,编译上传。

官方 API 文档覆盖模块:Battery、Button、Display、IMU、IR、Keyboard、Mic、microSD、Speaker。

### 8.3 UiFlow2

1. 用 **M5Burner** 下载并烧录 Cardputer-Adv 的 UiFlow2 固件(烧录时同样先进下载模式);
2. 烧录时配置 Wi-Fi SSID/密码、服务器、NTP、时区、启动模式(直接运行 / 启动菜单 / 仅网络配置);
3. 连接方式二选一:
   - **无线**:设备联网后在启动界面查看 Access Code,到 uiflow2.m5stack.com 输入配对;
   - **有线**:WebTerminal 选串口连接。
4. Blockly / MicroPython 编程,"Run Once" 临时运行,"Run Always" 固化到设备。

### 8.4 ESP-IDF

- 出厂固件仓库:`m5stack/M5Cardputer-UserDemo`,**分支 `CardputerADV`**;
- 要求 **ESP-IDF v5.4.2**;
- 构建流程:

```bash
python3 ./fetch_repos.py   # 拉取 11 个依赖组件
idf.py build
idf.py flash
```

### 8.5 PlatformIO

- platform: `espressif32`,board 选 ESP32-S3(8MB Flash、无 PSRAM),framework arduino;
- `lib_deps = m5stack/M5Cardputer`(自动带出 M5Unified/M5GFX);
- 建议开启 `board_build.flash_mode`、USB CDC:`-DARDUINO_USB_CDC_ON_BOOT=1`、`-DARDUINO_USB_MODE=1`。

---

## 9. Arduino 驱动库(M5Cardputer)详解

仓库:https://github.com/m5stack/M5Cardputer
最新 Release:**v1.2.0(2026-06)**;MIT 许可;描述:"Basic library for M5Stack M5Cardputer and **M5Cardputer-ADV** Board"(同一库同时支持两代)。
依赖:**M5Unified、M5GFX、IRremote**。

### 9.1 顶层 API(`M5Cardputer.h`)

```cpp
#include <M5Cardputer.h>

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, /*enableKeyboard=*/true);
}
void loop() {
    M5Cardputer.update();   // 每帧调用,刷新按键/键盘状态
}
```

`M5_CARDPUTER` 类成员(大多为 M5Unified 对象的引用):

| 成员 | 类型 | 说明 |
| :-- | :-- | :-- |
| `Display` / `Lcd` | `M5GFX&` | 屏幕(M5GFX 全 API:文字/图形/Sprite/Canvas) |
| `Power` | `Power_Class&` | 电池电压/电量(内部处理 G10 分压) |
| `Speaker` | `Speaker_Class&` | 扬声器(ES8311 播放通路,tone/wav) |
| `Mic` | `Mic_Class&` | 麦克风录音(ES8311 ADC 通路) |
| `BtnA` | `Button_Class&` | G0 按键 |
| `Keyboard` | `Keyboard_Class` | 56 键键盘 |
| `In_I2C` | `I2C_Class&` | 内部 I2C(G8/G9) |
| `Ex_I2C` | `I2C_Class&` | 外部 I2C(Grove G1/G2) |

### 9.2 键盘 API(`Keyboard_Class`)

- 底层通过 `KeyboardReader` 抽象:初代 Cardputer 用 `IOMatrix`(GPIO 扫描),**Adv 用 `TCA8418` reader**(I2C 0x34),`begin()` 自动选择,应用层 API 完全一致。
- 常用方法:

| 方法 | 说明 |
| :-- | :-- |
| `isChange()` | 本帧按键状态是否变化 |
| `isPressed()` | 当前按下的键数 |
| `isKeyPressed(char c)` | 某字符键是否按下 |
| `keyList()` | 当前按下键的坐标列表(`Point2D_t`,x=0..13, y=0..3) |
| `getKeyValue(coor)` | 坐标 → `KeyValue_t{value_first, value_second(Shift 层), value_third(Fn 层)}` |
| `updateKeysState()` / `keysState()` | 汇总状态 `KeysState`:`word`(可打印字符)、`hid_keys`、`modifier_keys`,以及 tab/fn/shift/ctrl/opt/alt/enter/space/esc/del/f1-f12/方向键等布尔位 |
| `capslocked()` / `setCapsLocked(bool)` | 大写锁定 |

典型输入循环:

```cpp
M5Cardputer.update();
if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    auto st = M5Cardputer.Keyboard.keysState();
    for (char c : st.word) { /* 输入字符 */ }
    if (st.enter) { /* 回车 */ }
    if (st.del)   { /* 退格 */ }
}
```

### 9.3 库自带示例

| 路径 | 内容 |
| :-- | :-- |
| `Basic/display` | 屏幕绘制 |
| `Basic/button` | G0 按键 |
| `Basic/buzzer` | 蜂鸣/扬声器发声 |
| `Basic/ir_nec` | 红外 NEC 发射(IRremote,G44) |
| `Basic/keyboard/singlePress` / `multiPress` / `inputText` | 键盘单键/多键/文本输入 |
| `Basic/keyboard/usbKeyboard` | 把 Cardputer 模拟成 USB HID 键盘 |
| `Basic/mic` / `mic_wav_record` | 麦克风波形 / 录 WAV |
| `Basic/sdcard` | SD 卡读写 |
| `UI/REPL` | 屏上 REPL 界面 |
| `Advanced/SSHClient` | SSH 客户端 |

---

## 10. 出厂固件(ESP-IDF UserDemo)结构

仓库:https://github.com/m5stack/M5Cardputer-UserDemo/tree/CardputerADV(ESP-IDF v5.4.2)

```
main/
├── apps/                 # 各内置应用
│   ├── app_launcher      # 启动器/主菜单
│   ├── app_chat          # 聊天(xiaozhi AI 语音相关)
│   ├── app_clock         # 时钟
│   ├── app_gps           # GPS 定位(Cap 的 GNSS,NMEA)
│   ├── app_imu           # BMI270 姿态演示
│   ├── app_keyboard      # 键盘测试
│   ├── app_lora_chat     # LoRa 聊天(SX1262 + RadioLib)
│   ├── app_record        # 录音
│   ├── app_remote        # 红外遥控
│   ├── app_repl          # REPL(PikaPython)
│   ├── app_sdcard        # SD 卡
│   ├── app_set_wifi      # Wi-Fi 配置
│   ├── app_stringir_toolkit  # IR 工具箱
│   ├── app_wifi_scan     # Wi-Fi 扫描
│   └── utils/
├── assets/               # 资源
└── hal/                  # 硬件抽象层
    ├── cap_lora868       # Cap LoRa 模块 HAL(SPI SX1262 + UART GNSS)
    ├── keyboard          # TCA8418 键盘 HAL
    └── utils/
```

- 依赖 11 个开源组件:键盘驱动、GPS、**RadioLib**(LoRa)、M5GFX、raylib、平滑 UI 工具包、**PikaPython**、**xiaozhi-esp32**(AI 语音)等,经 `fetch_repos.py` 拉取。
- `hal/cap_lora868` 与 `app_gps`、`app_lora_chat` 是开发 Cap LoRa1262 应用最直接的参考代码。

---

## 11. 键盘布局与键值映射

物理 4 行 × 14 列;每键三层含义:`普通层 / Shift(Aa)层 / Fn 层`(取自库 `Keyboard_def.h` / `Keyboard.h` 的 `_key_value_map`):

**第 1 行**

| 普通 | \` | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 0 | - | = | BS |
|:--|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| Shift | ~ | ! | @ | # | $ | % | ^ | & | * | ( | ) | _ | + | BS |
| Fn | ESC | F1 | F2 | F3 | F4 | F5 | F6 | F7 | F8 | F9 | F10 | F11 | F12 | DEL |

**第 2 行**:TAB q w e r t y u i o p [ ] \\(Shift 层为大写与 { } |)

**第 3 行**:FN、SHIFT、a s d f g h j k l ; ' ENTER
> Fn+`;` = **↑(上方向键)**

**第 4 行**:CTRL、OPT、ALT、z x c v b n m , . / SPACE
> Fn+`,` = **←**,Fn+`.` = **↓**,Fn+`/` = **→**

即方向键 = Fn + `; , . /`(呈倒 T 形排布)。

---

## 12. 开发注意事项与坑点汇总

1. **无 PSRAM**:ESP32-S3FN8 只有 512KB SRAM。240×135×16bit 全屏 Sprite ≈ 63KB 尚可,但多层 Canvas、长录音缓冲、TLS + JSON 大对象要小心内存。
2. **G8/G9 I2C 总线是"公共干道"**:键盘、IMU、音频控制、Cap 模块都在这条线上。自己直接 `Wire.begin()` 可能与 M5Unified 的 In_I2C 冲突,应复用 `M5Cardputer.In_I2C` 或 M5Unified 的 I2C 封装;外接 I2C 设备避开 0x18/0x34/0x69。
3. **SPI 总线共享**:SD 卡(CS=G12)与 Cap LoRa 的 SX1262(CS=G5)共享 G40/G14/G39。并发访问需事务保护;初始化顺序上先 `SPI.begin(40, 39, 14)` 再分别初始化设备。
4. **G44(IR)= 默认 UART0 RX、G43(I2S LRCK)= 默认 UART0 TX**:不要启用默认引脚的 UART0;调试输出走 USB CDC(`ARDUINO_USB_CDC_ON_BOOT=1`)。
5. **耳机插入自动断开扬声器**是硬件行为(HP_DET → 功放 CTRL),软件不可越过。
6. **BMI270 中断脚未接**,姿态检测只能轮询;需要低功耗运动唤醒的方案要另想办法。
7. **电池电压**:`Power.getBatteryVoltage()`(M5Unified)或自行 `analogRead(G10) × 2`;分压恒接,对深度睡眠功耗影响极小(200kΩ 级)。
8. **烧录救砖**:开关 OFF → 按住 G0 → 插 USB,进 ROM 下载模式。
9. **键盘读取依赖 `M5Cardputer.update()`**:主循环必须周期调用;TCA8418 有硬件 FIFO,轮询间隔宽松,但过长仍会丢连击。
10. **LoRa 频段合规**:SX1262 硬件支持 868–923MHz,实际使用需按所在地法规选频(如 EU868、US915、AS923 等);发射前必须接好天线,空载大功率发射可能损坏 PA。
11. **UiFlow2 固件与自编固件互斥**:烧自己的程序会覆盖 UiFlow2/出厂固件,可用 M5Burner 随时刷回。
12. **工作温度 0~40°C**、充电时注意环境温度(锂电池)。

---

## 13. Launcher 多固件启动器基础知识

> 仓库:https://github.com/bmorcelli/Launcher (原名 M5Launcher);调研时最新 Release 为 **v2.7.2**。
> 作用:让 Cardputer Adv 像"掌机"一样,从 TF 卡 / 网络选择并切换多个固件,解决单固件单功能的限制。

### 13.1 工作原理

ESP32-S3 **无法直接执行 TF 卡上的代码**(代码只能从内部 Flash XIP 或 SRAM 执行,SD 卡不是内存映射设备,且无进程级 MMU)。Launcher 采用"**常驻启动器 + 按需刷写**"方案:

1. Launcher 自身常驻 Flash 的固定分区(使用自带的 8MB 分区表 `custom_8Mb.csv`);
2. 固件 `.bin` 存放在 TF 卡上(或经 Wi-Fi 从 M5Burner OTA 列表下载);
3. 选中后 Launcher 将 `.bin` **写入应用分区**并重启进入该固件(切换耗时约十几秒);
4. 开机时按住特定按键可重新进入 Launcher 换其他固件。

因此它不是真正的动态加载(同一时刻 Flash 中只有一个应用固件),但使用体验接近"固件选单掌机",且是**唯一兼容现有社区固件生态**(Bruce、Nemo 等)的方案。

### 13.2 Cardputer Adv 支持状态(重要)

- **Adv 支持已合并进主线**(2025-09,贡献者 n0xa,见仓库 `boards/m5stack-cardputer/CardputerADV.md`),**与初代 Cardputer 共用同一个固件文件**;
- Release 资产中**没有单独的 "cardputer-adv" bin**,正确文件是:**`Launcher-m5stack-cardputer.bin`**(约 1.3MB);
- 兼容原理:同一编译环境同时内置两代键盘驱动,启动时在 I2C 0x34 尝试初始化 TCA8418 —— **成功 → 判定为 Adv(I2C 键盘);失败 → 回退初代 GPIO 矩阵键盘**,运行时自动检测,无需用户区分;
- 编译参数与本机原理图一致:`TCA8418_I2C_ADDR=0x34`、`SDA=8`、`SCL=9`、`INT=11`、电池 ADC=G10、背光=G38、ST7789 引脚 G33~G37;
- 早期的 `TheRealHaoLiu/cardputer-adv-launcher` fork 是支持合并前的过渡产物,现在直接用官方主线即可;
- **不要误刷** `Launcher-headless-esp32s3-8mb.bin`("headless" 是给无屏裸板用的)。

### 13.3 烧录方法

方式一(推荐):**Web Flasher** —— Chrome/Edge 打开 https://bmorcelli.github.io/Launcher/ ,选 Cardputer,浏览器直接烧录。

方式二:esptool(release 的 bin 为含 bootloader 的完整合并镜像,烧到 **0x0**):

```bash
esptool.py --chip esp32s3 -p <端口> write_flash 0x0 Launcher-m5stack-cardputer.bin
```

USB 不识别时先进下载模式:电源开关 OFF → 按住 G0 → 插 USB 后松开。

### 13.4 Adv 上的按键操作

| 功能 | 按键 |
| :--: | :-- |
| 上一项 | `,`(←)或 `;`(↑) |
| 下一项 | `/`(→)或 `.`(↓) |
| 确认 | Enter 或 G0 键 |
| 返回/取消 | `` ` `` 或 Backspace |

按住按键有自动重复(首次约 350ms 后开始,间隔约 150ms)。

### 13.5 主要功能

- **TF 卡固件浏览/刷写**:选择 SD 卡上的 `.bin` 刷入并启动;
- **OTA 在线安装**:列出 M5Burner 上适配本机的固件,直接联网下载安装;
- **WebUI**:设备开热点/联网后,用浏览器管理 TF 卡文件、无线上传 `.bin`;
- 固件目录(社区适配列表):https://bmorcelli.github.io/Launcher/catalog.html —— 给 Adv 装固件时优先从这里或 M5Burner 取 **Cardputer 版本**的 bin,分区兼容性最好。

### 13.6 已知限制与注意点

1. Adv 键盘走 **TCA8418 轮询/中断混合**方案,目前仅映射导航键,不支持组合键、无按字母跳转;
2. 切换固件 = 刷写 Flash,约十几秒,不适合频繁秒切场景;
3. 被刷入的固件若自带不同分区表,可能覆盖 Launcher 本身,需重新烧录 Launcher(从固件目录取的兼容版本一般不会);
4. 适配记录中提到 Adv 上 **G5 需拉高**以避免与 SD 卡 SPI 冲突(G5 是 EXT-14P/Cap-Bus 的 LoRa CS)——若你同时开发 Cap LoRa1262 外设,注意 Launcher 环境下 G5 的状态假设;
5. 出于同样原因,任何"多固件"方案里,各固件对共享 SPI 总线(SD + LoRa)的初始化顺序都要自行保证。

---

## 14. 参考资料与链接

### 官方文档
- 产品页(规格/管脚):https://docs.m5stack.com/zh_CN/core/Cardputer-Adv
- Arduino 快速上手:https://docs.m5stack.com/zh_CN/arduino/m5cardputer/program
- UiFlow2 快速上手:https://docs.m5stack.com/zh_CN/uiflow2/cardputer-adv/program
- Cap LoRa-1262 产品页:https://docs.m5stack.com/en/cap/Cap_LoRa-1262
- Cap LoRa 1262 商店页:https://shop.m5stack.com/products/cap-lora-1262-for-cardputer-adv-sx1262-atgm336h

### 原理图 / 硬件
- Cardputer-Adv 原理图 PDF:https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1178/Sch_M5CardputerAdv_v1.0_2025_06_20_17_19_58.pdf
- Stamp-S3 原理图 PDF:https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1150/Sch_StampS3_v0.3.3.pdf
- 尺寸图 PDF:https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1178/K132-Adv-cardputer-ADV.pdf
- 结构文件:https://github.com/m5stack/M5_Hardware/tree/master/Products/K132-Adv_Cardputer-Adv/Structures

### 代码仓库
- Arduino 驱动库:https://github.com/m5stack/M5Cardputer(v1.2.0,MIT)
- 出厂固件(ESP-IDF):https://github.com/m5stack/M5Cardputer-UserDemo/tree/CardputerADV(ESP-IDF v5.4.2)
- 依赖库:M5Unified、M5GFX(均 MIT);LoRa 推荐 RadioLib

### 多固件启动器(Launcher)
- 仓库:https://github.com/bmorcelli/Launcher
- Web Flasher(浏览器烧录):https://bmorcelli.github.io/Launcher/
- 固件目录:https://bmorcelli.github.io/Launcher/catalog.html
- Adv 适配说明:https://github.com/bmorcelli/Launcher/blob/main/boards/m5stack-cardputer/CardputerADV.md
- 动态加载替代方案:espressif/elf_loader(https://components.espressif.com/components/espressif/elf_loader/)、UiFlow2 MicroPython

### 关键芯片数据手册关键词
ESP32-S3FN8 / ST7789V2 / TCA8418RTWR / ES8311 / NS4150B / BMI270 / SX1262 / ATGM336H(AT6668) / TP4057 / SY7088 / SY8089
