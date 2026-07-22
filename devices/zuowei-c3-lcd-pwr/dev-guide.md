# zuowei-c3-lcd-pwr 德州撲克遊戲機 軟體開發指南 v1.0

> 本指南是四種裝置中的**第二份**開發指南。全專案共用佈局已由
> `../StickS3/dev-guide.md`(下稱「S3 指南」)§1–§5 定義,且 **StickS3 首版實作已完成、
> 共用核心 `common/components/poker_core/` 介面凍結**(S3 指南 §21)——
> 本指南**只覆蓋 zuowei-c3 的板級/HAL 實作與整合**,不重新設計、不修改任何共用部分。
>
> 規格權威層級(衝突時由高到低):
> 1. `../../common/PROTOCOL.md` v1.1 —— 協定層唯一權威
> 2. `../../common/PRODUCT-SPEC.md` v1.2 —— 架構/HAL/畫面/語音唯一權威(v1.2 含電量/亮度/音量 UX)
> 3. **磁碟上的 `common/components/poker_core/include/**` 標頭 = 凍結介面實體**;
>    與任何文件(含 S3 指南 §3 引文)不符時**以磁碟為準**(差異清單見本指南 §1.2)
> 4. `zuowei-c3-lcd-pwr/hardware-reference.md` + `README.md` —— 本板硬體唯一權威(標【实测】者已實機驗證)
> 5. S3 指南 §4 裁定(R1–R9)與 §21 回寫(R10–R12、WHOLE_ARCHIVE 等)—— **必遵**
> 6. 本指南 —— 只補「zuowei-c3 實作者會卡住的地方」,引用以「協定 §x」「產品 §x」「S3 §x」表示。

---

## 0. 讀者與範圍

- 讀者:負責實作 `zuowei-c3-lcd-pwr/` 的 coding agent / 韌體工程師,
  假定已通讀協定/產品文件與 S3 指南(尤其 §1–§5、§21)。
- 產出物:**只有** `zuowei-c3-lcd-pwr/` 一個 IDF 專案(main/ = HAL 實作 + 板級)。
  `common/` 一個位元組都不改;若實作中發現非改不可,先停下,走 S3 §21 回寫升版流程。
- 本板四大特徵(相對 StickS3):**單核 RISC-V、400KB SRAM 無 PSRAM、音訊走 VB6824 UART
  而非 I2S、GPIO13 軟體電源自鎖(可攔截關機!)**。前兩者影響記憶體與任務佈局(§3、§12),
  第三者是本指南核心難點(§6),第四者讓本機成為四機中**唯一能走優雅交接關機**的裝置(§7)。

---

## 1. 前置必讀與凍結邊界

### 1.1 凍結介面清單(只准實作,不准增改)

依 S3 §21「`common/` 一經兩機互通驗證,介面凍結,之後只准加不准改」。本裝置要做的
**全部**共用介面對接工作,就是提供下列六個 HAL 標頭的符號實作(標頭原文見磁碟):

| 標頭 | 本機須實作的符號 | 落地檔 |
|---|---|---|
| `hal/hal_display.h` | `hal_display_init/render/render_showdown/set_brightness` | `main/hal_display.c`(§8) |
| `hal/hal_input.h` | `hal_input_init`、`hal_input_set_incdec_cb`(收下不觸發)、`hal_input_get_number`(回 -1) | `main/hal_input.c`(§9) |
| `hal/hal_audio.h` | `hal_audio_init/play/play_at/stop/set_volume/path_latency_ms` | `main/hal_audio.c`(§6) |
| `hal/hal_battery.h` | `hal_battery_pct/charging` | `main/hal_battery.c`(§10) |
| `hal/hal_power.h` | `app_prepare_poweroff`(R11:main 提供單一定義,本機是**真實作**非空殼) | `main/hal_power.c`(§7) |
| `hal/hal_misc.h` | `hal_rand/yield_watchdog/settings_load/settings_save_*` + 弱符號覆寫 `pk_board_device_class`→**1** | `main/hal_misc.c`(§11) |

入口只呼叫共用碼的 `app_flow_start(name)`(內部 game_init→pbus_start),其餘共用 API
(pbus/game_state/master_engine/voice)**本裝置 main 一律不直接觸碰**——
唯二例外在 §7(`pbus_is_master`+`game_submit_leave`,關機交接)與 §6(`voice_open/read/close`,
HAL 拉流,產品 §2.3 授權)。

### 1.2 磁碟標頭 vs 文件的差異(撰寫本指南時逐檔核對結果)

以下為**磁碟現況**(= 權威),S3 指南 §3 的引文未全部反映,實作照磁碟:

1. **`hal_display.h` 的 `hal_screen_t` 多了 `battery_pct` 欄**(0–100;0xFF=USB)與
   `title_flags` 位枚舉 `TF_BATT_LOW/TF_LINK_LOST/TF_DEALER_NE_MASTER/TF_SUBMITTING`。
   產品 v1.2 §2.2 要求 HAL 在標題列右側常駐渲染電量 —— render 必須處理(§8.3)。
2. **`hal_misc.h` 多了 `hal_settings_load/save_volume/save_brightness`**(R10:亮度/音量
   NVS 持久化,namespace `poker`、key `vol`/`bri`,共用碼呼叫、各裝置實作)。
3. **`pbus.h` 多了** `pbus_device_class()`、`pbus_cmd_inflight()`、`pbus_consume_reject()`
   (S3 §21.3 已記錄的「只加不改」延伸);裝置類別經**弱符號** `pk_board_device_class()`
   注入(S3 §21.3 R12),本機回 **1**(協定 §8.1:1=zuowei-c3)。
4. **`side_pot.h` 是公開標頭**(S3 指南 §1 佈局把 side_pot 列為 src 內部;磁碟已升為公開)。
   對本裝置無影響(main 不呼叫),僅記錄。
5. **`game_state.h` 多了** `game_view_publish()` 與 `game_set_event_hook/link_hook/role_hook`;
   **`master_engine.h` 多了** `master_engine_begin()`。皆共用碼內部接線,main 不觸碰。
6. `pk_config.h` 較 S3 §3.1 引文多出協定 §13 常數全量、`PK_EVT_LOG_MASTER/MEMBER`、
   產品 §6.3 排程常數、`PK_VOL_DEFAULT=70`/`PK_BRI_DEFAULT=80`。直接使用,勿在板級重複定義。

### 1.3 S3 §21 回寫中對本機有直接效力的條目

- **§21.2-1 WHOLE_ARCHIVE**:main 元件必須加 `WHOLE_ARCHIVE`,否則 `hal_*`/`pk_board_*`
  符號解析失敗(poker_core↔main 循環引用)。§2.3 的 CMakeLists 已含。
- **§21.3 R10**:音量/亮度 NVS 持久化(推翻 R9 的不持久化),`hal_misc.c` 照 §11 實作。
- **§21.3 R11**:`app_prepare_poweroff()` 由 main 提供單一定義,common 不定義。
  **本機是全產品第一台真正會呼叫它的裝置**(中鍵長按關機路徑),實作見 §7.3。
- **§21.3 R12**:`pk_board_device_class()` 強符號覆寫,zuowei=1。
- **§21.5**:`pbus_t`(g_pb)含 64×210B≈13KB 事件日誌 + 狀態鏡像,是 poker_core 最大
  單一靜態結構 —— C3 立帳必計(§12)。**不要**試圖把成員側日誌縮到 32 槽:那要改共用碼,凍結。
- **§21.2-4 `-Werror` 四坑**(註解內 `*/`、struct 回傳加 const、一行兩敘述、顯式 include)照避。

---

## 2. 專案佈局與建置系統

### 2.1 目錄佈局

```
zuowei-c3-lcd-pwr/
├─ poker/                        # ★ 本指南產出物(全新 IDF 專案)
│  ├─ CMakeLists.txt
│  ├─ sdkconfig.defaults         # §13.1 全文
│  ├─ partitions.csv             # §13.2 全文
│  └─ main/
│     ├─ CMakeLists.txt  idf_component.yml
│     ├─ board_config.h          # 全部引腳/板級常數(唯一放 GPIO 的地方)
│     ├─ app_main.c
│     ├─ hal_display.c           # ← demo display.c + backlight.c 合併改造(§8)
│     ├─ hal_input.c             # ← demo buttons.c 改造(§9)
│     ├─ hal_audio.c             # ← 新寫,地基是 demo audio.c 的 VB6824 流式介面(§6)
│     ├─ hal_battery.c           # ← demo battery.c 改造(§10)
│     ├─ hal_power.c             # ← demo main.c 的 power_latch_on/power_off 遷入(§7)
│     └─ hal_misc.c              # ← StickS3/main/hal_misc.c 照抄改 device_class(§11)
├─ components/vb6824/            # 既有,原樣引用(含閉源 .a),一個字不改
└─ main/                         # 既有 demo,保留不動(複用是「拷貝改造」,不是引用)
```

**注意**:poker/ 與 demo(倉庫根 main/)是兩個獨立 IDF 專案。demo 模組**拷貝進 poker/main/
改名改造**,不跨專案 include —— demo 保持可燒可跑,是硬體回歸測試的對照組。

### 2.2 `poker/CMakeLists.txt`(全文)

```cmake
cmake_minimum_required(VERSION 3.16)

# 引用:① 全專案共用元件(poker_core) ② 本板 vb6824 元件(閉源 .a 音訊驅動)
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../common/components"
    "${CMAKE_CURRENT_LIST_DIR}/../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(poker_zuowei_c3)
```

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
        vb6824
        esp_lcd esp_lvgl_port lvgl
        esp_driver_spi esp_driver_gpio esp_driver_ledc
        esp_adc
        esp_timer esp_partition spi_flash
        esp_wifi nvs_flash
    WHOLE_ARCHIVE      # S3 §21.2-1:HAL 實作在 main,被 poker_core 反向引用;
                       # 不加則 hal_battery_pct/hal_rand/hal_settings_*/pk_board_device_class
                       # 全部 undefined reference(四機共通,一律照加)
)
```

與 StickS3 的差異:去 `esp_driver_i2c/i2s`(本板無 I2C 裝置、音訊不走 I2S),
加 `vb6824` 與 `esp_adc`(電池 ADC oneshot)。

### 2.4 `poker/main/idf_component.yml`(全文)

```yaml
dependencies:
  idf: ">=6.0"
  espressif/esp_lvgl_port: "^2"
```

**沒有 `78/esp-opus`**——這是與 demo 最重要的依賴差異:撲克韌體只需**下行 PCM 播放**
(語音資產是 IMA-ADPCM,由共用 `voice_adpcm.c` 解碼),不用麥克風、不需 Opus 解碼。
少了 opus 才有 §12 的主棧 8KB 結論。vb6824 元件本身不依賴 opus(Opus 只出現在
其上行資料的**內容**,我們永不 `enable_input`)。

### 2.5 工具鏈(RISC-V,與 StickS3 的 xtensa 不同)

```bash
. ~/.espressif/v6.0.1/esp-idf/export.sh
# 本機 export.sh 不把工具鏈加入 PATH(demo README 已驗證),C3 = riscv32:
export PATH="$HOME/.espressif/tools/riscv32-esp-elf/esp-15.2.0_20251204/riscv32-esp-elf/bin:$PATH"

cd devices/zuowei-c3-lcd-pwr
idf.py set-target esp32c3          # 僅首次
```

---

## 3. 任務模型(C3 單核落地)

遵 S3 §5:「單核裝置沿用同一優先級表,去掉核綁定;**優先級數字不得改**」。
本板實際跑的任務(全部 core0,無 affinity 概念):

| Task | 來源 | 優先級 | 棧 | 備註 |
|---|---|---|---|---|
| `wifi`(系統) | esp_wifi | 23 | — | ESP-NOW recv 回呼:只 memcpy 入佇列(共用 pbus_transport) |
| `audio` | main/hal_audio.c | **15** | 4096 | 20ms 節拍組幀 + `vb6824_audio_write`(§6.3) |
| `pbus` | poker_core | 12 | 6144 | 約束:pbus(12) < wifi(23) ✓;pbus(12) ≤ audio(15) ✓ |
| `__uart_task` | vb6824 元件 | **9**(元件內寫死) | 4096(Kconfig) | UART 事件→幀解析→rx ring |
| `__send_task` | vb6824 元件 | **9**(元件內寫死) | 2048(Kconfig) | tx ring 每 10ms 出隊 320B 寫 UART |
| `buttons` | main/hal_input.c | 6 | 2048 | 10ms 輪詢去抖 |
| `ui`(main task 兼) | app_flow | 5 | 8192(主棧,§12) | 手勢消費、game_view 輪詢、≤10fps |
| `LVGL` | esp_lvgl_port | 4 | 8192 | **不得設 task_affinity**(C3 單核;沿用預設 -1) |

S3 §5 那句「C3 的 VB6824 音訊管線優先級 9 教訓」在此落地:vb6824 兩個內部任務
寫死優先級 9,**低於 pbus(12)**,元件不改(閉源生態,demo 驗證基準)。後果是 pbus
突發(補洞、快照)可延遲 `__send_task` 的 10ms 節拍 —— 解法不是改優先級,而是 §6.2 的
**佇列水位緩衝**:tx ring 常態維持 ~60ms 佔用,吸收 >50ms 的排程延遲。我方 `audio` task
(15)只負責**灌水**,真正的出聲節拍由水位+VB6824 自身緩衝決定,故 audio 高於 pbus 依然成立且必要
(灌水斷供 = 水位跌破 = 齊播漂移)。

---

## 4. board_config.h 與 app_main

### 4.1 `main/board_config.h`(全文)

引腳全部出自 `hardware-reference.md`(標【实测】)與 demo `board_config.h`(實機驗證),照抄不改值;
撲克新增項(按鍵語義映射、音訊延遲、關機長按閾值)集中在檔尾。

```c
// zuowei-c3-lcd-pwr 板級配置 —— 唯一允許出現 GPIO 編號的檔案(權威:hardware-reference.md)
#pragma once
#include "driver/gpio.h"
#include "hal/adc_types.h"

// ---- LCD (ST7789, 1.54" 240x240, SPI2) ----
#define LCD_SPI_HOST        SPI2_HOST
#define LCD_PIN_MOSI        GPIO_NUM_1     // FSPID  【实测】
#define LCD_PIN_SCLK        GPIO_NUM_3     // FSPICLK【实测】
#define LCD_PIN_DC          GPIO_NUM_0     // 【实测】
#define LCD_PIN_RST         GPIO_NUM_2     // 【实测】
#define LCD_PIN_CS          GPIO_NUM_12    // ★【实测·实机验证】DIO 模式下空閒的 SPIHD 腳
                                           //   被板方複用作片選;設 NC = 整屏全黑(demo 最大坑)
                                           //   spi_common 的 GPIO12 conflict 警告 = 預期,忽略
#define LCD_H_RES           240
#define LCD_V_RES           240
#define LCD_PIXEL_CLOCK_HZ  (80 * 1000 * 1000)   // 復刻出廠 80MHz(demo 驗證)
#define LCD_SPI_MODE        3              // ★ 本板是 mode 3(StickS3 是 0,勿照搬)
#define LCD_INVERT_COLOR    true
#define LCD_MIRROR_X        false
#define LCD_MIRROR_Y        false
#define LCD_SWAP_XY         false          // 240×240 直用,無旋轉
#define LCD_OFFSET_X        0
#define LCD_OFFSET_Y        0

// ---- 背光 (LEDC PWM) ----
#define LCD_PIN_BL          GPIO_NUM_5     // LEDC ch0【实测】
#define BL_OUTPUT_INVERT    false

// ---- 按鍵(輸入上拉低有效;物理左→右 = GPIO8, GPIO9, GPIO7【实机确认】)----
#define BTN_OK_PIN          GPIO_NUM_8     // 物理最左 → KEY_OK(產品 §2.1 映射表)
#define BTN_NEXT_PIN        GPIO_NUM_7     // 物理最右 → KEY_NEXT
#define BTN_PWR_PIN         GPIO_NUM_9     // 物理中間(BOOT):不進共用層,長按=關機(§7)
#define BTN_PWR_HOLD_MS     800            // demo 驗證值;關機屬破壞性但可攔截,足夠防誤觸

// ---- 電池 ADC(GPIO4 / ADC1_CH4 —— ADC1 與 Wi-Fi 無衝突,demo 驗證)----
#define BAT_ADC_UNIT        ADC_UNIT_1
#define BAT_ADC_CHANNEL     ADC_CHANNEL_4  // = GPIO4【实测】
#define BAT_ADC_ATTEN       ADC_ATTEN_DB_12
#define BAT_DIVIDER         6.08f          // 【实测校准】pin 687mV ↔ 電池 4.18V
#define BAT_FULL_MV         4200
#define BAT_EMPTY_MV        3300

// ---- 電源管理【实机验证】----
#define PWR_KEEP_PIN        GPIO_NUM_13    // ★ 整機電源自鎖:高=供電,低=立即斷電(USB/電池皆然)
#define USB_DETECT_PIN      GPIO_NUM_21    // VBUS 檢測:高=插入(外部主動驅動;非充電狀態腳)

// ---- 音訊 (VB6824 over UART1 @2Mbps) ----
#define VB_PIN_TX           GPIO_NUM_20    // C3 TX → VB6824 RX【实测】
#define VB_PIN_RX           GPIO_NUM_10    // C3 RX ← VB6824 TX【实测】
#define AUDIO_SAMPLE_RATE   16000
#define AUDIO_PREFILL_FRAMES 3             // §6.3:開機灌 3 幀靜音(60ms 水位)
#define AUDIO_PATH_LATENCY_MS 80           // ★ 初始估算值:水位 60 + UART ~2 + VB6824 內部 ~20
                                           //   【待实测】§6.5 量測後回填,並記入 §16
```

### 4.2 `main/app_main.c`(全文級骨架)

```c
/* app_main.c -- zuowei-c3 德州撲克進入點。 */
#include "board_config.h"
#include "hal_power_board.h"       /* power_latch_on 的私有宣告(見 §7.1) */
#include "hal/hal_display.h"
#include "hal/hal_audio.h"
#include "app_flow.h"
#include "esp_mac.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "app_main";

void app_main(void)
{
    power_latch_on();          /* ★★ 必須是第一行(§7.2):電池開機時使用者長按中鍵,
                                *   鬆手前不自鎖 = 直接斷電。任何 init 都不得排在它前面。 */

    ESP_ERROR_CHECK(hal_display_init());   /* §8:含背光 LEDC(預設亮度由共用碼經 NVS 設回) */
    ESP_ERROR_CHECK(hal_audio_init());     /* §6:vb6824_init + voice 分區 mmap + audio task */
    /* hal_input_init 由共用 input_gesture_init 內部呼叫;
       hal_battery 為惰性初始化(首次讀取時 init,§10),此處無事可做 */

    char name[8];
    uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(name, sizeof(name), "C3-%02X%02X", mac[4], mac[5]);
#ifdef PK_DEBUG_SOLO
    ESP_LOGW(TAG, "[SOLO] smoke build");
#endif
    app_flow_start(name);                  /* 內部 game_init → pbus_start;不返回業務控制權 */
    ESP_LOGI(TAG, "boot complete: %s", name);
}
```

與 StickS3 app_main 的差異:無 I2C/PM1 前置儀式,換成 `power_latch_on()`;
名字前綴 `C3-`。啟動預算:上電→SCANNING ≤3s(產品 §8.2),本板無 PSRAM 初始化、
LCD 80MHz,demo 實測遠低於此限。

---

## 5. demo 模組複用計畫(逐檔)

demo(倉庫根 `main/`)已在本板實機驗證,是引數與坑的權威來源。逐檔裁定:

| demo 檔 | 處置 | 摘要 |
|---|---|---|
| `board_config.h` | **拷貝+擴充** | §4.1:引腳值原封;改按鍵宏名為語義名(OK/NEXT/PWR);加音訊/關機常數 |
| `display.c` | **拷貝改造 → hal_display.c** | init 段原樣照抄(含 CS=GPIO12、mode 3、80MHz、swap_bytes);去掉 `display_lock/unlock` 對外暴露(改用 `lvgl_port_lock` 內聚);補 `build_static_ui`+`render`+`render_showdown`(§8) |
| `backlight.c` | **併入 hal_display.c** | LEDC 參數原樣;`backlight_set` 改名 `hal_display_set_brightness`,加 10% 下限(產品 §2.2);去掉 `backlight_get`(共用碼自持狀態) |
| `battery.c` | **拷貝改造 → hal_battery.c** | ADC oneshot + curve-fitting 校準 + 16 次均值 + GPIO21 USB 檢測原樣;對外改為凍結介面兩函式(§10) |
| `buttons.c` | **拷貝改造 → hal_input.c** | 輪詢去抖骨架保留;**事件模型重做**:左/右鍵改上報 raw press/release(手勢合成在共用層,S3 裁定 R5:600ms 在 `input_gesture.c`,非 HAL 事);中鍵長按偵測留在 HAL 內部接關機(§9) |
| `audio.c` | **地基 → hal_audio.c** | 只繼承三件事:`vb6824_init(VB_PIN_TX, VB_PIN_RX)` 初始化次序、`vb6824_audio_enable_output`+`vb6824_audio_write` 的流式用法、`vb6824_audio_set_output_volume` 音量路徑。**丟棄**:opus 解碼、錄音回放、`audio_capture_*`、語音命令回呼(撲克不用喚醒詞;不註冊 `vb6824_register_voice_command_cb` 即可)、`audio_play_beep`(V_BEEP 在 voice.bin 裡) |
| `intercom.[ch]`、`intercom_proto.h` | **不複用** | Wi-Fi/ESP-NOW init 與收發已由共用 `pbus_transport.c` 全量覆蓋(S3 §17,其本身就是照 intercom 模式寫的);只把 demo `sdkconfig.defaults` 的 Wi-Fi 緩衝收緊項搬進 §13.1 |
| `main.c` | **只搬兩函式** | `power_latch_on()`/`power_off()` 遷入 `hal_power.c`(§7);LVGL 分頁 UI 全棄(共用 app_flow 取代) |

**改造時的通用鐵律**(demo README 踩坑 8 條的撲克版):任何 `lv_*` 必在
`lvgl_port_lock/unlock` 內;`swap_bytes=true` 必開;大緩衝 static/heap 不上棧;
UI/業務碼不得出現 GPIO 號。

---

## 6. 音訊 HAL(VB6824 UART 路徑)——本指南核心難點

### 6.1 管線事實(讀 `components/vb6824/vb6824.c` 源碼考據,寫死在元件裡)

本專案配置 `CONFIG_VB6824_TYPE_OPUS_16K_20MS_PCM_16K`(demo 同款,唯一實機驗證過的型別)下:

- **下行(播放)**:`vb6824_audio_write(pcm, len)` 把裸 PCM(16k/mono/int16)塞進
  **5760B 的 BYTEBUF tx ring(= 2880 樣本 = 180ms)**,ring 滿則 `portMAX_DELAY` 阻塞。
  `__send_task`(優先級 9)以 `vTaskDelayUntil` **每 10ms 出隊至多 320B(=10ms 音訊)**,
  包上 7B 幀頭/校驗經 UART1@2Mbps 送 VB6824(327B ≈ 1.6ms 線上時間)——
  **出聲節拍 = tx ring 的出隊節拍**,鎖在 C3 的 FreeRTOS tick 域(40MHz 晶振)。
- **閘門**:`g_output_enabled == false` 時 `vb6824_audio_write` **靜默丟棄資料**
  (不阻塞不報錯)。所以必須先 `vb6824_audio_enable_output(true)` 再寫,順序錯 = 無聲且無錯誤。
- **README/demo audio.h 警告「stream start/stop 勿逐幀開關」的本質**:`enable_output(false)`
  會清 ring + VB6824 側停 DAC,重開有不可控啟動延遲。對撲克的推論:
  **`enable_output(true)` 在 `hal_audio_init` 呼叫一次,之後永不呼叫 `enable_output(false)`**——
  這正好就是產品 §2.3 規則 1「輸出流常開、以靜音填充」在 UART 流上的同構實作:
  I2S 機的「常開」是 DMA 永轉,本機的「常開」是 **tx ring 永不斷流**(靜音也照灌)。
- **上行(麥克風)**:永不 `vb6824_audio_enable_input(true)`。不開輸入 = VB6824 不推流,
  rx ring(400B)空轉,無 CPU/RAM 代價。
- **音量**:`vb6824_audio_set_output_volume(0..100)` → 量化為 0..31 寫 VB6824 寄存器
  (UART 命令幀)。增益在 VB6824 側 DAC 後級,**不需要**軟體增益、也沒有 StickS3 的
  電池鉗制問題(8002 功放 + 小喇叭,demo 100% 音量電池供電無重啟)。

### 6.2 常開靜音填充策略:自定節拍 + 水位緩衝

不能照 demo 的用法(demo 是「攢一段→一次全塞→等它播完」,靠 ring 滿阻塞背壓,
佔用漂到 180ms 且不可知)——齊播需要**已知且穩定**的「寫入→出聲」延遲。作法:

**HAL audio task 以 `vTaskDelayUntil` 自定 20ms 節拍,每拍組一幀 640B(320 樣本)寫入;
開機先灌 `AUDIO_PREFILL_FRAMES=3` 幀靜音把水位墊到 60ms,之後灌入速率=出隊速率,
水位恆定在 40–60ms 間震盪。**

- 為什麼 60ms:須吸收 pbus(12)>vb6824 send task(9) 的優先級倒掛(§3)造成的出隊延遲,
  以及 Wi-Fi task 突發。60ms > 實測可能的最壞排程延遲一個數量級,又只佔 ring 容量 1/3
  (滿 180ms,永不觸發阻塞背壓 → `vb6824_audio_write` 恆定即回,節拍不被污染)。
- 節拍與 `s_head_ms` 映射同源(都是 40MHz 晶振:tick 與 esp_timer),樣本計數自洽,
  不需 StickS3 §15.3 偽代碼裡的漂移校正分支(那是給 I2S 阻塞寫節拍的;本機節拍是自己的
  `vTaskDelayUntil`,錯過死線會立即補跑,長期均值精確 20ms)。
- **已知限制(記入驗收)**:VB6824 DAC 掛在自己的 24MHz 晶振上,與 C3 40MHz 晶振存在
  ±~40ppm 相對漂移(≈ 每小時 ≤144ms),漂移積累體現在 VB6824 內部緩衝深度緩慢變化,
  無 API 可觀測。低水位策略讓它主要在「靜音期」以 VB6824 側 underrun 補靜音的方式自癒
  (聽感無損);若實測長局後齊播錯位單調增長,對策是在**確認整機靜音 ≥2s** 時重灌水位
  (close 當前流後 `enable_output(false)`→`(true)`→重新 prefill 並重置 `s_head_ms`),
  此決策實測後記 §16。首版不做。

### 6.3 `main/hal_audio.c`(核心全文)

結構照 StickS3 `hal_audio.c`(佇列/排程/切入邏輯**逐行同構**,那是已凍結的參考語義),
只換底層寫入與節拍源。可直接照抄:

```c
/* hal_audio.c -- VB6824 UART PCM 下行(指南 §6)。
 * 輸出流常開靜音填充(tx ring 永不斷流);play_at 樣本級切入;無電池音量鉗。 */
#include "hal/hal_audio.h"
#include "board_config.h"
#include "voice.h"
#include "vb6824.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>

static const char *TAG = "hal_audio";
#define FRAME 320                              /* 20ms @16k */

typedef struct { voice_id_t id; uint32_t at_ms; bool sched; } aq_item_t;
static QueueHandle_t s_q;
static aq_item_t s_pend[4]; static int s_npend;      /* 排程單,按 at_ms 升序 */
static aq_item_t s_imm[8];  static int s_nimm;       /* 即播 FIFO */
static voice_stream_t *s_cur;
static uint32_t s_head_ms;                           /* 下一幀首樣本的「出聲」本地時刻 */

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static bool time_ge(uint32_t a, uint32_t b) { return (int32_t)(a - b) >= 0; }

void hal_audio_play(voice_id_t id)                { aq_item_t i = { id, 0, false }; if (s_q) xQueueSend(s_q, &i, 0); }
void hal_audio_play_at(voice_id_t id, uint32_t t) { aq_item_t i = { id, t, true };  if (s_q) xQueueSend(s_q, &i, 0); }
void hal_audio_stop(void) { if (s_cur) { voice_close(s_cur); s_cur = NULL; } s_nimm = 0; s_npend = 0; }
uint16_t hal_audio_path_latency_ms(void) { return AUDIO_PATH_LATENCY_MS; }
void hal_audio_set_volume(uint8_t pct)
{
    if (pct > 100) pct = 100;
    vb6824_audio_set_output_volume(pct);   /* UART 命令幀,~10B;僅選單變更時呼叫,勿每幀 */
}

/* drain_queue / pop_pend / next_immediate:與 StickS3 hal_audio.c 逐行相同,不重抄 */

static void audio_task(void *arg)
{
    (void)arg;
    static int16_t buf[FRAME];                       /* static:C3 上勿佔任務棧 */

    /* 預灌水位:3 幀靜音(60ms),之後節拍恆定 → 佔用 40–60ms 震盪 */
    memset(buf, 0, sizeof(buf));
    for (int i = 0; i < AUDIO_PREFILL_FRAMES; i++)
        vb6824_audio_write((uint8_t *)buf, sizeof(buf));

    s_head_ms = now_ms() + AUDIO_PATH_LATENCY_MS;    /* 出聲時刻 = 現在 + 全路徑延遲 */
    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        drain_queue();
        memset(buf, 0, sizeof(buf));                 /* 1) 預設本幀 = 靜音(常開填充) */
        if (s_npend && time_ge(s_head_ms + 20, s_pend[0].at_ms)) {   /* 2) 排程搶佔 */
            int off = time_ge(s_pend[0].at_ms, s_head_ms)
                    ? (int)((s_pend[0].at_ms - s_head_ms) * 16) : 0; /* 樣本級切入;遲到 off=0 */
            if (off > FRAME) off = FRAME;
            if (s_cur) voice_close(s_cur);           /* 齊播優先,打斷即播型 */
            s_cur = voice_open(s_pend[0].id);
            pop_pend();
            if (s_cur && off < FRAME) voice_read(s_cur, buf + off, FRAME - off);
        } else if (s_cur) {                          /* 3) 續播當前流 */
            int n = voice_read(s_cur, buf, FRAME);
            if (n < FRAME) { voice_close(s_cur); s_cur = next_immediate();
                             if (s_cur) voice_read(s_cur, buf + (n > 0 ? n : 0), FRAME - (n > 0 ? n : 0)); }
        } else {                                     /* 4) 即播佇列 */
            s_cur = next_immediate();
            if (s_cur) voice_read(s_cur, buf, FRAME);
        }
        vb6824_audio_write((uint8_t *)buf, sizeof(buf));  /* 水位 <180ms,恆不阻塞 */
        s_head_ms += 20;
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(20));   /* 節拍源:FreeRTOS tick(1kHz) */
    }
}

static esp_err_t voice_partition_load(void)   /* 與 StickS3 逐行相同(subtype 0x40,"voice") */
{
    const esp_partition_t *p = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "voice");
    if (!p) { ESP_LOGE(TAG, "voice partition not found"); return ESP_ERR_NOT_FOUND; }
    const void *base; esp_partition_mmap_handle_t h;
    esp_err_t r = esp_partition_mmap(p, 0, p->size, ESP_PARTITION_MMAP_DATA, &base, &h);
    if (r != ESP_OK) return r;
    return voice_init(base, p->size);         /* 驗 magic 失敗 → 播放靜默 no-op,遊戲照跑 */
}

esp_err_t hal_audio_init(void)
{
    s_q = xQueueCreate(16, sizeof(aq_item_t));

    vb6824_init(VB_PIN_TX, VB_PIN_RX);        /* UART1@2Mbps + 內部兩 task(prio 9) */
    /* 不註冊 voice_command_cb、不 enable_input:撲克不用喚醒詞與麥克風 */
    vb6824_audio_enable_output(true);         /* ★ 常開,之後永不關(§6.2) */
    vb6824_audio_set_output_volume(70);       /* 開機預設;NVS 值由共用碼稍後 set_volume 蓋上 */

    voice_partition_load();

    xTaskCreate(audio_task, "audio", 4096, NULL, 15, NULL);   /* §3 任務表 */
    ESP_LOGI(TAG, "audio ready (VB6824 uart, latency=%dms)", AUDIO_PATH_LATENCY_MS);
    return ESP_OK;
}
```

語義要點(同 S3 §15.3,重申以防抄錯):`hal_audio_play_at` 的參數是**出聲時刻**
(`pbus_local_time_for(play_at)` 直傳);路徑延遲已內建在 `s_head_ms` 初始化,
**app_flow 與共用碼不再扣 `hal_audio_path_latency_ms()`,扣兩次 = 提早出聲**。

### 6.4 開機時序陷阱

`vb6824_init` 內部會發 GET_WAKEUP_WORD 並在 200ms 後檢查 VB6824 應答;VB6824 自身
上電引導約需數百 ms。若 `hal_audio_init` 緊貼上電執行、prefill 的前幾幀落在 VB6824
就緒之前,最壞損失 <100ms 靜音(無業務影響——首個需要準時的聲音離開機至少數秒)。
不加延時等待;若實測開機 V_BEEP 缺失,在 `enable_output(true)` 前補 `vTaskDelay(200ms)` 並記 §16。

### 6.5 AUDIO_PATH_LATENCY_MS 量測(必做,C3 是全產品最可疑路徑)

產品 §2.3 規則 2 點名:「C3 的 UART→VB6824 路徑延遲未知且最可疑,其指南必須含量測步驟;
若實測抖動 >±20ms,C3 齊播明文降級為『盡量對齊』並記入已知限制」。

**初值構成(80ms)**:水位 60ms(prefill 3 幀,寫入的樣本要排完水位才出隊)
+ UART 傳輸 ~2ms + VB6824 收幀→DAC 內部緩衝 **~20ms(純猜測,未知項)**。

**量測 = S3 §15.5 錄音對拍法原樣沿用**,異機混測版:

1. 一台 **StickS3(已校準,latency=62ms 基準)** + 本機,各燒 `-DPK_DEBUG_SOLO=1`
   校準建置(Master 每 3s 發 `play_at=now+700ms` 的 E_REMIND,兩機齊播 V_BEEP)。
   ※ 用已校準的 S3 當基準,而非兩台 C3 互測 —— 驗收目標本來就是異型 <30ms(產品 §9.2)。
2. 手機錄音 App(≥44.1kHz)置兩機正中,錄 ≥10 對 beep;Audacity 量每對起始沿 Δt。
3. **中位數**修正:C3 落後 Δt → `AUDIO_PATH_LATENCY_MS += Δt`(領先則減)。回填
   `board_config.h` 並記 §16。
4. **抖動判定**(C3 特有步驟):取全部 Δt 的 max-min。≤±20ms → 齊播達標;
   >±20ms → 依產品 §2.3 明文降級「盡量對齊」,記入已知限制與 §16,**不要**試圖
   用加大水位手段硬修(水位越深,vb6824 出隊節拍抖動的積分越大,只會更糟)。
5. 復測通過線:與 S3 混測中位數 <30ms(產品 §9.2);同型 C3 對測 <10ms。

### 6.6 與凍結介面的對account:`hal_audio_stop`

共用碼在系統選單 VOLUME 試播、打斷閒聊型播報時呼叫。本實作只清 HAL 側佇列與當前流,
**不清 vb6824 tx ring**(裡面最多還有 60ms 已寫入的尾音,自然播完)。60ms 殘尾聽感
可忽略且換得水位不塌;若驗收覺得「停不乾淨」,唯一正確修法是把殘尾也算進水位重建
(§6.2 的重灌程序),不是 `enable_output(false)`。

---

## 7. 電源自鎖與關機(hal_power.c)

本機是四裝置中**唯一**軟體可攔截關機的(xingzhi 硬體電源鍵、StickS3 PM1 雙擊、
Cardputer 撥鈕皆攔不到,見產品 §2.5)——協定 §9.3 優雅交接的「Master 主動退出」
路徑在本機是**常態關機路徑**,必須做對。

### 7.1 檔案與私有標頭

`hal_power.c` 對共用層只暴露凍結的 `app_prepare_poweroff()`;
對板內(app_main、hal_input)另出私有標頭 `hal_power_board.h`:

```c
/* hal_power_board.h -- 板內私有(不進 hal/) */
#pragma once
void power_latch_on(void);     /* app_main 第一行 */
void board_poweroff(void);     /* hal_input 中鍵長按呼叫:交接→斷電,不返回 */
```

### 7.2 `main/hal_power.c`(全文)

```c
/* hal_power.c -- GPIO13 電源自鎖 + 關機攔截(產品 §2.5;S3 §21.3 R11)。 */
#include "hal/hal_power.h"
#include "hal_power_board.h"
#include "board_config.h"
#include "pbus.h"                 /* pbus_is_master(凍結介面) */
#include "game_state.h"           /* game_submit_leave(凍結介面) */
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "hal_power";

void power_latch_on(void)         /* demo main.c 原樣遷入【实机验证】 */
{
    gpio_reset_pin(PWR_KEEP_PIN);
    gpio_set_direction(PWR_KEEP_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(PWR_KEEP_PIN, 1);
}

/* R11:main 提供單一定義。本機真的會走到這裡(中鍵長按),非 StickS3 式空殼。
 * 語義(產品 §2.5):若本機為 Master → 觸發協定 §9.3 交接,最多等 2s → 返回後才允許斷電。
 * 凍結介面下的實作路徑:C_LEAVE(game_submit_leave)→ Master 端(可能就是本機引擎)
 * 完成 E_PLAYER_LEFT 後按 §9.3.5 交接;本機以 pbus_is_master() 轉 false 為交接完成訊號。
 * 局中/2s 超時交接未完 → 照樣斷電,協定 §9.5 故障接管(≤6s)兜底 —— 不比拔電池差。 */
void app_prepare_poweroff(void)
{
    if (pbus_my_player_id() != 0xFF)
        game_submit_leave();                       /* 已入桌才有 LEAVE 可送 */
    if (!pbus_is_master()) { vTaskDelay(pdMS_TO_TICKS(150)); return; }  /* 讓 C_LEAVE 出去 */
    for (int i = 0; i < 20 && pbus_is_master(); i++)
        vTaskDelay(pdMS_TO_TICKS(100));            /* ≤2s 等 E_MASTER_HANDOFF 生效 */
    ESP_LOGW(TAG, "poweroff: master=%d after handoff wait", (int)pbus_is_master());
}

void board_poweroff(void)
{
    app_prepare_poweroff();                        /* ★ 關機前必走(產品 §2.5 硬性) */
    ESP_LOGW(TAG, "power off (PWR_KEEP -> LOW)");
    vTaskDelay(pdMS_TO_TICKS(50));                 /* 讓日誌從 USB-JTAG 出去 */
    gpio_set_level(PWR_KEEP_PIN, 0);               /* 整機斷電(USB/電池皆然) */
    vTaskDelay(portMAX_DELAY);                     /* 插著 USB 時斷不掉?見下方注意 2 */
}
```

**三個必知**:

1. **`power_latch_on()` 必須是 `app_main` 第一行**(demo README 坑 1【实机验证】):
   電池開機 = 使用者長按中鍵 ~2s,按住期間硬體強制供電,鬆手前韌體必須完成自鎖。
   IDF 開機到 app_main ~數百 ms,遠在 2s 窗口內,但**任何**排在它前面的初始化
   (尤其會失敗重試的)都是在賭斷電。開發期插著 USB 感知不到此腳 —— 一旦跑電池就是「開不了機」。
2. 插著 USB 拉低 GPIO13:hardware-reference.md 記載拉低「USB/電池皆然立即斷電」;demo 在
   power_off 後 `vTaskDelay(portMAX_DELAY)` 兜底(若特定批次板 USB 供電維持,裝置停擺
   在此,等待拔線)。照抄此兜底。
3. 中鍵(GPIO9=BOOT)關機後,**長按 ~2s 重新開機**;下載模式照常(上電時按住 BOOT)。

---

## 8. 顯示 HAL(hal_display.c)

### 8.1 init:demo `display.c` 原樣 + LVGL 資源縮編

`hal_display_init()` = demo `display_init()` 逐行照抄(SPI bus→panel_io→st7789→
invert/mirror/gap→lvgl_port),**保持這些 demo 驗證值,一個都別「順手改成 StickS3 的」**:

| 參數 | 本板值 | StickS3 值(勿抄) |
|---|---|---|
| `.spi_mode` | **3** | 0 |
| `.pclk_hz` | 80MHz | 40MHz |
| `.cs_gpio_num` | **GPIO12(必須!缺它全黑)** | GPIO41 |
| swap_xy / mirror | false/false/false | true/false/true |
| set_gap | (0,0) | (40,52) |
| lvgl buffer | `LCD_H_RES * 20`,`double_buffer=false`(9.6KB) | 240×40 雙緩衝(38KB) |
| lvgl_port cfg | priority=4、stack=8192、**不設 affinity** | affinity=1 |

- LVGL 緩衝裁定:demo 註釋已給結論——C3 無 PSRAM,單緩衝 20 行相比雙緩衝 40 行省
  ~28KB 且「UI 仍流暢」(實機)。撲克 UI ≤10fps、全靜態物件改屬性,20 行單緩衝**定案**;
  產品 §8.1 的 LVGL 預算 24KB 由 9.6KB + 物件池覆蓋,反而更寬。
- 背光:`bl_init_off()`(佔空 0 起步)→ init 完成後 `hal_display_set_brightness(80)`;
  `hal_display_set_brightness` = demo `backlight_set` + **10% 下限鉗**(產品 §2.2:
  「PWM 佔空比勿低於 10%」防調到全黑找不回選單)。LEDC 參數(5kHz/10-bit/ch0/GPIO5)照 demo。
- `spi_common` 會警告 `GPIO 12 is conflict with others and be overwritten`:預期行為,
  忽略(hardware-reference.md §5)。**不要**因為這條警告把 CS 改走軟體 GPIO ——demo 已驗證原生 CS 可用。

### 8.2 build_static_ui:240×240 佈局(全產品第二種邏輯佈局)

產品 §2.2:全產品只有 240×240 與 240×135 兩種邏輯佈局。StickS3 的 `hal_display.c`
物件集與 render 邏輯(含 card_text 花色紅顯、`##` 蓋牌、set_or_hide、title_bg 四色)
**原樣照抄**,只把 `build_static_ui` 的座標/字級換成 240×240 版。參考座標(28px 標題
/20px 內文,對齊 sdkconfig 三字型):

```
y=  0..30   title(montserrat_20;右端留 60px 給電量,§8.3)
y= 36..76   cards 行:自己 2 張大牌(montserrat_28,x=8,64)+ 公共 5 張(20,x=124+i*22)
y= 84..120  big(montserrat_28,置中)
y=124..212  lines[0..5](montserrat_14,行高 15,x=8)
y=196..204  progress bar(224×8,置中;與 lines[5] 互斥出現,照 StickS3 作法)
y=216..238  hint_ok(x=8,綠)/ hint_next(右對齊,藍)(montserrat_14)
showdown 容器:名字 y=8;兩張 hole montserrat_28 於 y=70(x=40,130);rank_text y=130;
              "+N" 金色 y=70 x=190;"YOU" 角標右上。
```

座標是**建議初值**,首次點亮後按實屏微調(240×240 空間寬裕,無 240×135 的「可省」裁剪
問題:lines 4–6、進度條全顯)。

### 8.3 render 的 battery_pct(磁碟標頭新欄,§1.2-1)

照 StickS3 `hal_display_render` 的 tbuf 寫法:標題文字後綴
` 83%` / ` USB`(0xFF)/ 斷線再綴 ` !LINK`;充電中(`hal_battery_charging()`——
本板恆 false,§10)不加符號。值由 app_flow 每 10s 填入,HAL 只管畫(產品 §2.2)。

### 8.4 首次點亮自檢

本板無旋轉/偏移疑點(demo 已驗證 240×240 直向 (0,0)),自檢只驗三件:
① 有畫面(黑屏 → 查 CS=GPIO12);② 顏色正(發青/錯亂 → invert_color 或 swap_bytes);
③ 邊框 1px 矩形四邊完整(缺邊 → offset,理論不會)。

---

## 9. 輸入 HAL(hal_input.c)

映射(產品 §2.1 表,硬性):**左 GPIO8 = KEY_OK,右 GPIO7 = KEY_NEXT;
中 GPIO9 不進共用層**,長按 = 關機(先 `app_prepare_poweroff`,§7)。

demo `buttons.c` 的改造點:demo 上報的是「短按/長按**事件**」(800ms 合成在 HAL),
凍結介面要的是「**raw press/release**」(600ms 手勢合成在共用 `input_gesture.c`,
S3 裁定 R5)。骨架(輪詢去抖照 demo,事件模型重做):

```c
/* hal_input.c -- 左/右鍵 raw 事件 + 中鍵關機(指南 §9)。 */
#include "hal/hal_input.h"
#include "hal_power_board.h"
#include "board_config.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static hal_input_cb_t s_cb;

static void btn_task(void *a)
{
    const gpio_num_t pin[2] = { BTN_OK_PIN, BTN_NEXT_PIN };
    bool stable[2] = {0}, last[2] = {0};
    bool pwr_last = false, pwr_stable = false;
    int  pwr_down_ms = 0;
    for (;;) {
        for (int i = 0; i < 2; i++) {                    /* 連續兩拍一致才翻轉 = 去抖 */
            bool raw = gpio_get_level(pin[i]) == 0;
            if (raw == last[i] && raw != stable[i]) {
                stable[i] = raw;
                if (s_cb) s_cb((hal_key_t)i, raw);       /* KEY_OK=0 / KEY_NEXT=1,raw 直報 */
            }
            last[i] = raw;
        }
        bool praw = gpio_get_level(BTN_PWR_PIN) == 0;    /* 中鍵:HAL 內部消化 */
        if (praw == pwr_last && praw != pwr_stable) { pwr_stable = praw; pwr_down_ms = 0; }
        pwr_last = praw;
        if (pwr_stable && (pwr_down_ms += 10) >= BTN_PWR_HOLD_MS)
            board_poweroff();                            /* 交接→斷電,不返回(§7.2) */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t hal_input_init(hal_input_cb_t cb)
{
    s_cb = cb;
    gpio_config_t io = { .pin_bit_mask = BIT64(BTN_OK_PIN) | BIT64(BTN_NEXT_PIN) | BIT64(BTN_PWR_PIN),
                         .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    ESP_ERROR_CHECK(gpio_config(&io));
    xTaskCreate(btn_task, "buttons", 2048, NULL, 6, NULL);
    return ESP_OK;
}

void hal_input_set_incdec_cb(hal_input_incdec_cb_t cb) { (void)cb; }  /* 本板無增強輸入 */
int  hal_input_get_number(void) { return -1; }
```

- 中鍵短按:**無功能**(demo 的翻頁語義廢除;共用碼只認兩鍵,產品 §5)。不上報、不蜂鳴。
- `board_poweroff()` 在 buttons task 上下文執行:棧 2048 夠(它只 gpio+vTaskDelay+
  兩個共用 getter/submit;若 `-Werror` 建置後實測 high-water 吃緊,提到 2560 並記 §16)。
- 關機長按 800ms 沿 demo 驗證值,與共用手勢 600ms 無耦合(不同鍵、不同層)。

---

## 10. 電池 HAL(hal_battery.c)

demo `battery.c` 改造(ADC oneshot + curve-fitting 校準 + 16 均值 + 分壓 6.08 全保留):

```c
uint8_t hal_battery_pct(void)
{
    ensure_init();                        /* 首次呼叫時 adc_oneshot_new_unit + GPIO21 入力 */
    if (gpio_get_level(USB_DETECT_PIN)) return 0xFF;   /* 插 USB:ADC 讀的是充電電壓,
                                          * 非電池 OCV,回報必虛高 → 一律 0xFF(顯示 USB)。
                                          * 與協定 §5「未知回 0xFF、嚴禁回 0」一致 */
    static uint32_t cache_t; static uint8_t cache_v = 0xFF;
    if (cache_v != 0xFF && now_ms() - cache_t < 1000) return cache_v;   /* 1s 快取 */
    int bat_mv = read_bat_mv();           /* = demo battery_read 的 16 均值 + 校準 + ×6.08 */
    int pct = (bat_mv - BAT_EMPTY_MV) * 100 / (BAT_FULL_MV - BAT_EMPTY_MV);
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;    /* (-Werror:兩敘述拆行,示意從簡) */
    cache_v = (uint8_t)pct; cache_t = now_ms();
    return cache_v;
}
bool hal_battery_charging(void) { return false; }   /* 板上無 CHRG 狀態腳(hardware-reference.md §2),
                                                     * 不可知回 false(產品 §2.4) */
```

- **ADC1 無 Wi-Fi 衝突**是本板優勢(demo README 明文:「电池走 ADC1,与 Wi-Fi 无冲突,
  读数正常」)——不需要 xingzhi(ADC2)的快取降級策略,但 1s 快取仍保留
  (pbus 每 2s 經 `get_battery_pct` 回呼要一次 + app_flow 每 10s 一次,免重複採樣 16 次)。
- 線性 3300–4200 曲線是 demo 遺留的已知粗糙點(README「仍待驗證」):鋰電放電曲線
  非線性,50% 附近會偏樂觀。首版照用(協定只拿它做 <15 低電豁免與顯示);
  若驗收覺得跳變難看,換 S3 §13.4 的 OCV 折線(4150→100…3300→0 內插),記 §16。
- 惰性初始化(`ensure_init`)讓 app_main 不需要顯式 battery init(§4.2)。

---

## 11. hal_misc.c(裝置類別 + NVS 設定)

`StickS3/main/hal_misc.c` **整檔照抄**,只改一行:

```c
uint8_t pk_board_device_class(void) { return 1; }   /* 協定 §8.1:1 = zuowei-c3 */
```

其餘(`hal_rand`=esp_random、`hal_yield_watchdog`=vTaskDelay(1)、
`hal_settings_load/save_*` 走 NVS namespace `poker` key `vol`/`bri`)零改動——
R10 的持久化語義四機一致,選單邏輯在共用 app_flow,本機自動獲得
「BATT %/VOLUME 40-70-100/BRIGHTNESS 30-60-100 + 重開機保留」全套(產品 §5.6/§9.16)。

---

## 12. 記憶體預算(C3 400KB、無 PSRAM —— 對產品 §8.1 立帳)

| 項目 | 產品 §8.1 預算 | 本實作估算 | 依據 |
|---|---|---|---|
| Wi-Fi/ESP-NOW 棧 | ~55 KB | ~50 KB | §13.1 收緊 RX/TX 緩衝 + 關 AMPDU(demo 驗證) |
| FreeRTOS+系統+主棧 | ~30 KB | ~28 KB | **主棧 8192**(見下)+ pbus 6144 + audio 4096 |
| LVGL partial buffer | ~24 KB | **~10 KB** | 240×20×2B 單緩衝(§8.1)+ 物件池 |
| pbus 事件日誌 | ~7 KB(32 槽) | **~13.5 KB(64 槽)** | S3 §21.5:現行共用碼 Master/成員共用 64 槽;凍結,C3 照付,差額由 LVGL 節省抵銷 |
| pbus 狀態+OOO+佇列 | ~3 KB | ~9 KB | ooo 8×~254B + rxq 24×258B + txq(S3 §21.5 實測配置) |
| 語音解碼緩衝 | 4 KB | 4 KB | 拉取式(共用) |
| vb6824 元件 | —(本板特有) | **~19 KB** | UART drv 3200×2 + tx ring 5760 + rx ring 400 + 兩任務棧 4096/2048 |
| core+app 邏輯 | ≤60 KB | ≤60 KB | 共用碼子預算(S3 已立) |
| **合計** | | **~194 KB** | **餘裕 >200KB ≥ 產品要求的 100KB** ✓ |

**主棧結論(任務要求明答)**:demo 的 12KB 主棧是為 `opus_decode` 準備的
(README 坑 5,6KB 直接溢出復位);撲克建置**無 opus**(§2.4),主棧回落到產品 §8.1
的 8KB —— `CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192`(與 StickS3 相同;main task 兼 ui task,
組 `hal_screen_t` + snprintf 類負載)。**若日後任何人往本專案加回 opus,主棧必須同步回 12288。**

LVGL partial buffer 結論:**240×20×2 = 9600B,單緩衝**(§8.1 表)。

---

## 13. sdkconfig.defaults 與 partitions.csv(全文)

### 13.1 `poker/sdkconfig.defaults`

```
# --- 目標晶片:ESP32-C3(單核 RISC-V @160MHz)---
CONFIG_IDF_TARGET="esp32c3"
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_160=y

# --- 控制台必須走 USB-Serial-JTAG:UART0 預設腳 GPIO20/21 已被 VB6824 與 USB 檢測佔用 ---
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y

# --- Flash 8MB **DIO** + 自訂分區(容 app + voice)---
# ★★ 嚴禁改 QIO(勿照抄 StickS3):本板 Zbit 25VQ64 跑 DIO,GPIO12/13 因 DIO 才空閒,
#    QIO 會把它們收回作 WP/HD → LCD 片選與電源自鎖雙雙失效(黑屏 + 開機即斷電)。
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="8MB"
CONFIG_ESPTOOLPY_FLASHMODE_DIO=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"

# --- 主棧 & FreeRTOS(§12:無 opus,8KB 足;FREERTOS_HZ=1000 供 10ms 去抖/20ms 音訊節拍)---
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
CONFIG_FREERTOS_HZ=1000

# --- VB6824:UART1,輸入 Opus16k(不用)+ 輸出 PCM16k,發送用任務(demo 驗證組合)---
CONFIG_VB6824_UART_PORT=1
CONFIG_VB6824_TYPE_OPUS_16K_20MS_PCM_16K=y
CONFIG_VB6824_SEND_USE_TASK=y
# CONFIG_VB6824_OTA_SUPPORT is not set

# --- LVGL 字體(與 StickS3 同三檔:14/20/28)---
CONFIG_LV_FONT_MONTSERRAT_14=y
CONFIG_LV_FONT_MONTSERRAT_20=y
CONFIG_LV_FONT_MONTSERRAT_28=y

# --- Wi-Fi 僅用於 ESP-NOW:C3 無 PSRAM,收緊緩衝 + 關 AMPDU(demo 實機驗證值)---
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=6
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=8
CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=8
CONFIG_ESP_WIFI_AMPDU_TX_ENABLED=n
CONFIG_ESP_WIFI_AMPDU_RX_ENABLED=n
```

### 13.2 `poker/partitions.csv`(與 StickS3 逐位元組一致)

```
# Name,     Type, SubType,  Offset,   Size,     Flags
nvs,        data, nvs,      0x9000,   0x6000,
phy_init,   data, phy,      0xf000,   0x1000,
factory,    app,  factory,  0x10000,  0x3E0000,
voice,      data, 0x40,     0x3F0000, 0x120000,
```

- `voice` 的**分區名與 subtype 0x40 四機必須一致**(S3 §18.2;§6.3 的載入碼即按此查找)。
- 本表覆蓋出廠雙 OTA 佈局:出廠的 `model` 分區(0x10000,VB6824 喚醒模型)被 factory
  app 覆蓋 —— **無礙**,撲克不用喚醒詞/VB OTA;VB6824 自身韌體在其自己的晶片裡。

---

## 14. 建置、燒錄、voice.bin

```bash
. ~/.espressif/v6.0.1/esp-idf/export.sh
export PATH="$HOME/.espressif/tools/riscv32-esp-elf/esp-15.2.0_20251204/riscv32-esp-elf/bin:$PATH"

cd devices/zuowei-c3-lcd-pwr
idf.py set-target esp32c3               # 僅首次
idf.py build                            # 正式建置
idf.py build -DPK_DEBUG_SOLO=1          # 單機冒煙建置(S3 §20.3;正式包必不帶,title 有 [SOLO] 防呆)
idf.py -p <PORT> flash monitor

# voice.bin(四機共用同一份,common/voice/ 已生成入版控)只在語音資產變更時單獨燒:
python $IDF_PATH/components/partition_table/parttool.py -p <PORT> \
    write_partition --partition-name=voice --input ../../common/voice/voice.bin
# 或:esptool --port <PORT> write-flash 0x3F0000 ../../common/voice/voice.bin
```

host 端測試沿用 `common/tools`(S3 §20.2 原樣,與裝置無關,已 37 檢查全過——
本裝置**不需要**重跑除非改了共用碼,而共用碼凍結):

```bash
tools/run_tests.sh        # 從 repo 根;-Werror 建置,印 "N passed, 0 failed"
```

---

## 15. 分階段驗收(對照產品 §9;C3 特有項加粗)

沿 S3 §20.4 四階段框架,每階段過了才進下一階段。C3 特有項是本表的存在理由:

| 階段 | 建置 | 內容(僅列 C3 特有/加嚴項;通用項見 S3 §20.4) | 對應產品 §9 |
|---|---|---|---|
| 0 冒煙 | SOLO | 通用全項,另加:**①電池開機**:拔 USB、長按中鍵 2s 開機成功(自鎖生效,§7.2);**②中鍵長按關機**:串口見 "power off" 日誌後整機斷電;**③LCD 非黑屏**(CS=GPIO12 自檢,§8.4)且 `GPIO 12 conflict` 警告僅一條屬預期;**④電量顯示**:拔 USB 顯示 %(ADC1 讀數正常、Wi-Fi 掃描期間讀值不跳 —— 對照 xingzhi ADC2 之坑,本板應天然通過);插 USB 顯示 `USB`;**⑤V_BEEP/數字播報**經 VB6824 出聲、VOLUME 三檔即試播 | 16 |
| 1 成桌儀式 | 正式 ×2–3(**必含 1 台 StickS3**) | 通用項 + **齊播延遲校準**(§6.5 全流程:量測→回填 AUDIO_PATH_LATENCY_MS→復測混型中位數 <30ms;抖動 >±20ms 則明文降級並記 §16) | 1、2、3 |
| 2 單街下注 | 正式 ×3 | 通用項(無 C3 特有) | 4–7 |
| 3 全流程 | 正式 ×3+ | 通用項 + **①優雅關機交接**:本機任 Master 時中鍵長按 → 觀察 E_MASTER_HANDOFF 在斷電前發出、他機 ≤2s 無縫接手(不觸發 §9.5 接管;這是全產品唯一可驗此路徑的機會);**②拔電池接管 ≤6s** 照常也要測(關機路徑 ≠ 掉電路徑);**③長局齊播漂移抽查**(≥1h 後重跑一次 §6.5 步驟 2,驗 §6.2 的晶振漂移限制是否可見) | 8–15、尤其 10 |

---

## 16. 實作經驗回寫(供後續裝置)

> **給實作 agent 的指示**:完成 zuowei-c3 實作(或任何一個驗收階段)後,把經驗按下列
> 小節追加到本章:**硬體無關**的(共用碼、編譯、文件歧義)同時回寫 S3 指南 §21;
> **本板硬體相關**的坑寫回 `zuowei-c3-lcd-pwr/hardware-reference.md`/`README.md`。
> 涉及 poker_core 介面的變更須走 S3 §21 升版流程(介面已凍結,只准加不准改)。
> 特別要求回填的實測值:`AUDIO_PATH_LATENCY_MS`(§6.5)、齊播抖動與是否降級(§6.5-4)、
> VB6824 開機就緒時序(§6.4)、長局漂移觀測(§15 階段 3-③)、任務棧 high-water(§12)。

### 16.1 板級/HAL 的坑(首版建置完成回寫)

1. **RISC-V GCC 15 比 StickS3 的 xtensa 工具鏈嚴,凍結核心會編不過。** 首建置即撞:
   `common/.../pbus_session.c` `on_cmd()` 觸發 `-Werror=stringop-overflow`(見 §16.4 R13,
   是真實 2-byte 溢位)。核心凍結不得改 → 於 `poker/CMakeLists.txt`(project() 前)加
   `idf_build_set_property(COMPILE_OPTIONS "-Wno-error=stringop-overflow" APPEND)` 把該檢查降回
   warning,整專案(含 core)即編過;**poker/main/ 自訂碼零 warning 不受影響**。真修法待主 agent 裁定。
2. **`ledc_channel_config_t` 的 `output_invert` 在 IDF6 是 `.flags.output_invert`**(demo backlight.c
   用 `.flags.output_invert=BL_OUTPUT_INVERT`);合併進 hal_display.c 的 `bl_init_off()` 時要用
   designated init `.flags = { .output_invert = BL_OUTPUT_INVERT }`,漏了不影響本板(invert=false)但別掉。
3. **`hal_display_init` 全用 board_config 宏、不照抄 StickS3 硬編值**:spi_mode 用 `LCD_SPI_MODE`(=3)、
   set_gap 用 `LCD_OFFSET_X/Y`(=0,0)、單緩衝 `buffer_size=LCD_H_RES*20 double_buffer=false`、
   `lvgl_port` 只設 priority=4/stack=8192**不設 task_affinity**(C3 單核,ESP_LVGL_PORT_INIT_CONFIG
   預設 -1)。照抄 StickS3 的 spi_mode=0/雙緩衝/affinity=1 會黑屏或浪費 ~28KB。
4. **電池 `(int)(mv * BAT_DIVIDER)`**:`BAT_DIVIDER` 是 `6.08f`,`mv` 是 int,乘積為 float,顯式轉 int
   無 warning;`BAT_FULL_MV/EMPTY_MV` 在 board_config 用**整數**(4200/3300)以走整數百分比運算。
5. **`snprintf`/`memcpy` 等要顯式 include**:hal_display.c 需 `<string.h>`+`<stdio.h>`;hal_audio.c 需
   `<string.h>`(沿 S3 §21.2-4d)。GCC 15 下這幾點都乾淨通過。
6. `hal_input.c` 的 `board_poweroff()` 在 buttons task(棧 2048)上下文執行,靜態編過;實際 high-water
   待真機(§16.3)。中鍵長按 `pwr_down_ms += 10` 的自增在 `if` 條件內,GCC 15 不報 misleading-indentation。

### 16.2 音訊路徑實測數據

- `AUDIO_PATH_LATENCY_MS` 仍為計算初值 **80**(水位 60 + UART ~2 + VB6824 內部 ~20【純猜測】)。
  **跨機錯位中位數、抖動 max-min、是否降級「盡量對齊」全部待真機用 §6.5 錄音對拍法量測後回填。**
- VB6824 開機就緒時序(§6.4)待真機:若開機 V_BEEP 缺失,在 `enable_output(true)` 前補 `vTaskDelay(200ms)`。
- 長局齊播漂移(§15 階段 3-③,±40ppm 晶振)待 ≥1h 真機抽查。

### 16.3 SRAM/性能實測

- **app 映像 `poker_zuowei_c3.bin` = 0x11ec00 = 1,174,528 bytes**(factory 分區 0x3E0000=4,063,232,
  **餘 71% / 0x2c1400**);對照 StickS3 的 1,135,376 bytes,+~39KB(vb6824 元件 + esp_adc,少了 i2c/i2s/es8311)。
- voice 分區 0x120000(1.125MB)容 common/voice/voice.bin(S3 實測 418KB),餘裕充足。
- 任務棧設定(§3 任務表):audio 4096 / buttons 2048 / ui(主棧)8192 / LVGL 8192 / pbus 6144(core)。
  **各棧 high-water、heap 餘量均為靜態估算(§12 合計 ~194KB,餘 >200KB),真機待補。**

### 16.4 文件歧義的新裁定(續 S3 §4/§21.3,R 編號續編)

**R13|凍結核心 `pbus_session.c` `on_cmd()` NOT_MASTER 導向回覆的真實堆疊緩衝溢位。**
`uint8_t body[8]` 卻 `memcpy(body + sizeof(pn_cmd_ack_t)/*=4*/, master_mac, 6)` 並以 `4+6=10` bytes
`pn_send_typed` 送出 —— 寫/讀越界 2 bytes(`pbus_proto.h` 行 107 註解確認尾端須附 mac[6],故 body 應為
`[10]`)。riscv32 GCC 15 以 `-Werror=stringop-overflow` 擋下,StickS3 xtensa 工具鏈未偵測。**裁定**:
核心凍結,本次不改;板級以 `-Wno-error=stringop-overflow` 降級讓建置過,行為與 StickS3 現行(同帶此 bug)
一致、未加劇。**真修法**(`body[10]`)須走 S3 §21 缺陷修復升版流程,**由主 agent 裁定**。已同步回寫
S3 §21.3 R13 與 §21.2-7(跨裝置通用經驗)。

> **✅ 已裁定修復(2026-07-21,主 agent)**:核心已改 `body[sizeof(pn_cmd_ack_t)+6]`,
> 本專案 CMakeLists 的 `-Wno-error` 降級已移除,esp32c3 重建綠(bin 0x11ec00)。
