# 德州撲克遊戲機 產品開發文件 v1.1

> 本文件與《POKER-NOW 核心協定 v1.1》(`PROTOCOL.md`)配套。
> 協定文件定義「裝置之間怎麼溝通」;本文件定義「每台裝置上的軟體長什麼樣」——
> 架構分層、共用遊戲核心、HAL 介面、2 按鍵互動模型、UI 畫面、英文語音資源與播放規則。
>
> **硬體基準 = 四種裝置的交集**:鋰電池 + LCD + **2 顆可用按鍵** + 揚聲器。
> 一切設計以此交集為底線;多餘硬體(第 3 顆鍵、鍵盤、IMU、更大螢幕)只能「錦上添花」,
> **不得成為任何流程的必要條件**。

**修訂記錄**

| 版本 | 重點 |
|---|---|
| v1.0 | 初稿 |
| v1.1 | 依兩份對抗式審查(Fable / Opus)修訂:RESULT 改逐位翻牌子畫面(含側池);附錄 A 重寫為全矩陣+全域遷移總則;新增 `screen_from_state()` 恢復入口;LONG_OK 語義收斂+系統選單;命令拒絕回饋;Fold/All-in 二確認;MY_TURN 常駐底牌;淘汰/補籌(SPECTATE/REBUY);音訊管線延遲校準+拉取式串流解碼+排程常數;數字合成補千位;CHIPS_MAX=9999;整機 SRAM 預算表;工具鏈基線(統一 IDF 6.0.1);小面板顯示降級規範。 |
| v1.2 | 使用者新增三項全裝置 UX 需求:**電量百分比常駐顯示**(標題列右側,`hal_screen_t.battery_pct`);**螢幕亮度可調**(`hal_display_set_brightness`,系統選單 BRIGHTNESS 項);**音量可調**(既有 VOLUME 項保留);亮度/音量設定經 NVS 持久化。 |
| v1.3 | 真機首測後使用者裁定的 UI 總改版(§2.2a):**仿真紙牌圖形**(白底圓角牌面、點數+花色圖形、♦♥ 紅 ♠♣ 黑、蓋牌花紋背);**遊戲中畫面固定五區版式**(狀態+電量 / 5 公共牌 / 桌面總籌碼 / 我的籌碼 / 2 手牌);全部畫面清晰化重繪;**渲染下沉為共用 LVGL 模組**(四機皆 LVGL,per-device hal_display 只留面板 bring-up 與亮度)。 |

**目標裝置與其超集能力(僅供各裝置指南加分利用)**

| 裝置 | 螢幕 | 可用按鍵 | 額外輸入 | 音訊路徑 |
|---|---|---|---|---|
| zuowei-c3-lcd-pwr | 240×240 | 3 | – | VB6824(UART,PCM 下行) |
| xingzhi-cube-1.54tft-wifi | 240×240 | 3 | – | 原生 I2S 功放(注意:demo 喇叭管線為 24kHz,播 16k 資源須重配 I2S 或重採樣) |
| Cardputer-Adv | 240×135 | 56 鍵鍵盤 + G0 | 鍵盤、IMU | ES8311 codec(無 MCLK,SCLK 時鐘模式) |
| StickS3 | 135×240(直向) | 2 | IMU(陀螺儀) | ES8311 codec |

---

## 0. 工具鏈與板級基線(發包前提)

- **四機統一 ESP-IDF v6.0.1 原生**開發(zuowei / xingzhi demo 已驗證;
  Cardputer 不用出廠的 IDF 5.4.2 + M5GFX 生態 —— M5GFX 在 IDF v6 無法編譯,
  esp_lcd 直驅 ST7789 四板皆可行)。顯示層統一 **esp_lcd(+ LVGL,經 esp_lvgl_port)**;
  無法跑 LVGL 的情況不存在(四機 SRAM 皆足,見 §8)。
- `pbus` 只依賴 `esp_wifi` / `esp_now` / FreeRTOS —— IDF 6.0.1 API 為準。
- **各裝置需新寫的板級驅動**(工作量務必計入):
  - Cardputer-Adv:TCA8418 鍵盤讀取(I2C 0x34,~150 行)、ES8311 無 MCLK 模式配置。
  - StickS3:M5PM1 驅動(**三坑必做**:啟動先使能 L3B 電源域否則 LCD 永遠黑屏;
    改寫 BTN_CFG 停用單擊復位且知悉寄存器帶電持久;插 USB 時 VBAT 讀 0 → 電量回報 0xFF)。
  - zuowei / xingzhi:直接複用各自 demo 的 display/backlight/battery/buttons/audio 模組。

## 1. 軟體架構

每台裝置的韌體分四層;**上兩層(遊戲核心 + UI 流程)為全裝置共用程式碼**,
下兩層(HAL 實作 + 板級驅動)per-device:

```
┌────────────────────────────────────────────────┐
│ app/  UI 流程(畫面狀態機、輸入合成、播報排程)      │ ← 共用(本文件 §4–§6)
├────────────────────────────────────────────────┤
│ core/ 遊戲核心(牌局狀態機、Master 決策引擎、       │ ← 共用(本文件 §3)
│       手牌評估器、語音串流解碼)+ pbus 協定層       │
├────────────────────────────────────────────────┤
│ hal/  hal_display / hal_input / hal_audio /      │ ← per-device 實作
│       hal_battery / hal_power / hal_misc(§2)     │    (各裝置《軟體開發指南》)
├────────────────────────────────────────────────┤
│ 板級驅動(§0:demo 複用或新寫)                     │
└────────────────────────────────────────────────┘
```

設計原則(承協定文件 §1 的深模組思路):

1. `core/` 是**深模組**:對 `app/` 只暴露「目前牌局快照 + 我可以做什麼 + 提交意圖」;
   協定、複製、容錯全部不可見。
2. `hal/` 的每個介面都刻意**窄**(§2)。窄介面 = 四份裝置實作的工作量與分歧都最小。
3. **共用碼禁止 `#ifdef` 板型**;裝置差異只能表達為 HAL 介面的不同實作
   (per-device 的常數,如音訊延遲,經 `hal_*_caps()` 取得,不進共用碼條件編譯)。

## 2. HAL 介面定義(per-device 實作的全部工作)

### 2.1 hal_input —— 原始鍵事件(手勢合成在共用層)

```c
typedef enum { KEY_OK, KEY_NEXT } hal_key_t;   // 邏輯鍵:流程只認識這兩顆

typedef void (*hal_input_cb_t)(hal_key_t k, bool pressed);  // 原始按下/放開
esp_err_t hal_input_init(hal_input_cb_t cb);   // 回呼於普通 task 上下文,含去抖

// —— 可選增強(有硬體才實作;共用碼視為加速捷徑,缺了不影響任何流程)——
// 數值畫面 ±1:IMU 傾斜、方向鍵、滾輪
typedef void (*hal_input_incdec_cb_t)(int8_t delta);
void hal_input_set_incdec_cb(hal_input_incdec_cb_t cb);   // 不支援可不呼叫
int  hal_input_get_number(void);   // 鍵盤直輸數字;-1=不支援(預設)
```

**手勢由共用層 `app/input_gesture.c` 統一合成**(單一實作,四板一致):
`SHORT`(<600ms)/ `LONG`(≥600ms 觸發一次)/ `REPEAT`(長按後每 120ms;
連發 2s 後步進 ×10)。**選單型畫面一律忽略 REPEAT**(防連跳誤選);REPEAT 僅用於數值畫面。

**映射規範**(各裝置指南遵循):

| 裝置 | KEY_OK | KEY_NEXT | 備註 |
|---|---|---|---|
| zuowei-c3 | 左鍵 GPIO8 | 右鍵 GPIO7 | 中鍵保留「長按關機」,關機前必呼叫 §2.5 hook |
| xingzhi-cube | 上鍵 GPIO39 | 下鍵 GPIO0 | 中鍵可作 incdec(-1)加分 |
| Cardputer-Adv | Enter | `.` | 方向鍵→incdec;數字鍵→get_number(加分) |
| StickS3 | KEY1 G11 | KEY2 G12 | IMU 傾斜→incdec(加分);**左側電源鍵須啟動時停用單擊復位(§0)** |

### 2.2 hal_display —— 語義級繪圖

共用 UI 碼**不直接畫像素**,只呼叫語義 API;各裝置以自家繪圖棧實作,自行決定橫直向。
**StickS3 必須軟體旋轉為 240×135 橫向渲染**(esp_lcd 支援,offset (52,40) 已知),
與 Cardputer 共用同一組佈局參數 —— 全產品實際只有兩種邏輯佈局:240×240 與 240×135。

```c
typedef struct {                  // 一般畫面的宣告式描述
    const char *title;            // 標題列(含狀態指示)
    uint8_t     title_color;      // TC_NORMAL/TC_ALERT/TC_TURN/TC_WIN
    uint8_t     title_flags;      // bit0 電池紅 bit1 斷線 bit2 Master≠Dealer(低電豁免) bit3 submitting
    uint8_t     battery_pct;      // 0–100;0xFF=未知。HAL 必須於標題列右側常駐渲染
                                  //   (如 "83%";0xFF 顯示 "USB";充電中加 ⚡/*)。app_flow
                                  //   每 10s 自 hal_battery 刷新;讀值與 Wi-Fi 衝突的裝置
                                  //   (xingzhi ADC2)顯示最近有效快取值
    const char *lines[6];         // 內文至多 6 行(NULL 結尾)
    const char *big;              // 大字區(數字、動作名),NULL=無
    hal_card_t  cards[7];         // 牌面區:自己 2 張 + 公共 5 張;0xFF=空位
    uint8_t     cards_face_up;    // bitmask:哪些牌翻開
    const char *hint_ok;          // 底部提示:OK 鍵目前意義(NULL=無)
    const char *hint_next;        // NEXT 鍵目前意義
    uint8_t     progress;         // 0–100 進度條;0xFF=不顯示
} hal_screen_t;

typedef struct {                  // 攤牌專用畫面(逐位翻牌,見 §4.3)
    const char *title;
    uint8_t     seat;             // 本頁攤牌者
    const char *name;             // 玩家名(或 "P3")
    hal_card_t  hole[2];          // 該玩家底牌(翻開)
    const char *rank_text;        // "FULL HOUSE"
    uint16_t    win_amount;       // 0=未贏;>0 顯示 "+N"
    bool        is_me;
} hal_showdown_page_t;

esp_err_t hal_display_init(void);
void      hal_display_render(const hal_screen_t *s);           // 全畫面重繪(≤10fps)
void      hal_display_render_showdown(const hal_showdown_page_t *p);
void      hal_display_set_brightness(uint8_t pct);             // 10–100(LEDC PWM;
                                  // zuowei GPIO5 / xingzhi GPIO13 / Cardputer G38(注意
                                  // 兼 RGB LED 電源,PWM 頻率 256Hz、佔空比勿低於 10%)/ StickS3 G38)

### 2.2a 視覺規格 v1.3(真機首測後使用者裁定;渲染實作下沉共用模組)

**架構修訂**:四機皆用 LVGL → 渲染從 per-device hal_display 下沉為共用
`poker_core/src/ui_lvgl.c`;per-device 只留:面板 bring-up(esp_lcd + lvgl_port 掛接)、
亮度控制、`ui_lvgl_attach(lv_display, 佈局尺寸)`。兩種佈局尺寸:240×240 與 240×135。

**仿真紙牌(所有畫面統一)**:
- 牌面 = 白底圓角矩形 + 黑邊;左上角點數(`A 2–9 10 J Q K`)+ 花色圖形置中。
- 花色為**圖形**(非字母):♠♣ 黑、♦♥ 紅(嵌入式小圖或 LVGL 繪製,禁止用 c/d/h/s 字母充數)。
- 蓋牌 = 深藍底 + 斜紋/格紋花樣背面。
- 尺寸兩檔:大牌(手牌)與小牌(公共牌),240×135 上等比縮小。

**遊戲中畫面(HAND 內所有狀態)固定五區版式,自上而下**:
```
┌──────────────────────────────┐
│ <狀態列:階段/輪到誰>   <電量%> │  ← 電量固定螢幕最右上角
│   [♠A][♥K][▒▒][▒▒][▒▒]        │  ← 5 張公共牌(未揭示=牌背)
│   POT: 23                     │  ← 桌面總籌碼(池底+在途注)
│   MY CHIPS: 15                │  ← 我的籌碼
│   [♦Q]  [♣J]                  │  ← 我的 2 張手牌(大牌)
└──────────────────────────────┘
```
- MY_TURN 時行動選單疊加於版式之上(半透明條/底部列,不得遮擋手牌)。
- 其他狀態(LOBBY/SEATING/CHIPS…)沿用五區骨架的「狀態列+電量」規範,
  內容區清晰大字,**每個畫面必須讓使用者一眼知道「現在等誰做什麼」**
  (畫面頂部一行動作指引,如 "PRESS OK TO CLAIM SEAT")。
```

**小面板(240×135)顯示優先序**(實作不得自行裁量):
必顯 = title、cards(自己 2 張)、big、hint;可省 = lines 第 4–6 行、進度條;
公共 5 張以半高小牌顯示。**任何面板禁止一行擠 >2 名玩家資訊**;
10 人總覽用標準縮寫、兩人一行(§4.4)。
牌面:`rank` 字元 `2–9 T J Q K A` + 花色(♣♦♥♠ 或 c/d/h/s),彩色面板紅色花色必須紅顯;
蓋牌畫「▒」框。

### 2.3 hal_audio —— PCM 輸出 + 排程(解碼在共用層,拉取式)

```c
// 共用層提供的拉取式解碼器(core/voice_adpcm.c):
//   voice_open(id) → handle;voice_read(h, pcm_buf, n) → 實際樣本數(0=結束)
// HAL 只負責:把 16kHz mono int16 PCM 送進自家喇叭路徑,並在指定時刻起播。

esp_err_t hal_audio_init(void);
void hal_audio_play(voice_id_t id);                    // 入佇列儘快播(串接不打斷)
void hal_audio_play_at(voice_id_t id, uint32_t local_ms);  // 排程齊播:到時刻切入樣本流
void hal_audio_stop(void);
void hal_audio_set_volume(uint8_t pct);                // 0–100(StickS3 電池供電鉗 ≤75)
uint16_t hal_audio_path_latency_ms(void);              // 本裝置「寫入→出聲」固定延遲(實測校準)
```

**齊播實作規範(硬性)**:
1. 輸出流**常開、以靜音填充**;`play_at` 只是「切入樣本流的時刻」——
   **禁止到時刻才臨時 start 輸出通道**(啟動延遲不可控)。
   (xingzhi 例外:I2S TX 空轉會嘟嘟聲的板子,可停通道但須實測 start 延遲並計入 latency。)
2. 排程時刻 = `pbus_local_time_for(play_at) - hal_audio_path_latency_ms()`。
   `AUDIO_PATH_LATENCY_MS` 由各裝置指南**實機量測**後定為常數
   (C3 的 UART→VB6824 路徑延遲未知且最可疑,其指南必須含量測步驟;
   若實測抖動 >±20ms,C3 齊播明文降級為「盡量對齊」並記入已知限制)。
3. 驗收含「兩台異型裝置齊播實測錯位 <30ms」(§9)。

### 2.4 hal_battery

```c
uint8_t hal_battery_pct(void);   // 0–100;0xFF=未知(StickS3 插 USB 時必回 0xFF,勿回 0)
bool    hal_battery_charging(void);  // 不可知時回 false
```

### 2.5 hal_power —— 關機攔截

```c
// 使用者觸發關機(如 zuowei 中鍵長按)前,板級碼必須先呼叫:
void app_prepare_poweroff(void);
// 共用碼在其中:若本機為 Master → 觸發協定 §9.3 優雅交接(最多等 2s)→ 返回後才允許斷電。
```

無軟體關機的裝置(xingzhi 硬體電源鍵、StickS3 PM1 雙擊)無法攔截 —— 協定
故障接管(§9.5)兜底;已知限制。

### 2.6 hal_misc

```c
uint32_t hal_rand(void);         // esp_random() 包裝(Master 洗牌熵源)
void     hal_yield_watchdog(void);
```

## 3. 共用遊戲核心(core/)

### 3.1 模組與職責

| 模組 | 職責 | 備註 |
|---|---|---|
| `pbus.[ch]` | 協定層(CORE_PROTOCOL §14 介面) | 事件按序交付 |
| `game_state.[ch]` | 全員一致狀態機:吃事件 → 更新 `pn_table_state_t` 鏡像 | 純函式,無 I/O;直接採事件內權威數值 |
| `master_engine.[ch]` | 僅本機為 Master 時啟用:驗證 CMD、推進牌局、洗牌、發事件、代打計時、結算/側池 | `on_role(true)` 掛上 |
| `hand_eval.[ch]` | 7 選 5 手牌評估器(§3.3) | 純函式,全裝置結果一致 |
| `voice_adpcm.[ch]` | IMA-ADPCM **拉取式串流解碼**(voice_open/voice_read)+ 資產索引 | 4KB 串流緩衝,禁整段解碼 |
| `app_flow.[ch]` | 畫面狀態機(§4)+ 恢復入口(§4.5)+ 播報排程(§6) | 只碰 HAL 與 core 介面 |
| `input_gesture.[ch]` | 原始鍵 → SHORT/LONG/REPEAT 合成(§2.1) | 共用單一實作 |

### 3.2 game_state 對 app 的介面(第二層深模組)

```c
typedef struct {
    const pn_table_state_t *st;      // 權威狀態鏡像
    uint8_t  my_player_id, my_seat;
    bool     my_turn;
    uint16_t call_amt, min_raise_to, max_raise_to;  // 來自最近的 E_ACTION_REQ
    bool     can_check;
    uint8_t  revealed_streets;       // UI 已可翻示到哪條街
    // —— 命令回饋(v1.1)——
    uint8_t  cmd_inflight;           // 1=有命令等待 ACK(UI 顯示 submitting)
    uint8_t  last_reject_reason;     // pn_reject_reason_t;0=無。讀取即清零(一次性消費)
} game_view_t;

const game_view_t *game_view(void);
void game_submit_action(uint8_t action, uint16_t raise_to);
void game_submit_chips(uint16_t amount);          // ≤ CHIPS_MAX(9999),master 端同步驗證
void game_submit_blinds(uint8_t sb, uint8_t bb, uint16_t cap);
void game_submit_dealer_claim(void);
void game_submit_seat_claim(uint8_t seat_no);
void game_submit_join_decide(uint8_t cand_id, bool allow);
void game_submit_ready_next(void);
void game_submit_leave(void);
void game_submit_ceremony_skip(void);
```

**命令回饋規範**:送出 → `cmd_inflight=1`(title_flags.submitting,忽略新輸入);
`CMD_ACK(OK)` → 清 inflight,等事件推進畫面;`REJECT/STALE` → 清 inflight、
設 `last_reject_reason`,**留在原畫面**:title 閃紅 1s + 顯示原因短語 + `V_BEEP`。
可輸入畫面(BLINDS/AMOUNT_PICK)必須**本地預驗證**(規則同協定 §8.5/§11.3),
使拒絕只剩競態一種來源。原因短語表:`NOT_YOUR_TURN`="Not your turn"、
`BAD_AMOUNT`="Invalid amount"、`CAP_EXCEEDED`="Over the cap"、`RAISE_LIMIT`="Raise limit"、
`NOT_DEALER`="Dealer only"、`SEAT_TAKEN`="Seat taken"、`TABLE_FULL`="Table full"、
`BAD_CONFIG`="Invalid blinds"、`WRONG_PHASE`="Not now"。

### 3.3 手牌評估器(hand_eval)

- 輸入 7 張(2 底 + 5 公);輸出 `(rank_cat, tiebreak[5])` 直接比大小。
- `rank_cat`:9=皇家同花順 … 0=高牌(同協定 §11.4)。
- 實作規範:21 種 C(7,5) 組合逐一評 5 張取最大 —— 無查表記憶體負擔
  (C3/Cardputer 無 PSRAM);單次 <1ms@160MHz,每局 ≤10 次,性能無虞。

```
eval7(cards[7]) -> best:
  best = (cat=-1)
  for each 5-combination c of cards:
      r = eval5(c)                # 排序後判:同花?順子?計數直方圖…
      if r > best: best = r
  return best
# eval5 比較鍵:(cat, k1..k5) 字典序;A2345 順子高牌記 5
```

- Master 用它決勝負;非 Master 裝置也各自計算全桌結果交叉檢查,
  不符 → 記 log、仍以 `E_HAND_RESULT` 為準。

### 3.4 Master 決策引擎要點

- 行動合法性驗證以協定 §11.3 為準;`C_SET_CHIPS` 追加驗證 `1 ≤ amount ≤ 9999`(CHIPS_MAX,
  超出回 `BAD_AMOUNT`;語音可播上限)。
- 回合終結:未棄未 all-in 者 `bet_round == cur_bet` 且都行動過 → 收池進下一街;
  只剩一人未棄 → 直接結算;全員 all-in → runout:連續發 `E_STREET(first_seat=0xFF)`,
  **相鄰街道 `play_at` 間隔 ≥3500ms**(容納翻牌顯示+語音)。
- 洗牌:52 張 Fisher–Yates,熵源 `hal_rand()`。

## 4. 畫面狀態機(app_flow)

畫面 = 狀態;協定事件與手勢是遷移邊。一般畫面由 `hal_screen_t` 渲染,
攤牌由 `hal_showdown_page_t` 渲染。

### 4.1 全域遷移總則(優先於附錄 A 的逐畫面表)

1. **HAND 內任何畫面**(HAND_DEALT/MY_TURN/AMOUNT_PICK/WAIT_TURN/SPECTATE)收到
   `E_STREET`→更新翻示(必要時回 WAIT_TURN);`E_HAND_RESULT`→RESULT;
   `E_HAND_ABORT`→ABORT 提示(3s)→INTERMISSION;`E_GAME_PAUSE`→PAUSED。**無條件遷移。**
2. **任何畫面**收到 `TABLE_DISSOLVE`/`on_link(DISSOLVED)` → "Merging tables…" 提示 1s → SCANNING;
   `on_link(LINK_LOST)` → 疊加斷線橫幅(title_flags.bit1),自動重連,不切畫面。
3. **恢復入口**:`on_link(RESYNCED)`(快照套用)或 ACCEPT_BACK 或排座重跑完成 →
   呼叫 `screen_from_state()`(§4.5)直接落點,不走線性流程。
4. 送出命令的畫面收到 `REJECT/STALE` → 自環(留原畫面顯示原因,§3.2)。
5. **系統選單**(§5):`UI_MENU` 於**任何畫面**皆開啟系統選單(全域最先處理)。
   `UI_BACK` 為情境化返回:BLINDS=回上一欄、AMOUNT_PICK=取消回 MY_TURN、其餘畫面=開系統選單(逃生)。

### 4.2 主流程

```
BOOT → SCANNING →(JOIN_ACK.ACCEPT / 自建桌)→ LOBBY →(E_ROSTER)→ DEALER_CALL
 →(E_DEALER_SET)→ SEATING →(E_SEATING_DONE)→ CHIPS_SET*(僅 chips==0 且未淘汰者)
 → BLINDS(莊家)/WAIT(他人)→(E_TABLE_CONFIG)→ WAIT →(E_HAND_START)→ HAND 循環:
   HAND_DEALT → {WAIT_TURN | MY_TURN → AMOUNT_PICK?}×各街 → RESULT(逐位翻牌)
   → INTERMISSION → (E_HAND_START) 回 HAND_DEALT
SCANNING 另有出口:JOIN_ACK.PENDING→PENDING_JOIN;ACCEPT_BACK→screen_from_state();
                  REJECT_FULL/REJECT_VERSION→提示 2s 後續掃描
```

### 4.3 RESULT = 逐位翻牌子畫面(v1.1 重定義)

RESULT 不用通用結構,改為**與序列齊播同步的分頁播報**:

- slot k(`play_at + k × slot_gap_ms`):全屏渲染 `E_HAND_RESULT.show[k]` ——
  座位/玩家名、2 張底牌(翻開)、牌型名大字、`payout` 中該座位的分得額(">+N",側池自然呈現)、
  贏家頁 title=TC_WIN。本機頁面加 "YOU" 標記。**畫面翻頁與語音播報同一時刻、同一順序**,
  10 人攤牌也永遠一次只顯示一人,240×135 放得下。
- `reason=1`(全棄)：無 show 頁,直接顯示贏家頁(不揭牌,只顯示 "+N")。
- 全部 slot 播完 → 尾頁(SUMMARY):我的新籌碼大字 + 按鈕移動指示("DEALER → P4")
  + 本機播報籌碼(§6.3);停 3s → INTERMISSION。
- 已棄牌者不在 show[] 中,不揭牌(標準規則)。

### 4.4 WAIT_TURN 總覽壓縮格式(全裝置一致)

- 標準縮寫:`P<seat>:<chips><標記>`,標記:`✓`=已行動 `*`=思考中 `F`=棄 `A`=all-in
  `X`=淘汰 `?`=離線。**兩人一行**,>6 人只顯示「池底 + 思考中者 + 我的狀態 + 已棄/全下計數」
  (`lines[6]` 上限內的確定性降級,不分頁 —— NEXT 保留給 peek 以外用途的畫面不存在,
  但分頁會破壞 2 鍵心智,故用降級)。
- 常駐:pot、目前街道、自己底牌(小圖)。

### 4.5 screen_from_state() —— 恢復落點表(快照/回歸/重排座後)

| 條件(依序判定) | 落點 |
|---|---|
| `phase==LOBBY` | LOBBY |
| `phase==DEALER_CALL` | DEALER_CALL |
| `phase==SEATING` 且我無座 | SEATING(等我的 seat_no 提示) |
| `phase==SEATING` 且我有座 | WAIT |
| `phase==CHIPS` 且我 chips==0 且未淘汰 | CHIPS_SET |
| `phase==CHIPS` 其他 | WAIT |
| `phase==BLINDS` 且我=莊家 | BLINDS |
| `phase==BLINDS` 其他 | WAIT |
| `phase==HAND` 且我淘汰/已棄/未發牌 | SPECTATE |
| `phase==HAND` 且 `to_act_seat==my_seat` | MY_TURN |
| `phase==HAND` 其他 | WAIT_TURN |
| `phase==INTERMISSION` 且我=莊家 | INTERMISSION(莊家) |
| `phase==INTERMISSION` 且我淘汰 | REBUY |
| `phase==INTERMISSION` 其他 | INTERMISSION(觀望) |
| `phase==PAUSED` | PAUSED |

`on_link(RESYNCED)` 一律強制呼叫本表;這也是附錄 A 完備性的交叉驗證基準。

## 5. 抽象輸入意圖互動模型(全產品統一心智)

**唯一心智:UP/DOWN 換、OK 定、BACK 返回、MENU 選單。**

共用碼只認識 5 種與硬體無關的抽象意圖 `UI_OK / UI_UP / UI_DOWN / UI_BACK / UI_MENU`;
各裝置 `hal_input.c` 自行把物理按鍵(2 鍵點/雙/長、3 鍵長按、全鍵盤)合成這 5 種意圖,
並以 `hal_input_hint()` 回報每種意圖在本機的觸發標籤(顯示於畫面提示列)。
> 歷史:v2.4 之前本節以單一「LONG_OK」手勢兼任「選單/返回」;之後拆為獨立的
> `UI_MENU`(永遠開系統選單)與 `UI_BACK`(情境化返回),不再綁定「長按 OK」這一特定物理手勢。
> 各裝置目前實際手勢見各 `devices/<board>/README.md` 與 `hal_input.c`(例:zuowei 長按中鍵=關機,
> 選單在左鍵長按;StickS3 選單在長按 OK)。

1. **選單/清單**:UI_UP/UI_DOWN 移動高亮(到尾繞回),UI_OK 確定;選項 ≤5/畫面;**忽略 REPEAT**。
2. **數值輸入**(籌碼、SB/BB/CAP、加注額):UI_UP +1、UI_DOWN −1(REPEAT 連發,2s 後 ×10 步進);
   到上限繞回下限;UI_OK 確認;UI_BACK 返回/取消。初值 = 情境預設
   (籌碼 15、raise=min_raise_to)。範圍:籌碼 1–9999(CHIPS_MAX)。
   增強:incdec(IMU/方向鍵)=±1;鍵盤 get_number 直輸。
3. **行動選單**(固定順序):`Check/Call → Bet/Raise → Fold → All-in`
   (不合法項不出現);游標**預設落在 Check/Call**;Fold 紅色。
4. **破壞性二確認**:選 Fold 或 All-in 按 UI_OK 後,title 變紅
   `"FOLD? OK=YES / BACK=NO"`(All-in 同理)—— UI_OK 確認送出,UI_BACK 取消回選單。
   Check/Call/Bet/Raise 單擊即行(高頻低險)。
5. **防誤觸**:MY_TURN 進場 150ms 忽略按鍵;選單忽略 REPEAT(§5.1)。
6. **系統選單**(UI_MENU,全畫面可達):
   `[RESUME] [BUY-IN](僅淘汰者,局間) [VOLUME] [BRIGHTNESS] [LEAVE TABLE]`。
   VOLUME:UI_OK 循環 10/40/70/100%(即試播 V_BEEP);
   BRIGHTNESS:UI_OK 循環 30/60/100%(即時生效);
   LEAVE TABLE:二確認 `"Leave? OK=YES / BACK=NO"` → `game_submit_leave()` → SCANNING。
   **任何單一手勢都不得直接離桌**(v1.0 的 LOBBY/PAUSED 直接離桌廢除)。
   **持久化**:音量與亮度變更即寫 NVS(namespace `poker`,key `vol`/`bri`),
   開機載入(板級預設,見各 board_config.h);選單邏輯在共用碼,四機自動獲得。
7. BLINDS 多欄:UI_OK=下一欄(末欄=送出),UI_UP/UI_DOWN=±1,UI_BACK=上一欄(**首欄 UI_BACK=開系統選單**)。
8. DEALER_CALL 逃生口 = 臨時 Master 自己按 OK 認領(協定 §8.3.3 的「產品層選項」即此;
   15s 超時重播由協定 E_REMIND 驅動,無需第三鍵)。

## 6. 語音系統(英文)

### 6.1 資產格式與管線

- **16 kHz mono,IMA-ADPCM(4-bit,≈8 KB/s)**,單一 `voice.bin`(索引 + 資料),
  隨韌體燒錄。**四機共用同一份 voice.bin**(位元組一致)。
- 片段實數 ≈ 75 條,總長 ≈ 90s ≈ **720 KB**(長句如排座提示 ~3s 已計;
  以實際生成回填精確值)。四機 flash 皆可容納(app 分區 ≥3.5MB)。
- 管線:文字表 → TTS(單一音色)→ 16k mono wav → `tools/wav2adpcm.py` →
  `voice.bin` + 自動生成 `voice_ids.h`。
- 播放:`voice_adpcm.c` 提供**拉取式串流解碼**(`voice_open(id)` / `voice_read(h,buf,n)`,
  4KB 緩衝,禁整段解碼);HAL 到排程時刻逐塊拉 PCM 餵喇叭(§2.3)。

### 6.2 片段清單(voice_id_t;文字即錄音內容)

**流程**:`V_CONFIRM_DEALER`="Please confirm the dealer" / `V_SEAT_PROMPT`="Player on the dealer's left, press your button" / `V_NEXT_SEAT`="Next player, press your button" / `V_SET_CHIPS`="Please set your chips" / `V_GAME_START`="Game starting" / `V_DEAL`="Dealing hole cards" / `V_PREFLOP`="Pre-flop betting" / `V_FLOP`="Flop" / `V_TURN`="Turn" / `V_RIVER`="River" / `V_SHOWDOWN`="Showdown" / `V_YOUR_TURN`="Your turn" / `V_NEW_DEALER`="New dealer" / `V_HAND_ABORT`="Hand cancelled, bets returned"

**動作回饋(本機)**:`V_CHECK`="Check" `V_CALL`="Call" `V_BET`="Bet" `V_RAISE`="Raise" `V_FOLD`="Fold" `V_ALLIN`="All in"

**結果**:`V_RANK_0..9`="High card"/"One pair"/"Two pair"/"Three of a kind"/"Straight"/"Flush"/"Full house"/"Four of a kind"/"Straight flush"/"Royal flush" / `V_I_WON`="I won" / `V_SPLIT`="Split pot"

**籌碼播報**:`V_YOU_HAVE`="You have" + 數字 + `V_CHIPS`="chips"

**數字合成(0–9999,規則完備)**:片段 `V_N0..V_N20`、`V_N30/40/…/90`、`V_HUNDRED`、`V_THOUSAND`。
組合(跳過為零的位段):
`N<20`→直讀;`21–99`→十位+個位(整十只讀十位);
`100–999`→百位數字+HUNDRED(+餘數規則遞迴,餘 0 不讀);
`1000–9999`→千位數字+THOUSAND(+餘數規則遞迴,餘 0 不讀)。
例:`15`=fifteen;`100`=one hundred;`347`=three hundred forty seven;
`1050`=one thousand fifty;`9999`=nine thousand nine hundred ninety nine。
籌碼域全域鉗制 ≤9999(CHIPS_MAX),不存在不可播數值。

**系統**:`V_PLAYER_JOINED`="A player joined" / `V_PLAYER_LEFT`="A player left" /
`V_PAUSED`="Game paused" / `V_RESUMED`="Game resumed" / `V_LOW_BATT`="Low battery" /
`V_CONN_LOST`="Connection lost" / `V_NOT_ENOUGH`="Not enough players" /
`V_WAIT_APPROVE`="Waiting for dealer approval" / `V_ELIMINATED`="You are out" / `V_BEEP`(提示音)

### 6.3 齊播 vs 本機播報(對應協定 §7)

| 類型 | 例 | 機制 |
|---|---|---|
| **齊播** | V_CONFIRM_DEALER、V_SEAT_PROMPT/V_NEXT_SEAT(每個 E_SEAT_PROMPT)、V_DEAL、V_PREFLOP、V_FLOP/TURN/RIVER/SHOWDOWN | 事件 `play_at` → `hal_audio_play_at()`(扣路徑延遲) |
| **序列齊播** | 攤牌牌型 + V_I_WON/V_SPLIT(E_HAND_RESULT) | slot k = **自己在 `show[]` 中的索引**(0 起);`play_at + k×slot_gap_ms` |
| **錯時本機** | 結算籌碼播報 | RESULT 尾頁起,依**座位序 × 1500ms** 錯開,各台播自己的 "You have N chips"(避免 10 台同時報數互相蓋台) |
| **本機即播** | 動作回饋、Your turn、系統提示、自己的 E_CHIPS_SET | 事件到即 `hal_audio_play()` |

**排程常數(全裝置一致,Master 產品層責任)**:
`GAP_DEAL_PREFLOP = 2000ms`(E_HAND_START 的 play_at 播 V_DEAL,+2000ms 播 V_PREFLOP);
runout 相鄰 E_STREET 的 play_at 間隔 ≥3500ms(§3.4);
`slot_gap_ms` 預設 2500(容最長牌型片段 ~1.2s + 翻頁閱讀時間);
**多贏家**:單一贏家播 V_I_WON;≥2 贏家各自在「贏家子 slot = 其在 show[] 中 is_winner 的序 × 1200ms」
播 `V_SPLIT`(取代 V_I_WON)。
**V_NEW_DEALER**:每局 `E_HAND_START.button_seat` 變更時,新莊家裝置本機播(其餘裝置僅顯示)。

## 7. 邊界情境 UX(對應協定容錯)

| 情境 | 畫面/語音行為 |
|---|---|
| 本機斷線(LINK_LOST) | title 紅 "RECONNECTING…"(疊加橫幅,不切畫面);重連成功 V_BEEP |
| Master 接管中 | 橫幅 "NEW DEALER TAKING OVER…";`E_HAND_ABORT` → ABORT 畫面 3s(V_HAND_ABORT + 顯示退注額)→ INTERMISSION |
| 兩桌合併(TABLE_DISSOLVE) | "Merging tables…" 1s → SCANNING(自動重加入勝方桌) |
| 局中有人離線 | WAIT_TURN 該座標記 `?`;15s 後 Master 代打(協定行為) |
| 中途想加入 | PENDING_JOIN "Waiting for dealer approval" + V_WAIT_APPROVE;UI_MENU=系統選單(取消) |
| 淘汰 | RESULT 後 → REBUY(局間)或 SPECTATE(局中);V_ELIMINATED 本機播一次 |
| 電量 <15% | title 電池紅;<8% 本機 V_LOW_BATT(每 10 分鐘至多一次);若因此觸發協定低電豁免(Dealer≠Master),title_flags.bit2 亮 |
| 座位被系統指派(E_SEAT_SET.auto=1) | 該裝置閃提示 "SEAT AUTO-ASSIGNED",玩家可線下協調後由莊家重跑排座 |
| 遊戲暫停(<2 人) | PAUSED 全屏 + V_PAUSED;恢復 V_RESUMED |
| 輪到我 30s 未動(協定 E_REMIND) | 本機重播 V_YOUR_TURN |
| 觀戰者(非玩家旁觀) | **非本版目標**(goal 未要求;淘汰/已棄者的 SPECTATE 即桌面旁觀視角) |

## 8. 非功能需求

### 8.1 整機 SRAM 預算(以最緊的 zuowei-c3(400KB,無 PSRAM)立帳)

| 項目 | 預算 | 備註 |
|---|---|---|
| Wi-Fi/LWIP/ESP-NOW 協定棧 | ~55 KB | IDF 預設(已含 buffer;不連 AP 可微降) |
| FreeRTOS + 系統 + 主棧 | ~30 KB | 主棧 8KB(無 opus,不需 12KB)+ 協定 task 4KB + 音訊 task 4KB |
| LVGL(partial buffer) | ~24 KB | 240×40×2bytes 雙緩衝 ≈19KB + 物件池;**禁全屏 framebuffer** |
| pbus:事件日誌 32×~210B | ~7 KB | 定長槽 210B(容 E_ROSTER 201B);**不得按 100B 截斷** |
| pbus:狀態+快照+亂序緩衝 | ~3 KB | 167B×2 + 8×250B |
| 語音:串流解碼緩衝 | 4 KB | 拉取式;禁整段解碼(3s=96KB) |
| core+app 邏輯(靜態+堆) | ≤60 KB | 共用碼子預算(原 v1.0 的 60KB 定位於此) |
| 餘裕 | ≥100 KB | 碎片與峰值 |

Cardputer(S3FN8 512KB,無 PSRAM)較 C3 寬鬆;兩台 PSRAM 機無壓力。

### 8.2 其他

| 項目 | 要求 |
|---|---|
| 主迴圈 | UI 重繪 ≤10 fps;輸入→畫面反饋 <100ms;輸入→CMD 送出 <50ms |
| 齊播精度 | 穩態 ±10ms(協定)+ 各機音訊路徑延遲校準(§2.3);**跨異型裝置實測錯位 <30ms**;C3 若實測不可達,明文降級並記錄 |
| 音量 | 預設板級(見 board_config.h);StickS3 電池供電鉗 ≤75(硬體);系統選單可調 10/40/70/100(NVS 持久化) |
| 亮度 | 預設 80%;系統選單可調 30/60/100(NVS 持久化) |
| 電量顯示 | 全部畫面標題列右側常駐(10s 刷新;0xFF→"USB") |
| 啟動 | 上電 → SCANNING ≤3s |
| 韌體體積 | app+voice ≤ 2.5 MB(最小分區 zuowei ota 槽 3.5MB) |

## 9. 驗收清單(對照 goal 20 步)

1. 3 台混型裝置(至少 1 台 C3 + 1 台 S3)10s 窗口成桌;LOBBY 成員數一致
2. 齊播 "Please confirm the dealer";搶按產生唯一莊家;**兩台異型裝置齊播錯位 <30ms(實測)**
3. 逐位排座,順序與物理座位一致;卡住時莊家可跳過(auto 指派有提示)
4. 各自設籌碼(預設 15、範圍 1–9999),每台本機播報自己的數量
5. 莊家設 SB/BB/CAP(預設 1/2/10);非法組合**本地預驗證擋下 + 競態時 REJECT 有紅字與蜂鳴回饋**
6. 發牌:自己 2 張(MY_TURN 常駐可見),公共 5 張蓋牌;齊播 "Dealing hole cards"
7. 四條街下注完整;行動選單只出現合法項;Fold/All-in 需二確認;raise-to 正確
8. 全棄 → 贏家裝置播 "I won";攤牌 → 逐位翻牌畫面與語音同步、多贏家播 "Split pot"、各台錯時播新籌碼
9. 換莊:按鈕左移、新莊家裝置接手(握手交接);新莊家本機播 "New dealer"
10. **拔電池測試**:局中直接關掉莊家裝置 → **≤3s 偵測、≤6s 完成接管**(NEW DEALER 橫幅)、本局作廢退注(ABORT 畫面)、左手玩家成新莊家續局
11. 中途關機再開 → ACCEPT_BACK + 快照 → `screen_from_state()` 正確落點、原籌碼回歸
12. 局間新裝置 HELLO → PENDING 畫面 → 莊家批准 → 重排座 → 新玩家(chips==0)自動進 CHIPS_SET → 入局
13. 任一玩家 AFK 不卡死任何儀式(超時/跳過路徑全可達);任意時刻可經系統選單退出
14. 淘汰者看到 SPECTATE/REBUY;補籌後下一局回到牌局
15. 10 人滿桌 + 多層 all-in:RESULT 逐位翻牌含側池分得額,總覽降級顯示不溢出
16. 每台裝置:標題列常駐電量百分比;系統選單可調音量與亮度且重開機後保留(NVS)

## 附錄 A:畫面 × 事件全矩陣

**總則(§4.1)適用於全表;此處只列各畫面特有遷移。凡未列出的事件:狀態照常更新、畫面不切。**

> 欄位為抽象意圖:`OK`=UI_OK、`UP/DOWN`=UI_UP/UI_DOWN、`BACK`=UI_BACK。
> 另有全域 `UI_MENU`:於**任何**畫面直接開系統選單(不列於下表,因非畫面特有)。
> 下表 BACK 欄標「系統選單」處,即該畫面 UI_BACK 的逃生語義 = 開系統選單。

| 畫面 | 進入條件 | OK | UP/DOWN | BACK | 特有離開遷移 |
|---|---|---|---|---|---|
| BOOT | 上電 | – | – | – | HAL 就緒→SCANNING |
| SCANNING | – | – | – | 系統選單(僅 VOLUME) | ACCEPT→LOBBY;PENDING→PENDING_JOIN;ACCEPT_BACK→§4.5;REJECT_FULL/VERSION→提示 2s 續掃 |
| LOBBY | 入桌 | – | – | 系統選單 | E_ROSTER→DEALER_CALL;人數不足解散/TABLE_DISSOLVE→SCANNING |
| DEALER_CALL | E_DEALER_CALL | C_DEALER_CLAIM | – | 系統選單 | E_DEALER_SET→SEATING |
| SEATING | E_SEAT_PROMPT | C_SEAT_CLAIM(我無座時) | – | (莊家)二確認後 C_CEREMONY_SKIP;(他人)系統選單 | E_SEATING_DONE→(我 chips==0 且未淘汰)CHIPS_SET;否則 WAIT |
| CHIPS_SET | 見 SEATING 出口 / §4.5 | 確認→C_SET_CHIPS | +1/REPEAT | 系統選單 | ACK 後→WAIT;REJECT→自環 |
| BLINDS | E_BLINDS_PROMPT(我=莊家) | 下一欄/末欄送出 | ±1/REPEAT | 上一欄(首欄=系統選單) | E_TABLE_CONFIG→WAIT;REJECT→自環 |
| WAIT | 他人操作中 | – | – | 系統選單 | E_HAND_START→HAND_DEALT;其餘事件按 §4.1/§4.5 |
| HAND_DEALT | E_HAND_START | peek 底牌(3s) | – | 系統選單 | E_ACTION_REQ(我)→MY_TURN;(他)→WAIT_TURN;§4.1 總則(runout 直達 RESULT 亦覆蓋) |
| MY_TURN | E_ACTION_REQ.seat=我 | 選定(Fold/All-in 進二確認) | 循環動作 | 系統選單 | Bet/Raise→AMOUNT_PICK;送出後→WAIT_TURN;REJECT→自環 |
| AMOUNT_PICK | 自 MY_TURN | C_ACTION 送出 | +1/REPEAT | **返回 MY_TURN** | 送出→WAIT_TURN;REJECT→自環 |
| WAIT_TURN | – | peek 底牌 | – | 系統選單 | E_ACTION_REQ(我)→MY_TURN;§4.1 總則 |
| SPECTATE | 我已棄/淘汰/未發牌(局中) | peek(已棄者) | – | 系統選單(含 BUY-IN 於局間) | §4.1 總則 |
| RESULT | E_HAND_RESULT | – | – | – | slot 分頁自動推進→SUMMARY 尾頁→INTERMISSION |
| ABORT | E_HAND_ABORT | – | – | – | 3s→INTERMISSION |
| INTERMISSION(莊家) | 結算後,我=莊家 | 選單確認 | 選單循環:[START NEXT HAND] [Px: APPROVE] [Px: DENY]… | 系統選單 | E_SEAT_PROMPT(批准重排座)→SEATING;E_HAND_START→HAND_DEALT |
| INTERMISSION(他人) | 結算後 | – | – | 系統選單 | E_SEAT_PROMPT→SEATING;E_HAND_START→HAND_DEALT |
| REBUY | 局間且我淘汰 | 確認→C_SET_CHIPS | +1/REPEAT | 系統選單 | ACK→INTERMISSION(他人);E_HAND_START→SPECTATE(未買入) |
| PENDING_JOIN | JOIN_ACK(PENDING) | – | – | 系統選單(取消→SCANNING) | E_JOIN_DECIDED(允)→SEATING;(否)→SCANNING |
| PAUSED | E_GAME_PAUSE | – | – | 系統選單 | E_GAME_RESUME→§4.5 落點 |
| SYSMENU | UI_MENU(或他畫面 BACK 逃生) | 確認選項 | 循環 | 關閉選單 | RESUME→原畫面;LEAVE 二確認→SCANNING;BUY-IN→REBUY |

矩陣完備性驗證基準:**每個 `pn_phase_t` × 本機角色(莊家/玩家/淘汰/待批)在 §4.5 表中
都有落點,且本表每個畫面對 §4.1 總則事件都可達出口** —— 實作時以此雙向檢查。
