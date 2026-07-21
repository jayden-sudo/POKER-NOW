# Cardputer-Adv 德州撲克遊戲機 軟體開發指南 v1.0

> 本指南是四種裝置中的**最後一份**(第一份 StickS3 定義全專案共用佈局)。
> 共用核心 `common/components/poker_core/` **已凍結並經三台裝置(StickS3 / zuowei-c3 /
> xingzhi-cube)建置驗證** —— 本指南**零共用碼工作**,不重抄任何協定/遊戲/UI 規格;
> 但本板是**板級工作量最大**的一台:無現成 demo 專案,**TCA8418 鍵盤驅動(§6)與
> ES8311 無 MCLK 音訊路徑(§8)都要新寫**,兩者在本指南給到函式級全文。
>
> 規格權威層級(衝突時由高到低):
> 1. `../../common/PROTOCOL.md` v1.1 —— 協定層唯一權威
> 2. `../../common/PRODUCT-SPEC.md` v1.2 —— 架構分層/HAL/畫面/語音唯一權威
> 3. `Cardputer-Adv/hardware-reference.md` —— 本板硬體唯一權威(引腳/位址/總線拓撲全出自此)
> 4. `../StickS3/dev-guide.md` —— 共用佈局基準(§1–§5)+ 文件矛盾裁定(§4,R1–R9)
>    + **實作經驗回寫(§21,R10–R14,必讀必遵)**
> 5. 本指南 —— 只寫 Cardputer-Adv 的差異與新驅動,引用以「協定 §x」「產品 §x」
>    「S3 §x」「參考 §x」(= hardware-reference.md)表示。

---

## 0. 讀者與範圍

- 讀者:負責實作的 coding agent / 韌體工程師,假定已通讀 S3 指南全篇(尤其 §1–§5、§21)。
- 產出物:`Cardputer-Adv/`(本裝置 IDF 專案)。`common/` **一個位元組都不改**;
  若撞到核心問題,走 S3 §21 升版流程,由主 agent 裁定。
- 硬體:M5Stack Cardputer-Adv,ESP32-S3FN8(雙核 Xtensa @240MHz,512KB SRAM,
  **8MB flash,無 PSRAM**)。參考 §2「大緩衝需精打細算」是本板貫穿性約束。
- 開發順序 = §16 分階段驗收順序:**先做 §6.4 鍵盤映射校準**(本板獨有前置),
  再 solo 冒煙,再上多機。

## 1. 前置必讀與凍結邊界(三份經驗回寫的板級落地)

以下規則出自三台已驗證裝置的回寫(S3 §21、zuowei §16、xingzhi §15),本板**一律照辦**:

1. **`main` 元件必加 `WHOLE_ARCHIVE`**(S3 §21.2-1,四機共通):HAL 符號被 poker_core
   反向引用,不加則 `hal_battery_pct/hal_rand/hal_settings_*/pk_board_device_class`
   全部 undefined reference。§2.3 的 CMakeLists 已含。
2. **R13 已修復**(S3 §21.3):`pbus_session.c` 的 2-byte 堆疊溢位已在核心修正,
   xingzhi 第三機重建驗證穩定。**本板 CMakeLists 嚴禁再加**
   `-Wno-error=stringop-overflow` 墊片(那是修復前 zuowei 的臨時物)。
3. **工具鏈 = StickS3/xingzhi 同款 xtensa esp-15.2.0_20251204 / IDF v6.0.1**。
   凍結核心在此工具鏈上已三次重建零 warning(xingzhi §15.1),新 `-Werror` 風險趨近於零;
   但首建置仍把 core 當新代碼看待(S3 §21.2-7),真撞上時只准板級降級、不准改核心。
4. **R14(顯示層,無害)**:`idf.py build` 尾端印的燒錄命令恆顯示 `--flash-mode dio`,
   與 sdkconfig 的 QIO 無關(bootloader header 攜真實模式)。勿因此改 sdkconfig。
5. **R10(NVS 持久化)**:音量/亮度走 NVS namespace `poker`、key `vol`/`bri`;
   `hal_misc.c` 照抄 StickS3(§12)。**R11**:`app_prepare_poweroff()` 由 main 提供
   單一 no-op 定義(本板側面電源開關 = P-MOS 物理切斷,攔截不到,§12)。
   **R12**:`pk_board_device_class()` 強符號覆寫,本板回 **3**(協定 §8.1:3=cardputer-adv)。
6. **電池整數百分比運算**(zuowei §16.1-4,xingzhi 再驗證):`BAT_FULL_MV/EMPTY_MV`
   用整數宏,凡 float 乘積顯式 `(int)` 轉型,不用浮點常數 —— §10 照此。
7. **`-Werror` 四坑照避**(S3 §21.2-4):註解裡不出現 `*/`;struct 回傳不加 `const`;
   一行一敘述;`<stdbool.h>/<string.h>/<stdio.h>` 顯式 include。
8. **音訊語義三度重申**(S3 §15.3 / xingzhi §6.2):`hal_audio_play_at` 的參數是
   **出聲時刻**(app_flow 直傳 `pbus_local_time_for(play_at)`),全路徑延遲已內建於
   `s_head_ms` 初始化,**上層不再扣 `hal_audio_path_latency_ms()`,扣兩次 = 提早出聲**。
9. **Master 端同步發佈**(S3 §21.1-1)等共用核心行為全部已在核心內落地,板級無感知。

## 2. 專案佈局與建置系統

### 2.1 目錄佈局

```
Cardputer-Adv/
├─ hardware-reference.md    # 硬體權威(既有)
├─ dev-guide.md            # 本指南
└─ poker/                        # ★ 本指南產出物(全新 IDF 專案;本板無 demo 專案)
   ├─ CMakeLists.txt
   ├─ sdkconfig.defaults         # §14.1 全文(無 PSRAM!)
   ├─ partitions.csv             # §14.2 全文(voice 分區對齊前三台)
   └─ main/
      ├─ CMakeLists.txt  idf_component.yml
      ├─ board_config.h          # 全部引腳/板級常數(唯一放 GPIO 的地方,§4.1)
      ├─ app_main.c              # §4.2(含共享匯流排「衛生腳位」)
      ├─ board_i2c.[ch]          # ← StickS3 原樣,只改 SDA/SCL 宏(§4.3)
      ├─ tca8418.[ch]            # ★ 新寫:鍵盤掃描晶片驅動(§6 全文)
      ├─ hal_input.c             # ★ 新寫:鍵位映射/Fn 層/數字直輸/G0(§7 全文)
      ├─ es8311_cardputer.[ch]   # ★ StickS3 es8311_min 改造:無 MCLK/SCLK 時鐘源(§8 全文)
      ├─ hal_audio.c             # ← StickS3 hal_audio.c 高複用(§8.4 diff)
      ├─ hal_display.c           # ← StickS3 hal_display.c 高複用(同 240×135 佈局,§9)
      ├─ hal_battery.c           # ★ 新寫(G10 ADC1 分壓 ×2,§10 全文)
      ├─ hal_power.c             # ← StickS3 原樣(空殼,§11)
      └─ hal_misc.c              # ← StickS3 照抄,device_class 改 3(§12)
```

出廠 UserDemo(IDF 5.4.2 + M5GFX 生態)**不作為程式碼母本**(產品 §0:M5GFX 在
IDF v6 無法編譯),僅在 §6.4 鍵位校準時作為映射對照來源之一。

### 2.2 `poker/CMakeLists.txt`(全文)

```cmake
cmake_minimum_required(VERSION 3.16)

# 引用全專案共用元件(poker_core);本板無自有 components/
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../common/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(poker_cardputer_adv)
```

(R13 已修:**不加**任何 `-Wno-error=*` 行,見 §1-2。)

### 2.3 `poker/main/CMakeLists.txt`(全文)

```cmake
idf_component_register(
    SRCS
        "app_main.c" "board_i2c.c"
        "tca8418.c" "es8311_cardputer.c"
        "hal_display.c" "hal_input.c" "hal_audio.c"
        "hal_battery.c" "hal_power.c" "hal_misc.c"
    INCLUDE_DIRS "."
    REQUIRES
        poker_core
        esp_lcd esp_lvgl_port lvgl
        esp_driver_spi esp_driver_i2c esp_driver_i2s
        esp_driver_gpio esp_driver_ledc
        esp_adc
        esp_timer esp_partition spi_flash
        esp_wifi nvs_flash
    WHOLE_ARCHIVE      # S3 §21.2-1:HAL 實作在 main,被 poker_core 反向引用;
                       # 不加則 hal_* / pk_board_device_class 全部 undefined reference
)
```

與 StickS3 的差異:去 `m5pm1.c/imu_bmi270.c`(本板無 PM1;IMU 不用,§13),
加 `tca8418.c`、`esp_adc`(電池 G10 走 ADC1 oneshot;StickS3 電量走 PM1 I2C 故無此項)。
`esp_driver_i2c` 保留(TCA8418 + ES8311 共用匯流排)。

### 2.4 `poker/main/idf_component.yml`(全文)

```yaml
dependencies:
  idf: ">=6.0"
  espressif/esp_lvgl_port: "^2"
```

**不用 `espressif/es8311` 元件**(S3 §21.2-2 定案:自帶最小 codec 驅動,免網路相依、
免 legacy-i2c 風險)—— 本板更有理由自帶:無 MCLK 模式需要改寄存器,§8.2 全文給出。

### 2.5 工具鏈(xtensa,與 StickS3/xingzhi 完全相同)

```bash
. ~/.espressif/v6.0.1/esp-idf/export.sh
# 本機 export.sh 不把工具鏈加入 PATH,手動補(S3 = xtensa):
export PATH="$HOME/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin:$PATH"

cd devices/Cardputer-Adv
idf.py set-target esp32s3          # 僅首次
```

## 3. 任務模型(= S3 指南 §5 原表,一處替換)

本板與 StickS3/xingzhi 同為 ESP32-S3 雙核,S3 §5 任務表(核綁定、優先級、棧深)照用,
唯一替換:`buttons`(GPIO 輪詢)→ **`kbd`(TCA8418 中斷+輪詢混合)**:

| Task | 核 | 優先級 | 棧 | 差異說明 |
|---|---|---|---|---|
| `kbd`(tca8418.c) | 0 | 6 | **3072** | 取代 buttons;棧加大:task 內做 I2C 交易(i2c_master_transmit 系列)+ 映射邏輯,2048 太緊 |
| `btn_g0`(hal_input.c) | 0 | 6 | 2048 | G0 並聯 OK 鍵的 10ms 輪詢(§7.4);結構 = StickS3 btn_task 單鍵版 |
| `audio` | 1 | 15 | 4096 | 同 S3(I2S 阻塞寫節拍) |
| `LVGL` | 1 | 4 | 8192 | affinity=1(S3 雙核慣例,xingzhi §15.1 驗證) |
| `pbus` / `ui` | 0 | 12 / 5 | 6144 / 8192 | 共用碼建立,零改動 |

約束自動成立:pbus(12) < wifi(23)、pbus(12) ≤ audio(15)(協定 §4.2)。
**I2C 併發注意**:匯流排上三個住戶(TCA8418/ES8311/BMI270,§5.1 參考),本專案只碰前兩個;
ES8311 僅在 `hal_audio_init`(app_main 上下文)寫寄存器,之後音量走軟體增益不再碰 I2C
(S3 §15.4 同理),TCA8418 交易全部在 kbd task —— **穩態下單一 task 觸 I2C**;
即便未來加 IMU 輪詢,IDF v6 `i2c_master` 對同一 bus handle 內建鎖,跨 task 亦安全。

## 4. board_config.h 與 app_main

### 4.1 `main/board_config.h`(全文)

引腳全部出自 `hardware-reference.md` §3/§4(原理圖版);本板無 demo 實測值,
顯示旋轉/偏移為推導初值,首點亮按 §9.4 自檢後回填 §17。

```c
// Cardputer-Adv 板級配置 —— 唯一允許出現 GPIO 編號的檔案(權威:hardware-reference.md §4)
#pragma once
#include "driver/gpio.h"

// ---- 共享 I2C(內部匯流排:TCA8418 / ES8311 / BMI270,參考 §5.1)----
#define BOARD_I2C_SDA        GPIO_NUM_8
#define BOARD_I2C_SCL        GPIO_NUM_9
#define I2C_ADDR_TCA8418     0x34
#define I2C_ADDR_ES8311      0x18        // CE 接地(參考 §3.3)
#define I2C_ADDR_BMI270      0x69        // 本板 0x69(StickS3 是 0x68!);本專案不初始化(§13)

// ---- 鍵盤(TCA8418RTWR,7 行 × 8 列電氣矩陣)----
#define KBD_INT_PIN          GPIO_NUM_11  // 低有效,晶片側 3.3k 上拉(參考 §3.2)
#define KBD_ROWS             7
#define KBD_COLS             8

// ---- G0 = BOOT / BtnA(參考 §4:運行時可作用戶按鍵)----
#define BTN_G0_PIN           GPIO_NUM_0   // §7.4:並聯 KEY_OK

// ---- LCD (ST7789V2 1.14", 實體 135×240 → 邏輯 240×135 橫向;參考 §3.7) ----
#define LCD_SPI_HOST         SPI3_HOST
#define LCD_PIN_MOSI         GPIO_NUM_35  // DAT
#define LCD_PIN_SCLK         GPIO_NUM_36
#define LCD_PIN_CS           GPIO_NUM_37
#define LCD_PIN_DC           GPIO_NUM_34  // RS
#define LCD_PIN_RST          GPIO_NUM_33
#define LCD_PIN_BL           GPIO_NUM_38  // ★ 兼 Stamp-S3A RGB LED 電源使能(PWR_EN);
                                          //   PWM 佔空下限 10%(產品 §2.2)
#define LCD_H_RES            240          // 邏輯橫向(與 StickS3 同一組 240×135 佈局)
#define LCD_V_RES            135
#define LCD_PIXEL_CLOCK_HZ   (40 * 1000 * 1000)
#define LCD_SPI_MODE         0
// 以下四項為推導初值(同款 1.14" ST7789 面板,沿 StickS3 R7 橫向參數);§9.4 自檢後定案:
#define LCD_INVERT_COLOR     true
#define LCD_SWAP_XY          true
#define LCD_MIRROR_X         false
#define LCD_MIRROR_Y         true
#define LCD_GAP_X            40
#define LCD_GAP_Y            52

// ---- 音訊(ES8311 + NS4150B;★ MCLK 未接主控,SCLK 作時鐘源,參考 §3.3)----
#define AUDIO_SAMPLE_RATE    16000
#define I2S_PIN_BCLK         GPIO_NUM_41  // ES8311 SCLK
#define I2S_PIN_LRCK         GPIO_NUM_43  // ES8311 LRCK(= 預設 U0TXD,勿啟 UART0!)
#define I2S_PIN_DOUT         GPIO_NUM_42  // ES8311 DSDIN(播放,MCU→codec)
// G46 = ASDOUT(錄音,codec→MCU):本專案不用麥克風,不接(§13)
#define AUDIO_VOL_DEFAULT    70
#define AUDIO_PATH_LATENCY_MS 62          // 初值:DMA 4×240=60ms + ES8311 群延遲 ~2ms
                                          // (與 StickS3 同構同值);§8.5 實測後回填

// ---- 電池(G10 ADC,100k/100k 分壓 → Vbat = 讀數 ×2,參考 §3.1)----
#define BAT_ADC_UNIT         ADC_UNIT_1
#define BAT_ADC_CHANNEL      ADC_CHANNEL_9   // ESP32-S3:GPIO10 = ADC1_CH9(無 Wi-Fi 衝突)
#define BAT_ADC_ATTEN        ADC_ATTEN_DB_12
#define BAT_DIVIDER_NUM      2               // ×2(整數,zuowei §16.1-4 慣例)
#define BAT_FULL_MV          4200
#define BAT_EMPTY_MV         3300
// 無充電狀態腳(TP4057 STAT 未引出):hal_battery_charging() 恆 false(§10)

// ---- 不使用但需「衛生處理」的共享 SPI 片選(§13,參考 §13.6 Launcher 經驗)----
#define UNUSED_CS_LORA       GPIO_NUM_5    // Cap-Bus / SX1262 NSS → 開機拉高
#define UNUSED_CS_SDCARD     GPIO_NUM_12   // microSD CS → 開機拉高
```

### 4.2 `main/app_main.c`(全文)

= StickS3 骨架去 M5PM1 三行,加共享匯流排衛生處理。本板無電源晶片前置坑,
啟動序比 StickS3 簡單;鍵盤在 `app_flow_start → input_gesture_init → hal_input_init`
內初始化(依賴 `board_i2c_init` 先行,次序由本檔保證)。

```c
/* app_main.c -- Cardputer-Adv 德州撲克進入點(指南 §4.2)。 */
#include "board_config.h"
#include "board_i2c.h"
#include "hal/hal_display.h"
#include "hal/hal_audio.h"
#include "app_flow.h"
#include "driver/gpio.h"
#include "esp_mac.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "app_main";

/* 共享 SPI 匯流排衛生(§13):本專案不初始化 microSD / Cap LoRa,但兩者共享
 * G40/G14/G39;若 Cap 模組在位、CS 懸浮,SX1262 可能亂駕 MISO(參考 §13.6:
 * Launcher 在 Adv 上需拉高 G5 的既往經驗)。開機把兩個片選鎖定為「未選中」。 */
static void bus_hygiene(void)
{
    gpio_config_t io = {
        .pin_bit_mask = BIT64(UNUSED_CS_LORA) | BIT64(UNUSED_CS_SDCARD),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(UNUSED_CS_LORA, 1);
    gpio_set_level(UNUSED_CS_SDCARD, 1);
}

void app_main(void)
{
    bus_hygiene();

    ESP_ERROR_CHECK(board_i2c_init());      /* i2c_new_master_bus(G8/G9);鍵盤/codec 共用 */
    ESP_ERROR_CHECK(hal_display_init());    /* §9 */
    ESP_ERROR_CHECK(hal_audio_init());      /* §8(含 voice 分區 mmap → voice_init) */

    char name[8];
    uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(name, sizeof(name), "CP-%02X%02X", mac[4], mac[5]);
#ifdef PK_DEBUG_SOLO
    ESP_LOGW(TAG, "[SOLO] smoke build");
#endif
    app_flow_start(name);                   /* 內部 game_init→pbus_start;鍵盤亦在此鏈上初始化 */
    ESP_LOGI(TAG, "boot complete: %s", name);
}
```

### 4.3 `board_i2c.[ch]`

`StickS3/main/board_i2c.[ch]` **原樣拷貝,零行改動**(引腳走 `BOARD_I2C_SDA/SCL`
宏,本板宏值 G8/G9 已在 §4.1)。要點重申:IDF v6 新 API `i2c_new_master_bus()`
+ `i2c_master_bus_add_device()`;100kHz;`enable_internal_pullup=true` 無害
(板上已有 3.3kΩ 硬上拉,參考 §5.1)。**單一 bus handle 由 `board_i2c_bus()` 分發**給
tca8418.c 與 es8311_cardputer.c —— 兩驅動各自 add_device,不得自建第二條匯流排
(參考 §12-2「G8/G9 是公共干道」)。

## 5. 複用計畫(差異驅動,逐檔裁定)—— 本指南的主索引

本板 HAL 母本 = **StickS3 `poker/main/`**(同 S3 雙核、同 ES8311、同 240×135 邏輯佈局,
複用度全四機最高);記憶體慣例 = **zuowei**(無 PSRAM:LVGL 單緩衝/全靜態分配)。

| poker/main 檔 | 來源 | 改點(完整清單) |
|---|---|---|
| `board_config.h` | 新寫 | §4.1 全文 |
| `app_main.c` | StickS3 | 去 m5pm1 三行;加 `bus_hygiene()`;前綴 `CP-`(§4.2) |
| `board_i2c.[ch]` | StickS3 **原樣** | 零改動(宏值換 G8/G9,§4.3) |
| `tca8418.[ch]` | **新寫** | §6 全文(本板兩大新驅動之一) |
| `hal_input.c` | **新寫** | §7 全文(映射/Fn 層/數字直輸/G0;取代 StickS3 hal_input.c + imu 空殼) |
| `es8311_cardputer.[ch]` | StickS3 `es8311_min.[ch]` 改造 | §8.2 全文(無 MCLK:REG01/REG02 兩處寄存器差異) |
| `hal_audio.c` | **StickS3 `hal_audio.c` ≈90% 原樣照抄** | §8.4 diff:①es8311_min→es8311_cardputer ②刪 m5pm1 include 與 amp/電池鉗三段 ③I2S gpio_cfg 無 MCLK。佇列/排程/樣本級切入/audio task 主迴圈**逐行保持**(三機驗證的凍結參考語義) |
| `hal_display.c` | **StickS3 `hal_display.c` ≈95% 原樣照抄** | §9:同 240×135 佈局,UI 物件/座標/render 全複用;init 段 3 處參數改動(引腳宏本已隔離、單緩衝、gap 自檢) |
| `hal_battery.c` | 新寫(結構仿 zuowei/xingzhi) | §10 全文(ADC1 oneshot + ×2 + 整數 pct + 1s 快取) |
| `hal_power.c` | StickS3 **原樣** | 只改註解:PM1 雙擊 → 側面物理電源開關(§11) |
| `hal_misc.c` | StickS3 **照抄** | 唯一改動:`pk_board_device_class()` 回 **3**(§12) |
| 不寫 | m5pm1、imu_bmi270、SD/LoRa/IR/麥克風任何驅動 | §13(不初始化 = 最強隔離) |

---

## 6. TCA8418 鍵盤驅動(新寫,本指南核心之一)

### 6.1 硬體事實(參考 §3.2/§5.1)

- TI TCA8418RTWR,I2C 7 位位址 **0x34**,掛共享匯流排 G8/G9;INT=**G11 低有效**
  (晶片側 3.3kΩ 上拉,ESP32 端配輸入即可,無需再上拉)。
- 56 鍵 = **ROW0–6 × COL0–7(7×8)電氣矩陣**,邏輯呈現 4 行 × 14 列。
- 晶片內建:去抖(預設開啟)、**10 級 FIFO 事件佇列**、按下/釋放事件
  (事件位元組 bit7 = 1 按下 / 0 釋放,bit6:0 = 鍵碼 = `row*10 + col + 1`)。
- INT 行為:FIFO 非空 → INT 拉低;**讀空 FIFO 並寫 1 清除 K_INT 位後才回高**。

### 6.2 讀取策略裁定:中斷 + 輪詢混合

純輪詢(如 StickS3 btn_task)可行(FIFO 兜底,參考 §12-9),但 10ms 輪詢 = 每秒 100 次
I2C 讀 INT_STAT,共享匯流排上是無謂流量;純中斷則怕邊沿丟失(I2C 清中斷與新事件競態)。
**裁定:GPIO 負緣中斷喚醒 + 50ms 輪詢兜底** —— 中斷只 give semaphore(零 I2C),
task 醒來後**以 INT 電平(而非中斷計數)決定是否掃 FIFO**,電平語義天然免疫丟邊沿;
50ms 超時輪詢兜底「中斷配置失效」的極端情況。實測若有按鍵黏滯/丟鍵,回寫 §17。

### 6.3 `main/tca8418.h` / `main/tca8418.c`(全文)

```c
/* tca8418.h -- TCA8418 鍵盤掃描晶片驅動(I2C 0x34,INT=G11 低有效)。
 * 上報電氣座標 (row 0-6, col 0-7) 的按下/釋放事件;鍵位語義在 hal_input.c(§7)。 */
#pragma once
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

typedef void (*tca8418_key_cb_t)(uint8_t row, uint8_t col, bool pressed);

/* 初始化 7×8 矩陣 + FIFO + 中斷,並建立 kbd task(core0, prio6, 3072)。
 * bus = board_i2c_bus();cb 於 kbd task 上下文被呼叫。 */
esp_err_t tca8418_init(i2c_master_bus_handle_t bus, gpio_num_t int_pin, tca8418_key_cb_t cb);
```

```c
/* tca8418.c -- TCA8418 驅動實作(指南 §6)。
 * 資料手冊:TCA8418(TI);鍵碼 = row*10 + col + 1(bit7 = press)。 */
#include "tca8418.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "tca8418";

/* ---- 寄存器(僅列本驅動用到的)---- */
#define REG_CFG          0x01   /* [7]AI [4]INT_CFG [3]OVR_FLOW_IEN [0]KE_IEN */
#define REG_INT_STAT     0x02   /* [4]CAD [3]OVR_FLOW [2]K_LCK [1]GPI [0]K_INT(寫1清除) */
#define REG_KEY_LCK_EC   0x03   /* [3:0] FIFO 內事件數 */
#define REG_KEY_EVENT_A  0x04   /* FIFO 讀口:0=空;[7]press [6:0]keycode */
#define REG_KP_GPIO1     0x1D   /* ROW0-7 撥入鍵盤矩陣(1=矩陣) */
#define REG_KP_GPIO2     0x1E   /* COL0-7 */
#define REG_KP_GPIO3     0x1F   /* COL8-9(本板不用,寫 0) */

#define CFG_KE_IEN       0x01
#define CFG_OVR_FLOW_IEN 0x08
#define CFG_INT_CFG      0x10   /* 中斷處理期間有新事件 → INT 再拉低(電平語義友好) */
#define INT_K_INT        0x01
#define INT_OVR_FLOW     0x08

static i2c_master_dev_handle_t s_dev;
static gpio_num_t         s_int_pin;
static tca8418_key_cb_t   s_cb;
static SemaphoreHandle_t  s_sem;

static esp_err_t wr(uint8_t reg, uint8_t val)
{
    uint8_t f[2] = { reg, val };
    return i2c_master_transmit(s_dev, f, 2, 100);
}
static esp_err_t rd(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 100);
}

static void IRAM_ATTR int_isr(void *arg)
{
    (void)arg;
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_sem, &hp);
    if (hp) portYIELD_FROM_ISR();
}

/* 讀空 FIFO 並分發事件;回傳處理的事件數 */
static int drain_fifo(void)
{
    int n = 0;
    for (;;) {
        uint8_t ev = 0;
        if (rd(REG_KEY_EVENT_A, &ev) != ESP_OK) break;   /* I2C 失敗:下輪重試 */
        if (ev == 0) break;                              /* FIFO 空 */
        uint8_t code = ev & 0x7F;                        /* 1..80 */
        if (code >= 1) {
            uint8_t row = (uint8_t)((code - 1) / 10);
            uint8_t col = (uint8_t)((code - 1) % 10);
            if (row < 7 && col < 8 && s_cb)
                s_cb(row, col, (ev & 0x80) != 0);
        }
        n++;
        if (n >= 16) break;                              /* 防病態迴圈(FIFO 深 10) */
    }
    /* 寫 1 清除中斷位(必須在 FIFO 讀空後,否則 K_INT 立即重置) */
    wr(REG_INT_STAT, INT_K_INT | INT_OVR_FLOW);
    return n;
}

static void kbd_task(void *arg)
{
    (void)arg;
    for (;;) {
        xSemaphoreTake(s_sem, pdMS_TO_TICKS(50));        /* 中斷喚醒;50ms 輪詢兜底 */
        if (gpio_get_level(s_int_pin) == 0)              /* 電平判定,免疫丟邊沿 */
            drain_fifo();
    }
}

esp_err_t tca8418_init(i2c_master_bus_handle_t bus, gpio_num_t int_pin, tca8418_key_cb_t cb)
{
    s_int_pin = int_pin;
    s_cb = cb;

    const i2c_device_config_t dcfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x34,
        .scl_speed_hz = 100 * 1000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dcfg, &s_dev), TAG, "add dev");

    /* 1) 7×8 矩陣:ROW0-6、COL0-7 撥入鍵盤掃描(參考 §3.2);去抖預設開啟不動 */
    ESP_RETURN_ON_ERROR(wr(REG_KP_GPIO1, 0x7F), TAG, "rows");
    ESP_RETURN_ON_ERROR(wr(REG_KP_GPIO2, 0xFF), TAG, "cols");
    ESP_RETURN_ON_ERROR(wr(REG_KP_GPIO3, 0x00), TAG, "cols89");

    /* 2) 清殘留:讀空 FIFO(上電/重啟殘鍵)+ 清全部中斷位 */
    for (int i = 0; i < 12; i++) { uint8_t ev; if (rd(REG_KEY_EVENT_A, &ev) != ESP_OK || ev == 0) break; }
    ESP_RETURN_ON_ERROR(wr(REG_INT_STAT, 0x1F), TAG, "int clr");

    /* 3) 開鍵事件中斷 + 溢出中斷 */
    ESP_RETURN_ON_ERROR(wr(REG_CFG, CFG_KE_IEN | CFG_OVR_FLOW_IEN | CFG_INT_CFG), TAG, "cfg");

    /* 4) INT 腳 + 負緣中斷(晶片側已有 3.3k 硬上拉) */
    s_sem = xSemaphoreCreateBinary();
    gpio_config_t io = {
        .pin_bit_mask = BIT64(int_pin),
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "int gpio");
    esp_err_t r = gpio_install_isr_service(0);
    if (r != ESP_OK && r != ESP_ERR_INVALID_STATE) return r;   /* 已安裝則沿用 */
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(int_pin, int_isr, NULL), TAG, "isr add");

    xTaskCreatePinnedToCore(kbd_task, "kbd", 3072, NULL, 6, NULL, 0);
    ESP_LOGI(TAG, "TCA8418 ready (7x8 matrix, INT=G%d)", (int)int_pin);
    return ESP_OK;
}
```

### 6.4 鍵位映射校準(本板獨有的必做前置)

參考文件給出**邏輯佈局**(參考 §11:4 行 × 14 列,三層鍵值)但**沒有電氣 (row,col) →
邏輯鍵位的映射表**(那在原理圖鍵盤頁 / M5Cardputer 庫 `Keyboard_def.h` 的 reader 層)。
§7 的 `key_of()` 預設採 **row-major 假定**(`idx = row*8 + col` → 邏輯 `(y=idx/14,
x=idx%14)`)—— 這是未經實機驗證的合理猜測,**首次上機必須校準**:

1. 以 `idf.py build -DPK_KEYMAP_DUMP=1` 建置(§7 的 `on_key` 會 LOGI 每次按鍵的
   `r/c/DOWN|UP` 與映射結果);
2. 依序按 `Enter`、`.`、`Fn`、`;`、`,`、`/`、`0`–`9`、`Backspace`(撲克用到的全部鍵),
   對照絲印核對映射輸出;
3. 不符 → 只改 §7 的 `key_of()`(通常是行/列序或蛇形走線差異,一個換算式的事);
   映射對照的第二來源:`m5stack/M5Cardputer` 庫(v1.2.0)`Keyboard_def.h` 的
   TCA8418 reader、或 UserDemo `CardputerADV` 分支 `hal/keyboard`;
4. 校準結果(最終換算式或查表)回寫 §17,並移除猜測標註。

撲克只用約 20 顆鍵,全程 ≤10 分鐘;**未校準前不得進入 §16 階段 0**。

## 7. 輸入 HAL(hal_input.c 全文)

映射規範(產品 §2.1 表,硬性):**Enter = KEY_OK、`.` = KEY_NEXT**;
方向鍵(Fn+`; , . /`,倒 T 形)→ incdec;數字鍵 0-9 → `hal_input_get_number` 直輸(加分);
G0(BtnA)= 並聯 KEY_OK(§7.4)。

**重要現狀說明(誠實記錄)**:凍結核心的 `app_flow.c` 目前**尚未消費**
`hal_input_set_incdec_cb` 與 `hal_input_get_number`(grep 驗證:common/src 零引用;
xingzhi 的中鍵 incdec 同樣處於「HAL 已備、共用層未接」狀態)。本板 HAL 照介面契約
**完整實作**兩者(符號必須存在,且加分功能只欠共用層一根線);共用層接線屬
「只准加不准改」的介面延伸(數值畫面輪詢 get_number / 註冊 incdec),
須走 S3 §21 流程由主 agent 排程 —— 在那之前,兩鍵心智(產品 §5)已完整可玩,
方向鍵/數字鍵為休眠加分項。此事記入 §16 驗收備註與 §17。

```c
/* hal_input.c -- TCA8418 鍵盤 → 撲克輸入(指南 §7)。
 * Enter=OK、'.'=NEXT(raw press/release,手勢合成在共用 input_gesture.c);
 * Fn+;,./ = incdec;數字直輸 get_number;G0 並聯 OK。 */
#include "hal/hal_input.h"
#include "board_config.h"
#include "board_i2c.h"
#include "tca8418.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdbool.h>

static const char *TAG = "hal_input";
static hal_input_cb_t        s_cb;
static hal_input_incdec_cb_t s_incdec;

/* ---- 邏輯鍵值:可列印鍵用 ASCII;特殊鍵用 >0x7F 常數 ---- */
enum {
    KC_NONE = 0,
    KC_ENTER = 0x0D, KC_BS = 0x08, KC_TAB = 0x09,
    KC_FN = 0x80, KC_SHIFT, KC_CTRL, KC_OPT, KC_ALT,
};

/* 邏輯佈局(參考 §11 普通層,4 行 × 14 列) */
static const uint8_t s_logical[4][14] = {
    { '`', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', KC_BS },
    { KC_TAB, 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\\' },
    { KC_FN, KC_SHIFT, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', KC_ENTER },
    { KC_CTRL, KC_OPT, KC_ALT, 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', ' ' },
};

/* 電氣 (row 0-6, col 0-7) → 邏輯鍵值。
 * 【row-major 假定,未經實機驗證 —— 必經 §6.4 校準,改這一個函式即可】 */
static uint8_t key_of(uint8_t r, uint8_t c)
{
    unsigned idx = (unsigned)r * 8u + c;            /* 0..55 */
    if (idx >= 56) return KC_NONE;
    return s_logical[idx / 14][idx % 14];
}

/* ---- 修飾層與數字直輸狀態(全部只在 kbd task 上下文觸碰)---- */
static bool s_fn_down;
static int  s_num = -1;                             /* -1 = 無輸入 */

static void on_key(uint8_t r, uint8_t c, bool pressed)
{
    uint8_t k = key_of(r, c);
#ifdef PK_KEYMAP_DUMP
    ESP_LOGI(TAG, "r=%u c=%u %s -> 0x%02x('%c')", r, c,
             pressed ? "DOWN" : "UP", k, (k >= 0x20 && k < 0x7F) ? k : '?');
#endif
    if (k == KC_FN) { s_fn_down = pressed; return; }

    /* Fn 層方向鍵 → incdec(按下沿觸發;參考 §11:Fn+;=↑ ,=← .=↓ /=→) */
    if (s_fn_down) {
        if (pressed && s_incdec) {
            if (k == ';' || k == '/') s_incdec(+1);          /* ↑ / → = +1 */
            else if (k == '.' || k == ',') s_incdec(-1);     /* ↓ / ← = -1 */
        }
        return;                                     /* 其餘 Fn 組合不進共用層 */
    }

    if (k == KC_ENTER) { if (s_cb) s_cb(KEY_OK, pressed); return; }
    if (k == '.') {                                 /* NEXT;導航即放棄數字輸入 */
        s_num = -1;
        if (s_cb) s_cb(KEY_NEXT, pressed);
        return;
    }

    if (!pressed) return;                           /* 以下僅按下沿 */
    if (k >= '0' && k <= '9') {                     /* 數字直輸(≤9999,PK_CHIPS_MAX) */
        int v = (s_num < 0 ? 0 : s_num) * 10 + (k - '0');
        s_num = (v > 9999) ? 9999 : v;
    } else if (k == KC_BS) {
        s_num = (s_num >= 10) ? s_num / 10 : -1;
    } else if (k != KC_SHIFT && k != KC_CTRL && k != KC_OPT && k != KC_ALT) {
        s_num = -1;                                 /* 其他字符鍵 = 放棄輸入 */
    }
}

/* ---- G0(BtnA)並聯 KEY_OK:10ms 輪詢去抖(StickS3 btn_task 單鍵版,§7.4)---- */
static void g0_task(void *a)
{
    (void)a;
    bool stable = false, last = false;
    for (;;) {
        bool raw = gpio_get_level(BTN_G0_PIN) == 0;
        if (raw == last && raw != stable) {
            stable = raw;
            if (s_cb) s_cb(KEY_OK, raw);
        }
        last = raw;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t hal_input_init(hal_input_cb_t cb)
{
    s_cb = cb;
    ESP_ERROR_CHECK(tca8418_init(board_i2c_bus(), KBD_INT_PIN, on_key));

    gpio_config_t io = { .pin_bit_mask = BIT64(BTN_G0_PIN),
                         .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&io);
    xTaskCreatePinnedToCore(g0_task, "btn_g0", 2048, NULL, 6, NULL, 0);
    ESP_LOGI(TAG, "keyboard input ready");
    return ESP_OK;
}

void hal_input_set_incdec_cb(hal_input_incdec_cb_t cb) { s_incdec = cb; }
int  hal_input_get_number(void) { return s_num; }   /* -1=無輸入;讀取不清零,
                                                     * 導航('.')/非數字鍵才清 */
```

### 7.1 語義要點

- **raw press/release 兩沿都要報**(KEY_OK/KEY_NEXT):共用 `input_gesture.c` 靠
  按下→放開的時長合成 SHORT/LONG/REPEAT(600/120ms,S3 裁定 R5)。TCA8418 天然給
  兩沿事件,直通即可;**不得**在 HAL 做任何時長合成。
- **Fn 修飾以「事件當下」判定**:Fn 按下期間,`.` 不再觸發 KEY_NEXT(方向鍵語義優先),
  避免 Fn+`.`(↓)漏成 NEXT。先按 `.` 再按 Fn 的交錯序列會讓 `.` 的釋放沿被 Fn 層
  吞掉 —— 對 gesture 合成無害(合成器對「無按下沿的釋放」免疫;若實測有黏鍵感,
  改為記錄「按下時的角色」並以同角色收尾,回寫 §17)。
- **incdec 無連發**:TCA8418 無硬體 auto-repeat,方向鍵每按一次 ±1。夠用
  (長按連發語義屬 KEY_NEXT 的 REPEAT,共用層已有);不做按住重複的模擬。
- **數字直輸契約**(共用層將來接線時依此):`get_number()` 回目前累積值(-1=無);
  數字鍵追加(鉗 9999 = PK_CHIPS_MAX)、Backspace 刪尾位、`.`/其他字符鍵放棄;
  Enter **不**清值(讓消費方在 OK 時刻先讀後清的次序不被 HAL 搶跑)。
- `s_num` 為 int 單寫者(kbd task)單讀者(ui task)——
  對齊 int 的讀寫在 Xtensa 上原子,無鎖安全。

### 7.2 為什麼不用 Shift/大小寫/其餘 50 顆鍵

產品 §5:全產品統一 2 鍵心智,鍵盤是超集能力,**不得成為流程必要條件**。
其餘鍵一律吞掉(`on_key` 落到「放棄輸入」分支),不蜂鳴、不上報 —— 桌上誤觸鍵盤
不產生任何遊戲輸入。

### 7.3 G0(BtnA)裁定(參考 §4:G0=BOOT/BtnA)

- **映射:並聯 KEY_OK**。理由:單手持機時拇指按 G0 比找 Enter 快;OK 是高頻確認鍵,
  並聯零學習成本;不映射 NEXT(避免兩顆物理鍵語義撞車)。
- 注意:G0 是 strapping 腳(BOOT)。運行時作輸入(上拉、低有效)完全安全;
  副作用僅存在於**復位瞬間**:按住 G0 重上電 = 進下載模式(§15.2)—— 這正是官方
  燒錄步驟,無需迴避。開機自動按住不放會進不了 app,屬使用者已知行為,不做軟體防護。

## 8. 音訊 HAL(ES8311 無 MCLK 模式 —— 本板關鍵差異)

### 8.1 硬體事實與時鐘拓撲(參考 §3.3)

- ES8311 控制走共享 I2C(0x18);I2S:**SCLK=G41、LRCK=G43、DSDIN=G42(播放)**;
  ASDOUT=G46(錄音)不接。
- **MCLK 未接主控 GPIO** —— 與 StickS3(MCLK 有獨立 G18)唯一的音訊差異。
  ES8311 支援以 **SCLK(BCLK)作內部時鐘源**:寄存器 **REG01 bit7(MCLK_SEL)=1
  → 內部 MCLK 取自 SCLK 腳**,再經 REG02 的預倍頻補回內部所需的 256fs。
- 時鐘推導(鎖死,勿改 I2S slot 配置):IDF `i2s_std` Philips 16-bit **MONO** 模式
  線上仍是雙 slot 幀 → `BCLK = 16000 × 16 × 2 = 512 kHz = 32fs`;
  ES8311 內部需 ~256fs → 預倍頻 **×8**(512kHz × 8 = 4.096MHz)。
  對應 REG02:`pre_div=1, pre_multi=×8` → 值 **0x18**(對照 StickS3 MCLK 模式的 0x10)。
- **NS4150B 功放無使能腳**:耳機插入(PJ-342 HP_DET)經 2N7002 硬體切斷功放
  (參考 §3.3)。**軟體完全不管**:無 amp GPIO、無 m5pm1_amp_set 對應物、
  無法也不需感知耳機在位 —— 這反而是四機中最乾淨的功放拓撲。
- 靜音策略照產品 §2.3 規則 1:輸出流常開、全零填充 = 數位真靜音(有 codec DAC,
  同 StickS3 §15.2 結論,無 xingzhi 直推板的嘟嘟聲風險)。

### 8.2 `main/es8311_cardputer.[ch]`(全文)

以 StickS3 `es8311_min.[ch]` 為底本改造;**與 StickS3 的寄存器差異只有兩處**
(REG01、REG02),其餘序列一致。StickS3 序列標註【待實測】(S3 §21.4),
本板同樣待真機驗證 —— 無聲/雜音時優先核對 0x01/0x02 分頻與 0x09/0x0A(SDP 16bit I2S)。

```c
/* es8311_cardputer.h -- ES8311 最小播放路徑(無 MCLK:SCLK 作時鐘源)。 */
#pragma once
#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>

esp_err_t es8311_cardputer_init(i2c_master_bus_handle_t bus, uint32_t sample_rate);
void es8311_cardputer_set_volume(uint8_t vol_0_100);   /* codec 音量;軟體增益另在 hal_audio */
```

```c
/* es8311_cardputer.c -- ES8311 DAC 播放路徑最小初始化(Cardputer-Adv 版,指南 §8.2)。
 * 差異(vs StickS3 es8311_min.c):本板 MCLK 未接主控 → REG01 bit7=1 以 SCLK 為
 * 內部時鐘源;SCLK=512kHz(16k×16bit×2slot)→ REG02 預倍頻 ×8 = 4.096MHz(256fs)。
 * codec 為 I2S slave;寄存器序列參照 ES8311 DS + espressif/es8311 預設路徑;
 * 實機微調記入 §17。 */
#include "es8311_cardputer.h"
#include "esp_log.h"

static const char *TAG = "es8311";
static i2c_master_dev_handle_t s_dev;

static esp_err_t wr(uint8_t reg, uint8_t val)
{
    uint8_t f[2] = { reg, val };
    return i2c_master_transmit(s_dev, f, 2, 100);
}

typedef struct { uint8_t reg, val; } rv_t;

esp_err_t es8311_cardputer_init(i2c_master_bus_handle_t bus, uint32_t sample_rate)
{
    (void)sample_rate;                         /* 鎖定 16k;變更需重推 REG02(§8.1) */
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x18,                /* CE 接地(參考 §3.3) */
        .scl_speed_hz = 100 * 1000,
    };
    esp_err_t r = i2c_master_bus_add_device(bus, &cfg, &s_dev);
    if (r != ESP_OK) return r;

    static const rv_t seq[] = {
        { 0x00, 0x1F }, { 0x00, 0x00 },   /* reset */
        { 0x01, 0xB0 },                   /* ★ clkmgr:bit7 MCLK_SEL=1(SCLK 作時鐘源)
                                           *   + MCLK/BCLK on(0x30)。StickS3 此處 0x30 */
        { 0x02, 0x18 },                   /* ★ clkmgr:pre_div=1,pre_multi=×8
                                           *   (512kHz→4.096MHz=256fs)。StickS3 此處 0x10 */
        { 0x03, 0x10 }, { 0x16, 0x24 }, { 0x04, 0x10 }, { 0x05, 0x00 },
        { 0x06, 0x03 }, { 0x07, 0x00 }, { 0x08, 0xFF },
        { 0x09, 0x0C },                   /* SDP In:16bit I2S */
        { 0x0A, 0x0C },                   /* SDP Out:16bit I2S(錄音不用,寫上無害) */
        { 0x0B, 0x00 }, { 0x0C, 0x00 },
        { 0x10, 0x1F }, { 0x11, 0x7F },   /* system power up analog */
        { 0x00, 0x80 },                   /* power up, slave mode */
        { 0x0D, 0x01 },                   /* system power up */
        { 0x45, 0x00 },
        { 0x37, 0x08 },                   /* DAC ramp */
        { 0x32, 0xBF },                   /* DAC volume(初值;後由 set_volume 覆寫) */
    };
    for (size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
        esp_err_t e = wr(seq[i].reg, seq[i].val);
        if (e != ESP_OK) { ESP_LOGW(TAG, "reg 0x%02x write failed (%d)", seq[i].reg, e); }
        /* codec 寫失敗只 LOGW 不中斷(S3 §21.2-2 慣例:無硬體時仍可跑) */
    }
    ESP_LOGI(TAG, "ES8311 init done (SCLK-derived clock, no MCLK)");
    return ESP_OK;
}

void es8311_cardputer_set_volume(uint8_t vol_0_100)
{
    if (vol_0_100 > 100) vol_0_100 = 100;
    uint8_t code = (uint8_t)((uint32_t)vol_0_100 * 255 / 100);   /* 0x32:0..255 */
    wr(0x32, code);
}
```

**真機無聲排錯順序**(照此逐項,別亂槍):
① I2C 讀回 0x00/0x01 確認晶片在線且未復位;② 示波/邏輯儀量 G41 是否 512kHz
(不是 → I2S slot 配置被動過);③ REG01 試 0xBF(全時鐘域開啟)排除分頻域未開;
④ REG02 依 espressif/es8311 元件 `coeff_div` 表中 `mclk=512000, rate=16000` 行
重推 pre_div/pre_multi/adc_dac_div;⑤ 結論回寫 §17。

### 8.3 I2S 配置(hal_audio.c 內,對照 StickS3)

```c
i2s_chan_config_t ch = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
ch.dma_desc_num = 4; ch.dma_frame_num = 240;          /* 4×240 ≈ 60ms 環形深度(不改!
                                                       * AUDIO_PATH_LATENCY_MS=62 由此推導) */
ESP_ERROR_CHECK(i2s_new_channel(&ch, &s_tx, NULL));
i2s_std_config_t std = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),   /* 無 MCLK 腳,預設即可 */
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = { .mclk = I2S_GPIO_UNUSED,            /* ★ 本板無 MCLK 腳(vs StickS3 G18) */
                  .bclk = I2S_PIN_BCLK, .ws = I2S_PIN_LRCK,
                  .dout = I2S_PIN_DOUT, .din = I2S_GPIO_UNUSED,
                  .invert_flags = { 0 } },
};
ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx, &std));
ESP_ERROR_CHECK(i2s_channel_enable(s_tx));            /* ★ 常開,之後永不 disable */
```

**警告**:`slot_cfg` 一個字都不能改 —— BCLK=512kHz 是 §8.2 REG02=0x18 的前提;
改 slot 寬度/立體聲 = BCLK 變 = codec 時鐘全錯(無聲或變調)。

### 8.4 `main/hal_audio.c`:StickS3 抄法(全文級 diff)

以 `devices/StickS3/main/hal_audio.c` 為底本**整檔拷貝**,
按下列 diff 修改;佇列/排程/樣本級切入/常開填充/audio task 主迴圈**逐行保持**
(三機驗證的凍結參考語義):

```c
/* 檔頭 include:換一刪一 */
-#include "es8311_min.h"
-#include "m5pm1.h"
+#include "es8311_cardputer.h"

/* effective_volume():無電池鉗(NS4150B 1W + 1750mAh,無 StickS3 的 PM1 過流限制) */
 static uint8_t effective_volume(void)
 {
-    uint8_t v = s_vol;
-    if (!m5pm1_is_external_powered() && v > AUDIO_VOL_BATT_MAX) v = AUDIO_VOL_BATT_MAX;
-    return v;
+    return s_vol;
 }

/* hal_audio_init():I2S gpio_cfg/clk_cfg 換 §8.3;codec 換名;刪功放段 */
-    es8311_min_init(board_i2c_bus(), AUDIO_SAMPLE_RATE * 256, AUDIO_SAMPLE_RATE);
-    es8311_min_set_volume(AUDIO_VOL_DEFAULT);
-    m5pm1_amp_set(2);
-    vTaskDelay(pdMS_TO_TICKS(25));
+    es8311_cardputer_init(board_i2c_bus(), AUDIO_SAMPLE_RATE);
+    es8311_cardputer_set_volume(AUDIO_VOL_DEFAULT);
+    /* NS4150B 無使能腳:耳機插入由硬體自動切換揚聲器/耳機,軟體不管(§8.1) */
```

`voice_partition_load()`(subtype 0x40 / "voice" / mmap)與
`xTaskCreatePinnedToCore(audio_task, "audio", 4096, NULL, 15, NULL, 1)` 逐行相同。

### 8.5 AUDIO_PATH_LATENCY_MS(初值 62)與量測

初值構成:DMA 環形 4×240 樣本 = 60ms + ES8311 DAC 群延遲 ~2ms —— 與 StickS3
**同構同值**(同 codec、同 DMA 深度、同 xtensa 阻塞寫)。
量測 = S3 §15.5 錄音對拍法原樣沿用,以已校準的 StickS3(62ms)作對端:
兩機 `-DPK_DEBUG_SOLO=1` 校準建置齊播 V_BEEP,手機錄音置中,Audacity 量 ≥10 對
起始沿 Δt 取中位數,加到落後方;回填 §4.1 並記 §17。預期 Δ≈±數 ms;
通過線:混測中位數 <30ms(產品 §9.2)。
**耳機注意**:量測用揚聲器;插耳機會硬體斷開揚聲器(§8.1),錄音對拍前拔掉耳機。

## 9. 顯示 HAL(hal_display.c)

### 9.1 複用裁定:StickS3 幾乎整檔照抄

本板與 StickS3 是**全產品僅有的兩台 240×135 邏輯佈局**(產品 §2.2),且同為
1.14" ST7789 系 135×240 面板橫置 —— `hal_display.c` 的 UI 物件、座標、`card_text`
花色紅顯、`##` 蓋牌、title_bg 四色、battery_pct 標題後綴、showdown 容器、render 邏輯
**全部原樣照抄** `StickS3/main/hal_display.c`。init 段僅三處差異:

| 參數 | 本板值 | StickS3 值 | 說明 |
|---|---|---|---|
| 引腳 | MOSI=35/SCLK=36/CS=37/DC=34/RST=33/BL=38 | 39/40/41/45/21/38 | 全走 §4.1 宏,程式碼零改動 |
| lvgl buffer | **`LCD_H_RES * 40`,`double_buffer = false`(19.2KB)** | 240×40 雙緩衝(38.4KB) | ★ 無 PSRAM,照 zuowei 慣例單緩衝(§13.1 立帳);UI ≤10fps 靜態物件改屬性,單緩衝足 |
| invert/swap/mirror/gap | true/true/(false,true)/(40,52) **推導初值** | 同值【部分實測】 | 同款面板同參數起步;**§9.4 自檢後定案**,±1 偏差回寫 §17 |

其餘(`spi_mode=0`、40MHz、`swap_bytes=true`、`buff_dma=true`、
lvgl_port priority=4/stack=8192/**affinity=1**、背光 LEDC 5kHz/10-bit/ch0、
`bl_init_off()` 佔空 0 起步防上電雪花)照 StickS3 不動。

### 9.2 背光 G38 = RGB LED 電源(本板特有注意)

G38 同時是 LCD 背光使能與 Stamp-S3A 板載 RGB LED 的電源使能(參考 §3.7)。後果:
1. **PWM 佔空下限 10% 硬性**(產品 §2.2):StickS3 版
   `hal_display_set_brightness()` 已含 `if (pct < 10) pct = 10;` 鉗制,照抄即滿足
   ——雙保險:共用選單亮度檔位最低 30%,正常路徑到不了 10% 以下;
2. RGB LED 資料腳本專案不驅動(LED 不初始化),電源被 PWM 斬波無可見副作用;
   若真機觀察到 LED 微光/閃爍,屬電源紋波點亮,記錄即可,不影響驗收。

### 9.3 `ledc_channel_config_t` 提醒(zuowei §16.1-2)

合併背光碼時 `.flags = { .output_invert = false }` 用 designated init 寫全
(本板非反相,漏了不影響行為,但別掉——四機同構)。

### 9.4 旋轉自檢(首次點亮必做;= S3 §14.4 原方法)

燒測試畫面:全屏 1px 紅框 + 左上 "TL" 標籤。驗:
① 四邊完整(缺邊/移位 → 微調 `LCD_GAP_X/Y` ±1..2);
② **鍵盤朝自己、螢幕在上方持機**時 "TL" 在左上(顛倒 → `LCD_MIRROR_X/Y` 同時取反);
③ 紅色是紅色(發青 → invert_color 或 swap_bytes)。
結果回寫 §17 並在 §4.1 移除「推導初值」標註。

## 10. 電池 HAL(hal_battery.c 全文)

G10 = **ADC1**_CH9(GPIO10),與 Wi-Fi 零衝突(xingzhi 的 ADC2 快取降級戲碼本板沒有);
100k/100k 分壓恆接,`Vbat = 讀數 × 2`(參考 §3.1)。**無充電狀態腳**(TP4057 STAT
未引出)→ `hal_battery_charging()` 恆 false(產品 §2.4:不可知回 false)。
**無 USB 在位檢測腳** → 插 USB 充電時 ADC 讀的是充電電壓(≈4.2V),百分比虛高
(顯示 ~100%),屬已知限制記入 §17;不像 StickS3 有「讀 0 須回 0xFF」的坑。

```c
/* hal_battery.c -- G10 ADC1 oneshot + 100k/100k 分壓 ×2(指南 §10)。
 * 整數百分比運算(zuowei §16.1-4 慣例);1s 快取;充電狀態不可知恆 false。 */
#include "hal/hal_battery.h"
#include "board_config.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_timer.h"
#include <stdbool.h>

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static bool s_inited;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void ensure_init(void)
{
    if (s_inited) return;
    adc_oneshot_unit_init_cfg_t ucfg = { .unit_id = BAT_ADC_UNIT };
    if (adc_oneshot_new_unit(&ucfg, &s_adc) != ESP_OK) return;
    adc_oneshot_chan_cfg_t ccfg = { .atten = BAT_ADC_ATTEN, .bitwidth = ADC_BITWIDTH_DEFAULT };
    adc_oneshot_config_channel(s_adc, BAT_ADC_CHANNEL, &ccfg);
    adc_cali_curve_fitting_config_t cal = {
        .unit_id = BAT_ADC_UNIT, .chan = BAT_ADC_CHANNEL,
        .atten = BAT_ADC_ATTEN, .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cal, &s_cali) != ESP_OK) s_cali = NULL;
    s_inited = true;
}

static int read_bat_mv(void)                        /* 8 均值 + 校準 + ×2 */
{
    int acc = 0, n = 0;
    for (int i = 0; i < 8; i++) {
        int raw, mv;
        if (adc_oneshot_read(s_adc, BAT_ADC_CHANNEL, &raw) != ESP_OK) continue;
        if (s_cali && adc_cali_raw_to_voltage(s_cali, raw, &mv) == ESP_OK) { acc += mv; n++; }
    }
    if (n == 0) return -1;
    return (acc / n) * BAT_DIVIDER_NUM;             /* 全整數,無 float 轉型問題 */
}

uint8_t hal_battery_pct(void)
{
    ensure_init();
    if (!s_inited) return 0xFF;                     /* ADC 不可用:未知(協定 §5,勿回 0) */
    static uint32_t cache_t; static uint8_t cache_v = 0xFF;
    if (cache_v != 0xFF && now_ms() - cache_t < 1000) return cache_v;   /* 1s 快取 */
    int mv = read_bat_mv();
    if (mv < 0) return cache_v;                     /* 瞬時失敗:回快取(可能 0xFF) */
    int pct = (mv - BAT_EMPTY_MV) * 100 / (BAT_FULL_MV - BAT_EMPTY_MV);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    cache_v = (uint8_t)pct;
    cache_t = now_ms();
    return cache_v;
}

bool hal_battery_charging(void) { return false; }   /* TP4057 STAT 未引出(參考 §3.1) */
```

線性 3300–4200 折線沿 zuowei/xingzhi 驗證慣例(協定只拿它做 <15 低電豁免與顯示);
嫌跳變難看再換 S3 §13.4 的 OCV 折線,記 §17。分壓恆接(200kΩ 級)不增待機負擔;
惰性初始化讓 app_main 不需顯式 battery init。

## 11. hal_power.c(空殼 + 註記)

本板側面電源開關 SW1 = **雙 P-MOSFET 物理切斷電池輸出**(參考 §3.1,OFF 待機 0.23µA)
—— 關機瞬間主控直接斷電,**軟體不可攔截、不可感知**,與 StickS3(PM1 雙擊)同類。
`StickS3/main/hal_power.c` **原樣拷貝**,只改註解:

```c
/* hal_power.c -- 關機攔截(產品 §2.5)。
 * Cardputer-Adv 關機 = 側面電源開關物理切斷(P-MOS),軟體攔截不到;
 * app_prepare_poweroff() 永不被板級呼叫,協定 §9.5 故障接管兜底(產品層已知限制)。
 * R11:main 提供單一 no-op 定義,common 不定義以免重複符號。 */
#include "hal/hal_power.h"
void app_prepare_poweroff(void) { }
```

驗收含義:本機 Master 被關機 = 拔電池等級的消失,§16 階段 3 的接管壓測對本板
與 StickS3 同等重要。

## 12. hal_misc.c(裝置類別 + NVS 設定)

`StickS3/main/hal_misc.c` **整檔照抄**,只改一行:

```c
uint8_t pk_board_device_class(void) { return 3; }   /* 協定 §8.1:3 = cardputer-adv */
```

其餘(`hal_rand`=esp_random、`hal_yield_watchdog`=vTaskDelay(1)、
`hal_settings_load/save_*` 走 NVS namespace `poker` key `vol`/`bri`)零改動 ——
R10 持久化語義四機一致,系統選單(BATT %/VOLUME/BRIGHTNESS/LEAVE)在共用
app_flow,本機自動獲得。

## 13. 不使用的硬體(明列,全部不初始化)

原則:**不初始化 = 最強隔離**。本板外設多、總線共享重(參考 §5),任何「順手 init」
都可能引入 SPI/I2C 副作用。逐項裁定:

| 硬體 | 位置 | 裁定 | 理由/副作用防護 |
|---|---|---|---|
| microSD | 共享 SPI(G40/G14/G39),CS=G12 | 不初始化 SPI host、不掛 VFS;**G12 開機拉高**(§4.2 bus_hygiene) | 撲克無存儲需求;CS 拉高 = 永不選中,卡在位也不上匯流排 |
| Cap LoRa1262(SX1262+GNSS) | 同一共享 SPI,CS=G5;UART G13/G15 | 不初始化;**G5 開機拉高** | 參考 §13.6 Launcher 經驗:「Adv 上 G5 需拉高以避免與 SD 卡 SPI 衝突」。**結論**:即便本專案兩者皆不用,只要 Cap 模組物理在位、NSS 懸浮,SX1262 可能因雜訊誤選中而駕馭 MISO/產生功耗 —— 兩個 CS 一律開機鎖高,3 行代碼的保險(§4.2)。GNSS UART 不開,G13/G15 保持復位態 |
| IR 發射 | G44(串 22Ω,高電平點亮) | 不初始化,**保持復位態(輸入)** | G44=預設 U0RXD、G43=U0TXD(已被 LRCK 佔用)→ **嚴禁啟用 UART0**;控制台走 USB-Serial-JTAG(§14.1)。G44 復位態浮空不點亮 IR(驅動需主動拉高) |
| BMI270 IMU | 共享 I2C 0x69 | 不初始化 | I2C 裝置不被定址就不上匯流排,零副作用。傾斜 incdec 加分項被鍵盤方向鍵(§7)完全取代且更精準;INT 腳未接主控(參考 §3.4)本就只能輪詢,不值得。**TODO 可留**:若日後想要,仿 S3 §16.2(100ms 輪詢 Y 軸、>25° 觸發)+ 本板位址 0x69 |
| 麥克風(MEMS→ES8311 ADC) | ES8311 MIC1P/N | 不啟用:§8.2 序列只上電 DAC 路徑,不開 ADC/PGA;I2S 不建 RX 通道,G46 不配 | 撲克無錄音需求;少一路 DMA 少一份緩衝(無 PSRAM 板上都是錢) |
| RGB LED | 資料腳(Stamp-S3A 內部),電源=G38 | 不初始化資料腳 | §9.2:電源隨背光 PWM,無驅動即無顯示 |
| Grove(G1/G2)/ EXT-14P 其餘腳 | — | 全部不碰 | 保持復位態 |

## 14. sdkconfig.defaults 與 partitions.csv(全文)

### 14.1 `poker/sdkconfig.defaults`

```
# --- 目標晶片:ESP32-S3FN8(雙核 240MHz;flash 封裝內 8MB)---
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y

# --- 控制台必須走 USB-Serial-JTAG(USB-C 原生 USB):
#     UART0 預設腳 G43/G44 已被 I2S LRCK 與 IR 佔用(參考 §5.3),嚴禁 UART0 ---
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y

# --- Flash 8MB QIO + 自訂分區(容 app + voice)---
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="8MB"
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"

# --- ★ 無 PSRAM(ESP32-S3FN8):嚴禁出現任何 CONFIG_SPIRAM* 行 ---
#     (勿照抄 StickS3/xingzhi 的 SPIRAM 三行;誤開會在啟動時 PSRAM 探測失敗甚至
#      佔用引腳。本檔以「不出現」為正確狀態,建置後可用
#      grep -c SPIRAM build/sdkconfig 應僅見 is-not-set 註釋行交叉檢查)

# --- 主棧 & FreeRTOS(產品 §8.1:無 opus,8KB 足;1000Hz 供 10ms 去抖/20ms 音訊節拍)---
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
CONFIG_FREERTOS_HZ=1000

# --- LVGL 字體(四機統一三檔:14/20/28)---
CONFIG_LV_FONT_MONTSERRAT_14=y
CONFIG_LV_FONT_MONTSERRAT_20=y
CONFIG_LV_FONT_MONTSERRAT_28=y

# --- Wi-Fi 僅用於 ESP-NOW:關 AMPDU 省內存(demo 已驗證;無 PSRAM 板尤其要省)---
CONFIG_ESP_WIFI_AMPDU_TX_ENABLED=n
CONFIG_ESP_WIFI_AMPDU_RX_ENABLED=n
```

QIO 說明:S3FN8 封裝內 flash 官方支援 QIO(xingzhi 同為 QIO 且經 R14 驗證顯示層
`dio` 字樣無害);若首次燒錄後 bootloader 報 flash 讀錯,降 DIO 並回寫 §17
(zuowei 的 DIO 是其外掛 Zbit flash 的特例,勿先入為主)。

### 14.2 `poker/partitions.csv`(與前三台逐位元組同構)

```
# Name,     Type, SubType,  Offset,   Size,     Flags
nvs,        data, nvs,      0x9000,   0x6000,
phy_init,   data, phy,      0xf000,   0x1000,
factory,    app,  factory,  0x10000,  0x3E0000,
voice,      data, 0x40,     0x3F0000, 0x120000,
```

voice 分區:subtype **0x40**、名稱 **"voice"**、偏移 **0x3F0000** —— 四機一致
(共用 `voice_partition_load()` 的查找參數與燒錄命令因此四機通用)。

## 15. 建置、燒錄、voice.bin

### 15.1 建置

```bash
. ~/.espressif/v6.0.1/esp-idf/export.sh
export PATH="$HOME/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin:$PATH"

cd devices/Cardputer-Adv
idf.py set-target esp32s3            # 僅首次
idf.py build                         # 正式建置
idf.py build -DPK_DEBUG_SOLO=1       # 單機冒煙建置
idf.py build -DPK_KEYMAP_DUMP=1      # 鍵位校準建置(§6.4;可與 SOLO 疊加)
idf.py -p <PORT> flash monitor
```

(`PK_KEYMAP_DUMP` 需在 main/CMakeLists.txt 加對應透傳:
`if(PK_KEYMAP_DUMP) target_compile_definitions(${COMPONENT_LIB} PRIVATE PK_KEYMAP_DUMP=1) endif()`,
放 `idf_component_register` 之後。)

### 15.2 進下載(燒錄)模式(參考 §8.1;救磚/USB 不識別時)

1. 側面電源開關撥到 **OFF**;
2. **按住 G0(鍵盤左上外側的 BOOT/BtnA 鍵,不是鍵盤矩陣裡的鍵)**;
3. 插 USB-C 後鬆開 → ROM 下載模式。

正常運行時 USB-Serial-JTAG 可直接燒錄,上述步驟僅兜底。
注意:電源開關 OFF = 電池斷路,但 USB 供電下主控仍可上電 —— 這正是此流程可行的原因。

### 15.3 voice.bin(四機共用同一份,只在語音資產變更時單獨燒)

```bash
python $IDF_PATH/components/partition_table/parttool.py -p <PORT> \
    write_partition --partition-name=voice --input ../../common/voice/voice.bin
# 或:esptool --port <PORT> write-flash 0x3F0000 ../../common/voice/voice.bin
```

`common/voice/voice.bin` 已生成入版控(S3 §21.4 實測 418,390 B / 74 片段),
1.125MB 分區餘裕充足。啟動時 `voice_init` 驗 magic,失敗 → 播放靜默 no-op,遊戲照跑。

## 16. 記憶體預算與分階段驗收

### 16.1 記憶體預算(S3FN8 512KB SRAM、無 PSRAM —— 對產品 §8.1 立帳)

| 項目 | 產品 §8.1(C3 基準) | 本板估算 | 依據 |
|---|---|---|---|
| Wi-Fi/ESP-NOW 棧 | ~55 KB | ~55 KB | 關 AMPDU(§14.1);S3 與 C3 同量級 |
| FreeRTOS+系統+主棧 | ~30 KB | ~32 KB | 主棧 8192 + pbus 6144 + audio 4096 + kbd 3072 + btn_g0 2048 + LVGL 8192(§3) |
| LVGL partial buffer | ~24 KB | **~20 KB** | 240×40×2B **單緩衝** 19.2KB(§9.1)+ 物件池 |
| pbus 事件日誌 | ~7 KB(32 槽) | ~13.5 KB(64 槽) | S3 §21.5:現行核心共用 64 槽,凍結照付 |
| pbus 狀態+OOO+佇列 | ~3 KB | ~9 KB | ooo 8×254B + rxq 24×258B + txq(S3 實測配置) |
| 語音解碼緩衝 | 4 KB | 4 KB | 拉取式(共用) |
| core+app 邏輯 | ≤60 KB | ≤60 KB | 共用碼子預算 |
| **合計** | | **~194 KB** | **對 512KB SRAM 餘裕 >250 KB**(扣 cache 配置後仍遠超產品要求的 ≥100KB)✓ |

結論:本板比 zuowei(400KB + 19KB vb6824)寬鬆一檔 —— **前提是守紀律**:
LVGL 單緩衝、不開 SPIRAM 選項、不加錄音/SD 緩衝、`static` 分配優先
(zuowei 慣例;所有本指南全文代碼均已 static/定長,無執行期 malloc 大塊)。
app 映像預期 ~1.14MB(三機基準 1,128,560–1,174,528 B 內插:本板驅動集 ≈ StickS3
去 m5pm1/imu 加 tca8418,量級持平),factory 3.87MB 餘裕 >70%。

### 16.2 PK_DEBUG_SOLO

同 S3 §20.3,零板級差異:`idf.py build -DPK_DEBUG_SOLO=1` → 單機自建桌跑通全流程。
本板 solo 冒煙的板級重點:鍵盤 Enter/`.` 全畫面走通、V_BEEP/數字播報(驗 §8.2 時鐘)、
G0 並聯 OK、系統選單 BATT %(電池供電顯真值;插 USB 充電顯 ~100%,§10 已知限制)。

### 16.3 分階段驗收(對照產品 §9;本板特有項加粗)

| 階段 | 建置 | 內容 | 對應產品 §9 |
|---|---|---|---|
| **-1 鍵位校準** | KEYMAP_DUMP | **§6.4:撲克全部用鍵對照絲印核對;結果回寫 §17。未過不得進階段 0** | — |
| 0 冒煙 | SOLO | 開機 ≤3s 進 SCANNING;自建桌全流程;全畫面可達;**Enter/`.`/G0 三鍵語義正確、長按 Enter 出系統選單(600ms 手勢經 TCA8418 兩沿事件驗證)**;V_BEEP/數字播報正常(**= ES8311 SCLK 時鐘模式一次驗證**);**插/拔耳機:揚聲器↔耳機硬體自動切換,軟體無感**;亮度三檔即時生效(**最低檔不黑屏 —— G38 下限 10% 鉗制**);音量三檔重啟保留 | — |
| 1 成桌與儀式 | 正式 ×2–3 台(混型) | 10s 窗口成桌;齊播 confirm dealer;搶莊唯一;逐位排座;**與 StickS3 錄音對拍(§8.5)回填 AUDIO_PATH_LATENCY_MS** | 1、2、3 |
| 2 單街下注 | 正式 ×3 | 籌碼/盲注(含本地預驗證與 REJECT 回饋);preflop 一輪動作正確;二確認;**(加分,若共用層已接線)數字鍵直輸籌碼:鍵入 250→畫面即顯→OK 送出;Backspace 修改;`.` 放棄回輪循**;**(加分同上)Fn+方向鍵 ±1** | 4、5、6、7 |
| 3 全流程 | 正式 ×3+ | 四街+攤牌逐位翻牌+側池;換莊;**側面開關直接關掉 Master → ≤3s 偵測、≤6s 接管**(本板關機不可攔截,§11);斷線回歸落點;中途加入/淘汰/補籌;10 人降級顯示 | 8–15 |
| **4 四機齊播矩陣** | 正式 ×4(四型各一) | **全專案收官項:StickS3 + zuowei + xingzhi + 本板同桌;四機齊播 V_CONFIRM_DEALER / V_DEAL / 逐街道語音,兩兩錄音對拍全 6 對錯位 <30ms;跑 ≥1h 長局抽查漂移;結果回寫四份指南各自 §「經驗回寫」** | 2、§8.2 齊播精度 |

---

## 17. 實作經驗回寫(供全專案收官與後續迭代)

> **給實作 agent 的指示**:完成 Cardputer-Adv 實作(或任何一個驗收階段)後,把經驗按
> 下列小節追加到本章:**硬體無關**的(共用碼、編譯、文件歧義)同時回寫 S3 指南 §21;
> **本板硬體相關**的坑寫回 `hardware-reference.md` §12(坑點匯總)。
> 涉及 poker_core 介面的變更須走 S3 §21 升版流程(介面凍結,只准加不准改;
> 文件歧義新裁定沿 R 編號續編,**目前用至 R14,本板自 R15 起**)。
> 特別要求回填的實測值/結論:
> - §6.4 鍵位映射校準結果(最終 `key_of()` 換算式或查表;row-major 假定是否成立);
> - §8.2 ES8311 SCLK 時鐘模式寄存器序列真機驗證結果(REG01=0xB0 / REG02=0x18 是否即通);
> - §8.5 `AUDIO_PATH_LATENCY_MS` 實測值與混型齊播中位數;
> - §9.4 顯示旋轉/gap 自檢定案值(推導初值 (40,52)/mirror_y 是否命中);
> - §9.2 背光 PWM 下的 RGB LED 觀察結論;
> - §10 充電時電量虛高的實際觀感(是否需要 UI 側「>95% 且無充電腳 → 顯示 CHG?」裁定);
> - §14.1 QIO 是否一次通過;
> - 共用層 incdec/get_number 接線(若本輪完成)的介面延伸記錄(§7 現狀說明作廢與否);
> - 各任務棧 high-water(尤其 kbd 3072)與 app 映像尺寸
>   (對照 StickS3 1,135,376 / zuowei 1,174,528 / xingzhi 1,128,560 B);
> - 階段 4 四機齊播矩陣全量數據(全專案唯一的四型混測,務必留檔)。

> 本節由實作 agent 回填。第一輪 = **建置級交付**(2026-07-21,cardputer;
> xtensa esp-15.2.0_20251204 / IDF v6.0.1)。標「【建置級】」= 靜態/編譯驗證,
> 標「【待實機】」= 需真機量測回填。硬體無關經驗已同步 StickS3 §21.7。

### 17.1 建置結果(第一輪)

- **正式建置、`-DPK_DEBUG_SOLO=1`、`-DPK_KEYMAP_DUMP=1`(可與 SOLO 疊加)三種組合全綠**,
  自訂碼零 warning。正式 bin 內 `strings | grep SOLO` = 0;SOLO bin 內 = 1(PUBLIC 編譯
  定義由 poker_core 傳遞到 main,`#ifdef PK_DEBUG_SOLO` 生效,main/CMakeLists 無需自加透傳)。
- **`PK_KEYMAP_DUMP` 透傳**:依 §15.1 在 main/CMakeLists.txt `idf_component_register` 之後加
  `if(PK_KEYMAP_DUMP) target_compile_definitions(${COMPONENT_LIB} PRIVATE PK_KEYMAP_DUMP=1) endif()`
  ——已落地,建置驗證 dump 分支正確編入。
- **無 PSRAM 確認**:`sdkconfig.defaults` 不含任何 `CONFIG_SPIRAM*`;建置後 `grep -i SPIRAM sdkconfig`
  僅見 `CONFIG_SOC_SPIRAM_SUPPORTED=y`(SoC 能力位,唯讀)與 `# CONFIG_SPIRAM is not set`,
  無任何啟用行。§14.1 的「以不出現為正確狀態」交叉檢查通過。
- **app 映像 = 1,136,832 bytes**(factory 3.87MB,餘 ~71%)。四機對照:
  xingzhi 1,128,560 < StickS3 1,135,376 < **cardputer 1,136,832** < zuowei 1,174,528。
  本板略高於 StickS3(去 m5pm1/imu、加 tca8418 + esp_adc 電池路徑,量級持平,符 §16.1 預期)。
- **凍結核心零改動即通過**(第四機驗證):未撞任何 `-Werror`;R13(pbus_session 堆疊溢位)修復後的
  核心在本板 xtensa 一次過,**未加任何 `-Wno-error` 墊片**(遵 §1-2)。**未發現新核心 bug。**

### 17.2 指南程式碼一處補完(§6.3 tca8418.c)

- §6.3 的 `tca8418.c` 全文使用了 `ESP_RETURN_ON_ERROR`,但檔頭 include 清單(及 `tca8418.h`)
  只含 `esp_err.h`,未含定義該巨集的 **`esp_check.h`**。照抄原文會 implicit-declaration 編譯失敗。
  **落地修正**:本板 `tca8418.c` 補一行 `#include "esp_check.h"`(其餘逐字照抄)。純板級補完,
  不涉核心。建議修訂指南 §6.3 include 清單補上此行。

### 17.3 待實機回填清單(本輪均為【建置級】,尚無硬體)

以下全部**僅通過編譯/靜態驗證**,需真機依 §16.3 階段 -1/0 校準後回填:

- ~~**§6.4 鍵位映射**:`hal_input.c::key_of()` 仍為 **row-major 假定**(`idx=r*8+c` → `s_logical[idx/14][idx%14]`),
  **未經實機驗證**~~ → **已定案(§17.5 R16,2026-07-21 真機):row-major 假定不成立**,
  解碼表依官方源碼推導、真機驗證通過,現駐 `tca8418.c::s_keymap[7][8]`。
- **§8.2 ES8311 SCLK 時鐘模式**:REG01=**0xB0**(MCLK_SEL=1 取 SCLK)、REG02=**0x18**(pre_multi ×8,
  512kHz→4.096MHz=256fs)已編入;是否即通、無聲/雜音排錯結論待實機(排錯順序見 §8.2 末)。
- **§8.5 `AUDIO_PATH_LATENCY_MS`**:仍為計算初值 **62**;與 StickS3 錄音對拍中位數待量測。
- **§9.4 顯示旋轉/gap**:`invert/swap_xy/mirror_x/mirror_y = true/true/false/true`、`gap=(40,52)`
  為沿 StickS3 的推導初值,LVGL 改為**單緩衝**(`double_buffer=false`,240×40×2B=19.2KB,無 PSRAM);
  四邊/方向/紅色自檢待實機,±偏差回填 §4.1 並移除「推導初值」標註。
- **§9.2 背光 PWM 下 RGB LED 觀察**:G38 下限 10% 鉗制已在 `hal_display_set_brightness()`;
  LED 微光/紋波觀察待實機。
- **§10 充電時電量虛高**:無 USB 在位腳,插 USB 讀充電電壓(~4.2V)顯 ~100%;UI 側是否需
  「>95% 且無充電腳 → 顯示 CHG?」待實機觀感裁定。
- **§14.1 QIO** 是否一次燒錄通過;各 task 棧 high-water(尤其 kbd 3072)待實機抓取。
- **§7 現狀**:`hal_input_set_incdec_cb` / `hal_input_get_number` 已完整實作(符號存在、加分邏輯就緒);
  共用層 `app_flow.c` 仍未消費(grep 確認 common/src 零引用),接線須走 S3 §21 介面延伸流程。
  兩鍵心智(Enter=OK / `.`=NEXT / G0 並聯 OK)完整可玩;方向鍵/數字直輸為休眠加分項。
- **§16.3 階段 1–4**(成桌、下注、全流程、四機齊播矩陣)全部待實機。

### 17.4 R15:LVGL「黑屏」根因 —— G38 背光 PWM 頻率(2026-07-21,真機)

**症狀**:面板初始化後直推 0xFFFF 白屏正常;LVGL 一啟動螢幕轉全黑並維持。
`double_buffer` true/false 皆黑;同底本 hal_display.c 在 StickS3 完全正常。

**排查(儀器化 flush)**:在 `lvgl_port_add_disp()` 後掛 `LV_EVENT_FLUSH_START`
事件,逐次統計 flush 區域與像素直方(zero/bg/white/other)。真機日誌證明:
- LVGL 內容正確:`flush#5-8` 有 white=29/239、other=178/231/302(標題字、牌面「--」);
  進 SCANNING 後每 ~100ms 全屏 4 條帶重刷,white=149/other=778 穩定存在;
- 傳輸路徑正確:LVGL 啟動 3 秒後繞過 LVGL 直呼 `esp_lcd_panel_draw_bitmap`
  推白帶回 `ESP_OK`(與開機白屏同一路徑);
- LVGL task 無 crash、flush 計數持續遞增(#400+)。
  → 像素資料確實一直在進面板 GRAM,「黑」的不是資料,是**背光**。

**根因**:本板 **G38 不是單純背光腳**,是「LCD 背光 + Stamp-S3A RGB LED」共用
電源使能(PWR_EN,經負載開關;參考 §3.7)。沿 StickS3 照抄的 **LEDC 5kHz** PWM
該電源軌跟不上,軌起不來 → 背光滅。時間線完全吻合:診斷版白屏期間 G38 為
恆定電平(有效 DC),`hal_display_set_brightness(80)` 首次輸出 5kHz PWM 的瞬間
= LVGL 啟動瞬間 → 被誤判為「LVGL 把畫面刷黑」。M5GFX 對 Cardputer/ADV 的
`Light_PWM` 特意用 **freq=256Hz、offset=16**(官方對此電路的既定參數),
StickS3 的背光是普通電晶體,5kHz 無感,故同碼異果。

**修法(最小)**:`hal_display.c::bl_init_off()` LEDC `freq_hz` 5000 → **256**
(10-bit 解析度不變;10% 下限鉗制照舊,高於 M5GFX offset 16/255≈6%)。
同時撤銷先前「單緩衝黑屏」誤判的改動:恢復 `double_buffer=false`(無 PSRAM
設計原意);兩種緩衝模式在儀器化下均正確產出並傳輸內容,與黑屏無關。

**§9.4 方向/gap 定案(使用者目視三輪,同日)**:背光修復後畫面可見,但方向
**180° 顛倒** → 依 StickS3 §14.3 預案「兩 mirror 同時取反」:
`mirror_x/mirror_y = false/true → true/false`(swap_xy=true、invert=true 不變)。
gap_y 按教科書公式「MX 反轉 → offset = 240−135−52 = 53」改為 53

**供他板參考(硬體相關,已同步 hardware-reference.md §12 精神)**:
背光腳兼任電源使能(load switch EN)的板子,LEDC 頻率不可照抄「普通背光」
的 5kHz;先查 M5GFX 同板 `Light_PWM` 的 freq/offset 再定。判官式教訓:
「白屏可見→LVGL 後變黑」不必然是顯示資料/旋轉/色序問題,先分離「資料進沒進
GRAM」與「背光在不在」兩個自由度(flush 儀器化 + 直推 draw_bitmap 各驗一半)。

### 17.5 R16:TCA8418 鍵位映射定案 —— row-major 假定不成立(2026-07-21,真機)

**症狀**:按 Enter 回報 KEY_NEXT;G0(並聯 OK)正常 → 鎖定 (row,col)→鍵 解碼表。

**根因**:實作時的 row-major 推定(`idx = r*8 + c` 後橫掃 4×14 邏輯佈局)與實際
電氣佈線**正交**。真實幾何(官方源碼):**每條電氣 ROW 對應鍵盤面上「相鄰兩豎條」
(4 實體行 × 2 邏輯列)**——COL0-3 = 左條(邏輯列 `2*row`)由頂至底,COL4-7 =
右條(邏輯列 `2*row+1`)。換算式:

```
x(邏輯列 0-13) = row*2 + (col>3 ? 1 : 0)
y(邏輯行 0-3,0=頂列)= col % 4
鍵 = 佈局表[y][x](參考 §11 普通層)
```

症狀完全吻合:Enter 的電氣座標 (6,6) 在舊表算得 idx=54 → `'/'`(被靜默吞掉);
而其正上方的 `\` 鍵 (6,5) 舊表算得 idx=53 → `'.'` = KEY_NEXT——「按 Enter 出 NEXT」
實為 `\`/Enter 區域錯位的表現。

**依據(兩處官方實作交叉一致)**:
1. `github.com/m5stack/M5Cardputer` @master(v1.2.x,ADV 支援)
   `src/utility/Keyboard/KeyboardReader/TCA8418.cpp::remap()` + `Keyboard.h::_key_value_map[4][14]`;
2. `github.com/m5stack/M5Cardputer-UserDemo` @CardputerADV
   `main/hal/keyboard/keyboard.cpp`(remap 與鍵值表完全同構)。
   事件碼→電氣座標兩邊同式:`code=(ev&0x7F)-1; row=code/10; col=code%10`(與本板驅動一致)。

**落地**:完整 56 鍵解碼表 `s_keymap[7][8]` 與 `tca8418_key_decode()` 移入
`tca8418.c`(附來源註釋);`hal_input.c` 刪除 `key_of()`/`s_logical`,只留撲克語義。
**邏輯映射同輪改版(使用者裁定)**:方向鍵**免 Fn**(絲印 `;`=↑ `,`=← `.`=↓ `/`=→),
`.` 與 `/` = KEY_NEXT(raw press/release 進手勢層)、`;` 與 `,` = incdec(−1)
(按下沿);Enter=KEY_OK、G0 並聯 OK 不變;Fn 層取消(s_fn_down 移除)。
注意:`hal_input_set_incdec_cb` 目前 common 仍零消費(§17.3),`;`/`,` 解碼正確
但無 UI 效果,待共用層接線(S3 §21 介面延伸流程)。

**真機驗證(PK_KEYMAP_DUMP=1,同日)**:(6,6)→0x0D→KEY_OK、(6,3)→`/`→KEY_NEXT、
(5,7)→`.`→KEY_NEXT、(1,4)→`3`(數字直輸)全部命中新表;開機日誌乾淨,自動入桌正常。

**完整 56 鍵電氣→鍵值表(普通層;行=ROW0-6,列=COL0-7)**:

| | C0 | C1 | C2 | C3 | C4 | C5 | C6 | C7 |
|---|---|---|---|---|---|---|---|---|
| **R0** | `` ` `` | TAB | FN | CTRL | 1 | q | SHIFT | OPT |
| **R1** | 2 | w | a | ALT | 3 | e | s | z |
| **R2** | 4 | r | d | x | 5 | t | f | c |
| **R3** | 6 | y | g | v | 7 | u | h | b |
| **R4** | 8 | i | j | n | 9 | o | k | m |
| **R5** | 0 | p | l | , | - | [ | ; | . |
| **R6** | = | ] | ' | / | BS | \ | ENTER | SPACE |

**供他板參考(硬體無關教訓,精神同步 S3 §21)**:掃描晶片的電氣矩陣與絲印佈局
的對應**不可用「row-major 直覺」推定**——廠商為省佈線常按「豎條分組」走線;
有官方驅動源碼時,先抄其 remap 再上機驗證,一次到位。
