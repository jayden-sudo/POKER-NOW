# xingzhi-cube-1.54tft-wifi 硬件配置说明

> 本文档由对出厂固件（小智 xiaozhi）逆向 + 官方仓库源码 + **JTAG 寄存器实测** 三方核对整理。
> **【实测】** = 通过读取 ESP32-S3 的 GPIO 交换矩阵寄存器 / 启动串口日志确认；
> **【仓库】** = 与 `78/xiaozhi-esp32` 官方板级源码一致；**【待验证】** = 推断或需实机进一步确认。
>
> ⚠️ 重要：本板实机与官方公开 config.h **有出入**（麦克风）。凡冲突处以【实测】为准，详见第 2 节备注。

## 1. 概览

| 项目 | 内容 | 来源 |
|---|---|---|
| 板型名(SKU) | `xingzhi-cube-1.54tft-wifi` | 【实测】固件 SKU |
| 主控 | ESP32-S3（QFN56, rev v0.2），双核 + LP 核，240MHz，Wi-Fi + BLE5 | 【实测】esptool |
| PSRAM | **内置 8MB**（octal, AP_3v3） | 【实测】esptool |
| Flash | Winbond 8MB（W25Q64，QIO，3.3V） | 【实测】esptool |
| 屏幕 | 1.54" 240×240 IPS，**ST7789**，SPI | 【实测/仓库】 |
| 麦克风 | **PDM 数字麦克风**（I2S0 PDM RX） | 【实测】 |
| 扬声器 | I2S 功放（无 codec 芯片，NoAudioCodecSimplexPdm） | 【实测/仓库】 |
| 唤醒方式 | 片上 esp-sr（WakeNet 模型在 `model` 分区，960KB） | 【实测】分区/固件 |
| 出厂固件 | 小智 xiaozhi v1.6.0（factory 分区，无 OTA 槽） | 【实测】启动日志 |
| MAC | fc:01:2c:d8:16:20 | 【实测】 |
| 供电 | USB-C（供电/充电 + USB-Serial-JTAG）+ 锂电池 | 【实测】 |
| 晶振 | 40MHz | 【实测】 |

## 2. GPIO 引脚分配（全部 JTAG 实测）

| 功能 | GPIO | 说明 | 状态 |
|---|---|---|---|
| **LCD** MOSI (SDA) | **GPIO10** | SPI3 / SPI3_D，输出信号 68 | 【实测】 |
| **LCD** SCLK (SCL) | **GPIO9** | SPI3 / SPI3_CLK，输出信号 66 | 【实测】 |
| **LCD** CS | **GPIO14** | SPI3 / SPI3_CS0，输出信号 71 | 【实测】 |
| **LCD** DC (RS) | **GPIO8** | 普通输出 | 【实测】 |
| **LCD** RST | **GPIO18** | 普通输出 | 【实测】 |
| **LCD** 背光 | **GPIO13** | LEDC PWM（LEDC ch0，信号 73），非反相 | 【实测】 |
| **喇叭** I2S BCLK | **GPIO15** | I2S1 输出 BCK（信号 28） | 【实测】 |
| **喇叭** I2S LRCK/WS | **GPIO16** | I2S1 输出 WS（信号 29） | 【实测】 |
| **喇叭** I2S DOUT | **GPIO7** | I2S1 输出 SD（信号 30） | 【实测】 |
| **麦克风** PDM CLK | **GPIO2** | I2S0 PDM 时钟（走 I2S0 WS，信号 27） | 【实测】 |
| **麦克风** PDM DATA | **GPIO3** | I2S0 PDM 数据（I2S0I_SD_IN，输入 mux ← GPIO3） | 【实测】 |
| 按键 上（原厂 音量-） | **GPIO39** | 输入，上拉，低有效 | 【实测】 |
| 按键 中（原厂 音量+） | **GPIO40** | 输入，上拉，低有效 | 【实测】 |
| 按键 下（原厂 聆听/待命, BOOT） | **GPIO0** | 输入，上拉，低有效 | 【实测】 |
| 电池电压检测 | **GPIO17** | ADC2 通道6（12dB 衰减，12bit），板载分压比≈**1.90**（实测电池 4.20V 校准） | 【实测·已校准】 |
| 充电状态检测 | **GPIO38** | 输入，高=充电中 | 【实测】 |
| **电源自锁** PWR_KEEP | **GPIO21** | RTC-GPIO，开机后置1维持供电（电池供电时需要）。真正的开关机由专用硬件电源键完成，不靠此脚软关机 | 【实测】 |
| USB D-/D+ | GPIO19 / GPIO20 | USB-Serial-JTAG（USB-C） | 【实测】 |

> **注意 / 与官方仓库的差异**
> - **麦克风是 PDM（CLK=GPIO2, DATA=GPIO3），不是标准 I2S**。官方 `config.h` 写的是标准 I2S（WS=4/SCK=5/DIN=6）+ `NoAudioCodecSimplex`，但本实机用的是 `NoAudioCodecSimplexPdm`，且 JTAG 矩阵证实 PDM 时钟在 GPIO2、数据从 GPIO3 读入。**自研固件必须按 PDM 接线，用 GPIO4/5/6 收不到声音。**
> - 第 4 个物理按键是**专用硬件电源键**（不接 MCU GPIO，固件读不到）：**短按开机、长按≈2 秒关机**，在硬件层直接控制整机电源。固件只读 3 个按键（GPIO0/39/40）。
> - **关机 = 长按这个硬件电源键 ≈2 秒**。固件侧拉低 GPIO21 无法可靠关机（尤其插着 USB 时 5V 旁路自锁），所以 demo 不做软关机、开关机全交给硬件电源键。GPIO21 仅在开机后拉高维持供电。
> - 喇叭走 **I2S1**、麦克风走 **I2S0**（PDM 只能用 I2S0）。两者独立，无 I2C codec 芯片。
> - 电池分压比已实测校准为 **1.90**（万用表实测电池 4.20V）；见 `demo/main/board_config.h` 的 `BAT_DIVIDER`。充电时读数会略偏高。
> - 三个固件可读按键物理排列（由上到下）= **GPIO39, GPIO40, GPIO0**（原厂功能 音量-/音量+/聆听）。

## 3. 音频架构

本板麦克风和扬声器**直接由 ESP32-S3 的 I2S 收发**，板上没有独立的音频 codec 或语音协处理芯片：

```
说话 → PDM 麦克风(GPIO2 CLK / GPIO3 DATA) → I2S0 PDM RX → S3
                                                          ↓ esp-sr(WakeNet) 片上唤醒
播放 → S3 → I2S1(BCLK=15 / WS=16 / DOUT=7) → I2S 功放 → 扬声器
```

- 采样率：麦克风 16kHz，扬声器 24kHz（`NoAudioCodecSimplexPdm`，simplex = 收发各用一个 I2S 口）。
- 唤醒词"你好小智"由片上 **esp-sr** 实现（模型在 `model` 分区），直接用 S3 的算力，无需外部语音芯片。
- 自研固件可直接用 ESP-IDF 的 `esp_driver_i2s`：PDM RX 用 `i2s_channel_init_pdm_rx_mode`，标准 TX 用 `i2s_channel_init_std_mode`。

## 4. 分区表（8MB Flash，出厂）

| 分区 | 偏移 | 大小 | 用途 |
|---|---|---|---|
| nvs | 0x9000 | 16KB | 配置/Wi-Fi |
| otadata | 0xd000 | 8KB | OTA 状态 |
| phy_init | 0xf000 | 4KB | RF 校准 |
| model | 0x10000 | 960KB | **esp-sr 唤醒词模型（WakeNet）** |
| factory | 0x100000 | 5MB | 应用（单 app，无 OTA 槽） |


## 5. 软件/开发环境要点

- 参考板级源码：`78/xiaozhi-esp32` → `main/boards/xingzhi-cube-1.54tft-wifi/`（注意麦克风按本文档 PDM 接线修正）。
- **开机即拉高 GPIO21**（`rtc_gpio_*`）维持供电：电池供电时保守保留。**开关机由专用硬件电源键完成**（短按开机、长按≈2s 关机），固件不做软关机。
- 控制台走 **USB-Serial-JTAG**（GPIO19/20，即 USB-C）。
- 显示：`esp_lcd_new_panel_st7789`，SPI3_HOST，spi_mode=3，pclk=80MHz，`invert_color=true`，240×240，offset 0。
- 电池 ADC 用 **ADC2**（S3 上 ADC2 与 Wi-Fi 有潜在冲突，用 oneshot 读取；出厂固件在 Wi-Fi 开启后仍能正常读）。



# 6. 硬件按键与屏幕的位置

```
+----------------+         
 | +------------+ ++ top    
 | |            | ++        
 | |            | ++ mid    
 | | LCD        | ++        
 | |            | ++ bottom 
 | |            | ++        
 | |            | |         
 | |            | |         
 | +------------+ ++ power  
 |                ++        
 +----------------+
```

