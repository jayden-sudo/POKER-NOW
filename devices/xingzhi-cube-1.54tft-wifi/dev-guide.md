# xingzhi-cube-1.54tft-wifi 德州撲克遊戲機 軟體開發指南 v1.0

> 本指南是四種裝置中的**第三份**開發指南。全專案共用佈局由 `../StickS3/dev-guide.md`
> (下稱「S3 指南」)§1–§5 定義;共用核心 `common/components/poker_core/` **介面凍結,
> 且已經 StickS3(xtensa)與 zuowei-c3(riscv32)兩機建置與互通驗證**,
> R13 堆疊溢位已在核心修復(S3 §21.3 R13 / zuowei §16.4)——
> **本指南只覆蓋 xingzhi-cube 的板級/HAL 實作與整合**,不重新設計、不修改任何共用部分。
>
> 規格權威層級(衝突時由高到低):
> 1. `../../common/PROTOCOL.md` v1.1 —— 協定層唯一權威
> 2. `../../common/PRODUCT-SPEC.md` v1.2 —— 架構/HAL/畫面/語音唯一權威(v1.2 含電量/亮度/音量 UX)
> 3. **磁碟上的 `common/components/poker_core/include/**` 標頭 = 凍結介面實體**;
>    與任何文件(含 S3 指南 §3 引文)不符時**以磁碟為準**(差異清單見 zuowei 指南 §1.2,原樣適用)
> 4. `xingzhi-cube-1.54tft-wifi/hardware-reference.md` + `README.md` —— 本板硬體唯一權威
>    (標【实测】者已 JTAG/實機驗證;與官方 xiaozhi 倉庫 config.h 衝突以【实测】為準)
> 5. S3 指南 §4 裁定(R1–R9)、§21 回寫(R10–R13)與 zuowei 指南 §16 回寫 —— **必遵**
> 6. 本指南 —— 只補「xingzhi 實作者會卡住的地方」,引用以「協定 §x」「產品 §x」「S3 §x」「zuowei §x」表示。

---

## 0. 讀者與範圍

- 讀者:負責實作 `xingzhi-cube-1.54tft-wifi/` 的 coding agent / 韌體工程師,
  假定已通讀協定/產品文件、S3 指南(尤其 §1–§5、§21)與 zuowei 指南 §16。
- 產出物:**只有** `xingzhi-cube-1.54tft-wifi/` 一個 IDF 專案(main/ = HAL 實作 + 板級)。
  `common/` 一個位元組都不改;若實作中發現非改不可,先停下,走 S3 §21 回寫升版流程。
- 本板四大特徵(相對前兩機):
  1. **與 StickS3 同為 ESP32-S3 雙核 + xtensa 工具鏈 + I2S 播放** —— 任務模型(§3)與
     `hal_audio.c`(§6)可近乎原樣照抄 StickS3,本指南主體是「差異清單」而非重寫;
  2. **無 codec 晶片**:喇叭 = S3 的 I2S1 直推功放(無 ES8311/無 I2C/無 MCLK),
     demo README 記載的「TX 空閒嘟嘟聲」由產品規範的常開靜音填充**天然消除**(§6.3);
  3. **電池走 ADC2_CH6(GPIO17),與 Wi-Fi 衝突** —— 開機先讀基準、Wi-Fi 後吃快取(§8),
     是本板唯一的新難點;
  4. **第 4 顆鍵是純硬體電源鍵**(不接 GPIO,韌體不可見)→ 關機不可攔截,
     `hal_power` 同 StickS3 為空殼(§10),協定 §9.5 接管兜底。

---

## 1. 前置必讀與凍結邊界

### 1.1 凍結介面清單(只准實作,不准增改)

本裝置的**全部**共用介面對接工作 = 提供下列六個 HAL 標頭的符號實作(標頭原文以磁碟為準):

| 標頭 | 本機須實作的符號 | 落地檔 |
|---|---|---|
| `hal/hal_display.h` | `hal_display_init/render/render_showdown/set_brightness` | `main/hal_display.c`(§7) |
| `hal/hal_input.h` | `hal_input_init`、`hal_input_set_incdec_cb`(中鍵 -1,**真實作**)、`hal_input_get_number`(回 -1) | `main/hal_input.c`(§9) |
| `hal/hal_audio.h` | `hal_audio_init/play/play_at/stop/set_volume/path_latency_ms` | `main/hal_audio.c`(§6) |
| `hal/hal_battery.h` | `hal_battery_pct/charging` | `main/hal_battery.c`(§8) |
| `hal/hal_power.h` | `app_prepare_poweroff`(R11:main 提供單一定義;本機同 StickS3 為**空殼**) | `main/hal_power.c`(§10) |
| `hal/hal_misc.h` | `hal_rand/yield_watchdog/settings_load/settings_save_*` + 弱符號覆寫 `pk_board_device_class`→**2** | `main/hal_misc.c`(§10) |

入口只呼叫共用碼的 `app_flow_start(name)`(內部 game_init→pbus_start)。本裝置 main
**不直接觸碰**任何其他共用 API;唯一例外是 §6 的 `voice_open/read/close`(HAL 拉流,產品 §2.3 授權)。

### 1.2 磁碟標頭 vs 文件的差異

zuowei 指南 §1.2 的六條核對結果**原樣適用**(磁碟未再變動),不重抄。與 render 直接相關的兩條重申:

1. `hal_screen_t` 含 `battery_pct` 欄(0–100;0xFF=USB)與 `title_flags`(TF_* 位)——
   §7.3 的 render 必須處理(值由共用 app_flow 填,HAL 只管畫)。
2. `pk_config.h` 已含 `PK_GESTURE_*`、`PK_VOL_DEFAULT=70`、`PK_BRI_DEFAULT=80` 等全量常數,
   板級**不得**重複定義;§9 的中鍵連發直接引用 `PK_GESTURE_LONG_MS/REPEAT_MS`。

### 1.3 前兩機經驗回寫中對本機有直接效力的條目

- **S3 §21.2-1 WHOLE_ARCHIVE**:main 元件必須加 `WHOLE_ARCHIVE`(poker_core↔main 循環引用),
  §2.3 的 CMakeLists 已含,四機共通,一律照加。
- **S3 §21.3 R10**:音量/亮度 NVS 持久化(namespace `poker`,key `vol`/`bri`),
  `hal_misc.c` 照抄 StickS3 即得(§10)。
- **S3 §21.3 R11**:`app_prepare_poweroff()` 由 main 提供單一定義;本機硬體電源鍵不可攔截
  → 空殼 + 註記(§10),與 StickS3 同款。
- **S3 §21.3 R12**:`pk_board_device_class()` 強符號覆寫,**xingzhi = 2**(協定 §8.1)。
- **S3 §21.3 R13(已修復)**:`pbus_session.c` 的 2-byte 堆疊溢位已由主 agent 修入核心,
  StickS3/zuowei 重建皆綠。**本機直接使用修復後核心,嚴禁照抄 zuowei §16.1-1 的
  `-Wno-error=stringop-overflow` 降級**(那是修復前的臨時墊片,已從 zuowei 專案移除)。
- **S3 §21.2-4 `-Werror` 四坑**(註解內 `*/`、struct 回傳加 const、一行兩敘述、顯式 include)照避;
  **zuowei §16.1-4**(BAT_FULL/EMPTY 用整數宏走整數運算)、**§16.1-2**(IDF6 的 LEDC 反相是
  `.flags.output_invert`)、**§16.1-5**(hal_display.c 顯式 `<string.h>+<stdio.h>`)本板同樣適用。
- **工具鏈風險評估**:本機 = StickS3 同款 xtensa esp-15.2.0 —— 凍結核心已在**完全相同**的
  工具鏈上編譯通過,§21.2-7「新工具鏈爆新 -Werror」的風險對本機趨近於零;首建置仍照規矩
  把 core 當新代碼看待,爆錯即停、報主 agent。
- **S3 §21.5 記憶體提醒**對本機不構成約束:S3 512KB SRAM + 8MB PSRAM,StickS3 同款佈局
  實測餘裕充足,照抄即可,不需 zuowei §12 式的逐項立帳。

---

## 2. 專案佈局與建置系統

### 2.1 目錄佈局

```
xingzhi-cube-1.54tft-wifi/
├─ poker/                        # ★ 本指南產出物(全新 IDF 專案)
│  ├─ CMakeLists.txt
│  ├─ sdkconfig.defaults         # §12.1 全文
│  ├─ partitions.csv             # §12.2 全文
│  └─ main/
│     ├─ CMakeLists.txt  idf_component.yml
│     ├─ board_config.h          # 全部引腳/板級常數(唯一放 GPIO 的地方,§4.1)
│     ├─ app_main.c              # §4.2(power_latch_on + 電池基準讀取次序)
│     ├─ hal_display.c           # ← 三源拼裝:demo display.c/backlight.c + zuowei UI 段(§7)
│     ├─ hal_input.c             # ← StickS3 hal_input.c 骨架 + 中鍵 incdec(§9)
│     ├─ hal_audio.c             # ← StickS3 hal_audio.c ≈95% 原樣抄(§6)
│     ├─ hal_battery.c           # ← demo battery.c 改造(ADC2 基準+快取,§8)
│     ├─ hal_battery_board.h     # 板內私有:hal_battery_boot_baseline()
│     ├─ hal_power.c             # ← StickS3 hal_power.c 原樣(空殼,§10)
│     └─ hal_misc.c              # ← StickS3 hal_misc.c 照抄,device_class 改 2(§10)
└─ main/                         # 既有 demo,保留不動(硬體回歸測試對照組)
```

**注意**:poker/ 與 demo(倉庫根 main/)是兩個獨立 IDF 專案。demo 模組是「拷貝改造」,
不跨專案 include —— demo 保持可燒可跑。

### 2.2 `poker/CMakeLists.txt`(全文)

```cmake
cmake_minimum_required(VERSION 3.16)

# 引用全專案共用元件(poker_core);本板無自有 components/
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../common/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(poker_xingzhi_cube)
```

**不要**照抄 zuowei 頂層 CMakeLists 裡曾出現的 `idf_build_set_property(COMPILE_OPTIONS
"-Wno-error=stringop-overflow" APPEND)`—— R13 已修,該行已廢(§1.3)。

### 2.3 `poker/main/CMakeLists.txt`(全文)

```cmake
idf_component_register(
    SRCS
        "app_main.c"
        "hal_display.c" "hal_input.c" "hal_audio.c"
        "hal_battery.c" "hal_power.c" "hal_misc.c"
    INCLUDE_DIRS "."
    REQUIRES
        poker_core
        esp_lcd esp_lvgl_port lvgl
        esp_driver_spi esp_driver_i2s
        esp_driver_gpio esp_driver_ledc
        esp_adc
        esp_timer esp_partition spi_flash
        esp_wifi nvs_flash
    WHOLE_ARCHIVE      # S3 §21.2-1:HAL 實作在 main,被 poker_core 反向引用;
                       # 不加則 hal_battery_pct/hal_rand/hal_settings_*/pk_board_device_class
                       # 全部 undefined reference(四機共通,一律照加)
)
```

與 StickS3 的差異:去 `esp_driver_i2c`(本板無任何 I2C 裝置),加 `esp_adc`(電池 ADC2 oneshot);
`esp_driver_i2s` 保留(喇叭 I2S1)。`rtc_gpio_*`(§4.2 電源自鎖)由 `esp_driver_gpio` 提供,無需另列。

### 2.4 `poker/main/idf_component.yml`(全文)

```yaml
dependencies:
  idf: ">=6.0"
  espressif/esp_lvgl_port: "^2"
```

與 demo 的依賴差異:**沒有 `78/esp-opus`**(撲克語音是 IMA-ADPCM,共用 `voice_adpcm.c` 解碼;
無對講、無麥克風、無 Opus)——這是 §12.1「主棧 8KB」結論的前提。
也沒有 `espressif/es8311`:本板無 codec 晶片。

### 2.5 工具鏈(xtensa,與 StickS3 完全相同)

```bash
. ~/.espressif/v6.0.1/esp-idf/export.sh
# 本機 export.sh 不把工具鏈加入 PATH(demo README 已驗證),S3 = xtensa:
export PATH="$HOME/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin:$PATH"

cd devices/xingzhi-cube-1.54tft-wifi
idf.py set-target esp32s3          # 僅首次
```

---

## 3. 任務模型(= S3 指南 §5 原表,零改動)

本板與 StickS3 同為 ESP32-S3 雙核,**S3 指南 §5 的任務表(含核綁定、優先級、棧深)
原封照用**,不重抄。逐項對應:

- `audio`(core1, prio 15, 棧 4096)= 本板 `hal_audio.c` 的 I2S 填充 task(§6.2);
- `LVGL`(core1, prio 4, 棧 8192, affinity=1)= `hal_display.c` 的 lvgl_port 配置(§7.1);
- `buttons`(core0, prio 6, 棧 2048)= `hal_input.c`(§9);
- `pbus`(core0, prio 12)/ `ui`(main task, prio 5)由共用碼建立,約束
  pbus(12)<wifi(23)、pbus(12)≤audio(15) 自動成立。

本板沒有 zuowei 的 VB6824 外掛任務,也沒有優先級倒掛問題 —— I2S 由 DMA 自走,
`i2s_channel_write` 阻塞節拍即出聲節拍,與 StickS3 完全同構。

---

## 4. board_config.h 與 app_main

### 4.1 `main/board_config.h`(全文)

引腳全部出自 `hardware-reference.md`(JTAG【实测】)與 demo `board_config.h`(實機驗證),照抄不改值;
撲克新增項集中在檔尾。demo 的 PDM 麥克風段**整段刪除**(撲克無麥克風需求,§6.1)。

```c
// xingzhi-cube-1.54tft-wifi 板級配置 —— 唯一允許出現 GPIO 編號的檔案(權威:hardware-reference.md)
#pragma once
#include "driver/gpio.h"
#include "hal/adc_types.h"
#include "driver/i2s_common.h"

// ---- LCD (ST7789, 1.54" 240x240, SPI3) ----【demo 实测原值,一個不改】
#define LCD_SPI_HOST        SPI3_HOST      // ★ SPI3(StickS3 同名巨集但腳位全不同)
#define LCD_PIN_MOSI        GPIO_NUM_10    // SPI3_D   【实测】
#define LCD_PIN_SCLK        GPIO_NUM_9     // SPI3_CLK 【实测】
#define LCD_PIN_CS          GPIO_NUM_14    // SPI3_CS0 【实测】
#define LCD_PIN_DC          GPIO_NUM_8     // 【实测】
#define LCD_PIN_RST         GPIO_NUM_18    // 【实测】
#define LCD_H_RES           240
#define LCD_V_RES           240
#define LCD_PIXEL_CLOCK_HZ  (80 * 1000 * 1000)   // 復刻出廠 80MHz(demo 驗證)
#define LCD_SPI_MODE        3              // ★ mode 3(StickS3 是 0,勿照搬)
#define LCD_INVERT_COLOR    true
#define LCD_MIRROR_X        false
#define LCD_MIRROR_Y        false
#define LCD_SWAP_XY         false          // 240×240 直用,無旋轉
#define LCD_OFFSET_X        0
#define LCD_OFFSET_Y        0

// ---- 背光 (LEDC PWM ch0) ----
#define LCD_PIN_BL          GPIO_NUM_13    // 【实测】非反相
#define BL_OUTPUT_INVERT    false

// ---- 按鍵(輸入上拉低有效;物理由上到下 = GPIO39/40/0)----
// 第 4 顆是專用硬體電源鍵(不接 GPIO,韌體不可見):短按開機、長按≈2s 關機。
#define BTN_OK_PIN          GPIO_NUM_39    // 上鍵 → KEY_OK(產品 §2.1 映射表)
#define BTN_NEXT_PIN        GPIO_NUM_0     // 下鍵 → KEY_NEXT(BOOT 腳:開機勿按住,§9)
#define BTN_INCDEC_PIN      GPIO_NUM_40    // 中鍵 → incdec(-1) 加分捷徑,不進共用層(§9)

// ---- 電池 / 電源 ----
#define BAT_ADC_UNIT        ADC_UNIT_2     // ★ ADC2 與 Wi-Fi 衝突:開機先讀基準(§8)
#define BAT_ADC_CHANNEL     ADC_CHANNEL_6  // = GPIO17【实测】
#define BAT_ADC_ATTEN       ADC_ATTEN_DB_12
#define BAT_DIVIDER         1.90f          // 【实测校准】萬用表實測電池 4.20V
#define BAT_FULL_MV         4200           // 整數(zuowei §16.1-4:走整數百分比運算)
#define BAT_EMPTY_MV        3300
#define CHARGE_DETECT_PIN   GPIO_NUM_38    // 高=充電中【实测】
#define PWR_KEEP_PIN        GPIO_NUM_21    // RTC-GPIO 電源自鎖:開機後拉高維持供電;
                                           // 關機由硬體電源鍵,韌體不做軟關機(hardware-reference.md §2)

// ---- 音訊(純 I2S 功放直推,無 codec 晶片;板上 PDM 麥克風本專案不用)----
#define AUDIO_SPK_I2S_PORT  I2S_NUM_1      // 喇叭走 I2S1【实测】(I2S0 留給 PDM,不初始化)
#define AUDIO_SPK_BCLK      GPIO_NUM_15
#define AUDIO_SPK_WS        GPIO_NUM_16
#define AUDIO_SPK_DOUT      GPIO_NUM_7
#define AUDIO_SAMPLE_RATE   16000          // 全產品統一 16k(§6.1)
#define AUDIO_VOL_DEFAULT   70
#define AUDIO_PATH_LATENCY_MS 60           // 初始估算:DMA 4×240=60ms + 功放≈0(無 codec 群延遲);
                                           // §6.4 實測後回填,並記 §15
```

### 4.2 `main/app_main.c`(全文)

= StickS3 `app_main.c` 去掉 I2C/M5PM1 三行,換上兩個本板前置:`power_latch_on()`
(demo main.c 原樣遷入)與 `hal_battery_boot_baseline()`(§8,**必須在 app_flow_start 之前**,
因為 Wi-Fi 在 `app_flow_start → game_init → pbus_start` 內才啟動)。

```c
/* app_main.c -- xingzhi-cube 德州撲克進入點(指南 §4.2)。 */
#include "board_config.h"
#include "hal_battery_board.h"     /* hal_battery_boot_baseline 的私有宣告(§8) */
#include "hal/hal_display.h"
#include "hal/hal_audio.h"
#include "app_flow.h"
#include "driver/rtc_io.h"
#include "esp_mac.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "app_main";

/* demo main.c 原樣遷入【实测】:GPIO21 是 RTC 腳,照出廠固件用 rtc_gpio_* 拉高自鎖。
 * 本板真正開關機由第 4 顆硬體電源鍵完成;此腳僅「開機後維持供電」(電池供電時需要,
 * 保守保留),拉低它並不能可靠關機(插 USB 時 5V 旁路自鎖,hardware-reference.md §2)。 */
static void power_latch_on(void)
{
    rtc_gpio_init(PWR_KEEP_PIN);
    rtc_gpio_set_direction(PWR_KEEP_PIN, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_hold_dis(PWR_KEEP_PIN);
    rtc_gpio_set_level(PWR_KEEP_PIN, 1);
}

void app_main(void)
{
    power_latch_on();                       /* 第一行(電池供電兜底;demo 同序) */

    ESP_ERROR_CHECK(hal_display_init());    /* §7 */
    ESP_ERROR_CHECK(hal_audio_init());      /* §6(含 voice 分區 mmap → voice_init) */
    hal_battery_boot_baseline();            /* ★ §8:Wi-Fi 啟動前讀 ADC2 基準,次序硬性 */

    char name[8];
    uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(name, sizeof(name), "XZ-%02X%02X", mac[4], mac[5]);
#ifdef PK_DEBUG_SOLO
    ESP_LOGW(TAG, "[SOLO] smoke build");
#endif
    app_flow_start(name);                   /* 內部 game_init→pbus_start(Wi-Fi 在此才啟) */
    ESP_LOGI(TAG, "boot complete: %s", name);
}
```

啟動預算:上電→SCANNING ≤3s(產品 §8.2)。本板 Octal PSRAM 初始化 + 80MHz LCD,
demo 實測遠低於此限;唯一新增項 `hal_battery_boot_baseline()` 的 16 次採樣 <5ms。

---

## 5. 複用計畫(差異驅動,逐檔裁定)—— 本指南的主索引

本板的 HAL 有**兩個母本**:StickS3 `poker/main/`(同 S3+I2S,音訊/顯示**結構**最接近,大量可抄)
與本板 demo `main/`(引腳與外設**參數**的實機權威)。原則:**結構抄 StickS3,參數抄 demo**。

| poker/main 檔 | 來源 | 改點(完整清單) |
|---|---|---|
| `board_config.h` | demo `board_config.h` 拷貝+擴充 | 引腳值原封;**刪 PDM 麥克風段**;按鍵宏改語義名(OK/NEXT/INCDEC);`BAT_FULL/EMPTY_MV` 改整數;加 `AUDIO_VOL_DEFAULT`、`AUDIO_PATH_LATENCY_MS`(§4.1) |
| `app_main.c` | StickS3 `app_main.c` | 去 `board_i2c_init/m5pm1_*` 三行;加 `power_latch_on()`(demo main.c 遷入)與 `hal_battery_boot_baseline()`;名字前綴 `XZ-`(§4.2) |
| `hal_audio.c` | **StickS3 `hal_audio.c` ≈95% 原樣照抄** | ①去 `board_i2c.h/es8311_min.h/m5pm1.h` 三個 include 與 `es8311_min_init/es8311_min_set_volume/m5pm1_amp_set` 三段呼叫(無 codec/無功放控制腳);②I2S 埠 `I2S_NUM_0`→`AUDIO_SPK_I2S_PORT`(I2S1);③`gpio_cfg` 無 MCLK(`I2S_GPIO_UNUSED`),clk_cfg 不需 `mclk_multiple`;④`effective_volume()` 去電池鉗(直接回 `s_vol`)。其餘(佇列/排程/樣本級切入/常開填充/audio task 逐行)**一字不改**(§6.2) |
| `hal_display.c` | **三源拼裝**:init 段=demo `display.c`;背光=demo `backlight.c`;UI/render 段=zuowei `poker/main/hal_display.c` | init 參數全走 §4.1 宏(mode 3/80MHz/gap(0,0)/無旋轉);lvgl_port `task_affinity=1`(S3 雙核,照 StickS3,**非** zuowei 的 -1);緩衝 240×40 雙緩衝(demo 同款);背光腳 GPIO13 + `.flags.output_invert`;UI 物件/座標/render 邏輯照 zuowei(同 240×240 佈局)含 `battery_pct` 後綴(§7) |
| `hal_input.c` | StickS3 `hal_input.c` 骨架 | 引腳換 39/0;**新增中鍵 GPIO40 → `incdec(-1)`**(真實作 `hal_input_set_incdec_cb`,取代 StickS3 `imu_bmi270.c` 的空殼職責;含 600/120ms 連發)(§9) |
| `hal_battery.c` | demo `battery.c` 改造 | 對外改為凍結兩函式;**保留 ADC2 失敗→快取邏輯**;新增 `hal_battery_boot_baseline()`(私有標頭);充電中(GPIO38 高)回 0xFF;1s 節流(§8) |
| `hal_battery_board.h` | 新寫(3 行) | 板內私有宣告,仿 zuowei `hal_power_board.h` 慣例(§8) |
| `hal_power.c` | StickS3 `hal_power.c` **原樣** | 只改註解:PM1 雙擊 → 硬體電源鍵(§10) |
| `hal_misc.c` | StickS3 `hal_misc.c` **照抄** | 唯一改動:`pk_board_device_class()` 回 **2**(§10) |
| 不複用 | demo `audio.c` 的 PDM/錄放/beep、`intercom.[ch]`、`main.c` 的分頁 UI | 撲克無麥克風;ESP-NOW 由共用 `pbus_transport.c` 全量覆蓋(其本身就照 demo intercom 模式寫成,S3 §17);LVGL UI 由共用 app_flow 取代 |

demo 唯二被**邏輯級**繼承(不只參數)的東西:`battery.c` 的「開機基準+快取回退」(§8)
與 `main.c` 的 `power_latch_on()`(§4.2)。其他一切結構性代碼以 StickS3 為母本。

---

## 6. 音訊 HAL(I2S1 直推功放)

### 6.1 與 demo / StickS3 的三個關鍵差異

1. **取樣率 16kHz 定案**。原廠小智固件的喇叭跑 24kHz(hardware-reference.md §3);
   本倉 demo 已改 16k 並實機驗證(demo `board_config.h` `AUDIO_SAMPLE_RATE 16000`)。
   撲克語音資產是 16k mono(voice.bin,S3 §19),**直接 I2S std 16k 配置,不做任何重採樣**
   ——四機一致,一行 `I2S_STD_CLK_DEFAULT_CONFIG(16000)` 了事。
2. **無 codec 晶片**:S3 的 I2S1 直推功放。沒有 ES8311(無 I2C 寫寄存器、無 MCLK 腳、
   無 codec 群延遲),也沒有 StickS3 的 AW8737 增益脈衝與電池音量鉗(那是 M5PM1 電源
   架構的硬體限制,本板無此問題)。音量 = **純軟體增益**(demo 驗證的 `apply_gain` 語義,
   與 StickS3 `apply_volume` 同式)。
3. **無麥克風需求**:板上 PDM 麥克風(I2S0)**完全不初始化** —— 不建 RX 通道、不佔 DMA、
   GPIO2/3 保持復位態。demo audio.c 的錄放/電平/對講接口全部丟棄。

### 6.2 `main/hal_audio.c`:StickS3 抄法(全文級 diff)

以 `devices/StickS3/main/hal_audio.c` 為底本**整檔拷貝**,
按下列 diff 修改;佇列/排程/切入/audio task 主迴圈**逐行保持**(那是兩機驗證過的凍結參考語義):

```c
/* 檔頭 include:刪三行,其餘不動 */
-#include "board_i2c.h"
-#include "es8311_min.h"
-#include "m5pm1.h"

/* effective_volume():無電池鉗(本板直推小功放,demo 100% 電池供電無重啟) */
 static uint8_t effective_volume(void)
 {
-    uint8_t v = s_vol;
-    if (!m5pm1_is_external_powered() && v > AUDIO_VOL_BATT_MAX) v = AUDIO_VOL_BATT_MAX;
-    return v;
+    return s_vol;
 }

/* hal_audio_init():I2S 配置換本板參數;刪 codec/功放段 */
 esp_err_t hal_audio_init(void)
 {
     s_q = xQueueCreate(16, sizeof(aq_item_t));

-    i2s_chan_config_t ch = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
+    i2s_chan_config_t ch = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_SPK_I2S_PORT, I2S_ROLE_MASTER);
     ch.dma_desc_num = 4; ch.dma_frame_num = 240;      /* 4×240 ≈ 60ms 環形深度(不改!
                                                        * AUDIO_PATH_LATENCY_MS=60 由此推導) */
     ESP_ERROR_CHECK(i2s_new_channel(&ch, &s_tx, NULL));
     i2s_std_config_t std = {
-        .clk_cfg = { .sample_rate_hz = AUDIO_SAMPLE_RATE,
-                     .clk_src = I2S_CLK_SRC_DEFAULT,
-                     .mclk_multiple = I2S_MCLK_MULTIPLE_256 },
+        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),   /* 16k;無 codec 不需 MCLK */
         .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
-        .gpio_cfg = { .mclk = I2S_PIN_MCLK, .bclk = I2S_PIN_BCLK, .ws = I2S_PIN_LRCK,
-                      .dout = I2S_PIN_DOUT, .din = I2S_GPIO_UNUSED,
-                      .invert_flags = { 0 } },
+        .gpio_cfg = { .mclk = I2S_GPIO_UNUSED, .bclk = AUDIO_SPK_BCLK, .ws = AUDIO_SPK_WS,
+                      .dout = AUDIO_SPK_DOUT, .din = I2S_GPIO_UNUSED,
+                      .invert_flags = { 0 } },
     };
     ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx, &std));
     ESP_ERROR_CHECK(i2s_channel_enable(s_tx));        /* ★ 常開,之後永不 disable(§6.3) */

-    es8311_min_init(board_i2c_bus(), AUDIO_SAMPLE_RATE * 256, AUDIO_SAMPLE_RATE);
-    es8311_min_set_volume(AUDIO_VOL_DEFAULT);
-    m5pm1_amp_set(2);
-    vTaskDelay(pdMS_TO_TICKS(25));
+    /* 本板無 codec/功放控制腳:功放常通電,靜音 = 全零樣本(§6.3) */

     voice_partition_load();                           /* 與 StickS3 逐行相同(subtype 0x40,"voice") */

     xTaskCreatePinnedToCore(audio_task, "audio", 4096, NULL, 15, NULL, 1);   /* §3 任務表 */
     ESP_LOGI(TAG, "audio ready (I2S1 direct, latency=%dms)", AUDIO_PATH_LATENCY_MS);
     return ESP_OK;
 }
```

語義要點(同 S3 §15.3,兩機驗證後第三次重申以防抄錯):`hal_audio_play_at` 的參數是
**出聲時刻**(`pbus_local_time_for(play_at)` 直傳);全路徑延遲已內建在 `s_head_ms`
初始化,**共用碼與 app_flow 不再扣 `hal_audio_path_latency_ms()`,扣兩次 = 提早出聲**。

### 6.3 「TX 空閒嘟嘟聲」與常開靜音填充:為什麼規範恰好治好本板

demo README 坑 3 記載:本板 I2S TX 通道使能後持續走時鐘,**不寫資料時 DMA 欠載
(underrun)→ 持續「嘟嘟」噪音**;demo 的對策是 `tx_start()/tx_stop()` 只在播放時使能。
產品 §2.3 規則 1 甚至為此留了「xingzhi 例外:可停通道但須實測 start 延遲並計入 latency」。

**本實作不走例外條款**,理由是因果分析:嘟嘟聲的來源是 *使能後斷供* 造成的 DMA 欠載
(硬體重複播舊描述符/未定義資料),不是時鐘本身。產品規則 1 的常開架構下,audio task
每 20ms 以阻塞 `i2s_channel_write` 灌入一幀(靜音或語音),**DMA 永不欠載 → 全零樣本
= 數位真靜音** —— 規範要求的常開填充恰好從根上消除 demo 的問題來源,而例外條款
(停通道 + 實測 start 延遲)反而會破壞 `play_at` 排程延遲的恆定性。兩者兼得,零代價。

殘餘風險:功放常通電時的**類比底噪**(全零輸入下的固有白噪)。demo 的 tx_stop 讓時鐘
停走、部分功放會進省電態,常開後這層「順帶靜音」消失。預期不可聞;若實測貼耳可聞,
**不得**退回 tx_stop 方案 —— 記錄實測分貝感受入 §15,並評估是否接受(它不影響任何驗收項)。

### 6.4 AUDIO_PATH_LATENCY_MS 量測(初值 60)

初值構成:DMA 環形深度 4×240 樣本 = 60ms(寫入的幀要排完環形佇列才出聲)+ 直推功放 ≈0ms
(無 codec 群延遲;對照 StickS3 62 = 60+2)。

量測 = S3 §15.5 錄音對拍法**原樣沿用**,以**已校準的 StickS3(62ms 基準)**作對端:
兩機燒 `-DPK_DEBUG_SOLO=1` 校準建置(Master 每 3s 發 `play_at=now+700ms` 的 E_REMIND
齊播 V_BEEP),手機錄音置中,Audacity 量 ≥10 對 beep 起始沿 Δt 取中位數,加到落後方;
回填 §4.1 並記 §15。通過線:與 S3 混測中位數 <30ms(產品 §9.2);同為 xtensa+I2S 阻塞寫
的同構路徑,預期 Δ≈±數 ms,若 >20ms 先查 `dma_desc_num/dma_frame_num` 是否被改動。

---

## 7. 顯示 HAL(hal_display.c)

### 7.1 init 段:demo `display.c` 參數 + StickS3 結構

`hal_display_init()` 結構照 StickS3(bl_init_off → SPI bus → panel_io → st7789 →
invert/swap/mirror/gap → lvgl_port → build_static_ui → 點亮背光),**參數全走 §4.1 宏**。
與兩個母本的參數對照(防「順手抄錯板」):

| 參數 | 本板值(demo 驗證) | StickS3(勿抄) | zuowei(勿抄) |
|---|---|---|---|
| SPI host / 腳位 | SPI3,MOSI=10/SCLK=9/CS=14/DC=8/RST=18 | SPI3,39/40/41/45/21 | SPI2,1/3/12/0/2 |
| `.spi_mode` | **3** | 0 | 3 |
| `.pclk_hz` | **80MHz** | 40MHz | 80MHz |
| invert / swap_xy / mirror | true / false / false,false | true / true / false,true | true / false / false,false |
| set_gap | **(0, 0)** | (40, 52) | (0, 0) |
| lvgl buffer | **240×40 雙緩衝(38.4KB,demo 同款)** | 240×40 雙緩衝 | 240×20 單緩衝 |
| lvgl_port cfg | priority=4、stack=8192、**affinity=1**(S3 雙核) | 同左 | 不設 affinity(C3 單核) |
| 背光 | GPIO13,LEDC ch0,5kHz/10-bit,`.flags.output_invert=false` | GPIO38 | GPIO5 |

背光:`bl_init_off()`(佔空 0 起步防上電雪花)照 StickS3,但 channel 配置**必須**帶
`.flags = { .output_invert = BL_OUTPUT_INVERT }`(zuowei §16.1-2;本板 false 不影響行為,
但巨集要接上);`hal_display_set_brightness()` 照 StickS3(10% 下限鉗 + 1023 線性佔空)。
init 完成後 `hal_display_set_brightness(80)`,NVS 值由共用碼稍後蓋上(R10)。

### 7.2 UI 段:zuowei `hal_display.c` 照抄(同一種 240×240 邏輯佈局)

產品 §2.2:全產品只有 240×240 與 240×135 兩種邏輯佈局。本板 = 240×240,與 zuowei 相同,
**`build_static_ui`、`card_text`、`set_or_hide`、`title_bg`、`hal_display_render`、
`hal_display_render_showdown` 整段照抄 `zuowei-c3-lcd-pwr/main/hal_display.c`**
(座標:標題 y=4/montserrat_20;自己 2 張 x=8,64/28 號字 y=36;公共 5 張 x=124+i×22/20 號字
y=44;big y=84 置中/28 號;lines[0..5] y=124+i×15/14 號;hint y=218 左綠右藍;
showdown 容器 name y=8、hole y=70 x=40,130、rank y=130、"+N" 金色 y=70 x=190、YOU 右上)。
記得顯式 `#include <string.h>` 與 `<stdio.h>`(zuowei §16.1-5)。

### 7.3 render 的 battery_pct(磁碟標頭欄位,§1.2)

照 zuowei §8.3 / StickS3 render 的 tbuf 寫法:標題文字後綴 ` 83%`,`battery_pct==0xFF`
時綴 ` USB`,斷線再綴 ` !LINK`。本板 `hal_battery_charging()` 有真值(GPIO38),
充電中共用層會在選單顯示 `CHG` —— HAL 只管照欄位畫,值由 app_flow 每 10s 填。

### 7.4 首次點亮自檢

本板無旋轉/偏移疑點(demo 已驗證 240×240 直向 (0,0)),自檢三件:
① 有畫面(黑屏 → 查 CS=14/DC=8 與 `spi_mode=3`);② 顏色正(發青/錯亂 → invert_color
或 `swap_bytes` 漏了);③ 邊框 1px 矩形四邊完整(缺邊 → offset,理論不會)。

---

## 8. 電池 HAL(hal_battery.c)—— 本板唯一新難點

### 8.1 問題與策略

GPIO17 = **ADC2**_CH6;S3 上 ADC2 與 Wi-Fi 共用 SAR 控制器,Wi-Fi 啟動後
`adc_oneshot_read` 大概率回 `ESP_ERR_TIMEOUT`(**返回錯誤,不是亂數** —— demo battery.c
已驗證)。撲克韌體 Wi-Fi 常開(WIFI_PS_NONE,ESP-NOW 常聽),所以:

1. **開機基準**:`hal_battery_boot_baseline()` 在 Wi-Fi 啟動前(app_main 內,
   `app_flow_start` 之前,§4.2)完整讀一次,存入快取 —— 這是 demo main.c
   「开 Wi-Fi 前先读一次电池」邏輯的直接複用;
2. **運行期**:每次 `hal_battery_pct()` 仍**嘗試**讀(Wi-Fi 空窗期偶爾會成功,白撿更新);
   16 次採樣全失敗 → 回快取。快取值不置 0、不置 0xFF —— 顯示凍結在最後成功值,
   這是本板的已知限制(記入驗收 §14 與 §15);
3. **充電中(GPIO38 高)回 0xFF**:充電時 ADC 讀的是充電電壓(4.2–4.3V,demo README 備註
   「充电中读数会略偏高」),非電池 OCV,回報必虛高 → 一律 0xFF(選單顯示 `BATT USB`,
   協定 §5「未知回 0xFF、嚴禁回 0」)。USB 供電但充飽(GPIO38 低)時讀的是浮充電壓
   ≈4.2V → 100%,可接受。

### 8.2 `main/hal_battery.c`(全文)

```c
/* hal_battery.c -- ADC2_CH6(GPIO17) + 充電檢測 GPIO38(指南 §8)。
 * ADC2 與 Wi-Fi 衝突:開機(Wi-Fi 前)讀基準,之後讀失敗回快取;充電中回 0xFF。 */
#include "hal/hal_battery.h"
#include "hal_battery_board.h"
#include "board_config.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "hal_battery";
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static bool s_cali_ok, s_inited;
static uint8_t s_cache = 0xFF;      /* 最後一次成功換算的 pct(Wi-Fi 佔用 ADC2 時回它) */
static int64_t s_next_us;

static void ensure_init(void)
{
    if (s_inited) return;
    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = BAT_ADC_UNIT };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_adc));
    adc_oneshot_chan_cfg_t ch_cfg = { .atten = BAT_ADC_ATTEN, .bitwidth = ADC_BITWIDTH_12 };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, BAT_ADC_CHANNEL, &ch_cfg));
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = BAT_ADC_UNIT, .chan = BAT_ADC_CHANNEL,
        .atten = BAT_ADC_ATTEN, .bitwidth = ADC_BITWIDTH_12,
    };
    s_cali_ok = (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) == ESP_OK);
#endif
    gpio_config_t io = { .pin_bit_mask = 1ULL << CHARGE_DETECT_PIN, .mode = GPIO_MODE_INPUT };
    gpio_config(&io);
    s_inited = true;
    ESP_LOGI(TAG, "battery ADC2_CH6 ready (cali=%d)", (int)s_cali_ok);
}

static int try_read_bat_mv(void)            /* 成功回電池 mV,失敗回 -1(demo battery.c 邏輯) */
{
    long acc = 0; int ok = 0, r = 0;
    for (int i = 0; i < 16; i++) {
        if (adc_oneshot_read(s_adc, BAT_ADC_CHANNEL, &r) == ESP_OK) { acc += r; ok++; }
    }
    if (ok == 0) return -1;                 /* Wi-Fi 佔用:ESP_ERR_TIMEOUT,非亂數 */
    int raw = (int)(acc / ok), mv;
    if (s_cali_ok) adc_cali_raw_to_voltage(s_cali, raw, &mv);
    else mv = raw * 3100 / 4095;            /* 12dB 近似(demo 同款兜底) */
    return (int)(mv * BAT_DIVIDER);         /* float 乘積顯式轉 int(zuowei §16.1-4) */
}

static uint8_t pct_from_mv(int mv)
{
    int pct = (mv - BAT_EMPTY_MV) * 100 / (BAT_FULL_MV - BAT_EMPTY_MV);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return (uint8_t)pct;
}

void hal_battery_boot_baseline(void)        /* app_main 於 app_flow_start(=Wi-Fi)前呼叫 */
{
    ensure_init();
    int mv = try_read_bat_mv();
    if (mv > 0) s_cache = pct_from_mv(mv);
    ESP_LOGI(TAG, "boot baseline: %d mV -> %u%%", mv, (unsigned)s_cache);
}

uint8_t hal_battery_pct(void)
{
    ensure_init();
    if (gpio_get_level(CHARGE_DETECT_PIN)) return 0xFF;   /* 充電中:讀數虛高 → 未知(§8.1-3) */
    int64_t now = esp_timer_get_time();
    if (now < s_next_us) return s_cache;                  /* 1s 節流(pbus 2s + app_flow 10s 都來要) */
    s_next_us = now + 1000000;
    int mv = try_read_bat_mv();
    if (mv > 0) s_cache = pct_from_mv(mv);                /* 失敗:保持快取(開機基準) */
    return s_cache;
}

bool hal_battery_charging(void)
{
    ensure_init();
    return gpio_get_level(CHARGE_DETECT_PIN) == 1;        /* 高=充電中【实测】 */
}
```

### 8.3 `main/hal_battery_board.h`(全文)

```c
/* hal_battery_board.h -- 板內私有(不進 hal/;仿 zuowei hal_power_board.h 慣例) */
#pragma once
void hal_battery_boot_baseline(void);   /* app_main 於 app_flow_start(=Wi-Fi 啟動)前呼叫 */
```

### 8.4 電量顯示 UX 對應(產品 §5.6 / R9+R10 全套自動獲得)

選單邏輯在共用 `app_flow.c`,本機零工作,只列本板實際呈現以供驗收比對:

| 供電狀態 | `hal_battery_pct()` | `hal_battery_charging()` | 標題列 / 系統選單首行 |
|---|---|---|---|
| 電池供電 | 0–100(Wi-Fi 佔用時=快取值) | false | ` 83%` / `BATT 83%` |
| USB 充電中 | 0xFF | **true** | ` USB` / `BATT USB` + `CHG` |
| USB 已充飽(GPIO38 低) | ≈100(浮充電壓) | false | ` 100%` / `BATT 100%` |

已知限制(驗收明文):電池供電 + Wi-Fi 常開時,百分比更新依賴 ADC2 空窗,**可能長時間
凍結在開機基準值**;低電 Master 豁免(協定 PN_BATT_MASTER_MIN=15)因此對本板遲鈍。
可接受 —— 出廠固件同款行為;實測空窗命中率記 §15。
線性 3300–4200 曲線沿 demo(粗糙但夠用);若驗收嫌跳變難看,換 S3 §13.4 OCV 折線,記 §15。

---

## 9. 輸入 HAL(hal_input.c)

映射(產品 §2.1 表,硬性):**上鍵 GPIO39 = KEY_OK,下鍵 GPIO0 = KEY_NEXT;
中鍵 GPIO40 = incdec(-1) 加分捷徑,不進共用層**。第 4 顆硬體電源鍵韌體不可見(§10)。

骨架 = StickS3 `hal_input.c`(10ms 輪詢、連續兩拍一致去抖、raw 直報;手勢 600/120ms
合成在共用 `input_gesture.c`,S3 裁定 R5)+ 中鍵段。與 StickS3 的差異:本板的 incdec
不再是 IMU 空殼(StickS3 `imu_bmi270.c`),而是**真實作** —— 數值畫面(CHIPS_SET/BLINDS/
AMOUNT_PICK/REBUY)按中鍵 -1,配合 NEXT 的 +1 形成雙向調值;非數值畫面共用碼不掛回呼,
中鍵自然無功能。

```c
/* hal_input.c -- 上 GPIO39=OK、下 GPIO0=NEXT raw 事件;中 GPIO40 → incdec(-1)(指南 §9)。 */
#include "hal/hal_input.h"
#include "board_config.h"
#include "pk_config.h"              /* PK_GESTURE_LONG_MS / PK_GESTURE_REPEAT_MS(顯式 include) */
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static hal_input_cb_t s_cb;
static hal_input_incdec_cb_t s_incdec;

static void btn_task(void *a)
{
    (void)a;
    const gpio_num_t pin[2] = { BTN_OK_PIN, BTN_NEXT_PIN };   /* KEY_OK / KEY_NEXT */
    bool stable[2] = { false, false }, last[2] = { false, false };
    bool mid_stable = false, mid_last = false;
    int  mid_down_ms = 0;
    for (;;) {
        for (int i = 0; i < 2; i++) {
            bool raw = gpio_get_level(pin[i]) == 0;
            if (raw == last[i] && raw != stable[i]) {          /* 連續兩拍一致才翻轉 = 去抖 */
                stable[i] = raw;
                if (s_cb) s_cb((hal_key_t)i, raw);
            }
            last[i] = raw;
        }
        /* 中鍵:HAL 內部消化,節奏對齊共用手勢(長按 600ms 後每 120ms 連發) */
        bool mraw = gpio_get_level(BTN_INCDEC_PIN) == 0;
        if (mraw == mid_last && mraw != mid_stable) {
            mid_stable = mraw;
            mid_down_ms = 0;
            if (mraw && s_incdec) s_incdec(-1);                /* 按下沿:-1 */
        }
        mid_last = mraw;
        if (mid_stable) {
            mid_down_ms += 10;
            if (mid_down_ms >= PK_GESTURE_LONG_MS) {
                mid_down_ms -= PK_GESTURE_REPEAT_MS;
                if (s_incdec) s_incdec(-1);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t hal_input_init(hal_input_cb_t cb)
{
    s_cb = cb;
    gpio_config_t io = {
        .pin_bit_mask = BIT64(BTN_OK_PIN) | BIT64(BTN_NEXT_PIN) | BIT64(BTN_INCDEC_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    xTaskCreatePinnedToCore(btn_task, "btns", 2048, NULL, 6, NULL, 0);   /* §3 任務表 */
    return ESP_OK;
}

void hal_input_set_incdec_cb(hal_input_incdec_cb_t cb) { s_incdec = cb; }
int  hal_input_get_number(void) { return -1; }   /* 無鍵盤 */
```

兩個必知:

1. **GPIO0 是 BOOT strap 腳**:上電/復位時按住下鍵會進下載模式 —— 運行期作輸入完全正常
   (上拉低有效,demo 實機驗證),但**開機瞬間勿按住下鍵**;驗收 §14 階段 0 含此提示。
   這與 zuowei 中鍵(GPIO9=BOOT)同性質,非缺陷。
2. incdec 是共用碼定義的**加速捷徑**(產品 §2.1):缺了不影響任何流程,共用碼只在數值
   畫面掛回呼。實作成本 ≈15 行,直接做;若首建置想極簡,`s_incdec(-1)` 兩處註釋掉即降
   為 StickS3 式空殼,不影響驗收主線(但 §14 階段 0-⑥ 隨之豁免)。

---

## 10. hal_power / hal_misc

### 10.1 `main/hal_power.c`(全文)

第 4 顆鍵是**專用硬體電源鍵**(不接 MCU,短按開機、長按≈2s 在硬體層直接斷電,
hardware-reference.md §2)——韌體看不到、攔不到,與 StickS3 PM1 雙擊同類(產品 §2.5 已列已知限制)。
`app_prepare_poweroff()` 依 R11 由 main 提供**單一空殼定義**,永不被板級呼叫;
Master 死亡由協定 §9.5 接管兜底(≤6s,驗收 §14 階段 3 必測)。

```c
/* hal_power.c -- 關機攔截(產品 §2.5;S3 §21.3 R11)。
 * xingzhi 關機 = 第 4 顆硬體電源鍵長按 ≈2s,硬體層直接斷電,韌體不可見、不可攔截;
 * 拉低 GPIO21 亦無法可靠關機(插 USB 時 5V 旁路自鎖,hardware-reference.md §2)。
 * app_prepare_poweroff() 永不被本板呼叫;協定 §9.5 故障接管兜底。空殼供介面完整。 */
#include "hal/hal_power.h"

void app_prepare_poweroff(void)
{
    /* no-op(同 StickS3 慣例;唯一真實作在 zuowei,其 GPIO13 自鎖可攔截) */
}
```

### 10.2 `main/hal_misc.c`

`StickS3/main/hal_misc.c` **整檔照抄**,只改一行:

```c
uint8_t pk_board_device_class(void) { return 2; }   /* 協定 §8.1:2 = xingzhi-cube */
```

其餘(`hal_rand`=esp_random、`hal_yield_watchdog`=vTaskDelay(1)、
`hal_settings_load/save_*` 走 NVS namespace `poker` key `vol`/`bri`)零改動 ——
R10 四機一致,本機自動獲得「BATT %/VOLUME 40-70-100/BRIGHTNESS 30-60-100 + 重開機保留」
全套(產品 §5.6/§9.16)。

---

## 11. PSRAM 使用方針(8MB Octal)

- `CONFIG_SPIRAM_MODE_OCT=y` 是**硬性**:本板 PSRAM 為內封 8MB octal(AP_3v3,
  esptool【实测】),配成 quad 開機即失敗。80MHz(demo 驗證值)。
- **保守方針(照 StickS3 慣例):一切既有緩衝留在內部 SRAM,PSRAM 只當餘裕**。逐項理由:
  - LVGL 緩衝(240×40×2B×2 ≈ 38.4KB,`buff_dma=true`):**可**放 PSRAM
    (esp_lvgl_port 支援 `buff_spiram`)但**無必要** —— S3 內部 512KB SRAM,StickS3 同款
    佈局實測餘裕充足;DMA 直達內部 RAM 也避免 PSRAM 帶寬與 cache 一致性開銷。不動。
  - voice.bin:flash mmap 零拷貝(S3 §19.3),與 RAM 無關。
  - `pbus_t`(≈13KB 事件日誌)、語音 4KB 解碼緩衝、各任務棧:全部靜態/內部,
    poker_core 凍結,無 PSRAM 介入點(任務棧本就不可放 PSRAM)。
- demo 唯一的 PSRAM 用戶是 4 秒錄音緩衝(128KB, `MALLOC_CAP_SPIRAM`)—— 撲克無錄音,
  此用法**不遷移**。
- 留給未來:若後續加大字型/圖片資產需要大堆,`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`
  隨取隨用;**不要**開 `CONFIG_SPIRAM_USE_MALLOC` 全域接管(避免不可 DMA 記憶體被驅動誤拿)。

---

## 12. sdkconfig.defaults 與 partitions.csv(全文)

### 12.1 `poker/sdkconfig.defaults`

對照 demo `sdkconfig.defaults` 逐項裁定:demo 未鎖 CPU 頻率(補 240MHz);
demo `PARTITION_TABLE_SINGLE_APP_LARGE` → 換自訂分區(容 voice);
**demo 的 12KB 主棧是給 `opus_decode` 的(對講),撲克無 opus → 8192 定案**
(與 StickS3/zuowei 相同;若日後任何人往本專案加回 opus,主棧必須同步回 12288 ——
zuowei §12 同款結論)。其餘(USB console、8MB QIO、Octal PSRAM、LVGL 字體、關 AMPDU)原封。

```
# --- 目標晶片:ESP32-S3(雙核 240MHz)---
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y

# --- 控制台走 USB-Serial-JTAG(USB-C,GPIO19/20;demo 同)---
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y

# --- Flash 8MB QIO(Winbond W25Q64,demo 已驗證 QIO;此處與 zuowei 的 DIO 不同,勿互抄)---
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="8MB"
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
# demo 用 SINGLE_APP_LARGE;poker 換自訂分區容 voice(§12.2)
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"

# --- 內置 8MB Octal PSRAM(硬性 OCT,§11)---
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y

# --- 主棧 & FreeRTOS(demo 12288 是給 opus 的;撲克無 opus → 8192,§12.1)---
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
CONFIG_FREERTOS_HZ=1000

# --- LVGL 字體(四機統一三檔:14/20/28)---
CONFIG_LV_FONT_MONTSERRAT_14=y
CONFIG_LV_FONT_MONTSERRAT_20=y
CONFIG_LV_FONT_MONTSERRAT_28=y

# --- Wi-Fi 僅用於 ESP-NOW:關 AMPDU 省內存(demo 已驗證)---
CONFIG_ESP_WIFI_AMPDU_TX_ENABLED=n
CONFIG_ESP_WIFI_AMPDU_RX_ENABLED=n
```

### 12.2 `poker/partitions.csv`(與 StickS3/zuowei 逐位元組一致)

```
# Name,     Type, SubType,  Offset,   Size,     Flags
nvs,        data, nvs,      0x9000,   0x6000,
phy_init,   data, phy,      0xf000,   0x1000,
factory,    app,  factory,  0x10000,  0x3E0000,
voice,      data, 0x40,     0x3F0000, 0x120000,
```

- `voice` 分區名與 subtype 0x40 **四機必須一致**(S3 §18.2;§6.2 的載入碼按此查找)。

---

## 13. 建置、燒錄、voice.bin、PK_DEBUG_SOLO

```bash
. ~/.espressif/v6.0.1/esp-idf/export.sh
export PATH="$HOME/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin:$PATH"

cd devices/xingzhi-cube-1.54tft-wifi
idf.py set-target esp32s3               # 僅首次
idf.py build                            # 正式建置
idf.py build -DPK_DEBUG_SOLO=1          # 單機冒煙建置(S3 §20.3;title 帶 [SOLO] 防呆,
                                        # 正式包必不帶:strings build/*.bin | grep -c PK_DEBUG_SOLO == 0)
idf.py -p <PORT> flash monitor

# voice.bin(四機共用同一份,common/voice/ 已生成入版控)只在語音資產變更時單獨燒:
python $IDF_PATH/components/partition_table/parttool.py -p <PORT> \
    write_partition --partition-name=voice --input ../../common/voice/voice.bin
# 或:esptool --port <PORT> write-flash 0x3F0000 ../../common/voice/voice.bin
```

host 端測試沿用 `common/tools`(S3 §20.2;共用碼凍結且 37 檢查全過,本裝置不需重跑,
除非動了共用碼 —— 而共用碼不准動)。

PK_DEBUG_SOLO 用途(S3 §20.3 原樣):單機驗 UI 全畫面矩陣、語音齊播路徑
(play_at 對自己也走排程)、本板三 HAL(顯示/音訊/電池快取)。

---

## 14. 分階段驗收(對照產品 §9;xingzhi 特有項加粗)

沿 S3 §20.4 四階段框架,每階段過了才進下一階段。通用項見 S3 §20.4,此處只列本板特有/加嚴:

| 階段 | 建置 | xingzhi 特有項 | 對應產品 §9 |
|---|---|---|---|
| 0 冒煙 | SOLO | **①靜音無嘟嘟聲**:開機後不播任何語音靜聽 ≥30s,喇叭應無 demo README 坑 3 的持續噪音(常開靜音填充驗證,§6.3);再播 V_BEEP/數字播報確認出聲正常。**②ADC2 電量快取顯示**:(a) 拔 USB 電池開機,串口確認 `boot baseline: … mV -> …%` 在 `pbus`/Wi-Fi 日誌**之前**出現;(b) 進 SCANNING(Wi-Fi 已開)後標題列 % 仍有值(=快取,不歸 0 不變 0xFF);(c) 插 USB(充電)→ 顯示 `USB`,選單 `BATT USB`+`CHG`。**③硬體電源鍵**:長按≈2s 斷電、短按再開機(韌體無感知;電池供電開機驗 `power_latch_on` 生效);**開機時勿按住下鍵(GPIO0=BOOT)**,誤入下載模式屬正常復位即可。**④亮度三檔**即時生效+重開機保留(R10)。**⑤VOLUME 三檔**即試播。**⑥中鍵 incdec**:CHIPS_SET 畫面按中鍵 -1、長按連發;非數值畫面中鍵無功能 | 16 |
| 1 成桌儀式 | 正式 ×2–3(**必含 1 台已校準 StickS3**) | **齊播延遲校準**(§6.4:錄音對拍 → 回填 `AUDIO_PATH_LATENCY_MS` → 復測混型中位數 <30ms,預期本板與 S3 同構路徑 Δ 極小) | 1、2、3 |
| 2 單街下注 | 正式 ×3 | 通用項;數值畫面用中鍵-1 + NEXT+1 雙向調 raise-to 順手度抽查 | 4–7 |
| 3 全流程 | 正式 ×3+(**首次三機全型號同桌:StickS3 + zuowei + xingzhi**) | **①三機齊播實測**:同桌齊播事件(confirm dealer / Dealing 等)手機錄音對拍,**三機兩兩錯位均 <30ms**(產品 §9.2 加嚴到三機矩陣;xtensa-I2S × riscv-UART 異構全覆蓋,這是全產品首次可測項)。**②硬體電源鍵拔機**:本機任 Master 局中長按電源鍵斷電 → 他機 ≤3s 偵測、≤6s 接管(NEW DEALER + ABORT 退注);本板無優雅交接路徑(§10),接管是常態退出路徑,反覆壓測(同 StickS3 §20.4 註記)。**③長桌局電量觀察**:≥1h 牌局中標題列 % 是否曾更新(ADC2 空窗命中率),結果記 §15 | 8–15、尤其 10 |

---

## 15. 實作經驗回寫(供 Cardputer-Adv 與後續迭代)

> **給實作 agent 的指示**:完成 xingzhi 實作(或任何一個驗收階段)後,把經驗按下列小節
> 追加到本章:**硬體無關**的(共用碼、編譯、文件歧義)同時回寫 S3 指南 §21;
> **本板硬體相關**的坑寫回 `xingzhi-cube-1.54tft-wifi/hardware-reference.md`/`README.md`。
> 涉及 poker_core 介面的變更須走 S3 §21 升版流程(介面凍結,只准加不准改;
> 文件歧義新裁定沿 R 編號續編,目前用至 R13)。
> 特別要求回填的實測值:
> - `AUDIO_PATH_LATENCY_MS` 實測值與混型齊播中位數(§6.4);
> - 常開靜音下功放底噪聽感結論(§6.3);
> - ADC2 在 Wi-Fi 常開下的讀取成功率 / 電量顯示凍結時長(§8.4、§14 階段 3-③);
> - 三機齊播矩陣數據(§14 階段 3-①);
> - 各任務棧 high-water 與 app 映像尺寸(對照 StickS3 1,135,376 B / zuowei 1,174,528 B)。

### 15.1 板級/HAL 的坑

> 完成時間 2026-07-21;階段 = 建置級(桌上無實機,實機驗收項見各節【待實機】)。

- **首建置一次過,自訂碼零 warning**(正式 + `-DPK_DEBUG_SOLO=1` 兩種建置均綠),
  工具鏈 = StickS3 同款 xtensa esp-15.2.0_20251204 / IDF v6.0.1。**證實 §1.3、§21.2-7
  的判斷:凍結核心在完全相同工具鏈上第三次重建無新 -Werror,風險趨近於零。**
  **未發現任何 poker_core bug**;zuowei §16.1-1 的 `-Wno-error=stringop-overflow` 墊片
  確認不需要(R13 修復在第三塊板上穩定),CMakeLists 未加該行、建置正常。
- **hal_audio.c 的 §6.2 diff 原樣套用即過**:刪三個 include(`board_i2c.h`/`es8311_min.h`/
  `m5pm1.h`)與三段呼叫(es8311/m5pm1)後無殘留符號;`effective_volume()` 直接 `return s_vol`;
  `I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE)` + `mclk=I2S_GPIO_UNUSED` 編譯乾淨,
  佇列/排程/audio task 逐行未動。埠 `AUDIO_SPK_I2S_PORT`(I2S1)無誤。
- **電池整數運算(zuowei §16.1-4)確認適用本板**:`BAT_FULL_MV/EMPTY_MV` 用整數宏、
  `try_read_bat_mv()` 對 `mv * BAT_DIVIDER`(float)顯式 `(int)` 轉型 →
  無 `-Wfloat-conversion`。demo `battery.c` 原用 `4200.0f/3300.0f` 浮點,已按指南改整數。
- **CMakeLists REQUIRES 差異(去 `esp_driver_i2c`、加 `esp_adc`)驗證無誤**:連結期無
  undefined reference;`WHOLE_ARCHIVE` 照加,`hal_battery_pct`/`hal_rand`/`hal_settings_*`/
  `pk_board_device_class` 全部解析。`rtc_gpio_*`(電源自鎖)由 `esp_driver_gpio` 提供,
  未另列亦可連結。
- **hal_display.c 三源拼裝的唯一實作抉擇**:UI/render 段照 zuowei 逐行抄,init 段對 zuowei
  母本做三處參數改動 —— (1) `lvgl_port_cfg` 加 `port.task_affinity = 1`(S3 雙核,§7.1);
  (2) 顯示緩衝改 `LCD_H_RES*40` 雙緩衝(demo 同款,取代 zuowei 的 20 行單緩衝);
  (3) CS/mode/pclk/gap 全走本板 §4.1 宏(GPIO14 / mode3 / 80MHz / (0,0))。背光沿用
  demo `backlight.c` 的 5kHz/10-bit/ch0 + `.flags.output_invert=BL_OUTPUT_INVERT`(false)。

### 15.2 音訊路徑實測數據

- `AUDIO_PATH_LATENCY_MS` 保持計算值 **60**(DMA 4×240=60ms + 直推功放群延遲≈0,無 codec)。
  **跨機錯位中位數【待實機】**:須用 §6.4 錄音對拍法對已校準 StickS3(62ms)量測後回填。
  預期本板與 S3 同為 xtensa+I2S 阻塞寫同構路徑,Δ 極小(對照 S3 §21.4)。
- **常開靜音填充底噪聽感【待實機】**(§6.3):理論上全零樣本 = 數位真靜音,DMA 永不欠載,
  應消除 demo README 坑 3 的 TX 空閒嘟嘟聲;功放常通電的類比底噪待貼耳實測。
- voice.bin 為四機共用同一份(S3 §21.4 實測 418,390 B / 74 片段);1.125MB voice 分區餘裕充足。

### 15.3 SRAM/性能實測

- **app 映像(正式建置)= 1,128,560 bytes**(0x113870;factory 3.87MB,餘 72%);
  SOLO 建置 = 1,128,624 bytes(多 [SOLO] log 字串)。**為三機最小**
  (對照 StickS3 1,135,376 / zuowei 1,174,528),與本板去 I2C/codec/IMU/opus 驅動的裁剪一致。
- task 棧設定沿 §3(= S3 §5 任務表):audio 4096 / LVGL 8192(affinity=1)/ buttons 2048;
  pbus/ui/gesture 由共用碼建立。**各任務 high-water【待實機】**。
- 8MB Octal PSRAM 配置生效(`SPIRAM_MODE_OCT=y`/`SPEED_80M=y`);保守方針下 LVGL 38.4KB
  雙緩衝走內部 SRAM(`buff_dma=true`),PSRAM 目前無用戶(§11),餘裕充足。

### 15.4 文件歧義的新裁定(續 S3 §4/§21.3,R 編號自 R14 起)

- **R14(顯示層,無害)**:`idf.py build` 結尾印出的 esptool 燒錄命令含 `--flash-mode dio`,
  但 `sdkconfig` 實際為 `CONFIG_ESPTOOLPY_FLASHMODE_QIO=y`(已驗證)。此 `dio` 是 IDF 燒錄
  參數字串的預設顯示值,**非實際燒錄模式**——bootloader header 依 sdkconfig 攜帶真實 QIO,
  開機正常。屬顯示層歧義,§12.1 的 QIO 定案不變,無需修正 sdkconfig,亦不必手動指定 flash-mode。
