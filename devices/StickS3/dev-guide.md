# StickS3 德州撲克遊戲機 軟體開發指南 v1.0

> 本指南是四種裝置中的**第一份**開發指南。除 StickS3 本身的板級/HAL 實作外,
> 它**定義全專案的共用程式碼佈局**(§1–§5):後續 zuowei-c3-lcd-pwr、xingzhi-cube、
> Cardputer-Adv 三份指南與實作一律以本文佈局為基準,**不得修改 `common/` 下任何檔案的
> 介面與行為**(除非依 §21 回寫流程升版)。
>
> 規格權威層級(衝突時由高到低):
> 1. `../../common/PROTOCOL.md` v1.1 —— 協定層唯一權威
> 2. `../../common/PRODUCT-SPEC.md` v1.1 —— 架構分層/HAL/畫面/語音唯一權威
> 3. `StickS3/hardware-reference.md` —— StickS3 硬體唯一權威(標【實測】者優先於官方數據)
> 4. 本指南 —— 只補「實作者會卡住的地方」,不重抄上述文件;引用以「協定 §x」「產品 §x」表示。
>
> 本指南中的 M5PM1 暫存器位址與位定義,已對照本機已驗證的參考實作
> 官方 M5PM1 庫(github.com/m5stack/M5PM1)與真機驗證過的 M5PM1 驅動實作
> Arduino 庫(`M5PM1.h` 暫存器註釋)雙重核對,可直接照抄。

---

## 0. 讀者與範圍

- 讀者:負責實作的 coding agent / 韌體工程師,假定熟悉 ESP-IDF v6.0.1、FreeRTOS、LVGL 9。
- 產出物:`common/`(全裝置共用,一次寫成)+ `StickS3/`(本裝置 IDF 專案)。
- 開發順序建議:§20 的分階段驗收即開發順序 —— 先讓 solo 冒煙跑通,再上多機。

---

# 第一部分:全專案共用佈局(全裝置遵守)

## 1. 目錄佈局與檔案歸屬

```
Texas/
├─ common/                              # ★ 全裝置共用,唯一一份
│  ├─ components/
│  │  └─ poker_core/                    # IDF 元件(經 EXTRA_COMPONENT_DIRS 引用)
│  │     ├─ CMakeLists.txt
│  │     ├─ include/                    # 公開標頭(§3 逐一定義)
│  │     │  ├─ pk_config.h              #   常數總表 + PK_DEBUG_SOLO
│  │     │  ├─ pbus_proto.h             #   線上格式(協定文件全部 packed 結構)
│  │     │  ├─ pbus.h                   #   協定層介面(協定 §14 + 裁定 R2 補遺)
│  │     │  ├─ game_state.h             #   狀態鏡像 reducer + game_view(產品 §3.2)
│  │     │  ├─ master_engine.h          #   Master 決策引擎
│  │     │  ├─ hand_eval.h              #   7 選 5 評估器
│  │     │  ├─ voice.h                  #   ADPCM 拉取式解碼 + 數字合成
│  │     │  ├─ voice_ids.h              #   ← 生成物(tools/gen_voice.sh 回填,入版控)
│  │     │  ├─ app_flow.h               #   畫面狀態機入口
│  │     │  ├─ input_gesture.h          #   手勢合成(產品 §2.1)
│  │     │  └─ hal/                     #   HAL「介面」(僅標頭;實作在各裝置 main/)
│  │     │     ├─ hal_display.h  hal_input.h  hal_audio.h
│  │     │     ├─ hal_battery.h  hal_power.h  hal_misc.h
│  │     └─ src/
│  │        ├─ pbus.c                   # 協定 task 主迴圈、交付、CRC 環
│  │        ├─ pbus_transport.c         # Wi-Fi/ESP-NOW init、recv 回呼、發送
│  │        ├─ pbus_reliab.c            # 事件日誌、亂序緩衝、補洞、CMD 重試/去重
│  │        ├─ pbus_session.c           # 發現/建桌/加入/心跳/接管三階段/交接
│  │        ├─ pbus_clock.c             # 桌面時鐘 EWMA
│  │        ├─ game_state.c             # 純 reducer(host 可編譯)
│  │        ├─ master_engine.c          # 命令驗證、牌局推進、儀式/超時計時
│  │        ├─ side_pot.c               # 側池切池結算(host 可編譯)
│  │        ├─ hand_eval.c              # 純函式(host 可編譯)
│  │        ├─ voice_adpcm.c            # IMA-ADPCM 拉取式解碼 + 資產索引
│  │        ├─ number_speech.c          # 0–9999 → voice_id 序列
│  │        ├─ app_flow.c               # 畫面狀態機 + screen_from_state
│  │        ├─ app_screens.c            # 各畫面 → hal_screen_t 的組裝
│  │        └─ input_gesture.c          # SHORT/LONG/REPEAT 合成
│  ├─ voice/
│  │  ├─ voice_list.tsv                 # 片段清單(id<TAB>text,順序即 enum 順序)
│  │  └─ voice.bin                      # ← 生成物(四機位元組一致,入版控)
│  └─ tools/
│     ├─ gen_voice.sh                   # macOS say + afconvert → wav → voice.bin
│     ├─ wav2adpcm.py                   # 打包器(§19 規格)
│     └─ test_hand_eval.c               # host 端牌型向量測試(§20.2)
├─ StickS3/                       # 本裝置專案(§12–§19)
│  ├─ CMakeLists.txt
│  ├─ sdkconfig.defaults
│  ├─ partitions.csv
│  └─ main/
│     ├─ CMakeLists.txt  idf_component.yml
│     ├─ board_config.h                 # 全部引腳/板級常數(唯一放 GPIO 的地方)
│     ├─ app_main.c
│     ├─ board_i2c.[ch]                 # 共享 I2C 匯流排(G47/G48,新驅動)
│     ├─ m5pm1.[ch]                     # M5PM1 驅動(§13)
│     ├─ hal_display.c  hal_input.c  hal_audio.c
│     ├─ hal_battery.c  hal_power.c  hal_misc.c
│     └─ imu_bmi270.c                   # 加分項,TODO 可留空殼(§16.2)
├─ zuowei-c3-lcd-pwr/             # 後續裝置同構(main/ = HAL 實作 + 板級)
├─ xingzhi-cube-1.54tft-wifi/
└─ Cardputer-Adv/
```

**歸屬劃線(硬性)**:

| 區域 | 誰可以改 | 內容 |
|---|---|---|
| `common/components/poker_core/**` | 只有「經 §21 回寫流程升版」可改 | 協定、遊戲核心、UI 流程、手勢、語音解碼、HAL **介面** |
| `common/voice/**`、`common/tools/**` | 同上 | 語音資產與生成管線(四機共用同一 voice.bin) |
| `<device>/main/**` | 各裝置指南/實作自理 | HAL **實作**、板級驅動、app_main、board_config.h |
| `<device>/{CMakeLists.txt,sdkconfig.defaults,partitions.csv}` | 各裝置自理 | 建置與分區 |

判定準則:**檔案裡出現 GPIO 編號、I2C 位址、esp_lcd/esp_driver_* 呼叫 → 必屬裝置 main/**;
出現協定結構、牌局規則、畫面遷移 → 必屬 poker_core。共用碼禁止 `#ifdef` 板型(產品 §1.3)。

## 2. 建置系統(CMakeLists 全文)

### 2.1 `common/components/poker_core/CMakeLists.txt`

```cmake
idf_component_register(
    SRCS
        "src/pbus.c" "src/pbus_transport.c" "src/pbus_reliab.c"
        "src/pbus_session.c" "src/pbus_clock.c"
        "src/game_state.c" "src/master_engine.c" "src/side_pot.c"
        "src/hand_eval.c"
        "src/voice_adpcm.c" "src/number_speech.c"
        "src/app_flow.c" "src/app_screens.c" "src/input_gesture.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_event esp_timer nvs_flash
)
# 單機冒煙模式:idf.py build -DPK_DEBUG_SOLO=1(§20.3)
if(PK_DEBUG_SOLO)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC PK_DEBUG_SOLO=1)
endif()
```

注意:poker_core **不** REQUIRES 任何顯示/音訊/GPIO 驅動 —— 它只宣告 `hal/*.h`,
符號由裝置 main 元件在最終連結時提供(靜態庫連結順序 IDF 自動處理)。
ESP-NOW 標頭 `esp_now.h` 由 `esp_wifi` 元件提供,無需另列。

### 2.2 `StickS3/CMakeLists.txt`(頂層)

```cmake
cmake_minimum_required(VERSION 3.16)

# 引用全專案共用元件(後續裝置照抄本行,只改相對深度)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../common/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(poker_sticks3)
```

### 2.3 `StickS3/main/CMakeLists.txt`

```cmake
idf_component_register(
    SRCS
        "app_main.c" "board_i2c.c" "m5pm1.c"
        "hal_display.c" "hal_input.c" "hal_audio.c"
        "hal_battery.c" "hal_power.c" "hal_misc.c"
        "imu_bmi270.c"
    INCLUDE_DIRS "."
    REQUIRES
        poker_core
        esp_lcd esp_lvgl_port lvgl
        esp_driver_spi esp_driver_i2c esp_driver_i2s
        esp_driver_gpio esp_driver_ledc
        esp_timer esp_partition spi_flash
)
```

### 2.4 `StickS3/main/idf_component.yml`

```yaml
dependencies:
  idf: ">=6.0"
  espressif/esp_lvgl_port: "^2"
  espressif/es8311: "^1"
```

> `espressif/es8311` 若其現行版本仍呼叫舊版 I2C 驅動而在 IDF v6 下編譯失敗
> (IDF v6 已移除 legacy `driver/i2c.h`),處置:把該元件的 `es8311.c/h` 拷入
> `main/`,將其 I2C 讀寫函式機械式改寫為 `i2c_master_transmit/…_receive`
> (參照 §13 的 m5pm1.c 寫法,約 20 行改動),並把結果記入 §21。

## 3. poker_core 公開介面(標頭逐一定義)

以下簽名為**規範**;實作可增私有標頭(`src/*_int.h`),不得增改公開簽名。

### 3.1 `pk_config.h`

```c
#pragma once
// 協定 §13 常數表全量照抄為 PN_T_* / PN_* 巨集(值不得改),此處只列指南新增者:
#define PK_CHIPS_MAX        9999          // 產品 §3.2
#define PK_CHIPS_DEFAULT    15
#define PK_EVT_SLOT_BYTES   210           // 事件日誌定長槽(產品 §8.1:容 E_ROSTER 201B)
#define PK_OOO_BUF_SLOTS    8             // 亂序緩衝(協定 §6.2)
#define PK_GESTURE_LONG_MS  600           // 產品 §2.1
#define PK_GESTURE_REPEAT_MS 120
#define PK_GESTURE_X10_MS   2000

#ifdef PK_DEBUG_SOLO                      // §20.3 單機冒煙
#define PK_MIN_PLAYERS      1
#define PK_PAUSE_MIN_ALIVE  1
#else
#define PK_MIN_PLAYERS      3             // 協定 §8.1
#define PK_PAUSE_MIN_ALIVE  2             // 協定 §8.6
#endif
```

### 3.2 `pbus_proto.h`

內容 = 協定文件 §5、§6、§8、§11、§18 的**全部** packed 結構與枚舉原文照抄
(`pn_hdr_t`、`pn_pkt_type_t`、`pn_cmd_*`、`pn_evt_*`、`pn_table_state_t`、
`pn_heartbeat_t`…),加上事件/命令 ID 枚舉(`PN_EVT_ROSTER=1 …`、`PN_CMD_DEALER_CLAIM=1 …`),
以及**按裁定 R1(§4)修正後的** `_Static_assert` 表。不在此重抄;僅列修正後的斷言值:

```c
_Static_assert(sizeof(pn_hdr_t)              == 14, "");
_Static_assert(sizeof(pn_table_state_t)      == 185, "");  // 裁定 R1:非 §18 的 167
_Static_assert(sizeof(pn_snapshot_t)         == 189, "");  // 裁定 R1:非 §18 的 171
_Static_assert(sizeof(pn_evt_roster_t)       == 201, "");
_Static_assert(sizeof(pn_evt_hand_start_t)   == 92,  "");
_Static_assert(sizeof(pn_evt_action_req_t)   == 8,   "");
_Static_assert(sizeof(pn_evt_action_t)       == 16,  "");
_Static_assert(sizeof(pn_evt_street_t)       == 4,   "");
_Static_assert(sizeof(pn_evt_hand_result_t)  == 95,  "");  // 裁定 R1:非 §18 的 96
_Static_assert(sizeof(pn_evt_hand_abort_t)   == 52,  "");
_Static_assert(sizeof(pn_heartbeat_t)        == 10,  "");
_Static_assert(sizeof(pn_status_t)           == 7,   "");
_Static_assert(sizeof(pn_hello_t)            == 11,  "");
_Static_assert(sizeof(pn_announce_t)         == 4,   "");
_Static_assert(sizeof(pn_join_ack_t)         == 10,  "");
```

另補一個協定文件未給結構、只給欄位的裝飾性事件負載:

```c
typedef struct __attribute__((packed)) {
    uint8_t target_seat;   // 0xFF=全體
    uint8_t kind;          // 1=催行動(重播 V_YOUR_TURN) 2=儀式重播提示
} pn_evt_remind_t;         // E_REMIND (0x80)
```

### 3.3 `pbus.h`

```c
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "pbus_proto.h"

typedef enum {
    PBUS_LINK_JOINED, PBUS_LINK_PENDING, PBUS_LINK_LOST,
    PBUS_LINK_RESYNCED, PBUS_LINK_DISSOLVED,
} pn_link_state_t;

typedef struct {                                     // 協定 §14 原文
    void (*on_event)(const pn_evt_hdr_t *e, const void *body, size_t len);
    void (*on_role)(bool i_am_master);
    void (*on_link)(pn_link_state_t s);
    uint32_t (*get_battery_pct)(void);               // 0xFF=未知(裁定 R3)
} pbus_callbacks_t;

esp_err_t pbus_start(const pbus_callbacks_t *cb, const char *name);
esp_err_t pbus_submit_cmd(uint8_t cmd, const void *arg, size_t len);
esp_err_t pbus_publish_evt(uint8_t evt, const void *body, size_t len, uint32_t play_at);
uint32_t  pbus_table_now(void);
uint32_t  pbus_local_time_for(uint32_t table_ms);
const pn_table_state_t *pbus_state(void);            // 唯讀權威狀態鏡像

/* ---- 介面補遺(裁定 R2,協定 §14 缺 Master 收 CMD 的路徑)---- */
typedef struct { uint8_t result; uint8_t reason; } pbus_cmd_verdict_t;
typedef pbus_cmd_verdict_t (*pbus_cmd_handler_t)(uint8_t player_id, uint8_t cmd,
                                                 const void *arg, size_t len);
void    pbus_set_cmd_handler(pbus_cmd_handler_t h);  // 僅本機為 Master 時被呼叫
void    pbus_set_idle_hook(void (*hook)(void));      // 協定 task 每 ≤50ms 呼叫一次
uint8_t pbus_my_player_id(void);                     // 0xFF=未入桌
bool    pbus_is_master(void);
void    pbus_leave(void);                            // 本地清會話 → 回掃描(C_LEAVE 已送出後用)
```

### 3.4 `game_state.h`(產品 §3.2 原文 + 接線函式)

```c
#pragma once
#include "pbus_proto.h"
#include <stdbool.h>

typedef struct {
    const pn_table_state_t *st;
    uint8_t  my_player_id, my_seat;
    bool     my_turn;
    uint16_t call_amt, min_raise_to, max_raise_to;
    bool     can_check;
    uint8_t  revealed_streets;
    uint8_t  cmd_inflight;
    uint8_t  last_reject_reason;      // 讀取即清零
} game_view_t;

void game_init(const char *my_name);  // 掛 pbus callbacks + reducer + cmd handler,呼叫 pbus_start
const game_view_t *game_view(void);   // UI task 讀;見 §7 併發規範

void game_submit_action(uint8_t action, uint16_t raise_to);
void game_submit_chips(uint16_t amount);
void game_submit_blinds(uint8_t sb, uint8_t bb, uint16_t cap);
void game_submit_dealer_claim(void);
void game_submit_seat_claim(uint8_t seat_no);
void game_submit_join_decide(uint8_t cand_id, bool allow);
void game_submit_ready_next(void);
void game_submit_leave(void);
void game_submit_ceremony_skip(void);

/* 純 reducer:pbus 交付路徑內呼叫;host 可編譯測試 */
void game_state_apply(pn_table_state_t *st, const pn_evt_hdr_t *e,
                      const void *body, size_t len);
```

### 3.5 `master_engine.h`

```c
#pragma once
#include "pbus.h"

void master_engine_enable(bool on);        // 由 game_init 掛在 on_role 上
pbus_cmd_verdict_t master_engine_on_cmd(uint8_t player_id, uint8_t cmd,
                                        const void *arg, size_t len);
void master_engine_tick(void);             // 掛 pbus idle hook:超時代打/儀式重播/自動代設/局間時序
```

### 3.6 `hand_eval.h`

```c
#pragma once
#include <stdint.h>
typedef struct { uint8_t cat; uint8_t kick[5]; } hand_rank_t;  // cat 同協定 §11.4(9=皇家)
hand_rank_t hand_eval7(const uint8_t cards[7]);   // card = rank*4+suit(協定 §5)
int  hand_rank_cmp(const hand_rank_t *a, const hand_rank_t *b);  // >0:a 勝;0:平
```

### 3.7 `voice.h`

```c
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "voice_ids.h"                    // 生成物:typedef enum {...} voice_id_t; VOICE_COUNT

esp_err_t voice_init(const void *bin, size_t len);   // 傳入 voice.bin 映射位址(§19.3)
typedef struct voice_stream voice_stream_t;
voice_stream_t *voice_open(voice_id_t id);           // NULL=id 無效
int  voice_read(voice_stream_t *h, int16_t *pcm, int max_samples);  // 回實際樣本數;0=結束
void voice_close(voice_stream_t *h);
uint32_t voice_duration_ms(voice_id_t id);

int voice_expand_number(uint16_t n, voice_id_t out[8]);  // 0..9999 → 片段序列,回長度(≤6)
```

### 3.8 `app_flow.h` / `input_gesture.h`

```c
// app_flow.h
#pragma once
void app_flow_start(const char *device_name);  // 建 UI task;內部呼叫 game_init
```

```c
// input_gesture.h
#pragma once
#include "hal/hal_input.h"
typedef enum {
    GEST_OK_SHORT, GEST_OK_LONG, GEST_OK_REPEAT,
    GEST_NEXT_SHORT, GEST_NEXT_LONG, GEST_NEXT_REPEAT,
} gesture_t;
typedef void (*gesture_cb_t)(gesture_t g);
void input_gesture_init(gesture_cb_t cb);      // 內部呼叫 hal_input_init;600ms/120ms/2s×10 見 pk_config.h
void input_gesture_ignore_repeat(bool ign);    // 選單型畫面設 true(產品 §2.1)
```

### 3.9 `hal/*.h`

內容 = 產品文件 §2.1–§2.6 的介面**原文照抄**,僅補型別落地:

```c
// hal/hal_display.h 補充定義
typedef uint8_t hal_card_t;                    // 協定 §5 卡牌編碼;0xFF=空位
enum { TC_NORMAL, TC_ALERT, TC_TURN, TC_WIN }; // title_color
// hal_screen_t / hal_showdown_page_t / init / render / render_showdown:產品 §2.2 原文
void hal_display_set_brightness(uint8_t pct);  // 10–100;裁定 R9(全裝置必須實作)
```

`hal_input.h`(產品 §2.1)、`hal_audio.h`(§2.3)、`hal_battery.h`(§2.4)、
`hal_power.h`(§2.5,`app_prepare_poweroff()` 由 poker_core 實作、板級呼叫)、
`hal_misc.h`(§2.6)。

## 4. 文件矛盾裁定(本指南權威決議)

實作**必須**按以下裁定執行;每條附理由,供後續指南沿用。

**R1|協定 §18 sizeof 表三處與 §6.6/§11.4 結構定義不符。**
按欄位逐位元組相加:`pn_table_state_t` = 7+4+16×10+14 = **185**(§18 誤記 167;
§6.6 註釋「本局(12 bytes)」實為 14);`pn_snapshot_t` = 2+2+185 = **189**(空中 203,
仍單包);`pn_evt_hand_result_t` = 3+3×10×3+2 = **95**(§18 誤記 96)。
裁定:**結構欄位定義為權威**,`_Static_assert` 鎖定 185/189/95(§3.2 表)。
理由:欄位是四機互通的實體,尺寸只是推導值;且全部仍 ≤250,不影響單包原則。
四機共用 poker_core 同一份 `pbus_proto.h`,不存在異構分歧風險。

**R2|協定 §14 自稱「完整介面」,但 Master 決策引擎收不到 CMD。**
§14 的七函式+四回呼沒有任何把玩家 CMD 交給 Master 端驗證的路徑。裁定:新增
`pbus_set_cmd_handler()`(§3.3):pbus 完成 §6.3 的 `(MAC,cmd_id)` 去重與重複 ACK 後,
把**首見**命令交 handler 同步裁決,pbus 據回傳值發 `CMD_ACK`。
理由:去重/重試/ACK 屬可靠層職責留在 pbus,命令語義裁決屬遊戲層 —— 分界與 §1 深模組原則一致;
線上格式零改動。

**R3|`pbus_callbacks_t.get_battery_pct` 回傳 `uint32_t`,產品 §2.4 `hal_battery_pct` 回傳 `uint8_t`。**
裁定:兩者原樣保留,`game_init` 中以轉接函式黏合(`return (uint32_t)hal_battery_pct();`)。
理由:兩份文件各自為權威,轉接零成本。

**R4|產品 §3.1 模組名 `voice_adpcm.[ch]` vs 佈局要求公開標頭 `voice.h`。**
裁定:公開標頭 = `voice.h`(§3.7),實作檔 = `src/voice_adpcm.c`。命名對齊,非語義衝突。

**R5|手勢長按閾值:產品 §2.1 定 600ms,兩個 demo 用 800ms。**
裁定:600ms(產品文件是權威,demo 只是參考碼)。

**R6|`PK_DEBUG_SOLO` 對暫停規則的最小外溢。**
需求說 solo 旗標「只影響 join 窗口判斷」,但協定 §8.6「存活 <2 → E_GAME_PAUSE」會讓
單人桌成桌即暫停,冒煙目的落空。裁定:`PK_MIN_PLAYERS` 與 `PK_PAUSE_MIN_ALIVE`
兩個本地判斷常數同時受旗標控制(§3.1),**線上格式與事件語義零改動**,
與正式韌體互通時行為仍正確(它只會更寬容,不會發出非法事件)。

**R7|StickS3 橫向偏移推導。**
硬體文件【實測】給的是直向 (52,40)(`invert_color=true`);esp_lcd 的
`swap_xy(true)` 後 CASET/RASET 軸互換,`esp_lcd_panel_set_gap` 必須跟著換軸 →
橫向為 **set_gap(40, 52)**。此為推導值,§14.3 附上開機自檢步驟(邊框矩形)驗證,
若有 ±1 偏差以實測回寫 §21。

**R8|E_REMIND 負載結構協定未給。** 裁定:按 §3.2 的 `pn_evt_remind_t`(2 bytes)。
裝飾性事件(0x80+),舊裝置忽略即可,不涉及升版。

**R9|使用者體驗追加需求(2026-07-21):每台裝置須能 ①顯示電量百分比
②調整螢幕亮度 ③調整音量。**
產品 §5.6 系統選單只有音量。裁定:系統選單擴充為
`[RESUME] [BUY-IN](僅淘汰者,局間) [VOLUME] [BRIGHTNESS] [LEAVE TABLE]`,
並在選單首行常駐顯示 `BATT <pct>%`(`hal_battery_pct()==0xFF` 時顯示
`BATT USB`,充電中加 `⚡`/`CHG`);HAL 新增 `hal_display_set_brightness()`(§3.9),
BRIGHTNESS 項與 VOLUME 同交互:NEXT 循環 30/60/100%(即時生效)。
理由:三項全屬本機 UI/板級行為,零協定影響;放系統選單(LONG_OK 全畫面可達)
不增加任何新手勢,不破壞 2 鍵心智。實作位置:選單邏輯在共用 `app_flow.c`
(四機自動獲得),亮度/音量落地在各裝置 HAL。亮度與音量檔位選擇**不持久化**
(牌局本就不落 NVS,協定 §17.2 同哲學),開機回預設(亮度 80、音量 70)。

## 5. 任務模型(全裝置基準;S3 雙核佈局)

遵守協定 §4 執行模型:recv 回呼只入佇列;協定 task 優先級低於 Wi-Fi、不高於音訊。

| Task | 來源 | 核 | 優先級 | 棧 | 職責 |
|---|---|---|---|---|---|
| `wifi`(系統) | esp_wifi | 0 | 23 | — | ESP-NOW recv 回呼在此執行:**只做長度/magic/version 檢查 + memcpy 入佇列** |
| `pbus` | poker_core | 0 | 12 | 6144 | L1–L3 全部處理;`game_state_apply`、`on_event`、`master_engine_on_cmd/tick`(idle hook)都在此 task 上下文 |
| `audio` | 裝置 hal_audio | 1 | 15 | 4096 | I2S 常開填充、`voice_read` 拉流、排程切入(§15.3) |
| `LVGL`(esp_lvgl_port) | 裝置 hal_display | 1 | 4 | 8192 | `lv_timer_handler`;共用碼經 `hal_display_render` 間接觸達 |
| `ui`(main task 兼) | app_flow | 0 | 5 | 8192 | 手勢消費、`game_view` 輪詢、組 `hal_screen_t`、≤10fps 重繪 |
| `buttons` | 裝置 hal_input | 0 | 6 | 2048 | 10ms 輪詢去抖 → raw 回呼 |

- 約束檢查:pbus(12) < Wi-Fi(23) ✓;pbus(12) ≤ audio(15) ✓(協定 §4.2)。
- 單核裝置(zuowei-C3)沿用同一優先級表,去掉核綁定;**優先級數字不得改**
  (C3 的 VB6824 音訊管線優先級 9 教訓已折算進本表:C3 指南可把 audio 提到其板上管線之上,但 pbus 不得高於 audio)。
- 併發規範:牌局狀態只在 pbus task 寫;UI task 只讀 `game_view()` 快照(§7);
  `pbus_publish_evt`/`pbus_submit_cmd` 內部入佇列,任意 task 可呼叫。

---

# 第二部分:共用核心各模組實作要點

只寫「會卡住的地方」;規格本體見協定/產品文件對應章節。

## 6. pbus

### 6.1 檔案內部結構與關鍵資料

```c
// pbus 私有狀態(全部只在 pbus task 觸碰)
typedef struct {
    // 會話
    uint16_t table_id, epoch;
    uint8_t  master_mac[6];  uint8_t my_player_id;
    enum { SCAN, JOINING, PENDING, TEMP_MASTER, MEMBER, MASTER,
           TAKEOVER_STAGGER, TAKEOVER_CLAIM } role;
    // 可靠層
    uint16_t next_expected_seq;                 // 成員側
    uint16_t seq_alloc;                         // Master 側:下一個要編派的 seq
    uint8_t  evt_log[PK_EVT_LOG][PK_EVT_SLOT_BYTES]; // Master 64 / 成員 32 槽,環形,槽含空中原包
    struct { uint16_t seq; uint16_t len; uint8_t pkt[250]; } ooo[PK_OOO_BUF_SLOTS];
    uint32_t crc_ring[8];                       // 對齊比較用:seq&7 → 交付後 CRC
    // 時鐘
    int64_t  clock_offset_q8;                   // (table_ms - local_ms) << 8,EWMA α=1/4
    uint8_t  clock_freeze_hb;                   // 新任 Master 前 2 個心跳凍結 EWMA
    // CMD 客戶端
    uint16_t cmd_id;  /* 在途命令緩衝 + 重試計數 + 150ms 定時 */
    // Master 端命令去重表:{mac, last_cmd_id, last_verdict} × 10
    pn_table_state_t st;                        // 權威狀態鏡像(pbus_state() 回傳此)
} pbus_t;
```

- 事件日誌槽存**完整空中包**(hdr+evt_hdr+body),補發 EVT_RTX 直接重放原包,免重組。
- pbus task 主迴圈:`xQueueReceive(rxq, …, 20ms)` → 處理包;超時分支跑定時器輪
  (心跳、STATUS、HELLO/ANNOUNCE、gap timer、cmd 重試、接管計時)+ `idle_hook()`。
  所有「定時」用 `esp_timer_get_time()` 比較,不開額外 esp_timer 回呼(避免跨上下文)。

### 6.2 交付路徑(亂序緩衝,協定 §6.2)——偽代碼

```
on_evt_pkt(pkt):                            # 已過 magic/version/table_id 過濾
  if pkt.epoch < cur: drop
  if pkt.epoch > cur: adopt_epoch(pkt)      # 更新 epoch/master;缺洞照走補洞
  d = (int16_t)(pkt.seq - next_expected)
  if d < 0: drop                            # 重複/舊包
  elif d == 0:
      deliver(pkt); next_expected++
      loop: 若 ooo 有 seq==next_expected → deliver + 移除 + next_expected++
      if ooo 為空: cancel gap_timer
      else: rearm gap_timer                 # 仍有更遠的洞
  else:
      ooo_insert(pkt)                       # 滿 8 槽 → 丟 seq 最大者(離交付點最遠)
      if !gap_timer: arm(60 + rand(0..30) ms)

gap_timer 到期:
  to = (ooo 最小 seq) - 1
  GAP_REQ{next_expected, to} → 單播 Master   # 接管期改向最優候選(§6.4)
  重試 ≤4 次 / 200ms;仍缺 → SNAP_REQ(5 次,1s)→ 仍失敗 on_link(LINK_LOST)

deliver(pkt):
  game_state_apply(&st, evt_hdr, body)      # 先更新鏡像
  crc_ring[seq & 7] = crc32(&st, sizeof st) # 供 STATUS 對齊比較
  cb->on_event(evt_hdr, body, len)          # 上層絕不見亂序/缺洞/重複
```

注意:`E_MASTER_HANDOFF`/`E_MASTER_TAKEOVER` 是**正常編序事件**,epoch 推進發生在
交付它們(或收到更高 epoch 心跳)時 —— adopt_epoch 不重置 `next_expected`
(seq 跨任期接續,協定 §6.1)。

### 6.3 桌面時鐘(協定 §7)

```
on_master_pkt(hdr):                         # 任何來自現任 Master 的包
  sample = hdr.table_ms - local_ms()
  if !inited: offset = sample
  elif freeze_hb > 0: freeze_hb--           # 新任前 2 個心跳只驗證不更新
  else: offset += (sample - offset) / 4     # EWMA α=0.25(定點 Q8)
table_now()        = local_ms() + offset
local_time_for(t)  = t - offset
本機上任 Master:沿用已收斂 offset 繼續映射發包 → 時間軸無階躍;
廣播齊播事件 = 發 3 次(0/40/200ms)+ 緊跟一個立即心跳(協定 §6.1)。
```

### 6.4 接管三階段(協定 §9.5)——偽代碼

```
偵測: master_silence ≥ 3s(或單播連敗 5 次) 且 phase != LOBBY
      (LOBBY 期 → 直接清會話回 SCAN,防殭屍桌,協定 §8.1.3)

階段1 宣告:
  rank = 座位環序距離(排座前用 player_id 環序)     # 協定 §9.2
  等待 400ms × rank;期間:
     收到現任 HEARTBEAT(epoch≥cur) → 取消
     收到 MASTER_CLAIM 且 better(他,我) → FOLLOW:向其單播 CLAIM_INFO{my_last_seq}
  廣播 MASTER_CLAIM{epoch+1, my_last_seq, my_pid} ×3 /100ms

階段2 同步(身為最優候選):
  收 CLAIM_INFO:若 info.last_seq > my_last_seq →
     向該成員 GAP_REQ{my_last+1, info.last_seq}(它以尾端日誌 EVT_RTX 回)
  期間收到 better CLAIM → 立即退讓轉 FOLLOW
  T_CLAIM_SETTLE(800ms)到期:以「已收齊的最高 seq」封頂

階段3 上任:
  epoch = cur+1;seq_alloc = highest_seq + 1
  pbus_publish_evt(E_MASTER_TAKEOVER{new_epoch, dead_master_player})   # 佔一個 seq
  等 T_TAKEOVER_GRACE(300ms)收殘餘 CLAIM_INFO(落後者之後靠 STATUS→補發/快照)
  cb->on_role(true) → master_engine 依 st.phase 處置:
     phase==HAND → E_HAND_ABORT(退注表 = 各家 bet_hand,§8.3)
                   死者標離線;§9.2 順位下一人為新莊家 → 走局間時序開新局
     其他 → 以現行 phase 續跑(儀式進度都在權威狀態內)

better(a, b): a.last_seq > b.last_seq;平手取 §9.2 順位距離小者
雙 Master 消解:見到更高 epoch 心跳 → 立即降級 + SNAP_REQ
```

### 6.5 其他要點

- **CMD 客戶端**:在途僅允許 1 筆(UI 層 `cmd_inflight` 已擋新輸入);
  `CMD_ACK(NOT_MASTER)` → 用附帶 MAC 改投重送,`cmd_id` 不變(冪等)。
- **Master 端 CRC 對齊比較**:只比 `status.last_recv_seq` 落在 crc_ring 覆蓋窗內的;
  同一成員連續 2 次不符 → 單播 SNAPSHOT。
- **membership 事件由 pbus 發**(E_ROSTER/E_PLAYER_JOINED/OFFLINE/BACK/LEFT/E_JOIN_PENDING
  /E_JOIN_DECIDED 的編序廣播),L3 遊戲事件由 master_engine 發 —— 分工以「是否需要牌局
  知識」為界。E_JOIN_DECIDED 的觸發命令 C_JOIN_DECIDE 仍走 R2 handler,由 master_engine
  驗證後回頭呼叫 pbus 的 session 介面(src 內部函式,不公開)。
- 建桌衝突(MAC 字典序)、TABLE_DISSOLVE ×3、JOIN 窗口延長(<3 人 +5s×4)照協定 §8.1,
  無實作陷阱,不展開。

## 7. game_state

- `game_state_apply` 是**純 reducer**:`switch(evt)` 逐事件更新 `pn_table_state_t`,
  **凡事件內含權威數值(chips_left/bet_round/bet_hand/pot/cur_bet/raise_count)一律直接覆寫**,
  本地推算只用於交叉檢查 log(協定 §6.6)。未用槽位清零(參與 CRC!)。
- `game_view()` 併發規範:pbus task 每次交付後把 view 寫入雙緩衝
  (`view_buf[2]` + `atomic_uint idx`),UI task 讀 `view_buf[idx]`。
  遲一幀無害;禁止 UI 直接讀 `pbus_state()`(撕裂)。
- `last_reject_reason` 讀取即清零 → getter 內 `atomic_exchange(…, 0)`。
- `revealed_streets`:由 `E_STREET` 推進(UI 翻示狀態,非權威狀態一部分)。
- 本地預驗證(產品 §3.2):`game_submit_*` 在送出前按協定 §8.5/§11.3 同款規則檢查,
  非法直接設 `last_reject_reason`、不發包 —— 使空中 REJECT 只剩競態一種來源。

## 8. master_engine(含側池)

### 8.1 內部結構

```
master_engine.c
  ├─ engine_ctx:{ deck[52], deck_pos, acted_bitmap, street_deadline,
  │               ceremony_deadline, intermission_step, pending_join[…] }
  ├─ on_cmd():switch(cmd) → validate → pbus_publish_evt(…) → verdict
  ├─ tick(): 儀式 15s 重播(E_REMIND)/ 籌碼盲注 60s 代設 / 離線 15s 代打 /
  │          在線 30s 催促 / C_READY_NEXT 30s 自動 / runout 街道排程
  └─ hand_flow:E_HAND_START 組包(洗牌+扣盲)→ 行動輪推進 → E_STREET → 結算
side_pot.c:settle()(§8.3)
```

- 只有 `master_engine_enable(true)` 後 on_cmd/tick 才生效;引擎**不持有自己的狀態副本**,
  一律讀 `pbus_state()`(同 task,無鎖)—— 接管後續跑天然成立。
- 行動輪終結判定:未棄未 all-in 者全部 `bet_round == cur_bet` 且 acted → 發 `E_STREET`
  (收池時點);只剩一人未棄 → 直接 `E_HAND_RESULT(reason=1)`;
  全員 all-in → runout:逐街 `E_STREET(first_seat=0xFF)`,相鄰 `play_at` 間隔 ≥3500ms(產品 §3.4)。
- `E_ACTION_REQ` 計算:`call_amt=cur_bet`;`min_raise_to = (raise_count<4 && 有人可加) ?
  cur_bet+min(min_raise, 剩餘 cap 空間) : 0`;`max_raise_to = min(cur_bet+bet_cap, bet_round+chips)`
  (bet_cap=0 → 無上限);all-in 例外照協定 §11.3(不足額全下不重開加注、不增 raise_count)。
- 局間統一時序(協定 §11.5)以 `intermission_step` 順序機實作,C_READY_NEXT 之後
  才發 `E_MASTER_HANDOFF`(低電豁免:下任 battery<15 且 ≠0xFF → new_master=原任)。

### 8.2 洗牌與發牌

Fisher–Yates over 52,熵源 `hal_rand()`;發牌順序:先 board[5],再按座位環序每人 2 張
(順序其實不影響公平性,但**必須固定**,便於測試向量重現)。短盲/貼盲即全下照協定 §11.2。

### 8.3 側池結算(side_pot.c)——偽代碼(這是最容易寫錯的地方)

```
settle(st, out_payout[], out_chips_after[]):
  # 輸入:每人 bet_hand(本局總投入,含已收池)、fold/allin 旗標、7 張牌
  levels = sorted unique { bet_hand[p] : p 未棄 且 bet_hand[p] > 0 }
  prev = 0
  for L in levels:                          # 每個 all-in 額度切一池;最後一級=主動注封頂
      pot_L = Σ_全體p ( min(bet_hand[p], L) - min(bet_hand[p], prev) )
              # 棄牌者也按其投入貢獻,但永不參與 eligibility
      elig  = { p : p 未棄 且 bet_hand[p] ≥ L }
      best  = max_{p∈elig} hand_eval7(hole[p] + board)
      winners = { p∈elig : rank == best },按「按鈕左手起」座位環序排列
      share = pot_L / |winners|;  rem = pot_L % |winners|
      每個 winner += share;winners 前 rem 名各 +1     # 奇數籌碼歸最靠按鈕左手(協定 §11.4)
      prev = L
  # 性質檢查(實作必加 assert):Σ payout == Σ bet_hand == st.pot(結算時點)
```

- **不叫注退還**免特判:最高 level 只有下注者自己 eligible → 那一池自動「退」給他。
- `reason=1`(全棄剩一人):不跑 settle,贏家直得 `pot + Σ bet_round(在途)`。
- `E_HAND_RESULT.show[]` 順序 = 播報順序:先攤牌義務者(最後主動下注者,無則按鈕左手起),
  其餘 elig 依座位環序;已棄牌者不入 show[](產品 §4.3)。
- `chips_after[]` 為權威;接收端不重算(協定 §11.4)。

## 9. hand_eval

21 組合暴力法(產品 §3.3)。`eval5` 骨架(C,host 可測):

```c
static hand_rank_t eval5(const uint8_t c[5]) {
    uint8_t rank[5], suit[5], cnt[13] = {0};
    for (int i = 0; i < 5; i++) { rank[i] = c[i] >> 2; suit[i] = c[i] & 3; cnt[rank[i]]++; }
    bool flush = suit[0]==suit[1] && suit[0]==suit[2] && suit[0]==suit[3] && suit[0]==suit[4];
    uint16_t mask = 0; for (int i = 0; i < 5; i++) mask |= 1u << rank[i];
    int straight_hi = -1;
    for (int hi = 12; hi >= 3; hi--)
        if (((mask >> (hi-4)) & 0x1F) == 0x1F) { straight_hi = hi; break; }
    if (straight_hi < 0 && (mask & 0x100F) == 0x100F) straight_hi = 3;  // A2345,高牌記 5
    // 直方圖排序:出現次數優先、同次數點數大優先 → groups[]
    // cat 判定順序:SF(=8,hi==12 時 9)> 四條 7 > 葫蘆 6 > 同花 5 > 順子 4
    //             > 三條 3 > 兩對 2 > 一對 1 > 高牌 0
    // kick[5]:groups 展開後補零;順子/同花順 kick[0]=straight_hi
    ...
}
hand_rank_t hand_eval7(const uint8_t c[7]) {
    static const uint8_t idx[21][5] = { /* C(7,5) 全 21 組,固定表 */ };
    hand_rank_t best = { .cat = 0xFF };  // 哨兵:cat 0xFF 表未初始化
    for (int k = 0; k < 21; k++) { ... 取 max ... }
    return best;
}
int hand_rank_cmp(const hand_rank_t *a, const hand_rank_t *b) {
    return memcmp(a, b, 6);   // cat + kick[5] 字典序;struct 無 padding(全 u8)
}
```

坑:①A2345 的比較鍵是 5(rank index 3),不是 A;②兩對的 kick 序 =(大對、小對、踢腳);
③皇家同花順 = SF 且 `kick[0]==12` → cat 改 9(協定 rank_cat 播報用)。
全部進 §20.2 的 host 向量。

## 10. voice(voice_adpcm.c / number_speech.c)

- 資產索引:`voice_init` 只存 bin 基址,`voice_open` 查表 → stream 結構
  {資料指標、剩餘樣本、IMA 狀態(predictor,index)、4KB 內部緩衝}。
  **禁整段解碼**(產品 §6.1);`voice_read` 每次至多解 `max_samples`,
  4-bit nibble 流順序:低 nibble 先。
- IMA 解碼器:標準 89 級 step 表 + 16 項 index 表(與 wav2adpcm.py 編碼端同表,
  §19.2;表值以任何標準 IMA-ADPCM 參考為準,編解碼同源即可)。
- 每 clip 自帶 4B 初始狀態(§19.2),流打開時載入 —— clip 間互不依賴,可任意順序播。
- 數字合成(產品 §6.2 規則完備,直接照抄實作):

```c
static int push_00_99(voice_id_t *o, int k, unsigned v) {
    if (v == 0) return k;
    if (v <= 20) { o[k++] = V_N0 + v; return k; }
    unsigned t = v / 10, u = v % 10;
    o[k++] = (t == 2) ? V_N20 : (voice_id_t)(V_N30 + (t - 3));
    if (u) o[k++] = V_N0 + u;
    return k;
}
int voice_expand_number(uint16_t n, voice_id_t out[8]) {
    int k = 0;
    if (n > PK_CHIPS_MAX) n = PK_CHIPS_MAX;
    if (n == 0) { out[0] = V_N0; return 1; }
    if (n >= 1000) { out[k++] = V_N0 + n / 1000; out[k++] = V_THOUSAND; n %= 1000; }
    if (n >= 100)  { out[k++] = V_N0 + n / 100;  out[k++] = V_HUNDRED;  n %= 100; }
    return push_00_99(out, k, n);
}
```

前提:`voice_ids.h` 生成時 **V_N0..V_N20 連續、V_N30..V_N90 連續**(§19.1 的
voice_list.tsv 順序保證)。播報「You have N chips」= 依序 `hal_audio_play(V_YOU_HAVE)`、
展開序列、`V_CHIPS`(hal_audio_play 佇列串接語義,產品 §2.3)。

## 11. app_flow

- 畫面 = enum(附錄 A 的 20 個)+ `render(screen) → hal_screen_t`(app_screens.c)+
  遷移函式。**先實作產品 §4.1 全域遷移總則為前置攔截**,再進逐畫面 switch —— 順序反了
  會漏 HAND 內的無條件遷移。
- `screen_from_state()` 直譯產品 §4.5 表(依序判定,第一個命中即回傳):

```c
screen_t screen_from_state(void) {
    const game_view_t *v = game_view();
    const pn_table_state_t *st = v->st;
    bool dealer = (st->button_seat != 0xFF && v->my_seat == st->button_seat);
    switch (st->phase) {
    case PN_PH_LOBBY:       return SCR_LOBBY;
    case PN_PH_DEALER_CALL: return SCR_DEALER_CALL;
    case PN_PH_SEATING:     return (v->my_seat == 0xFF) ? SCR_SEATING : SCR_WAIT;
    case PN_PH_CHIPS:       return (my_chips(v) == 0 && !my_eliminated(v)) ? SCR_CHIPS_SET : SCR_WAIT;
    case PN_PH_BLINDS:      return dealer ? SCR_BLINDS : SCR_WAIT;
    case PN_PH_HAND:
        if (my_eliminated(v) || my_folded(v) || !my_dealt(v)) return SCR_SPECTATE;
        return (st->to_act_seat == v->my_seat) ? SCR_MY_TURN : SCR_WAIT_TURN;
    case PN_PH_INTERMISSION:
        if (dealer) return SCR_INTERMISSION_DEALER;
        return my_eliminated(v) ? SCR_REBUY : SCR_INTERMISSION;
    case PN_PH_PAUSED:      return SCR_PAUSED;
    default:                return SCR_SCANNING;
    }
}
```

- RESULT 逐位翻牌:UI task 以 `pbus_local_time_for(play_at + k*slot_gap_ms)` 排本地
  翻頁定時,與語音 slot 同刻(產品 §4.3/§6.3);語音 slot 只播**自己**在 show[] 的那格。
- 播報排程職責在 app_flow(不在 HAL):齊播 → `hal_audio_play_at(id,
  pbus_local_time_for(play_at))`;本機即播 → `hal_audio_play`。HAL 的路徑延遲補償
  在 HAL 內部(§15.3),app_flow 不管。
- 二確認(Fold/All-in/LEAVE)、系統選單、submitting 旗標、REJECT 短語表:產品 §3.2/§5
  已完備,直譯即可。防誤觸:MY_TURN 進場 500ms 吞輸入。
- 系統選單按裁定 R9 擴充:首行 `BATT <pct>%`(0xFF→`BATT USB`,充電中加 `CHG`,
  每次開選單時讀一次 `hal_battery_pct()`/`hal_battery_charging()`);
  `[BRIGHTNESS]` 項 NEXT 循環 30/60/100 → `hal_display_set_brightness()` 即時生效;
  `[VOLUME]` 照產品 §5.6(40/70/100 + 試播 V_BEEP)。兩者選檔邏輯共用同一個
  cycle-item 實作,只差回呼。

---

# 第三部分:StickS3 板級與 HAL(本指南主體)

引腳/位址一律出自 `hardware-reference.md`;與官方文件衝突處以【實測】為準。

## 12. board_config.h 與 app_main

### 12.1 `main/board_config.h`(全文)

```c
// StickS3 板級配置 —— 唯一允許出現 GPIO 編號的檔案(參照 hardware-reference.md)
#pragma once
#include "driver/gpio.h"

// ---- 共享 I2C(內部匯流排:ES8311/BMI270/M5PM1)----
#define BOARD_I2C_SDA        GPIO_NUM_47
#define BOARD_I2C_SCL        GPIO_NUM_48
#define I2C_ADDR_ES8311      0x18
#define I2C_ADDR_BMI270      0x68        // 注意:Cardputer-Adv 是 0x69,本機 0x68
#define I2C_ADDR_M5PM1       0x6E

// ---- LCD (ST7789P3, 實體 135×240 直向 → 邏輯 240×135 橫向) ----
#define LCD_SPI_HOST         SPI3_HOST
#define LCD_PIN_MOSI         GPIO_NUM_39
#define LCD_PIN_SCLK         GPIO_NUM_40
#define LCD_PIN_CS           GPIO_NUM_41
#define LCD_PIN_DC           GPIO_NUM_45   // RS
#define LCD_PIN_RST          GPIO_NUM_21
#define LCD_PIN_BL           GPIO_NUM_38   // LEDC PWM
#define LCD_H_RES            240           // 邏輯橫向
#define LCD_V_RES            135
#define LCD_PANEL_W          135           // 面板原生
#define LCD_PANEL_H          240
#define LCD_PIXEL_CLOCK_HZ   (40 * 1000 * 1000)
#define LCD_INVERT_COLOR     true          // 【實測】hardware-reference §11.8
#define LCD_SWAP_XY          true          // 橫向
#define LCD_MIRROR_X         false         // §14.3 自檢後若上下顛倒:兩個 mirror 同時取反
#define LCD_MIRROR_Y         true
#define LCD_GAP_X            40            // 裁定 R7:直向 (52,40)【實測】→ 橫向換軸
#define LCD_GAP_Y            52

// ---- 按鍵(輸入上拉低有效;左側鍵是 M5PM1 電源鍵,韌體不可讀!)----
#define BTN_OK_PIN           GPIO_NUM_11   // KEY1 → KEY_OK(產品 §2.1 映射表)
#define BTN_NEXT_PIN         GPIO_NUM_12   // KEY2 → KEY_NEXT

// ---- 音訊 ES8311 + AW8737 ----
#define AUDIO_SAMPLE_RATE    16000
#define I2S_PIN_MCLK         GPIO_NUM_18
#define I2S_PIN_BCLK         GPIO_NUM_17
#define I2S_PIN_LRCK         GPIO_NUM_15
#define I2S_PIN_DOUT         GPIO_NUM_16   // MCU→ES8311(= ES8311 的 DIN,播放)
#define I2S_PIN_DIN          GPIO_NUM_14   // ES8311→MCU(= ES8311 的 DOUT,錄音/校準用)
#define AUDIO_VOL_DEFAULT    70
#define AUDIO_VOL_BATT_MAX   75            // 電池供電硬鉗(hardware-reference §9.3)
#define AUDIO_PATH_LATENCY_MS 62           // 初始估算值:DMA 60ms + codec ~2ms;§15.5 實測後回填
```

### 12.2 `main/app_main.c`(骨架)

```c
void app_main(void)
{
    board_i2c_init();                       // i2c_new_master_bus(G47/G48, 100kHz)
    ESP_ERROR_CHECK(m5pm1_init(board_i2c_bus()));
    ESP_ERROR_CHECK(m5pm1_enable_l3b_domain());   // ★ 必須先於 hal_display_init,否則黑屏(§13.2)
    ESP_ERROR_CHECK(m5pm1_poker_btn_cfg());       // ★ 停用單擊復位(§13.3)

    ESP_ERROR_CHECK(hal_display_init());          // §14
    ESP_ERROR_CHECK(hal_audio_init());            // §15(含 voice 分區 mmap → voice_init)
    // hal_input_init 由 input_gesture_init 內部呼叫;app_flow_start 內部呼叫 game_init→pbus_start
    char name[8];
    uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(name, sizeof(name), "S3-%02X%02X", mac[4], mac[5]);
    app_flow_start(name);                         // 不返回業務控制權;app_main 可結束
}
```

## 13. M5PM1 驅動(I2C 0x6E)

寄存器映射(對照 M5PM1 手冊 V1.9 + 官方 Arduino 庫,並經 ble-page-turner 真機驗證):

```c
#define PM1_REG_DEVICE_ID   0x00
#define PM1_REG_PWR_SRC     0x04   // [2:0] 0=5VIN 1=5VINOUT 2=battery
#define PM1_REG_SYS_CMD     0x0C   // 0xA1 = 關機
#define PM1_REG_GPIO_MODE   0x10   // [4:0] 每 GPIO 方向,1=輸出
#define PM1_REG_GPIO_OUT    0x11   // [4:0] 輸出電平
#define PM1_REG_GPIO_IN     0x12   // [4:0] 輸入電平(唯讀)
#define PM1_REG_GPIO_DRV    0x13   // [4:0] 1=開漏(預設),0=推挽
#define PM1_REG_GPIO_PUPD0  0x14   // GPIO0–3 每腳 2bit:00=無 01=上拉 10=下拉
#define PM1_REG_GPIO_FUNC0  0x16   // GPIO0–3 每腳 2bit:00=GPIO 01=IRQ 10=WAKE 11=特殊
#define PM1_REG_VBAT_L      0x22   // 電池電壓 mV 低 8 位
#define PM1_REG_VBAT_H      0x23   // 高 4 位([3:0])
#define PM1_REG_VIN_L       0x24   // USB/5V 輸入電壓 mV(判斷外部供電)
#define PM1_REG_VIN_H       0x25
#define PM1_REG_BTN_CFG_1   0x49   // [7]DL_LOCK [6:5]DBL_DLY [4:3]LONG_DLY [2:1]CLK_DLY
                                   // [0]SINGLE_RST_DIS:1=停用單擊復位   ← 本專案要改的位
#define PM1_REG_BTN_CFG_2   0x4A   // [0]DOUBLE_OFF_DIS:0=雙擊關機使能(保持)
#define PM1_REG_AW8737_PULSE 0x53  // [7]REFRESH(寫1執行) [6:5]脈衝數0..3 [4:0]PM1 GPIO 號
#define PM1_BTN_CFG_1_DEFAULT 0x2A // 出廠值(單擊復位使能、長按2s、間隔250ms)
```

I2C 讀寫用 IDF v6 新驅動(`i2c_master_transmit`/`i2c_master_transmit_receive`,
100kHz;注意 hardware-reference §11.9:函式名是 `i2c_new_master_bus()`)。

### 13.1 啟動順序(必須照此順序,缺一即踩坑)

```c
// app_main 最前:
m5pm1_init(bus);                 // 加 device、讀 DEVICE_ID 驗證在線
m5pm1_enable_l3b_domain();       // 坑1:LCD 供電
m5pm1_poker_btn_cfg();           // 坑2:單擊復位
// hal_battery 內部再處理 坑3:VBAT 讀 0
```

### 13.2 使能 L3B 電源域(否則 LCD 永遠黑屏)

背景(hardware-reference §11.2):PM1 復位會清空自身 GPIO 寄存器,其 GPIO2 =
`PYG2_L3B_EN` 即 LCD 電源域使能 —— 清零後 esp_lcd「初始化成功」但面板無電,黑屏且無任何錯誤。

```c
esp_err_t m5pm1_enable_l3b_domain(void)
{
    uint8_t r;
    // 1) FUNC=GPIO:GPIO2 功能位([5:4])清為 00
    pm1_read(PM1_REG_GPIO_FUNC0, &r); pm1_write(PM1_REG_GPIO_FUNC0, r & ~(3u << 4));
    // 2) 先寫 OUT=1(在切輸出方向前鎖存高電平,避免使能腳瞬間拉低斷電)
    pm1_read(PM1_REG_GPIO_OUT, &r);   pm1_write(PM1_REG_GPIO_OUT, r | (1u << 2));
    // 3) 推挽驅動(PM1 GPIO 預設開漏,外部無上拉則拉不高)
    pm1_read(PM1_REG_GPIO_DRV, &r);   pm1_write(PM1_REG_GPIO_DRV, r & ~(1u << 2));
    // 4) 最後才切輸出方向
    pm1_read(PM1_REG_GPIO_MODE, &r);  pm1_write(PM1_REG_GPIO_MODE, r | (1u << 2));
    return ESP_OK;   // 各步 ESP_RETURN_ON_ERROR,略
}
```

### 13.3 改寫 BTN_CFG:停用單擊復位(協定 §17.5 硬性要求)

```c
esp_err_t m5pm1_poker_btn_cfg(void)
{
    // 出廠 0x2A + SINGLE_RST_DIS(bit0)=1 → 0x2B:
    //   單擊不再復位(誤觸電源鍵 ≠ 瞬間離線);雙擊關機、長按 2s 下載模式保留。
    ESP_RETURN_ON_ERROR(pm1_write(PM1_REG_BTN_CFG_1, PM1_BTN_CFG_1_DEFAULT | 0x01), TAG, "");
    ESP_RETURN_ON_ERROR(pm1_write(PM1_REG_BTN_CFG_2, 0x00), TAG, "");  // 顯式寫,勿信現值
    return ESP_OK;
}
```

三個必知:
1. **寄存器帶電持久**(hardware-reference §11.3):裝著電池時重刷韌體/PM1 復位都不恢復
   出廠值 —— 所以每次啟動**顯式寫入完整期望值**(上面兩行無條件執行),
   並提供 `m5pm1_restore_pwr_btn_defaults()`(寫回 0x2A/0x00)供刷回其他韌體前呼叫。
2. 停用的是「開機狀態下的單擊復位」;關機狀態下單擊開機不受影響。
3. 驗證方法:燒錄後單擊左側鍵 —— 串口**不得**重新枚舉、日誌時間戳**不得**歸零;
   若出現「USB 重枚舉 + 時間戳歸零 + 無 panic 回溯」= PM1 硬體復位(hardware-reference §11.1),
   說明寫入沒生效。

### 13.4 VBAT / 充電狀態(hal_battery.c)

```c
uint8_t hal_battery_pct(void)
{
    uint16_t vin = 0, vbat = 0;
    if (m5pm1_get_vin_mv(&vin) != ESP_OK) return 0xFF;
    if (vin > 4000) return 0xFF;          // ★ 插 USB 時 VBAT 可能讀 0(hardware-reference §11.7)
                                          //   協定 §5:一律回 0xFF(未知),嚴禁回 0
    if (m5pm1_get_battery_mv(&vbat) != ESP_OK || vbat == 0) return 0xFF;
    return lipo_pct_from_mv(vbat);        // OCV 折線:4150→100 … 3300→0,線性內插
}
bool hal_battery_charging(void)
{
    // CHG_STAT = PM1 GPIO0 輸入,低有效;僅外部供電時有意義(電池供電時充電器斷電、引腳懸浮)
    // GPIO0 需一次性配置:FUNC0 bit[1:0]=00、MODE bit0=0(輸入)、PUPD0 bit[1:0]=00(★無上拉,
    // PM1 內部上拉會蓋過 STAT 弱下拉造成永遠讀「未充電」——已驗證的坑)
    if (!m5pm1_is_external_powered()) return false;   // PWR_SRC(0x04) != battery
    uint8_t in; return pm1_read(PM1_REG_GPIO_IN, &in) == ESP_OK && !(in & 1);
}
```

VBAT 讀取:`0x22` 起連讀 2 字節,`mv = raw[0] | ((raw[1] & 0x0F) << 8)`(12-bit mV)。
讀取節流:pbus 每 2s 要一次(STATUS),HAL 內部快取 1s 即可,無 ADC/Wi-Fi 衝突問題
(走 I2C,不是 ADC2 —— 那是 xingzhi 的坑,本機沒有)。

### 13.5 hal_power / hal_misc

- StickS3 關機 = PM1 雙擊,硬體行為**攔截不到**(產品 §2.5 已列為已知限制):
  `hal_power` 只需提供空實作 + `app_prepare_poweroff()` 永不被板級呼叫的註釋;
  協定 §9.5 接管兜底。
- `hal_rand()` = `esp_random()`;`hal_yield_watchdog()` = `vTaskDelay(1)` 包裝。

## 14. 顯示(esp_lcd + ST7789P3 + esp_lvgl_port)

### 14.1 初始化(照 xingzhi `display.c` 已驗證模式,參數換本板)

```c
esp_err_t hal_display_init(void)
{
    // 背光先關(LEDC 佔空 0),初始化完成再開 —— 避免上電雪花
    bl_init_off();

    spi_bus_config_t bus = {
        .mosi_io_num = LCD_PIN_MOSI, .sclk_io_num = LCD_PIN_SCLK,
        .miso_io_num = -1, .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * 40 * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = LCD_PIN_CS, .dc_gpio_num = LCD_PIN_DC,
        .spi_mode = 0, .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .trans_queue_depth = 10, .lcd_cmd_bits = 8, .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_SPI_HOST, &io_cfg, &s_io));

    esp_lcd_panel_dev_config_t pcfg = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB, .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(s_io, &pcfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, LCD_INVERT_COLOR)); // 【實測】必須
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel, LCD_SWAP_XY));           // 硬體 MADCTL 旋轉
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, LCD_MIRROR_X, LCD_MIRROR_Y));
    esp_lcd_panel_set_gap(s_panel, LCD_GAP_X, LCD_GAP_Y);                   // 裁定 R7:(40,52)
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    port.task_priority = 4;  port.task_stack = 8192;  port.task_affinity = 1;  // §5 任務表
    ESP_ERROR_CHECK(lvgl_port_init(&port));
    lvgl_port_display_cfg_t disp = {
        .io_handle = s_io, .panel_handle = s_panel,
        .buffer_size = LCD_H_RES * 40,          // 240×40×2B×2 ≈ 38KB 內部 RAM
        .double_buffer = true,
        .hres = LCD_H_RES, .vres = LCD_V_RES,   // 240×135(邏輯橫向,LVGL 全程不知直向存在)
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = { .swap_xy = LCD_SWAP_XY,
                      .mirror_x = LCD_MIRROR_X, .mirror_y = LCD_MIRROR_Y },
        .flags = { .buff_dma = true, .swap_bytes = true },  // swap_bytes 必開(demo 已驗證)
    };
    if (!lvgl_port_add_disp(&disp)) return ESP_FAIL;
    build_static_ui();       // §14.2:一次性建全部 LVGL 物件
    bl_set(80);              // LEDC 開背光
    return ESP_OK;
}
```

### 14.2 hal_display_render:宣告式 → LVGL 的映射骨架

物件**一次建成、render 只改屬性**(≤10fps 全量更新,LVGL 自己做髒區):

```c
// 240×135 佈局(Cardputer-Adv 共用同一組參數 —— 產品 §2.2「兩種邏輯佈局」之一)
// ┌ title bar(h=18,底色隨 title_color)────────────┐
// │ cards 行(h=36):自己 2 張大牌 26×34 + 公共 5 張半高 18×24 │
// │ big 區(h=30,置中大字)/ lines[0..2](12px 行高)      │
// │ lines / progress(h=6)                                  │
// └ hint bar(h=14:左 "OK:xxx" 右 "NEXT:xxx")──────────┘
static lv_obj_t *s_title, *s_lines[6], *s_big, *s_hint_ok, *s_hint_next, *s_prog;
static lv_obj_t *s_card[7], *s_card_lbl[7];      // [0..1] 自己,[2..6] 公共

static void card_text(hal_card_t c, char out[8], lv_color_t *col) {
    static const char rk[] = "23456789TJQKA";
    static const char *su[] = {"c", "d", "h", "s"};       // ♦♥ 紅顯(產品 §2.2)
    if (c == 0xFF) { strcpy(out, "--"); *col = lv_color_hex(0x666666); return; }
    snprintf(out, 8, "%c%s", rk[c >> 2], su[c & 3]);
    *col = ((c & 3) == 1 || (c & 3) == 2) ? lv_color_hex(0xE03030) : lv_color_hex(0xF0F0F0);
}

void hal_display_render(const hal_screen_t *s)
{
    if (!lvgl_port_lock(50)) return;              // 一切 lv_* 必在鎖內(demo 鐵律)
    lv_label_set_text(s_title, s->title ? s->title : "");
    apply_title_style(s->title_color, s->title_flags);   // 電池紅/斷線/submitting 圖示疊加
    for (int i = 0; i < 6; i++)
        lv_label_set_text(s_lines[i], s->lines[i] ? s->lines[i] : "");
    set_or_hide(s_big, s->big);
    for (int i = 0; i < 7; i++) {
        char t[8]; lv_color_t col; bool up = s->cards_face_up & (1u << i);
        card_text(up ? s->cards[i] : 0xFF, t, &col);
        if (!up && s->cards[i] != 0xFF) strcpy(t, "##");  // 蓋牌「▒」風格框
        lv_label_set_text(s_card_lbl[i], t);
        lv_obj_set_style_text_color(s_card_lbl[i], col, 0);
        lv_obj_set_style_border_opa(s_card[i], s->cards[i] != 0xFF ? LV_OPA_100 : LV_OPA_20, 0);
    }
    set_or_hide(s_hint_ok, s->hint_ok);  set_or_hide(s_hint_next, s->hint_next);
    if (s->progress == 0xFF) lv_obj_add_flag(s_prog, LV_OBJ_FLAG_HIDDEN);
    else { lv_obj_clear_flag(s_prog, LV_OBJ_FLAG_HIDDEN); lv_bar_set_value(s_prog, s->progress, LV_ANIM_OFF); }
    lvgl_port_unlock();
}
```

`hal_display_render_showdown`:獨立滿屏容器(name + 2 張翻開大牌 48×64 + rank_text
大字 + "+N" 金色 + is_me 的 "YOU" 角標 + TC_WIN 底色),render 時隱藏一般畫面容器、
顯示 showdown 容器,離開 RESULT 時反向。小面板顯示優先序(必顯/可省)按產品 §2.2 執行。

### 14.3 背光與亮度(裁定 R9 的 StickS3 落地)

G38 背光走 LEDC PWM(5kHz / 10-bit;本專案不開 light sleep,無 hardware-reference
§11.5 的 LEDC 凍結問題):

```c
static void bl_init_off(void) {
    ledc_timer_config_t t = { .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = LEDC_TIMER_0,
                              .duty_resolution = LEDC_TIMER_10_BIT, .freq_hz = 5000,
                              .clk_cfg = LEDC_AUTO_CLK };
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    ledc_channel_config_t c = { .gpio_num = LCD_PIN_BL, .speed_mode = LEDC_LOW_SPEED_MODE,
                                .channel = LEDC_CHANNEL_0, .timer_sel = LEDC_TIMER_0,
                                .duty = 0, .hpoint = 0 };
    ESP_ERROR_CHECK(ledc_channel_config(&c));
}
void hal_display_set_brightness(uint8_t pct) {
    if (pct < 10) pct = 10;               // 下限防「調到全黑找不回選單」
    if (pct > 100) pct = 100;
    uint32_t duty = (1023u * pct) / 100;  // 亮度感知非線性,先線性;不滿意再換 gamma 2.2 表
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}
#define bl_set(pct) hal_display_set_brightness(pct)   // §14.1 用;開機預設 80
```

電量百分比顯示不需 StickS3 額外工作:共用系統選單直接呼叫 §13.4 的
`hal_battery_pct()`(插 USB 回 0xFF → 選單顯示 `BATT USB`,正是本機該有的行為)。

### 14.4 旋轉自檢(首次點亮必做)

燒一個測試畫面:全屏紅色邊框 1px + 左上角 "TL" 標籤。驗:
① 邊框四邊完整可見(缺邊/移位 → gap 錯,微調 LCD_GAP_X/Y ±1..2 並回寫 §21);
② "TL" 在 USB 口朝左持機時位於左上(顛倒 → `LCD_MIRROR_X/Y` 同時取反);
③ 紅色是紅色(發青 → invert_color 或 swap_bytes 漏了)。

## 15. 音訊(ES8311 + AW8737 + I2S)

### 15.1 I2S(播放常開;錄音通道僅延遲校準測試建置開啟)

```c
i2s_chan_config_t ch = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
ch.dma_desc_num = 4;  ch.dma_frame_num = 240;          // 4×240 樣本 ≈ 60ms 環形深度
ESP_ERROR_CHECK(i2s_new_channel(&ch, &s_tx, NULL));    // 校準建置:(&ch,&s_tx,&s_rx)
i2s_std_config_t std = {
    .clk_cfg = { .sample_rate_hz = AUDIO_SAMPLE_RATE,
                 .clk_src = I2S_CLK_SRC_DEFAULT,
                 .mclk_multiple = I2S_MCLK_MULTIPLE_256 },   // MCLK=4.096MHz(有獨立 G18!)
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = { .mclk = I2S_PIN_MCLK, .bclk = I2S_PIN_BCLK, .ws = I2S_PIN_LRCK,
                  .dout = I2S_PIN_DOUT, .din = I2S_GPIO_UNUSED },
};
ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx, &std));
ESP_ERROR_CHECK(i2s_channel_enable(s_tx));             // ★ 常開,之後永不 disable(產品 §2.3 硬性)
```

與 Cardputer 不同,本機 **MCLK 有獨立腳(G18)**,ES8311 用標準 MCLK 模式,不需 SCLK 時鐘模式。

### 15.2 ES8311 + AW8737 功放

```c
// codec:espressif/es8311 元件(相容性預案見 §2.4)
es8311_clock_config_t clk = {
    .mclk_from_mclk_pin = true, .mclk_frequency = AUDIO_SAMPLE_RATE * 256,
    .sample_frequency = AUDIO_SAMPLE_RATE,
};
s_codec = es8311_create(bus_or_port, ES8311_ADDRRES_0 /* 0x18 */);
ESP_ERROR_CHECK(es8311_init(s_codec, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16));
es8311_voice_volume_set(s_codec, AUDIO_VOL_DEFAULT, NULL);

// 功放 AW8737:不接 ESP32 GPIO,由 M5PM1 GPIO3(PYG3_SPK_Pulse)脈衝控制。
// PM1 提供代發脈衝的寄存器 0x53:[7]=REFRESH [6:5]=脈衝數 [4:0]=PM1 GPIO 號。
// AW8737 脈衝數 = 增益檔:1=低 2=中 3=高;0 = 拉低關斷(靜音)。
esp_err_t m5pm1_amp_set(uint8_t pulses /*0..3*/) {
    return pm1_write(PM1_REG_AW8737_PULSE, 0x80 | ((pulses & 3) << 5) | 3 /*GPIO3*/);
    // 寫入後 PM1 需 ~20ms 執行脈衝序列,期間勿再寫此寄存器
}
// hal_audio_init 尾:m5pm1_amp_set(2);  vTaskDelay(pdMS_TO_TICKS(25));   // 中增益常開
```

功放**常開**(輸出流常開靜音填充,無雜音問題 —— xingzhi 的 TX 空轉嘟嘟聲是無 codec
直推功放的板子的問題,本機有 ES8311 DAC,靜音即真靜音)。若實測待機底噪明顯,
退而求其次:播放前 `amp_set(2)`、靜音 >2s 後 `amp_set(0)`,並把 20ms 開啟延遲計入
排程(提前 25ms 喚醒);此決策實測後回寫 §21。

### 15.3 播放管線與 hal_audio_play_at(排程齊播核心)

```c
#define FRAME 320                                   // 20ms @16k
typedef struct { voice_id_t id; uint32_t at_ms; bool sched; } aq_item_t;
static QueueHandle_t s_q;                           // play/play_at 全入此
static aq_item_t s_pend[4];  static int s_npend;    // 已收排程單(按 at_ms 升序)
static voice_stream_t *s_cur;                       // 目前佔用輸出的流
static uint32_t s_head_ms;                          // 下一幀首樣本的「出聲」本地時刻

void hal_audio_play(voice_id_t id)                { aq_item_t i={id,0,false}; xQueueSend(s_q,&i,0); }
void hal_audio_play_at(voice_id_t id, uint32_t t) { aq_item_t i={id,t,true};  xQueueSend(s_q,&i,0); }
uint16_t hal_audio_path_latency_ms(void)          { return AUDIO_PATH_LATENCY_MS; }

static void audio_task(void *arg)      // core1, prio15(§5)
{
    int16_t buf[FRAME];
    s_head_ms = now_ms() + AUDIO_PATH_LATENCY_MS;   // = DMA 深度 60ms + codec 群延遲
    uint32_t frames = 0; uint64_t t0 = esp_timer_get_time();
    for (;;) {
        drain_queue();                              // 排程單插 s_pend;立即單接到 FIFO 尾
        memset(buf, 0, sizeof(buf));                // 1) 預設本幀 = 靜音(常開填充)
        // 2) 排程搶佔:首張排程單落入本幀窗口 [head, head+20) ?
        if (s_npend && time_ge(s_head_ms + 20, s_pend[0].at_ms)) {
            int off = s_pend[0].at_ms > s_head_ms
                    ? (s_pend[0].at_ms - s_head_ms) * 16 : 0;   // 樣本級切入(協定 §7.3:遲到即播 off=0)
            if (s_cur) { voice_close(s_cur); }      // 齊播優先,打斷閒聊型即播
            s_cur = voice_open(s_pend[0].id);  pop_pend();
            if (s_cur) voice_read(s_cur, buf + off, FRAME - off);
        } else if (s_cur) {                         // 3) 續播當前流
            int n = voice_read(s_cur, buf, FRAME);
            if (n < FRAME) { voice_close(s_cur); s_cur = next_immediate(); /* 串接 */ }
        } else {
            s_cur = next_immediate();               // 4) 佇列有即播單就開播
            if (s_cur) voice_read(s_cur, buf, FRAME);
        }
        apply_volume(buf, FRAME, effective_volume());   // §15.4 電池鉗制在此
        size_t wr; i2s_channel_write(s_tx, buf, sizeof(buf), &wr, portMAX_DELAY);
        s_head_ms += 20;  frames++;
        if ((frames & 63) == 0) {                   // 5) 漂移校正:樣本計數 vs esp_timer
            uint32_t ideal = t0/1000 + AUDIO_PATH_LATENCY_MS + frames*20;
            int32_t drift = (int32_t)(s_head_ms - ideal);
            if (drift > 2 || drift < -2) s_head_ms -= drift;   // I2S 由 APLL/晶振驅動,漂移極小
        }
    }
}
```

語義說明:`hal_audio_play_at` 的 `local_ms` 是**出聲時刻**;產品 §2.3 規則 2 的
「排程時刻 = local_time_for(play_at) − latency」在本實作中等價地內建於
`s_head_ms` 初始化(head 本身已含全路徑延遲),app_flow 直接傳
`pbus_local_time_for(play_at)` 即可,**不要在上層再扣一次延遲**(扣兩次 = 提早出聲)。

### 15.4 音量與電池鉗制

```c
static uint8_t effective_volume(void) {
    uint8_t v = s_vol;                              // 系統選單 40/70/100
    if (!m5pm1_is_external_powered() && v > AUDIO_VOL_BATT_MAX)
        v = AUDIO_VOL_BATT_MAX;                     // 電池供電 ≤75%:防過流重啟(硬體限制)
    return v;
}
```

軟體增益(int32 乘除 + 飽和)套在寫 I2S 前;codec 音量固定 70,只動軟體增益,
避免 I2C 寫音量與播放搶匯流排。

### 15.5 AUDIO_PATH_LATENCY_MS 量測(無示波器:「錄音對拍」法)

齊播精度驗的是**機間相對差**(<30ms,產品 §2.3),絕對延遲全桌同偏不影響體感,
因此校準以相對法為主:

1. 兩台裝置燒校準建置(`idf.py build -DPK_DEBUG_SOLO=1` + 測試頁:成桌後 Master 每 3s
   發一個帶 `play_at = now+700ms` 的 E_REMIND,兩機齊播 `V_BEEP`)。
2. 手機錄音 App(≥44.1kHz)置兩機正中間等距處,錄 ≥10 對 beep。
3. 導入 Audacity 放大波形,量每對 beep 起始沿的 Δt(樣本差 ÷ 采樣率),取**中位數**
   (丟棄含補洞重播的異常對)。
4. 把 Δt 加到落後那台的 `AUDIO_PATH_LATENCY_MS`。首台基準用計算值 62ms
   (DMA 4×240 樣本=60ms + ES8311 群延遲 ~2ms)。
5. 復測:中位數 <10ms 通過(同型機);異型機混測留給跨裝置驗收(產品 §9.2)。

可選絕對法(單機):校準建置同時開 I2S RX + ES8311 ADC 回錄,對「排程出聲時刻」
與「錄到 beep 的樣本位置」做差 —— 但含錄音路徑延遲,只作交叉參考,不作准。
量得的常數回填 `board_config.h` 並記入 §21。

## 16. 按鍵與 IMU

### 16.1 hal_input(G11/G12,輸入上拉低有效)

輪詢去抖(照 xingzhi buttons.c 模式,10ms 週期):**HAL 只上報 raw press/release**,
SHORT/LONG/REPEAT 合成在共用層 input_gesture.c(產品 §2.1,600ms 閾值,裁定 R5)。

```c
static hal_input_cb_t s_cb;
static void btn_task(void *a) {
    const gpio_num_t pin[2] = { BTN_OK_PIN, BTN_NEXT_PIN };   // KEY_OK / KEY_NEXT
    bool stable[2] = {0}, last[2] = {0};
    for (;;) {
        for (int i = 0; i < 2; i++) {
            bool raw = gpio_get_level(pin[i]) == 0;
            if (raw == last[i] && raw != stable[i]) {         // 連續兩拍一致才翻轉 = 去抖
                stable[i] = raw;
                if (s_cb) s_cb((hal_key_t)i, raw);            // 普通 task 上下文(規範要求)
            }
            last[i] = raw;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
esp_err_t hal_input_init(hal_input_cb_t cb) {
    s_cb = cb;
    gpio_config_t io = { .pin_bit_mask = BIT64(BTN_OK_PIN) | BIT64(BTN_NEXT_PIN),
                         .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    ESP_ERROR_CHECK(gpio_config(&io));
    xTaskCreatePinnedToCore(btn_task, "btns", 2048, NULL, 6, NULL, 0);
    return ESP_OK;
}
```

不用 GPIO 中斷 —— 本專案不做 light sleep(WIFI_PS_NONE 常開),輪詢最穩,
且避開 hardware-reference §11.4 的 `gpio_wakeup_enable` 覆寫中斷類型的坑(該坑僅睡眠方案需防)。

### 16.2 BMI270 傾斜 → incdec(加分項,不得阻塞主線)

`imu_bmi270.c` 初版**允許空殼**:`hal_input_set_incdec_cb` 收下回呼但不觸發 ——
共用碼視 incdec 為加速捷徑,缺失不影響任何流程(產品 §2.1)。
TODO(後續迭代):I2C 0x68(**本機不是 0x69**),100ms 輪詢加速度 Y 軸,
傾角 >25° 每 300ms 觸發 ±1,回中(<10°)停止;僅在數值畫面由 app_flow 啟停輪詢。
IMU 中斷腳接的是 PM1(PYG4),不直達 ESP32,**不要**嘗試中斷方案。

## 17. Wi-Fi / ESP-NOW 初始化(pbus_transport.c —— 共用碼,此處給全文)

照 xingzhi `intercom.c` 已驗證模式,差異:廣播 peer、成員動態 add_peer、回呼零處理。

```c
esp_err_t pn_transport_init(void)
{
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase(); nvs_flash_init();
    }
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));            // 不連 AP
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));  // 協定 §4:固定 ch1
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));               // 進 App 即關省電(含掃描期!)

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(pn_recv_cb));
    static const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    esp_now_peer_info_t peer = { .channel = 0, .ifidx = WIFI_IF_STA, .encrypt = false };
    memcpy(peer.peer_addr, bcast, 6);
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));                     // 廣播 peer 必須先加
    return ESP_OK;
}

// Wi-Fi task 上下文:協定 §4 執行模型 —— 只驗最小限度 + 入佇列,其餘一概不做
static void pn_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len < (int)sizeof(pn_hdr_t) || len > 250) return;
    const pn_hdr_t *h = (const pn_hdr_t *)data;
    if (h->magic != PN_MAGIC || h->version != PN_VERSION) return;
    if ((int)(sizeof(pn_hdr_t) + h->len) > len) return;
    pn_rx_item_t it;                                   // {src[6]; u16 len; u8 pkt[250]}
    memcpy(it.src, info->src_addr, 6);
    it.len = len;  memcpy(it.pkt, data, len);
    xQueueSend(s_rxq, &it, 0);                         // 不阻塞,滿則丟(補洞機制兜底)
}
```

成員 peer 管理:pbus_session 收到 `E_ROSTER` / `E_PLAYER_JOINED` 後對每個成員 MAC
`esp_now_add_peer`(已存在回 `ESP_ERR_ESPNOW_EXIST`,忽略即可);≤11 peers,無上限問題。
`esp_now_send` 只在 pbus task 呼叫(回呼內禁發,協定 §4.1)。

## 18. sdkconfig.defaults 與 partitions.csv(全文)

### 18.1 `StickS3/sdkconfig.defaults`

```
# --- 目標晶片:ESP32-S3-PICO-1-N8R8 ---
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y

# --- 控制台走 USB-Serial-JTAG(USB-C 原生 USB)---
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y

# --- Flash 8MB QIO + 自訂分區(容 app + voice)---
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="8MB"
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"

# --- 8MB Octal PSRAM(N8R8;佔用 GPIO35/36/37)---
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y

# --- 主棧 & FreeRTOS(產品 §8.1:主棧 8KB,本機無 opus 不需 12KB)---
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
CONFIG_FREERTOS_HZ=1000

# --- LVGL 字體(標題 14 / 內文 20 / big 28)---
CONFIG_LV_FONT_MONTSERRAT_14=y
CONFIG_LV_FONT_MONTSERRAT_20=y
CONFIG_LV_FONT_MONTSERRAT_28=y

# --- Wi-Fi 僅用於 ESP-NOW:關 AMPDU 省內存(demo 已驗證)---
CONFIG_ESP_WIFI_AMPDU_TX_ENABLED=n
CONFIG_ESP_WIFI_AMPDU_RX_ENABLED=n
```

### 18.2 `StickS3/partitions.csv`

```
# Name,     Type, SubType,  Offset,   Size,     Flags
nvs,        data, nvs,      0x9000,   0x6000,
phy_init,   data, phy,      0xf000,   0x1000,
factory,    app,  factory,  0x10000,  0x3E0000,
voice,      data, 0x40,     0x3F0000, 0x120000,
```

- factory 3.87MB(app 預算 ≤2.5MB,產品 §8.2,餘裕充足);voice 1.125MB
  (語音估 ~720KB + 餘裕);總用量 ~5.1MB < 8MB,尾部留白供未來 OTA 改造。
- `voice` 用自訂 subtype `0x40` —— **四機分區表中此 subtype 與分區名必須一致**
  (poker_core 無關,但 §19.3 的讀取碼四機共用寫法)。

## 19. voice.bin:生成管線與載入

### 19.1 生成(common/tools/gen_voice.sh)

`common/voice/voice_list.tsv`:每行 `ID<TAB>Text`,**順序即 voice_id 枚舉順序**;
內容 = 產品 §6.2 全部片段(V_N0..V_N20 連續、V_N30..V_N90 連續,見 §10 前提;
`V_BEEP` 特殊:不走 TTS,由 wav2adpcm.py 內建生成 880Hz/120ms 正弦)。

```bash
#!/usr/bin/env bash
# gen_voice.sh —— macOS 專用:say(TTS)→ afconvert(16k mono wav)→ wav2adpcm.py
set -euo pipefail
cd "$(dirname "$0")/../voice"
VOICE="Samantha"          # 單一音色,全部片段一致(產品 §6.1)
T=$(mktemp -d)
while IFS=$'\t' read -r id text; do
  [ -z "$id" ] || [ "${id:0:1}" = "#" ] && continue
  [ "$id" = "V_BEEP" ] && continue                     # 由打包器合成
  say -v "$VOICE" -o "$T/$id.aiff" "$text"
  afconvert -f WAVE -d LEI16@16000 -c 1 "$T/$id.aiff" "$T/$id.wav"
done < voice_list.tsv
python3 ../tools/wav2adpcm.py voice_list.tsv "$T" \
    -o voice.bin \
    --header ../components/poker_core/include/voice_ids.h
echo "voice.bin: $(stat -f%z voice.bin) bytes"
```

### 19.2 wav2adpcm.py 規格(打包格式 = voice_adpcm.c 解析格式,鎖死)

```
CLI: wav2adpcm.py <list.tsv> <wav_dir> -o voice.bin --header voice_ids.h
輸入 wav 必須:16000Hz / mono / s16le(不符即報錯退出,不做重採樣)
voice.bin(全小端):
  檔頭:  magic "PKV1"(4B) | u16 count | u16 version=1
  索引表: count × { u32 data_off(自檔案起始) | u32 adpcm_bytes | u32 pcm_samples }
  資料區: 每 clip = 4B 狀態頭 { s16 initial_predictor | u8 initial_step_index | u8 rsv=0 }
          + IMA-ADPCM nibble 流(標準 89 級 step 表;低 nibble 在前;奇數樣本尾 nibble 補 0)
編碼:每 clip 狀態獨立重置(clip 可任意順序隨機訪問);首樣本進狀態頭,自第 2 樣本起編 nibble。
V_BEEP:內建生成 880Hz、120ms、-6dBFS 正弦後照常編碼。
voice_ids.h 生成:
  // AUTO-GENERATED by wav2adpcm.py — DO NOT EDIT
  typedef enum { V_CONFIRM_DEALER = 0, ... , VOICE_COUNT } voice_id_t;
  #define VOICE_BIN_MAGIC 0x31564B50   // "PKV1"
校驗:枚舉順序 == tsv 順序 == 索引表順序;打包後自檢 V_N0+20==V_N20、V_N30+6==V_N90。
```

### 19.3 載入方式(裁定:獨立分區 + mmap,不嵌入 app)

理由:語音資產(~720KB)與 app 解耦,重生成語音不必重刷 app;四機同一 bin 直接燒分區。

```c
// hal_audio_init 內:
const esp_partition_t *p = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "voice");
assert(p);
const void *base; esp_partition_mmap_handle_t h;
ESP_ERROR_CHECK(esp_partition_mmap(p, 0, p->size, ESP_PARTITION_MMAP_DATA, &base, &h));
ESP_ERROR_CHECK(voice_init(base, p->size));   // 解碼器直接讀 flash 映射,零拷貝(拉取式,無整段解碼)
```

燒錄(與 app 分開,語音變更時才需要):

```bash
python $IDF_PATH/components/partition_table/parttool.py -p <PORT> \
    write_partition --partition-name=voice --input ../../common/voice/voice.bin
# 或等價:esptool --port ... write-flash 0x3F0000 ../../common/voice/voice.bin
```

啟動時 `voice_init` 驗 magic,失敗 → LOG_E + 全部播放呼叫靜默 no-op(遊戲仍可玩,只無聲)。

---

# 第四部分:開發與驗證

## 20. 建置、測試、分階段驗收

### 20.1 建置命令(本工作區實況,見 CLAUDE.md)

```bash
. ~/.espressif/v6.0.1/esp-idf/export.sh
# 本機 export.sh 不加工具鏈 PATH,手動補(S3 = xtensa):
export PATH="$HOME/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin:$PATH"

cd devices/StickS3
idf.py set-target esp32s3          # 僅首次
idf.py build                       # 正式建置
idf.py build -DPK_DEBUG_SOLO=1     # 單機冒煙建置(§20.3)
idf.py -p <PORT> flash monitor
```

### 20.2 host 端單元測試(hand_eval / side_pot / game_state 皆純 C)

```bash
cd common
cc -std=c11 -DPK_HOST_TEST -I components/poker_core/include \
   tools/test_hand_eval.c components/poker_core/src/hand_eval.c -o /tmp/he && /tmp/he
```

前提:hand_eval.c / side_pot.c / game_state.c **只 include stdint/stddef/string 與
poker_core 自家標頭**,不碰任何 esp_* / freertos(pk_config.h 對 esp 依賴零)。
`test_hand_eval.c` 至少涵蓋向量:皇家同花順判 9;A2345 輪子順(比 23456 小);
兩對踢腳分勝負;葫蘆>同花;四條帶踢腳;board plays(桌面即最佳,雙方平分);
7 張中 6 張同花取大五張;side_pot 向量:三層 all-in 分池、棄牌者貢獻、
不叫注自動回退、奇數籌碼歸按鈕左手、Σpayout==Σbet_hand 恆等。

### 20.3 單機冒煙模式(PK_DEBUG_SOLO)

- 建置旗標 `-DPK_DEBUG_SOLO=1` → `PK_MIN_PLAYERS=1`、`PK_PAUSE_MIN_ALIVE=1`(§3.1、裁定 R6)。
- 效果:一台裝置自建桌 → join 窗口關閉不因 <3 人解散 → E_ROSTER(1 人)→ 儀式
  (自己搶莊、seat 0 免認領直接 SEATING_DONE)→ 設籌碼/盲注 → E_HAND_START(發給自己)
  → 「只剩一人未棄」規則自然立即 E_HAND_RESULT(reason=1) → 局間循環。
- **不汙染協定**:線上格式、事件語義零改動;它只放寬兩個本地門檻判斷。
  正式建置必須不帶旗標(CI/驗收檢查 `strings build/*.bin | grep -c PK_DEBUG_SOLO` 應為 0
  ——實作時在 solo 模式的開機 title 加 "[SOLO]" 字樣防呆)。
- 用途:單機驗 UI 全畫面矩陣、語音齊播路徑(play_at 對自己也走排程)、M5PM1/顯示/音訊 HAL。

### 20.4 分階段驗收(對照產品 §9;每階段過了才進下一階段)

| 階段 | 建置 | 內容 | 對應產品 §9 條目 |
|---|---|---|---|
| 0 冒煙 | SOLO | 開機 ≤3s 進 SCANNING;自建桌跑通 LOBBY→儀式→設定→發牌→RESULT→局間;全畫面可達;V_BEEP/數字播報正常;單擊電源鍵不復位(§13.3 驗證法);系統選單:BATT % 正確(插 USB 顯示 `BATT USB`)、BRIGHTNESS 三檔即時生效、VOLUME 三檔即試播(R9) | — |
| 1 成桌與儀式 | 正式 ×2~3 台(可混 xingzhi) | 10s 窗口成桌、成員數一致;齊播 confirm dealer;搶莊唯一;逐位排座+跳過路徑;齊播錯位 <30ms(§15.5 方法複測) | 1、2、3 |
| 2 單街下注 | 正式 ×3 | 籌碼/盲注設定(含本地預驗證與 REJECT 回饋);E_HAND_START 各自見底牌;preflop 一輪 check/call/raise/fold 正確;raise-to 語義、二確認 | 4、5、6、7 |
| 3 全流程 | 正式 ×3+ | 四街+攤牌逐位翻牌+側池;換莊交接;**拔電池接管 ≤6s**;斷線回歸 screen_from_state 落點;中途加入/退出/淘汰/補籌;10 人滿桌降級顯示 | 8–15 |

階段 3 的拔電池測試對本機格外重要:StickS3 雙擊關機攔截不到(§13.5),
接管路徑是本機的常態退出路徑,必須反覆壓測。

---

# 第五部分

## 21. 實作經驗回寫(供後續裝置)

> **給實作 agent 的指示**:完成 StickS3 實作(或任何一個驗收階段)後,把**硬體無關**的
> 經驗按下列小節追加到本章(硬體相關的坑寫回 `hardware-reference.md` §11)。
> 後續三份裝置指南(zuowei-c3 / xingzhi-cube / Cardputer-Adv)撰寫前**必讀本章**。
> 涉及 poker_core 介面變更的,須同步更新 §3/§4 並在此註明版本與理由;
> `common/` 一經兩機互通驗證,介面凍結,之後只准加不准改。

> **回寫者**:StickS3 首版實作(2026-07-21)。狀態:全專案編譯通過(app 1.08MB / 72% 分區餘裕)、
> host 測試 37 檢查全過、voice.bin 生成驗證(74 片段 / 418KB)。**尚未上真機**,
> 故 §21.4/§21.5 的實測欄位仍為估算,標【待實測】。以下經驗**硬體無關**,後續三機直接複用。

### 21.1 共用核心(poker_core)的坑

1. **Master 端發事件必須「同步套用」才能讀回**(最重要)。`pbus_publish_evt()` 依規範「入佇列、
   任意 task 可呼叫」,但 master_engine 在 `on_cmd`/`tick`(皆 pbus task 上下文)連發多個事件並需要
   **讀回剛更新的 `st`** 才能推進牌局。若 publish 只入佇列,則同一次 on_cmd 內的後續讀 `st` 看到舊值。
   **裁定/作法**:master_engine 直接呼叫內部 `pn_publish_locked()`(同步:編序→存 log→`pn_deliver`
   套 reducer→廣播),繞過 txq;txq 僅供「真正跨 task」的公開 API 用。後續裝置的引擎沿用此分界。
2. **`game_view` 全部由 `st` 純推導,不要另存 E_ACTION_REQ 快照**。`call_amt/min_raise_to/
   max_raise_to/can_check` 可由 `cur_bet/min_raise/raise_count/bet_cap/我的 chips,bet_round` 現算
   (公式見 `game_view_publish`)。好處:交付順序(publish 先於 on_event)不會造成 view 落後一幀。
3. **reducer 的 phase 由 prompt 事件驅動**:協定沒有「set phase」事件,`game_state_apply` 在
   `E_DEALER_CALL/E_SEAT_PROMPT/E_CHIPS_PROMPT/E_BLINDS_PROMPT/E_HAND_START/E_HAND_RESULT/
   E_HAND_ABORT/E_GAME_PAUSE/E_GAME_RESUME` 各自落 phase。**E_ROSTER 不改 phase**(仍 LOBBY),
   避免加入窗口期就誤觸儀式;窗口關閉由 pbus_session 主動 `publish E_ROSTER` 後直接
   `master_engine_begin()` 啟動儀式(session→engine 的單向啟動信號)。
4. **座位↔player_id 映射是 reducer 最易錯處**:`st.p[]` 以 player_id 索引,而 `E_HAND_START.deal[]`、
   `E_ACTION.seat`、`E_HAND_RESULT.*` 全以**座位**表達。統一用 `seat_to_pid()` 轉換;未發牌座位清零。
5. **side_pot 恆等式的守門**:切池邊界要**含棄牌者的 bet_hand**;對「無 eligible(僅棄牌者/超額不叫注)」
   的分層,把該層退還給貢獻者,`Σpayout==Σbet_hand` 才在「棄牌者投入 > 任何未棄者」的病態向量下仍成立
   (host 測試 S4 專測此)。「不叫注自動退還」不需特判——最高層只有下注者 eligible 自然退回。
6. **hand_eval 直方圖排序必須穩定**:按 (count desc, rank desc) 排 groups 時,若用選擇排序的兩兩交換,
   同 count 的 rank 次序會被打亂,導致踢腳比較錯誤(實測踩到:`777K9` 被誤評為輸給 `777K4`)。
   **改用插入排序**(穩定),同 count 保持輸入的 rank 遞減序。host 向量務必含「三條帶兩張大踢腳」。
7. **亂序緩衝/gap timer 開停要小心語義**:每次 `d==0` 交付並排空連續 OOO 後,先 `t_gap_arm=0` 再
   `pn_gap_arm_if_needed()`(僅在仍有更遠的洞時重新武裝),避免「有洞卻沒 timer」或「無洞空轉 timer」。

### 21.2 編譯與工具鏈

1. **`main` 元件必須 `WHOLE_ARCHIVE`**(最關鍵、四機共通)。§2.1 說「HAL 符號由 main 在最終連結提供,
   IDF 自動處理」——**實測不成立**:poker_core.a 與 main.a 互相引用(main 呼叫 app_flow;poker_core
   反向呼叫 `hal_*`),GNU ld 的 archive 掃描順序(poker_core 在 main 前)無法解析,出現大量
   `undefined reference to hal_battery_pct/hal_rand/hal_input_init/hal_settings_*`。**解法**:在
   `main/CMakeLists.txt` 的 `idf_component_register(...)` 加 `WHOLE_ARCHIVE`,強制 main 全 archive 進連結。
   後續三機的 main **一律照加**。(§2.1 應補此註記。)
2. **不用 `espressif/es8311` 元件,自帶最小 codec 驅動**(§2.4 預案主動觸發)。理由:①免網路相依 ②免
   legacy-i2c 相容風險。`main/es8311_min.[ch]` 用新 `i2c_master_transmit` 寫 ~20 個暫存器完成 16k/MCLK
   播放路徑,codec 寫失敗只 LOGW 不中斷(無硬體時仍可跑)。**寄存器序列未上真機驗證,列【待實測】**。
3. **IDF v6 原生 API 對齊**:I2C=`i2c_new_master_bus`+`i2c_master_bus_add_device`;
   I2S=`i2s_new_channel`/`i2s_channel_init_std_mode`/`i2s_channel_write`;voice 讀 flash=
   `esp_partition_find_first(DATA,0x40,"voice")`+`esp_partition_mmap`。全部編譯通過。
4. **`-Werror` 下自訂碼零容忍,四個實際踩到的坑**(後續照避):
   (a) **註解裡別出現 `*/`**:`/* … E_JOIN_*/REMIND … */` 的 `*/` 提前關閉註解,引發整檔連鎖 parse error;
   (b) **struct 回傳值別加 `const`** → `-Werror=ignored-qualifiers`;
   (c) **一行兩敘述**(`if(a)x; if(b)y;`)→ `-Werror=misleading-indentation`,拆行;
   (d) 純函式檔(hand_eval/voice_adpcm)要**顯式 `#include <stdbool.h>`**、app 層要顯式
       `#include "pk_config.h"`——不要依賴間接引入。
5. **esp_lvgl_port `^2` 拉到 LVGL 9.5.0**;§14 的 render 程式碼(`lv_label_set_text`/`lv_bar_set_value`/
   `LV_OBJ_FLAG_HIDDEN`/`lvgl_port_lock`)在 9.5 全可用,無需改。
6. **host 測試可移植性**:hand_eval.c / side_pot.c / number_speech.c / voice_adpcm.c 只依賴
   stdint/stddef/string/stdbool,桌機 `cc` 可直編(voice_adpcm 需一個 3 行 esp_log/esp_err stub)。
   `_Static_assert` 尺寸(185/189/95)在桌機與 xtensa 都通過,證明 packed 佈局跨編譯器一致。
7. **【來源 zuowei】新工具鏈更嚴的靜態分析會讓「凍結且驗證過」的核心編不過。** StickS3 用
   xtensa 工具鏈編過的 `common/`,在 zuowei-c3 的 **riscv32 esp-15.2.0(GCC 15)** 下,`pbus_session.c`
   `on_cmd()` 觸發 `-Werror=stringop-overflow`(詳見 R13——那是**真實的 2-byte 堆疊溢位**,非誤報)。
   教訓:①「一機編過」不等於「四機編過」,每台機首建置都要把 core 當新代碼看待;②GCC 版本差是四機
   整合期的固定風險,後續 xingzhi/cardputer 若換更新工具鏈可能再爆新的 `-Werror`;③板級只能加
   `idf_build_set_property(COMPILE_OPTIONS "-Wno-error=<檢查>" APPEND)` 於**自身** `CMakeLists.txt`
   把 error 降回 warning(不觸碰凍結核心,自訂碼不受影響),真修法走 §21 升版由主 agent 裁定。

### 21.3 文件歧義的新裁定(續 §4)

**R10|音量/亮度是否 NVS 持久化:指南 R9(不持久化)vs 產品 §5.6 v1.2(持久化)。**
裁定:**持久化**(依產品 v1.2)。理由:R9 立論前提明寫「產品 §5.6 系統選單只有音量」,而 v1.2 是
**晚於 R9 的、明確的使用者追加需求**,已推翻該前提;持久化零協定影響、實作僅一次 NVS 讀/寫。
落地:namespace `poker`,key `vol`/`bri`,開機載入(缺省 vol70/bri80),變更即寫;選單邏輯在共用
`app_flow.c`,NVS 讀寫在各裝置 `hal_misc.c`(`hal_settings_load/save_*`,新增於 `hal_misc.h`)。
**建議**:回饋修訂指南 §4 R9,標注被 R10 取代。

**R11|`app_prepare_poweroff()` 由誰實作:§3.9(poker_core 實作)vs §13.5(StickS3 main 空實作)。**
裁定:本版由 **main 提供單一定義**(StickS3 無法攔截 PM1 雙擊關機,故 no-op),common 不定義以免重複符號。
**建議**:未來若要共用「關機前若為 Master 先交接」邏輯,common 提供 `__attribute__((weak))` 預設,
各裝置 main 可覆寫;本版為避免弱符號在 WHOLE_ARCHIVE 下的行為分歧,先只放 main。

**R12|裝置類別注入。** poker_core 不能知道自己是哪台機。作法:pbus 內宣告
`__attribute__((weak)) uint8_t pk_board_device_class(void){return 0;}`,各裝置 main 提供強符號
(StickS3 `hal_misc.c` 回 4)。後續三機照此(zuowei=1/xingzhi=2/cardputer=3)。

**共用介面延伸(非衝突,但需在介面凍結前記錄,格式同「只准加不准改」):**
- `pbus.h` 增 client 側回饋 getter:`pbus_cmd_inflight()`、`pbus_consume_reject()`——協定 §14 只給
  Master 側 CMD 路徑(R2),缺 client 收 ACK 後把 REJECT reason 交 UI 的路徑,以此二 getter 補。
- `game_state.h` 增 `game_set_event_hook/link_hook/role_hook` + `game_view_publish()`——供 app_flow
  觀察事件(播報排程)與 link 狀態,不改任何既有簽名。
- `master_engine.h` 增 `master_engine_begin()`——session 於 E_ROSTER 後啟動儀式的單向信號。
這些都是「加」,兩機互通驗證後隨 §3 一併凍結。

**R13|【來源 zuowei】凍結核心 `pbus_session.c` `on_cmd()` 的真實堆疊緩衝溢位。**
NOT_MASTER 導向回覆組包處:
```c
uint8_t body[8]; pn_cmd_ack_t *a = (pn_cmd_ack_t *)body;   /* pn_cmd_ack_t 為 4 bytes */
...
memcpy(body + sizeof(pn_cmd_ack_t), g_pb.master_mac, 6);   /* 寫入 offset 4..9 = 需 10 bytes */
pn_send_typed(rx->src, PN_PKT_CMD_ACK, body, sizeof(pn_cmd_ack_t) + 6);  /* 也讀 10 bytes 送出 */
```
`body` 只有 **8 bytes**,但依 `pbus_proto.h` 行 107 的設計註解(「NOT_MASTER 時尾端附 uint8_t
master_mac[6]」)明確需要 `4 + 6 = 10` bytes。這是**真實的 2-byte 堆疊寫越界**(且把 2 bytes 相鄰
堆疊內容當 MAC 尾送出),非誤報。xtensa GCC 未偵測,riscv32 GCC 15 以 `-Werror=stringop-overflow`
擋下。**裁定**:核心凍結,不在本次改;正確修法是 `uint8_t body[10];`(或 `[12]` 對齊),須走 §21
「只准加不准改」以外的**缺陷修復升版**流程,由主 agent 決定。**影響評估**:僅在「非 Master 收到
被誤送的 CMD」這條少見路徑觸發;協定 §9.5 接管期或 dealer≠master 短窗可能命中。zuowei 首版以
板級 `-Wno-error=stringop-overflow` 讓建置通過,行為與 StickS3 現行(同帶此 bug)一致,未加劇。

> **✅ 已裁定修復(2026-07-21,主 agent)**:核心 `pbus_session.c` 已改為
> `uint8_t body[sizeof(pn_cmd_ack_t) + 6]`;zuowei 板級的 `-Wno-error` 降級已移除;
> StickS3(xtensa)與 zuowei(riscv32 GCC 15)兩專案重建皆綠。
> 後續裝置直接使用修復後的核心,無需任何 workaround。

### 21.4 齊播與音訊實測數據

- `AUDIO_PATH_LATENCY_MS` 仍為計算值 **62**(DMA 4×240=60ms + ES8311 群延遲 ~2ms);**跨機錯位中位數
  待真機用 §15.5 錄音對拍法量測後回填**。
- ES8311 `es8311_min.c` 寄存器序列已於真機修正並讀回驗證(2026-07-21,見 §21.9 R16):
  舊序列 0x01=0x30 未開 DAC 時鐘、0x02=0x10 分頻錯、缺 0x12/0x13 輸出級 —— 現對齊
  espressif/es8311 官方元件 coeff 表 {4096000,16000}。
- 功放常開改為 `m5pm1_amp_set(1)`(PM1 GPIO3 推挽恆高,§21.9 R16);底噪結論【待實測】。
- voice.bin 實測 **418,390 bytes / 74 片段**(macOS `say -v Samantha`,16k mono IMA-ADPCM),遠低於
  §6.1 的 720KB 估算——1.125MB 的 voice 分區餘裕充足;`voice_duration_ms` 驗 V_BEEP=120ms 準確。

### 21.5 SRAM/性能實測

- **均為靜態估算,真機待補**。task 棧設定:pbus 6144 / audio 4096 / ui 8192 / gesture 2560 /
  buttons 2048 / LVGL 8192(§5 任務表)。
- app 映像 **1,135,376 bytes**(factory 3.87MB,餘 72%);voice 418KB。
- pbus 佇列:rxq 24 槽、txq 12 槽(各 `sizeof(pn_rx_item_t)`≈258B / `pn_txreq_t`≈220B)。
- **給 C3/Cardputer(無 PSRAM)的提醒**:`pbus_t`(g_pb)含 64 槽事件日誌(64×210≈13KB)+ 狀態鏡像
  ——是 poker_core 最大的單一靜態結構,C3 立帳時務必計入 BSS;必要時成員側日誌可縮到 32 槽
  (協定僅要求成員 ≥32,Master 64;目前為省事兩者共用 64,C3 可分開)。

### 21.6 第三機(xingzhi-cube)回寫的跨裝置經驗

> 來源:xingzhi-cube-1.54tft-wifi 實作(2026-07-21,建置級)。硬體無關,四機通用。

- **凍結核心在第三塊板(同 xtensa esp-15.2.0 / IDF v6.0.1)首建置一次過、自訂碼零 warning**,
  正式 + `-DPK_DEBUG_SOLO=1` 皆綠。**R13 修復穩定**:xingzhi 未加 zuowei §16.1-1 的
  `-Wno-error=stringop-overflow` 墊片,建置正常——該墊片確認為修復前的臨時物,新板一律不加。
  §21.2-7「新工具鏈爆新 -Werror」對同工具鏈裝置的風險經三機驗證趨近於零。
- **app 映像三機對照**(factory 3.87–5MB,均餘充足):StickS3 1,135,376 B > zuowei 1,174,528 B
  > **xingzhi 1,128,560 B(最小)**。裝置映像大小與其驅動集正相關:xingzhi 去 I2C/codec/IMU/opus
  故最小。後續裝置立帳可用此三點作內插基準。
- **`hal_battery.c` 的整數百分比運算(原 zuowei §16.1-4)已在 xingzhi 再次驗證無害**:
  `mv * BAT_DIVIDER`(float)顯式 `(int)` 轉型即可消 `-Wfloat-conversion`;凡走 ADC 分壓的板級
  電量碼建議統一此式,不用浮點 `BAT_FULL/EMPTY_MV`。
- **文件歧義 R14(顯示層)**:`idf.py build` 尾端印出的燒錄命令恆含 `--flash-mode dio`,與
  `CONFIG_ESPTOOLPY_FLASHMODE_QIO=y` 無關(bootloader header 依 sdkconfig 攜真實模式)。屬 IDF
  顯示層預設字串,四機通用,勿因此誤改 sdkconfig 或手動加 flash-mode。

### 21.7 第四機(Cardputer-Adv)回寫的跨裝置經驗

> 來源:Cardputer-Adv 實作(2026-07-21,建置級)。硬體無關,四機通用。
> 本板為全專案最後一台,板級工作量最大(TCA8418 鍵盤 + ES8311 無 MCLK 兩個新驅動)。

- **凍結核心在第四塊板(同 xtensa esp-15.2.0 / IDF v6.0.1)首建置一次過、自訂碼零 warning**,
  正式 + `-DPK_DEBUG_SOLO=1` + `-DPK_KEYMAP_DUMP=1` 三組合皆綠。**R13 修復穩定**,本板未加任何
  `-Wno-error` 墊片。**四機均未發現新核心 bug**,§3 介面凍結經四機驗證。
- **app 映像四機定案對照**(factory 3.87–5MB,均餘充足):
  xingzhi **1,128,560** < StickS3 **1,135,376** < cardputer **1,136,832** < zuowei **1,174,528** B。
  cardputer 略高於 StickS3(去 m5pm1/imu、加 tca8418 + esp_adc,量級持平)。四點內插基準完整。
- **`PK_DEBUG_SOLO` 傳遞機制確認**:poker_core CMakeLists 以 `target_compile_definitions(... PUBLIC
  PK_DEBUG_SOLO=1)` 宣告,經 `REQUIRES poker_core` 傳遞到各裝置 main —— **各板 main/CMakeLists
  無需自加透傳**,`#ifdef PK_DEBUG_SOLO` 在 app_main.c 直接生效。正式 bin `strings|grep SOLO`=0、
  SOLO bin=1 為驗證正確與否的快速檢查。板級自有的除錯宏(如 cardputer 的 `PK_KEYMAP_DUMP`)才需
  在 main/CMakeLists 用 `PRIVATE` 自行透傳。
- **`ESP_RETURN_ON_ERROR` / `ESP_RETURN_ON_FALSE` 需顯式 `#include "esp_check.h"`**:
  凡板級新驅動(非 hal_* 而是晶片驅動,如 tca8418.c / es8311)用到這類巨集,務必在該 .c 直接
  include esp_check.h ——`esp_err.h` 不含這些巨集,漏了會 implicit-declaration 編譯失敗。
  `board_i2c.c` 已含此 include(四機共用),新驅動照辦。
- **裝置類別四機定案**(R12 落地):StickS3=4 / zuowei=1 / xingzhi=2 / **cardputer=3**,
  各板 `hal_misc.c` 提供強符號覆寫 pbus.c 的 weak 預設。

### 21.8 真機四機聯調回寫(2026-07-21,主 agent;核心 v1.1 真機修訂批次)

> 來源:四機首次真機聯調。以下 9 處**核心修訂已直接落入 `poker_core`**(搜尋「v1.1 真機修訂」
> 可定位全部改動),對應的教訓硬體無關、四機通用。

**協定層真 bug(不修無法多機共存):**
1. **`TABLE_DISSOLVE` 未過濾 table_id** —— 同場域任何過期孤桌的解散廣播會把**別人的桌一起炸掉**
   (實測:三人儀式桌被殺,全員退回掃描)。修:只認指名本桌的 dissolve;payload 的
   `target_table_id/mac` 作導引,收到即轉投新桌。
2. **`on_hello` 的 PENDING/REJECT 分支漏 `pn_add_peer`** —— ESP-NOW 對非 peer 單播**靜默失敗**,
   候選者收不到 JOIN_ACK 無限循環。教訓:**任何單播回覆前必先 add_peer**;esp_now_send 的
   返回值不會告訴你這件事(fire-and-forget 包裝把錯誤吞了)。
3. **成桌後 Master 停發 ANNOUNCE**(實作漏了協定 §8.2)—— 晚開機/重啟的裝置永遠找不到已成之桌。
   修:ROLE_MASTER 以 1Hz 續發 ANNOUNCE(phase=實際 phase);掃描端對 in-game 桌走
   HELLO→PENDING/ACCEPT_BACK。
4. **E_JOIN_PENDING 無去重** —— 候選者每次 HELLO 都觸發一筆新事件(實測 3Hz 洗版,
   會沖掉 64 筆事件日誌尾端)。修:MAC+10s TTL 快取。

**狀態機死路(卡死型,靠超時/回退修復):**
5. **JOINING 無超時** —— 目標桌消失(對方重啟/斷電)即永久卡死。修:5s 回掃描
   (`PN_T_JOIN_GIVEUP_MS`)。開發期反覆燒錄會大量製造「幽靈 ANNOUNCE」,此路徑必踩。
6. **LOBBY 期 Master 失聯只做了「不接管」、漏了「回掃描」**(協定 §8.1.3 後半)。
7. **PENDING 期 Master 失聯無處理**。修:心跳斷 10s 回掃描。
8. **建桌衝突合併(§8.1.3)整段未實作** —— 多台同時開機 → 多桌鎖步循環永不相遇。
   修:TEMP_MASTER 聽到更小 MAC 的 LOBBY ANNOUNCE → 廣播帶導引的 dissolve ×3 → 轉投對方。

**UI 陷阱:**
9. **`screen_from_state` 未處理「未入桌」** —— 清零狀態的 `phase=0` 恰是 `PN_PH_LOBBY`,
   被解散後畫面顯示「LOBBY 0 players」且按鍵無綁定,外觀如死機。修:`my_player_id==0xFF`
   一律 SCANNING。教訓:**枚舉 0 值不要同時當「有效狀態」與「memset 預設」**。

**診斷方法論(留在核心,長期有效):**
- 事件交付(`pbus_rel: evt seq/id/phase/n`)、角色轉移(`role A -> B`)、CMD 收發、按鍵 raw
  皆有 INFO 日誌 —— 多機狀態機**不可在啞日誌下除錯**(本輪一半時間耗在「沉默=凍結?」的誤判上,
  實際是成桌成功但無任何日誌)。
- 真機聯調的最大干擾源是**開發流程本身**:反覆燒錄/重啟的裝置留下幽靈桌與半死會話,
  防禦性超時(第 5–7 條)不是錦上添花,是開發期就必須有的地基。

**另:Cardputer G38 背光教訓見 Cardputer 指南 §17.4(R15)**:背光腳兼電源使能(負載開關)的板子
LEDC 頻率不可照抄(5kHz 打死電源軌,M5GFX 同板用 256Hz)—— 新板 bring-up 先查 M5GFX 同板
`Light_PWM` 參數。

### 21.9 StickS3 完全無聲修復(2026-07-21,真機驗證)

**R16|StickS3 無聲雙根因:PM1 GPIO3 開漏未改推挽(功放 EN 拉不高)+ ES8311 DAC 時鐘/輸出級未開。**

- **根因 1(主)**:AW8737 功放 EN 由 M5PM1 GPIO3(PYG3_SPK_Pulse)驅動。舊碼只寫脈衝寄存器
  0x53(`0x80|(n<<5)|3`,編碼本身符合手冊 [7]REFRESH [6:5]NUM [4:0]GPIO),但 **M5PM1 手冊 V1.9
  明載 GPIO_DRV(0x13) 上電預設 0x1F = 全開漏**,且 0x53 條目註明「開漏需外部上拉,否則須先設推挽」
  —— 板上無上拉,EN 恆低,功放從未開啟。日誌「ES8311 init done」「I2S 寫入無錯」全部正常,
  因為資料路徑活著、只是最後一級斷電。**修法**(官方 M5PM1 教程 spk amp 章 / M5Unified 同款):
  FUNC0(0x16)[7:6]=00 普通 GPIO → DRV(0x13).3=0 推挽 → OUT(0x11).3=1 → MODE(0x10).3=1 輸出;
  讀回自證含 GPIO_IN(0x12).3=1(引腳實測高)。關功放 = OUT.3=0(IR 接收前需要)。
- **根因 2(並存)**:`es8311_min.c` 舊序列 3 處錯:① 0x01=0x30 只開 MCLK/BCLK,
  **DAC/ADC 數位與類比時鐘(bits3:0)全關** → 應 0x3F;② 0x02=0x10 = pre_multi×4 分頻錯 → 應 0x00
  (256fs 直入);③ **缺 0x12=0x00(DAC 上電)與 0x13=0x10(DAC→HP drive 輸出級)** —— 輸出級斷路。
  修正後全序列對齊 espressif/es8311 官方元件,init 尾端讀回 10 個關鍵寄存器比對
  (0x01/0x02/0x06/0x09/0x0A/0x0D/0x12/0x13/0x31/0x32),真機日誌 `readback verified (10/10)`。
- **真機證據**(2026-07-21 開機日誌):
  `m5pm1: AW8737 amp ON verified: MODE=0x0c OUT=0x0c DRV=0x13 FUNC0=0x00 IN=0x1d`
  (MODE.3=1 / OUT.3=1 / DRV.3=0 推挽 / FUNC0[7:6]=00 / **IN.3=1 引腳物理高**)。
- **教訓(四機通用)**:① PM1 類外掛 PMIC 的 GPIO **預設開漏**是系統性陷阱(L3B 修復時已踩過一次,
  本次同一坑換了引腳再踩)——凡驅動使能腳,FUNC/DRV/OUT/MODE 四件套一次寫齊並讀回;
  ② codec「init done」日誌與 I2S 無錯**不能證明發聲鏈路完整**,無聲排查順序:功放使能 → codec
  時鐘樹/輸出級讀回 → 音量;③ 寄存器序列必須以官方驅動為準逐位核對,「看起來像」的分頻值
  (0x30 vs 0x3F)是最貴的一類錯。

### 21.10 串口測試通道與第二輪真機聯調(2026-07-21,主 agent)

**測試基礎設施(常駐,四機皆有):**
- **UI 語義 trace**:每次 render 輸出一行 `ui: [畫面] t=.. g=.. pot=.. my=.. hand=.. board=..`
  (app_screens.c);多機狀態全程可由串口觀測,無需肉眼看屏。
- **按鍵注入**:串口寫單字元即注入手勢(`o/O`=OK 短/長、`n/N/r`=NEXT、`d/u`=incdec;
  pk_testio.c,經 usb_serial_jtag driver RX,與 console 日誌 TX 並存)。
- 主機端 `serial_io.py <port> <log> <cmdfile>`:同埠雙向(即時落檔 + 輪詢命令檔),
  配合注入字元可全自動驅動整場多機牌局(成桌→儀式→整局→換莊→容錯)。

**第二輪自主測試抓到並修復的 5 個 bug(搜「B1..B5」定位):**
- **B1** `screen_from_state` 以 `button_seat` 判莊家,但首局按鈕到發牌才設 → BLINDS 畫面無人可達,
  全桌卡 60s 超時代設。修:按鈕未定時莊家=排座 seat 0。
- **B2** `advance_action` 從按鈕位掃首行動者 → 4+ 人局 preflop 首行動錯成按鈕(應 UTG=BB 左手)。
  修:`advance_action_from((nd==2)? sb_seat : next_active_seat(bb_seat))`。
- **B3(嚴重)** `do_abort` 的 `refund[]` 空槽經 memset 得 `seat=0` ≠ 規格哨兵 0xFF →
  reducer 把空槽當「seat 0 → chips=0」執行,**seat 0 玩家籌碼被清零**(真機踩中)。
  教訓:**凡協定陣列用 0xFF 空槽哨兵者,builder memset 後必須顯式補哨兵**(settle_hand 有做、
  do_abort 漏做 —— 同型結構要逐一過)。
- **B4 硬化** 孤家寡人臨時桌聽到「遊戲中」ANNOUNCE → 棄桌改投(重啟裝置錯過掃描窗的第二回歸路徑)。
- **B5** 未入桌(table_id==0)裝置會吃到別桌 EVT 廣播並被 epoch 仲裁降級成幽靈成員 → handle_evt
  對未入桌一律丟棄。
**修復後全部真機驗證**:BLINDS 畫面即達、UTG 首行動、成員中途重啟 2 秒 ACCEPT_BACK 回局中、
Master 被殺 → 接管+廢局退注(無人歸零)+新局 → 死者重啟回歸正好接手行動,全程無人工干預。

### 21.11 無聲問題最終根因(2026-07-21 深夜,主 agent;R17)

**StickS3:I2S 播放腳接反。** `hardware-reference.md` §3 的「DOUT=G14(錄音)/DIN=G16(播放)」
是**慣例推斷且推反了**(該文件自己有標注「方向為慣例推斷」)。以 78/xiaozhi-esp32
`m5stack-stick-s3`(同硬體、實證出聲)為準:**DSDIN(播放進 codec)=G14、ASDOUT(錄音)=G16**。
症狀特徵:所有寄存器讀回正確、功放 EN 實測高、I2S 無錯 —— 唯獨 PCM 灌進了 codec 的輸出腳。
教訓:**引腳「方向」永遠要用會出聲/會動的參考實作驗證,寄存器讀回自證救不了接錯的線**。

**Cardputer-Adv:三重封印。** ① I2S MONO slot 使 BCLK=256kHz,與 ES8311 SCLK 模式分頻
(pre_multi×8 → 需 512kHz)錯一倍 → 改 STEREO(樣本左右複製);② REG01=0xB0 低半位元組=0
→ DAC/ADC 內部時鐘全關(StickS3 曾修過的同款 bug,抄序列時要抄「修過的」版本)→ 0xBF;
③ 序列缺 0x0E/0x12/0x13/0x1C/0x31(類比上電/DAC 上電/輸出級/解靜音)→ 補齊對齊 StickS3
真機驗證序列。另:codec 0x32 固定 0dB、音量單走軟體增益(修雙重衰減 ≈ −24dB)。

**通用教訓:同一顆 codec 的第二份驅動不要「重新推導」——直接複製已真機驗證的序列再改差異點
(時鐘源),並列 diff 自查。**

> **R17 補遺(v1.7)**:插 USB 時 M5PM1 VBAT 不一定讀 0,實測會出現**數百 mV 非零殘值**,
> 繞過 `vbat==0` 守門 → UI 顯示 0%。守門條件應為合理性下限 `vbat < 3000mV → 未知(0xFF)`
> (運作中鋰電不可能 <3.0V)。
