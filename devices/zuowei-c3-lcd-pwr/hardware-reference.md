# zuowei-c3-lcd-pwr 硬件配置说明

> 本文档由对出厂固件（小智 xiaozhi）的逆向 + JTAG 寄存器实测整理而来。
> 标注 **【实测】** 的项已通过读取 ESP32-C3 的 GPIO 交换矩阵寄存器 / 启动串口日志确认；
> 标注 **【待验证】** 的项为推断或需实机反馈确认。

## 1. 概览

| 项目 | 内容 | 来源 |
|---|---|---|
| 板型名 | `zuowei-c3-lcd-pwr` | 【实测】固件 SKU |
| 主控 | ESP32-C3（QFN32，rev v0.4），单核 RISC-V @160MHz，Wi-Fi + BLE5 | 【实测】esptool |
| Flash | Zbit 25VQ64（丝印 `ZBX2409 25VQ64CIJG`），8MB，DIO | 【实测】esptool |
| 语音芯片 | Wanson **VB6824**（丝印 `Wanson VB6824`） | 【实测】固件 |
| 功放 | `8002`（丝印 `8002D1 CGDF1`），单声道 D 类功放 | 【实测】丝印/信号链 |
| 屏幕 | 1.54" 240×240 IPS，ST7789 驱动（排线丝印 GMT154-03） | 【实测】固件驱动 |
| 出厂固件 | 小智 xiaozhi v1.7.2，四博/DOIT（OTA: xiaozhi.doit.am） | 【实测】启动日志 |
| 唤醒词 | "你好小智" | 【实测】固件 |
| MAC | 34:cd:b0:ba:30:64 | 【实测】 |
| 供电 | USB-C（供电/充电 + USB-Serial-JTAG 烧录调试口） | 【实测】 |
| 晶振 | 40MHz（C3）、24MHz（VB6824） | 【实测】丝印 |

## 2. GPIO 引脚分配

| 功能 | GPIO | 说明 | 状态 |
|---|---|---|---|
| LCD MOSI (SDA) | **GPIO1** | SPI2 / FSPID，输出信号 65 | 【实测】 |
| LCD SCLK (SCL) | **GPIO3** | SPI2 / FSPICLK，输出信号 63 | 【实测】 |
| LCD DC (RS) | **GPIO0** | 普通输出 | 【实测】 |
| LCD RST | **GPIO2** | 普通输出 | 【实测】 |
| LCD **CS (片选)** | **GPIO12** | SPI2 FSPICS0。**关键**：DIO 模式下 GPIO12(SPIHD) 空闲，被复用作 LCD 片选。缺它则面板忽略所有 SPI → 全黑 | 【实测·实机验证】 |
| **电源自锁 PWR_KEEP** | **GPIO13** | 输出（DIO 模式下 GPIO13/SPIWP 空闲）。**拉高=维持整机供电；拉低=立即整机断电**（USB/电池皆然，即软关机）。固件须在开机后尽快拉高 | 【实机验证】 |
| LCD 背光 | **GPIO5** | LEDC PWM 调光，输出信号 45（LEDC ch0），非反相 | 【实测】 |
| 按键（物理最左） | **GPIO8** | 输入，上拉，低有效 | 【实机确认】 |
| 按键（物理中间, BOOT） | **GPIO9** | 输入，上拉，低有效 | 【实机确认】 |
| 按键（物理最右） | **GPIO7** | 输入，上拉，低有效 | 【实机确认】 |
| VB6824 UART TX（C3→VB） | **GPIO20** | UART1 TX，输出信号 9（U1TXD） | 【实测】 |
| VB6824 UART RX（VB→C3） | **GPIO10** | UART1 RX，输入信号 9（U1RXD） | 【实测】 |
| 电池电压检测 | **GPIO4** | ADC1 通道 4（12dB 衰减），板载分压比 ≈ **6.08**（引脚 687mV ↔ 电池 4.18V 实测校准） | 【实测·已校准】 |
| USB/VBUS 插入检测 | **GPIO21** | 输入。插 USB = 主动驱动为高，拔掉 = 主动驱动为低（VBUS 分压，非悬空、非充电状态脚） | 【实机验证】 |
| USB D-/D+ | GPIO18 / GPIO19 | USB-Serial-JTAG（USB-C） | 【实测】 |
| SPI Flash | GPIO14–17 | 板载 8MB Flash（DIO：CLK/CS/D/Q）。GPIO12/13 空闲，分别复用为 LCD 片选、电源自锁 | 【实测】 |

> **注意**
> - **LCD CS 在 GPIO12**（不是 NC！）。这是 DIO Flash 模式下空闲的 SPIHD 脚被复用作片选。最初照搬 DOIT 参考板设成 CS=NC，导致面板不被片选、整屏全黑——这是本次调试最大的坑，已实机验证修正。
> - **GPIO13 是整机电源自锁，不是 LCD 使能**（实验修正了早期误判）。开机方式：插入 USB，或关机状态下**长按中键(GPIO9)约 2 秒**——按住期间硬件强制供电，固件必须在此窗口内拉高 GPIO13 完成自锁（demo 放在 `app_main` 第一行），否则松手即断电。出厂固件的"自动关机"功能就是拉低此脚。
> - 三个物理按键排列（左→右）= **GPIO8, GPIO9, GPIO7**（已实机确认）。GPIO9 是 C3 的 BOOT 引脚，同时兼作**电源键**（关机后长按 2 秒开机）。
> - DC=GPIO0 / RST=GPIO2 已实机验证。
> - 电池分压比已按实测校准为 **6.08**（引脚 687mV 对应电池 4.18V）；换算见 `demo/main/board_config.h` 的 `BAT_DIVIDER`。
> - GPIO21 只反映 **USB 是否插入**，无法区分"充电中/已充满"（板上未发现引到 GPIO 的 CHRG 状态脚）。

## 3. 关键架构：音频不走 I2S

**麦克风与扬声器都不直接连 ESP32-C3，而是全部挂在 VB6824 上。** VB6824 是离线语音前端芯片，
负责麦克风采集、扬声器 DAC 输出、硬件 Opus 编解码、离线唤醒、降噪、AEC。它与 C3 之间只用一路
UART（UART1，波特率 **2,000,000**）通信。

信号链：
```
说话 → 麦克风 → VB6824(降噪/Opus编码) → UART(GPIO10) → C3 → (联网/处理)
播放 → C3 → UART(GPIO20) → VB6824(PCM→DAC) → 8002 功放 → 扬声器 / 3.5mm 耳机口
```

**音频数据格式**（出厂 VB6824 固件，与小智协议一致）：
- 麦克风输入（C3 读到的）：**Opus 16kHz / 20ms / 单声道**（每帧约 40 字节）
- 扬声器输出（C3 写出的）：**PCM 16kHz / 单声道 / int16**

因此：
- 播放（扬声器/音量）只需写 PCM，简单；
- "录音后回放"需要先把麦克风的 Opus 解码成 PCM 再写给扬声器（demo 用 `78/esp-opus` 解码实现）。

C3 上没有任何直连的 I2S 麦克风/喇叭，**任何自研固件的音频部分都必须通过 VB6824 的 UART 库**
（闭源 `.a`，源见 `demo/components/vb6824`，来自 `github.com/SmartArduino/DOIT_AI`）。

## 4. 分区表（8MB Flash，出厂）

| 分区 | 偏移 | 大小 | 用途 |
|---|---|---|---|
| nvs | 0x9000 | 16KB | 配置/Wi-Fi |
| otadata | 0xd000 | 8KB | OTA 状态 |
| phy_init | 0xf000 | 4KB | RF 校准 |
| model | 0x10000 | 960KB | VB6824 离线唤醒模型 |
| ota_0 | 0x100000 | 3.5MB | app 槽 A |
| ota_1 | 0x480000 | 3.5MB | app 槽 B（出厂运行槽） |


## 5. 软件/开发环境与注意事项

- 出厂固件：ESP-IDF v5.4；`demo/` 使用 ESP-IDF v6.0.1（已验证编译、链接、实机运行）。
- **控制台必须走 USB-Serial-JTAG**（GPIO18/19，即 USB-C 口）：UART0 默认引脚 GPIO20/21 已被 VB6824 占用。
  `sdkconfig` 需 `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`。
- **LCD CS 用 GPIO12 时**，`spi_common` 会打印一条
  `GPIO 12 is conflict with others and be overwritten` 警告——因为它名义上属于 Flash 引脚组，
  实际 DIO 模式下空闲，可忽略。
- **主任务栈 ≥ 12KB**（`CONFIG_ESP_MAIN_TASK_STACK_SIZE=12288`）：`opus_decode` 栈占用大，
  6KB 栈会溢出导致复位（已实机踩坑）。
- **VB6824 组件**：IDF 6.0 下 `REQUIRES` 需拆分为 esp_driver_gpio/esp_driver_uart/esp_ringbuf 等
  （老的整体 `driver`/`json` 组件名已失效）；预编译库用 `libs/idf5.5/libesp32c3.a`，闭源但可正常链接。
- **麦克风为常态推流**：开启输入后 VB6824 持续输出 Opus 帧，无需先说唤醒词。
- 显示建议直接用 `esp_lcd` + `espressif/esp_lvgl_port` + LVGL（demo 即此方案）。



# 6. 硬件 LCD 与 按键的位置关系

```
   left   mid   right 
   +---+ +---+  +---+ 
  ++---+-+---+--+---++
  | +---------------+|
  | |               ||
  | |               ||
  | |   LCD         ||
  | |               ||
  | |               ||
  | |               ||
  | |               ||
  | +---------------+|
  +------------------+
```

