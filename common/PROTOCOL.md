# POKER-NOW 核心協定 v1.1

> 德州撲克遊戲機的 ESP-NOW 通訊核心協定。
> 適用裝置:zuowei-c3-lcd-pwr(ESP32-C3)、xingzhi-cube-1.54tft-wifi(ESP32-S3)、
> Cardputer-Adv(ESP32-S3FN8)、StickS3(ESP32-S3-PICO),以及未來任何具備
> 「電池 + LCD + ≥2 按鍵 + 揚聲器」的 ESP32 系列裝置。
> 本文件只定義**通訊協定與複製模型**;UI / 按鍵互動 / 語音資源屬《產品開發文件》範疇。

**修訂記錄**

| 版本 | 重點 |
|---|---|
| v1.0 | 初稿 |
| v1.1 | 依兩份對抗式審查(Fable / Opus)修訂:引入 **master_epoch** 與 **桌面時鐘**;接管改「日誌最完整者當選+先補齊再編號」;權威狀態加 `bet_hand`(退注/側池地基);統一局間交接時序;建桌合併加 `TABLE_DISSOLVE`;儀式超時與莊家跳過;heads-up/短盲/淘汰規則;限注語義補全;補齊全部事件 packed 結構;明訂 recv 回呼與任務模型;修正齊播提前量與各結構位元組數。 |

---

## 1. 設計目標與原則

1. **深模組(Deep Module)**:協定層對上層遊戲邏輯只暴露一個小介面 ——
   「可靠、有序、全員一致的事件日誌 + 成員/角色管理」。ESP-NOW 的不可靠廣播、
   重傳、去重、時鐘同步、莊家容錯接管等複雜度全部封裝在協定層之內。
   上層遊戲邏輯是一個**純確定性狀態機**:相同的事件序列在任何裝置上必然推導出相同的牌局狀態。
2. **全狀態複製(Full Replication)**:牌局的**每一筆資訊(含所有人的底牌)都廣播給所有裝置**。
   任何一台裝置在任何時刻都持有完整牌局狀態,因此任何裝置都可以接任 Master
   (goal 需求 9:防莊家裝置離線/損壞)。本遊戲為線下娛樂,不要求資料保密(goal 需求 11)。
3. **單一序列器 + 任期(Sequencer + Epoch)**:同一時刻只有一台 **Master** 為事件編派
   全域序號;每一任 Master 持有遞增的 **epoch(任期號)**。事件以 `(epoch, seq)` 全序排列,
   高 epoch 無條件勝過低 epoch —— 這是消解雙 Master / 舊 Master 復活 / seq 重用的根本機制。
   所有玩家輸入(命令)先送 Master 驗證,Master 決議後以已編序事件廣播。
   衝突(例如兩人同時搶「我是莊家」)由序號天然仲裁:先被編序者勝。
4. **心跳 + 缺口修補(NACK-based)**:ESP-NOW 廣播無 MAC ACK。可靠性由
   「事件序號 + Master 心跳週期性通告最新序號 + 接收端主動請求補洞」達成,
   而非逐包 ACK(10 台裝置逐包 ACK 會放大 10 倍空中流量)。
5. **命令冪等不變式**:所有 CMD 的語義必須冪等(set / to-amount 式,禁止增量式)。
   理由:命令去重表不隨 Master 轉移,交接窗口的重送必須天然無害。未來新增命令必須遵守。
6. **硬體無關**:協定不假設按鍵數、螢幕大小、音訊路徑。凡與硬體相關者
   (語音檔、UI)一律以抽象 ID 表達,由各裝置韌體自行對應。
7. **單包完成**:所有訊息(含標頭)≤ 250 bytes(ESP-NOW v1 上限),不做分片。
   本文件 §18 附各結構精確 sizeof 表。

## 2. 術語

| 術語 | 定義 |
|---|---|
| **裝置 (Device)** | 一台實體遊戲機,以其 Wi-Fi STA MAC(6 bytes)唯一識別。 |
| **玩家 (Player)** | 加入牌桌的裝置,持有 `player_id`(加入順序編號,0 起)。 |
| **座位 (Seat)** | 實體座次,於排座儀式一次認領:`seat 0` = **排座當下**的莊家,逆時針遞增。之後每局的莊家由 `button_seat` 指定(左移),seat 編號本身不再移動。 |
| **莊家 (Dealer / Button)** | 撲克規則上的按鈕位。每局結束後左移一位(座位環上遞增,略過淘汰/離線者)。 |
| **Master** | 協定角色:事件序列器,持有現行 epoch。**常態 = 莊家裝置**;低電量豁免(§9.4)或故障接管(§9.5)時允許暫時 Dealer≠Master。 |
| **epoch(任期)** | Master 任期號,交接/接管時 +1。事件全序 = `(epoch, seq)`。 |
| **桌面時鐘 (Table Clock)** | 全桌共享的毫秒時間軸,由第一任 Master 的本機時鐘建立,**跨任期接續**(§7)。 |
| **命令 (CMD)** | 玩家裝置 → Master 的意圖(未生效)。單播,需 Master 回應。 |
| **事件 (EVT)** | Master → 全體的已編序事實(已生效)。廣播,靠補洞機制可靠化。 |
| **牌桌 (Table)** | 一場遊戲會話,以 16-bit 隨機 `table_id`(≠0)識別,隔離同場域多桌。 |
| **一局 (Hand)** | 從發牌到結算的一輪,`hand_no` 遞增(u8 回繞,僅作顯示與相等比較)。 |

## 3. 分層總覽

```
┌──────────────────────────────────────────────┐
│  遊戲邏輯(純狀態機,確定性;Master 端另有決策引擎)│  ← 各裝置韌體實作(邏輯可共用)
├──────────────────────────────────────────────┤
│  協定層介面 pbus_*(§14,小介面)                │  ← 本文件定義
│  ┌────────────────────────────────────────┐  │
│  │ L3 遊戲事件目錄(§10、§11)                │  │
│  │ L2 成員/角色:發現、加入、心跳、容錯(§8、§9)│  │
│  │ L1 可靠層:epoch/seq、補洞、快照、時鐘(§6、§7)│ │
│  │ L0 傳輸:ESP-NOW 廣播+單播(§4)            │  │
│  └────────────────────────────────────────┘  │
├──────────────────────────────────────────────┤
│  esp_wifi / esp_now(ESP-IDF)                 │
└──────────────────────────────────────────────┘
```

## 4. 傳輸層(L0)與執行模型

| 項目 | 規定 |
|---|---|
| Wi-Fi 模式 | `WIFI_MODE_STA`,**不連 AP**。 |
| 信道 | 固定 **channel 1**(所有裝置一致;沿用既有 demo 慣例)。 |
| 省電 | **進入遊戲 App 即設 `WIFI_PS_NONE`**(含發現/掃描期 —— 省電模式下廣播接收極不可靠,掃描期漏聽 ANNOUNCE 會導致誤建衝突桌)。電量換可靠性,為本協定的明確取捨;StickS3 續航限制見 §17。 |
| 廣播 | 目的地 `FF:FF:FF:FF:FF:FF`,需事先 `esp_now_add_peer` 廣播位址。 |
| 單播 | 目的地為成員 MAC。收到 E_ROSTER / E_PLAYER_JOINED 後把成員加入 peer 表(≤10 + 廣播 = 11 peers,低於 ESP-NOW 20 peer 上限)。 |
| 加密 | **不使用** ESP-NOW 加密(加密 peer 上限僅 6~7,且本場景不需保密)。 |
| 包長 | 整包(含協定頭)≤ **250 bytes**。 |
| 速率 | 預設(1Mbps 長前導),不調整 —— 房間內距離短,可靠性優先。 |

**執行模型(實作規範,非建議)**:

1. `esp_now_register_recv_cb` 的回呼執行於 Wi-Fi task —— **回呼內只允許:長度/magic/version 校驗 + `memcpy` 入協定佇列(`xQueueSend(…, 0)` 不阻塞,滿則丟)**。
   禁止在回呼內做去重、CRC、交付上層、或呼叫 `esp_now_send`(參照已驗證的 `intercom.c` 模式)。
2. 協定層所有 L1–L3 處理在**獨立協定 task** 內完成;優先級低於 Wi-Fi、
   且不得高於板上音訊管線(C3 上 VB6824 任務優先級 9 的既有教訓)。
3. 上層回呼(§14)由協定 task 呼叫;上層不得在回呼內長時間阻塞。

## 5. 封包通用格式與編碼慣例

- **位元組序:一律小端**(全部為同族 ESP32 晶片)。
- 所有結構 `__attribute__((packed))`;**未用槽位、未滿字串一律逐位元組清零**
  (權威狀態參與 CRC,垃圾位元組會造成假分歧)。
- 卡牌編碼:`card = rank * 4 + suit`;`rank`:0=2 … 8=T, 9=J, 10=Q, 11=K, 12=A;
  `suit`:0=♣ 1=♦ 2=♥ 3=♠;`0xFF` = 無/未知。
- 籌碼、注額單位:**1 籌碼**,`uint16_t`。
- 電量:0–100;**`0xFF` = 未知**(StickS3 插 USB 時 VBAT 讀 0 的既有陷阱,不得回報 0)。
- 時間戳:**桌面時鐘毫秒**(`uint32_t`,回繞約 49.7 天,單場無虞;§7)。

### 5.1 通用標頭(每包開頭 14 bytes)

```c
#define PN_MAGIC    0x504B          // 線上位元組序:4B 50
#define PN_VERSION  1               // 主版本,不相容則拒絕互通

typedef struct __attribute__((packed)) {
    uint16_t magic;                 // PN_MAGIC
    uint8_t  version;               // PN_VERSION
    uint8_t  type;                  // pn_pkt_type_t(§5.2)
    uint16_t table_id;              // 牌桌識別;發現階段未知時填 0,鎖定目標後必填
    uint16_t epoch;                 // 發送者所知的現行任期(§6.1);發現階段 0
    uint16_t len;                   // 標頭之後的負載長度
    uint32_t table_ms;              // 發送當下的桌面時鐘(Master 的包為權威;成員的包為其換算值)
} pn_hdr_t;                          // 14 bytes
```

發送者身分不放標頭 —— ESP-NOW 回呼直接提供來源 MAC,以 MAC 查成員表。

### 5.2 封包類型總表

| type | 名稱 | 方向 | 傳輸 | 用途 |
|---|---|---|---|---|
| 1 | `HELLO` | 未入桌裝置 → 全體 | 廣播 | 「我想加入」信標(§8.1) |
| 2 | `ANNOUNCE` | Master → 全體 | 廣播 | 牌桌招募/存在通告(§8.1) |
| 3 | `JOIN_ACK` | Master → 新裝置 | 單播 | 接受/暫緩/拒絕加入(§8.2) |
| 4 | `CMD` | 玩家 → Master | 單播 | 玩家意圖(§10) |
| 5 | `CMD_ACK` | Master → 玩家 | 單播 | 命令收妥/拒絕(§6.3) |
| 6 | `EVT` | Master → 全體 | 廣播 | 已編序遊戲事件(§11) |
| 7 | `EVT_RTX` | 持有者 → 請求者 | 單播 | 補發歷史事件(常態由 Master;接管期成員間亦可,§9.5) |
| 8 | `GAP_REQ` | 缺洞者 → 持有者 | 單播 | 請求補洞(§6.2) |
| 9 | `HEARTBEAT` | Master → 全體 | 廣播 | 心跳:最新序號+狀態雜湊+時鐘(§6.4) |
| 10 | `STATUS` | 成員 → Master | 單播 | 成員回報:電量/已收序號/對齊 CRC(§6.4) |
| 11 | `SNAP_REQ` | 成員 → Master | 單播 | 請求全量快照 |
| 12 | `SNAPSHOT` | Master → 請求者 | 單播 | 全量狀態快照(單包,§6.5) |
| 13 | `MASTER_CLAIM` | 接管候選 → 全體 | 廣播 | 容錯接管宣告(§9.5) |
| 14 | `CLAIM_INFO` | 成員 → 接管候選 | 單播 | 回報自身 `last_seq`(接管日誌同步,§9.5) |
| 15 | `TABLE_DISSOLVE` | 讓桌 Master → 全體 | 廣播 | 本桌解散,導引成員遷移(§8.1) |

## 6. 可靠層(L1)

### 6.1 任期與序號

- 每筆 EVT 標記 `(epoch, seq)`:`epoch` 為現任 Master 任期(交接/接管 +1),
  `seq` 為全域遞增序號(自 1 起,**跨任期接續**,不歸零)。
- **接收規則(全序仲裁)**:
  - `pkt.epoch < cur_epoch` → 直接丟棄(舊任期殘包 / 復活的舊 Master;它聽到現任心跳會自行降級)。
  - `pkt.epoch == cur_epoch` → 正常處理,以 `seq` 去重與排序。
  - `pkt.epoch > cur_epoch` → 任期推進(來自 `E_MASTER_HANDOFF` / `E_MASTER_TAKEOVER` / 新任心跳):
    更新 `cur_epoch` 與 Master 身分;若 `pkt.last_seq` 顯示自己有缺 → 走補洞/快照。
- 每筆 EVT 廣播 **2 次**(間隔 `T_EVT_REBCAST` = 40ms)作前向冗餘;
  **帶 `play_at` 的齊播事件廣播 3 次**(0 / 40 / 200ms)**並緊跟一個立即心跳**(§7)。
- Master 在 RAM 保留最近 `EVT_LOG_KEEP`(=64)筆事件供補發。
- **所有成員保留同一份事件尾端日誌(≥32 筆)** —— 用途有二:
  (a) 接管期把**新 Master 自己缺的洞**補給它(§9.5,這是主要用途);
  (b) 新 Master 上任後補別人的洞。
- `seq` 為 16-bit,以距離窗口比較(`(int16_t)(a-b)`)處理回繞。

### 6.2 補洞(GAP repair)

接收端維護 `next_expected_seq`:

1. 收到 `seq == next_expected_seq` → 交付上層,`next_expected_seq++`,並檢查亂序緩衝可否連續交付。
2. 收到 `seq > next_expected_seq` → 暫存(亂序緩衝 8 筆),啟動補洞計時器
   `T_GAP_DELAY`(=60ms **± rand(0..30ms) 抖動**,防同步風暴)後仍缺 → `GAP_REQ{from,to}`。
3. `GAP_REQ` 重試最多 `GAP_RETRY`(=4)次,間隔 200ms;仍失敗 → 轉為請求快照(§6.5)。
4. Master 在 100ms 內收到 ≥3 個成員對同一缺口的 GAP_REQ → 改用**廣播**補發一次。
5. 事件**嚴格按 `(epoch, seq)` 順序交付**上層 —— 上層永遠看不到亂序/缺洞/重複。

```c
typedef struct __attribute__((packed)) {
    uint16_t from_seq;   // 含
    uint16_t to_seq;     // 含
} pn_gap_req_t;
```

### 6.3 命令可靠性(玩家 → Master)

- 每台裝置維護自己的 `cmd_id`(`uint16_t` 遞增)。
- 送出 CMD 後等 `CMD_ACK`;`T_CMD_RETRY`(=150ms)未獲 ACK 則重送,最多 `CMD_RETRY`(=5)次。
- Master 以 `(MAC, cmd_id)` 去重;**且所有命令語義冪等**(§1 原則 5)——
  跨 Master 交接的重送天然無害。
- 5 次皆失敗 → 協定層向上層回報 `LINK_LOST`,同時開始懷疑 Master 死亡(§9.5)。

```c
typedef struct __attribute__((packed)) {
    uint16_t cmd_id;
    uint8_t  cmd;        // pn_cmd_t(§10)
    uint8_t  _pad;       // 0
    // 之後接各命令參數(§10)
} pn_cmd_hdr_t;

typedef struct __attribute__((packed)) {
    uint16_t cmd_id;     // 回應哪筆命令
    uint8_t  result;     // pn_cmd_result_t
    uint8_t  reason;     // pn_reject_reason_t(result=REJECT 時有效)
} pn_cmd_ack_t;

typedef enum {           // pn_cmd_result_t
    PN_CMD_OK        = 0,   // 已受理,效果將以 EVT 呈現
    PN_CMD_REJECT    = 1,   // 拒絕,見 reason
    PN_CMD_STALE     = 2,   // 命令所指狀態已過期(如對已結束的行動輪下注)
    PN_CMD_NOT_MASTER= 3,   // 我不是現任 Master(附我所知的現任;發送者應改投新 Master)
} pn_cmd_result_t;

typedef enum {           // pn_reject_reason_t
    PN_RJ_NOT_YOUR_TURN=1, PN_RJ_BAD_AMOUNT=2, PN_RJ_CAP_EXCEEDED=3,
    PN_RJ_RAISE_LIMIT=4,   PN_RJ_NOT_DEALER=5, PN_RJ_SEAT_TAKEN=6,
    PN_RJ_TABLE_FULL=7,    PN_RJ_BAD_CONFIG=8, PN_RJ_WRONG_PHASE=9,
} pn_reject_reason_t;
```

`CMD_ACK(NOT_MASTER)` 的負載尾端附 `uint8_t master_mac[6]`(發送者所知現任),
玩家端據此改投目標重送(cmd_id 不變,冪等保證安全)。

### 6.4 心跳與成員狀態

- Master 每 `T_HEARTBEAT`(=500ms)廣播:

```c
typedef struct __attribute__((packed)) {
    uint16_t last_seq;      // 最新事件序號(成員據此發現靜默期掉包)
    uint32_t state_crc;     // last_seq 時點的權威狀態 CRC32(§6.6)
    uint8_t  phase;         // pn_phase_t(§6.6)
    uint8_t  hand_no;
    uint8_t  master_player; // 現任 Master 的 player_id(排座前也有效;0xFF=LOBBY 未定)
    uint8_t  battery_pct;   // Master 自己的電量(0xFF=未知)
} pn_heartbeat_t;
```

- 成員每 `T_STATUS`(=2s)單播:

```c
typedef struct __attribute__((packed)) {
    uint16_t last_recv_seq;  // 我已連續交付到的序號
    uint32_t state_crc;      // 「我交付到 last_recv_seq 時點」的狀態 CRC
    uint8_t  battery_pct;    // 0xFF=未知
} pn_status_t;
```

- **CRC 對齊比較**:Master 為最近 8 個 seq 保留 CRC 環形緩衝,
  只把成員的 `state_crc` 與**自己在同一 `last_recv_seq` 時點**的 CRC 比對;
  成員落後太多(超出環形緩衝)只當落後處理(補發),不判分歧。
  同一成員**對齊比較**連續 2 次不符 → 主動單播快照糾正。
- Master 據 STATUS 維護成員存活表(`T_PEER_DEAD`=5s 未聞 → `E_PLAYER_OFFLINE`)。

### 6.5 快照(Snapshot)

用途:中途加入/回歸、補洞失敗、狀態分歧糾正、接管後同步落後者。

```c
typedef struct __attribute__((packed)) {
    uint16_t as_of_seq;      // 本快照生效的序號
    uint16_t epoch;          // 生效任期
    pn_table_state_t state;  // §6.6,185 bytes
} pn_snapshot_t;             // 189 bytes(+14 標頭 = 203,單包)
```

接收方整體覆寫本地狀態,`next_expected_seq = as_of_seq + 1`,繼續收事件。
`SNAP_REQ` 無負載;1s 未獲 SNAPSHOT 則重發(上限 5 次,之後回報 `LINK_LOST`)。

### 6.6 權威狀態與一致性驗證

```c
typedef enum {   // pn_phase_t(參與 CRC,值域固定,不得私自增改)
    PN_PH_LOBBY=0, PN_PH_DEALER_CALL=1, PN_PH_SEATING=2, PN_PH_CHIPS=3,
    PN_PH_BLINDS=4, PN_PH_HAND=5, PN_PH_INTERMISSION=6, PN_PH_PAUSED=7,
} pn_phase_t;

typedef struct __attribute__((packed)) {
    // -- 桌面配置(7 bytes)--
    uint16_t table_id;
    uint8_t  sb, bb;            // 小盲/大盲(預設 1/2)
    uint16_t bet_cap;           // 單次下注/加注「增量」上限(預設 10;0=無上限,§11.3)
    uint8_t  n_players;
    // -- 桌面進度(4 bytes)--
    uint8_t  phase;             // pn_phase_t
    uint8_t  hand_no;
    uint8_t  button_seat;       // 本局莊家(0xFF=尚無)
    uint8_t  raise_count;       // 本輪已加注次數(限注用,§11.3)
    // -- 成員(按 player_id 排,16 bytes × 10)--
    struct __attribute__((packed)) {
        uint8_t  mac[6];
        uint8_t  seat;          // 0xFF=未定
        uint8_t  flags;         // bit0 在線 bit1 本局棄牌 bit2 all-in
                                // bit3 淘汰(chips=0 等待補籌) bit4 本局有發牌
        uint16_t chips;         // 手上籌碼(未投入)
        uint16_t bet_round;     // 本輪已投注額(在途)
        uint16_t bet_hand;      // 本局累計已投入(含已收池;結算/退注/側池的依據)
        uint8_t  hole[2];       // 底牌(0xFF=未發)
    } p[10];
    // -- 本局(14 bytes)--
    uint8_t  board[5];          // 公共牌(發牌即定;揭示與否是 UI 狀態)
    uint8_t  street;            // 0=preflop 1=flop 2=turn 3=river 4=showdown
    uint8_t  to_act_seat;       // 輪到誰(0xFF=無)
    uint16_t pot;               // 已收池(不含各家 bet_round 在途注;收池時點見 §11.3)
    uint16_t cur_bet;           // 本輪目前最高注(to-amount)
    uint16_t min_raise;         // 最小加注增量
    uint8_t  _pad;              // 0
} pn_table_state_t;             // 185 bytes(7+4+16×10+14;精確值見 §18)
```

- `state_crc` = 對上述結構的 CRC32(IEEE)。玩家顯示名 **不在** 權威狀態
  (只經 E_ROSTER / E_PLAYER_JOINED 傳遞,不參與 CRC)。
- **事件為一等公民、狀態為推導結果**:正常路徑靠事件重放;CRC/快照只是安全網。
- 為了讓異質韌體不因實作差異分歧,**會改變籌碼/池的事件都內含權威結果數值**
  (§11),接收端**以事件內數值為準**,本地計算僅用於 UI 預測與交叉檢查。

## 7. 桌面時鐘與齊播(goal 需求 3、8、10、12、14)

**桌面時鐘(Table Clock)**:全桌唯一、單調的毫秒時間軸。

1. 第一任(臨時)Master 以自己的本機毫秒建立桌面時鐘,此後**每一任 Master 都接續同一時間軸**:
   - 成員平時即維護 `offset = table_ms - local_ms`(對 Master 包的 `table_ms` 做 EWMA,α=0.25;
     單跳空中延遲 <2ms,穩態誤差 ±10ms)。
   - **交接/接管時,新 Master 用自己已收斂的 offset 把本機時鐘映射回桌面時鐘繼續發包**——
     時間軸無階躍,齊播跨任期不失準。新 Master 上任後前 2 個心跳期間,成員**凍結 EWMA**
     (只驗證不更新),吸收 ±10ms 級的小跳變。
2. 需要齊播的事件帶 `play_at`(桌面時鐘):`play_at = now + T_AUDIO_LEAD`(=700ms)。
   成員換算本地時間排程播放。齊播事件廣播 3 次(0/40/200ms)+ 緊跟立即心跳,
   使漏包者最遲在 ~260ms 內發現缺口,700ms 提前量涵蓋一輪補洞
   (60+200+來回 ≈ 300ms)後仍有餘裕。
   (注:`T_AUDIO_LEAD` 不涵蓋補洞**最壞**情況 —— 4 次重試 ≈ 900ms;此時走規則 3 降級。)
3. 事件若在 `play_at` 之後才到(極端補洞遲到),**立即播放**(寧可稍慢不可不播)。
4. 依序播報(如攤牌逐一報牌型)由事件內 `slot_gap_ms` 排程:
   第 k 位播報者在 `play_at + k × slot_gap_ms` 開播,無需再協商。

## 8. 成員與會話管理(L2)

### 8.1 發現與建桌(goal 步驟 1–2)

所有裝置進入遊戲 App 後(此刻即已 `WIFI_PS_NONE`):

1. 監聽 `T_SCAN`(=1.5s):收到 `ANNOUNCE(phase=LOBBY)` → 鎖定該 `table_id`,走加入流程(§8.2)。
2. 沒聽到 → 延遲 `rand(0..500ms)` 後自任**臨時 Master**:產生隨機 `table_id`(≠0),
   `epoch=1`,建立桌面時鐘,每 300ms 廣播 `ANNOUNCE`,進入 **10 秒加入窗口**(`T_JOIN_WINDOW`)。
3. **建桌衝突消解(含成員遷移)**:臨時 Master 聽到另一個 `ANNOUNCE(phase=LOBBY)`
   且對方 MAC 字典序**更小** → 讓桌:
   a. 廣播 `TABLE_DISSOLVE{target_table_id, target_mac}` ×3(間隔 100ms);
   b. 自己改走加入流程。
   已加入落敗桌的成員收到 `TABLE_DISSOLVE` → 清空會話,向 `target` 重新 HELLO。
   **LOBBY 期(E_ROSTER 定格前)Master 失聯不觸發 §9.5 接管** —— 成員直接回到掃描狀態
   (防殭屍桌復活)。
4. 每台加入中的裝置每 300ms 廣播 `HELLO`;**鎖定目標後 `pn_hdr_t.table_id` 必填**,
   Master 只理會指名自己的 HELLO(同房兩桌互不干擾)。收到 `JOIN_ACK(PENDING)` 後降頻至 2s。

```c
typedef struct __attribute__((packed)) {
    uint8_t  device_class;   // 1=zuowei-c3 2=xingzhi-cube 3=cardputer-adv 4=sticks3 0=其他
    uint8_t  battery_pct;    // 0xFF=未知
    uint8_t  fw_ver;
    char     name[8];        // 顯示名(不足補 0)
} pn_hello_t;

typedef struct __attribute__((packed)) {
    uint8_t  phase;          // pn_phase_t(LOBBY=招募中;其他=遊戲中,供中途加入判斷)
    uint8_t  n_players;
    uint16_t join_close_ms;  // 距加入窗口關閉的剩餘毫秒(LOBBY 時;其他期 0xFFFF)
} pn_announce_t;

typedef struct __attribute__((packed)) {
    uint8_t  result;         // 0=ACCEPT 1=PENDING(遊戲中,等莊家批准) 2=REJECT_FULL
                             // 3=REJECT_VERSION 4=ACCEPT_BACK(回歸,隨後單播快照)
    uint8_t  player_id;      // ACCEPT/ACCEPT_BACK 時有效
    uint16_t table_id;
    uint16_t epoch;
    uint32_t table_ms;       // 讓新成員立刻初始化時鐘 offset
} pn_join_ack_t;
```

- 加入窗口結束:`n_players < 3` → 自動延長 5s,**最多 4 次**(共 30s);
  仍不足 → 廣播解散提示(各機顯示/播報「人數不足」),回到掃描狀態。
  ≥3 → Master 廣播 `E_ROSTER`(定格成員表)並進入莊家確認儀式(§8.3)。
- 窗口內收到 `HELLO` → `JOIN_ACK(ACCEPT)` + `E_PLAYER_JOINED` 事件同步全桌。滿 10 人回 `REJECT_FULL`。
- **E_ROSTER 定格後**(儀式期)收到的新 HELLO 一律按「遊戲中加入」(PENDING)處理。

### 8.2 加入(任何時機)

- **LOBBY 期**:自動接受(上限 10)。
- **遊戲中**(goal 步驟 20):回 `JOIN_ACK(PENDING)`(候選者 UI 顯示「等待莊家批准」)。
  Master 在**局間**(§11.5 統一時序)廣播 `E_JOIN_PENDING`,由莊家 `C_JOIN_DECIDE` 決定。
  允許 → 進入重排座流程(§8.4 重跑);舊玩家籌碼保留,新玩家 `C_SET_CHIPS` 輸入籌碼。
- **斷線重連**:HELLO 的 MAC 已在成員表 → `JOIN_ACK(ACCEPT_BACK)` + 單播快照 +
  廣播 `E_PLAYER_BACK`。不需批准、不改座次、籌碼原樣。

### 8.3 莊家確認儀式(goal 步驟 3)

1. Master 廣播 `E_DEALER_CALL{play_at}` → 全體齊播 *"Please confirm the dealer"*
   並顯示「我是莊家」按鈕。
2. 玩家按下 → `C_DEALER_CLAIM`。**第一筆被編序者勝** → `E_DEALER_SET{player_id}`;
   其餘 claim 回 `CMD_ACK(STALE)`。
3. 超時:`T_CEREMONY_REMIND`(=15s)無人按 → 重播提示音(可重複);
   臨時 Master 的螢幕另提供「指定自己為莊家」的逃生選項(產品層)。
4. 莊家若非臨時 Master → 走 **交接程序(§9.3)** 把 Master 移交給莊家裝置。

### 8.4 排座(goal 步驟 4;新玩家加入後重跑)

物理座次只有人知道,協定用「逐位認領」確定:

1. Master 廣播 `E_SEAT_PROMPT{seat_no=1, play_at}`:齊播/顯示
   *"Player on the dealer's left, press your button"*。
2. 該玩家按鍵 → `C_SEAT_CLAIM{seat_no}` → `E_SEAT_SET{seat_no, player_id}`
   (先到先得;已有座位者的 claim 回 `REJECT(SEAT_TAKEN)`)。
3. `seat_no` 遞增,直到所有玩家有座 → `E_SEATING_DONE`。莊家固定 `seat 0`,無需認領。
4. 超時:15s 無人認領 → 重播提示;**莊家可 `C_CEREMONY_SKIP`**(§10)把「所有尚未認領者」
   交由 Master 依 `player_id` 順序自動指派剩餘座位(物理順序可能不符,由玩家自行協調換位重跑,
   但流程絕不死鎖)。

### 8.5 籌碼與盲注設定(goal 步驟 5–6)

- `E_CHIPS_PROMPT` 後,每位玩家本機調數字(最低 1,預設 15)→ `C_SET_CHIPS{amount}`
  → `E_CHIPS_SET{player, amount}`。裝置收到**自己的** `E_CHIPS_SET` 後本機播報籌碼數。
- 超時:`T_CHIPS_AUTO`(=60s)未確認者,Master 以預設 15 代設(`E_CHIPS_SET{auto=1}`)。
- 全員設定完 → `E_BLINDS_PROMPT` → 莊家 `C_SET_BLINDS{sb,bb,bet_cap}`(預設 1/2/10)
  → `E_TABLE_CONFIG`。**驗證**:`sb ≥ 1`、`bb ≥ sb`、`bet_cap = 0(無上限)或 ≥ bb`,
  不合法回 `REJECT(BAD_CONFIG)`。60s 未設 → 以預設值代設。
- 之後走 §11.5 的局前時序進入第一局。

### 8.6 離開與離線

- 主動退出:`C_LEAVE` → `E_PLAYER_LEFT{player, reason=QUIT}`。局中退出視同棄牌
  (Master 先以 `E_ACTION{FOLD, auto=1}` 結束其行動義務再發 LEFT)。
  **Master 本人退出走 §9.3 優雅交接,嚴禁直接消失。**
- 被動離線:`T_PEER_DEAD`(=5s)無聲息 → `E_PLAYER_OFFLINE`。
  - 輪到離線者:Master 等 `T_ACT_OFFLINE`(=15s)未回歸 → 代打 check(可過牌)否則 fold
    (`E_ACTION{auto=1}`)。
  - 離線者籌碼保留,回歸(§8.2)即恢復。
- **在線但不行動**:`T_ACT_REMIND`(=30s)→ 該裝置本機重播提示音 + Master 廣播提醒事件;
  不自動代打(線下遊戲,人在現場,由人催促;流程性儀式的死鎖已由 §8.3–8.5 超時解決)。
- 存活成員 <2 → `E_GAME_PAUSE`;**恢復條件**:離線者回歸使存活 ≥2 時,Master 廣播 `E_GAME_RESUME`。
- **淘汰**:結算後 `chips=0` 者置 `flags.淘汰`,不參與後續發牌;
  其可在任何**局間**以 `C_SET_CHIPS` 重新買入(線下自行約定),清除淘汰旗標。

## 9. 角色與容錯

### 9.1 角色一覽

| 角色 | 數量 | 產生方式 |
|---|---|---|
| 臨時 Master | 1 | 建桌者(§8.1),epoch=1,莊家確認後交接 |
| Master | 1 | 常態=莊家裝置;交接(§9.3)/接管(§9.5)轉移,每次轉移 epoch+1 |
| Dealer(按鈕) | 1 | 第一局搶按產生;之後每局左移(`E_HAND_START.button_seat` 為準) |

### 9.2 接任順序鍵(全文統一)

接管/交接的「下一位」定義(確定性,全桌可獨立計算):

1. **排座完成後**:座位環序(`master_seat+1, +2, …` mod 座位數),略過離線/淘汰者。
2. **排座完成前**:`player_id` 環序;再不能決(理論上不會)→ MAC 字典序最小。

### 9.3 優雅交接(換莊 §9.4、莊家確認 §8.3、Master 主動退出)

**交接必須握手,舊 Master 在確認新任接手前不得停止服務**:

1. 舊 Master 廣播 `E_MASTER_HANDOFF{new_master_player, new_epoch=cur+1}`(正常編序事件)。
2. 新 Master 收到後:以 `new_epoch` + 接續的 seq、接續的桌面時鐘,**立即開始發心跳**。
3. 舊 Master 聽到新任的第一個心跳才停止心跳與 EVT_RTX 服務;
   `T_HANDOFF_ACK`(=1.5s)未聽到 → 視為交接失敗,**收回**:廣播
   `E_MASTER_HANDOFF{next 順位}` 換下一位(略過失聯者),epoch 再 +1。
4. 交接窗口內誤投舊 Master 的 CMD → `CMD_ACK(NOT_MASTER + 新任 MAC)`,玩家端重投(冪等)。
5. **Master 主動退出**:局間才允許 —— 先完成 `E_PLAYER_LEFT`,再按上述程序交接後才可下線。
   局中想退 → 先由 Master 對自己代打 fold 完本局(或等本局結束),局間再走交接。

### 9.4 每局換莊(goal 步驟 18)與低電量豁免

- 莊家(按鈕)隨 `E_HAND_START.button_seat` 左移(§9.2 順位,略過淘汰/離線)。
- **常態不變式**:Master = 莊家裝置,故局前交接一次(§11.5 統一時序)。
- **低電量豁免**:若下任莊家裝置 `battery_pct < 15`(且非 0xFF),
  Master **不移交**(按鈕照移、Master 留在原機),廣播的 `E_MASTER_HANDOFF` 以
  `new_master = 原任` 表達「本局 Dealer≠Master」。目的:250mAh 的 StickS3 之類裝置
  低電時別當序列器,降低局中接管機率。遊戲行為不受影響(Master 只是通訊角色)。

### 9.5 Master 故障接管(goal 需求 9)

**偵測**:連續 `T_MASTER_DEAD`(=3s,6 個心跳)未收 HEARTBEAT/EVT,
或對 Master 單播連續 5 次失敗。(LOBBY 期例外:§8.1.3,回掃描不接管。)

**接管三階段(先選最完整、再補齊、才編新事件)**:

1. **宣告**:候選者(§9.2 順位)等 `T_CLAIM_STAGGER × 順位距離`(=400ms/位)後
   廣播 `MASTER_CLAIM{proposed_epoch=cur+1, my_last_seq, my_player_id}` ×3(間隔 100ms)。
   聽到**更優**候選(更高 `last_seq`,平手則順位更近)→ 立即退讓、改回成員。
2. **同步**:存活成員收到 CLAIM → 單播 `CLAIM_INFO{my_last_seq}` 給最優候選;
   候選者發現有人比自己完整 → 向其 `GAP_REQ` 拉缺洞(成員以尾端日誌 `EVT_RTX` 回覆,
   §6.1 成員留日誌的主要用途)。收齊(或 `T_CLAIM_SETTLE`=800ms 到期,以已知最高 seq 為準)。
3. **上任**:廣播 `E_MASTER_TAKEOVER{new_epoch, dead_master_player}`
   (以 `全場最高 seq+1` 起編,epoch+1),再等 `T_TAKEOVER_GRACE`(=300ms)
   收殘餘 CLAIM_INFO,之後才發布第一筆新遊戲事件。落後成員以 STATUS 回報後被補齊/快照。

**上任後的牌局處置**:
- 死者是莊家且**局中** → 本局作廢:`E_HAND_ABORT`(§11.6,退注表由 `bet_hand` 確定性計算,
  這正是 §6.6 加入 `bet_hand` 的原因),死者標離線,以 §9.2 順位下一人為新莊家開新局。
- 局間/儀式期 → 直接以新 Master 身分續跑當前階段(儀式狀態都在權威狀態的 `phase` 內)。

**雙 Master 消解**:epoch 高者無條件勝;同 epoch(理論上不可能,防禦性規定)取 §9.2 順位近者。
被否定方立即降級並 SNAP_REQ 重新同步。**分叉窗口內已發生的 UI/語音副作用不保證一致**
(協定只保證狀態收斂;誤播的提示音無法收回,屬已知限制 §17)。

## 10. 命令目錄(CMD,玩家 → Master)

| cmd | 名稱 | 參數 | 階段 | 冪等性 |
|---|---|---|---|---|
| 1 | `C_DEALER_CLAIM` | – | 莊家確認 | 天然(先到先得) |
| 2 | `C_SEAT_CLAIM` | `seat_no u8` | 排座 | 天然 |
| 3 | `C_SET_CHIPS` | `amount u16` | 籌碼設定/局間補籌 | set 語義 |
| 4 | `C_SET_BLINDS` | `sb u8, bb u8, bet_cap u16` | 盲注設定(僅莊家) | set 語義 |
| 5 | `C_ACTION` | `action u8, amount u16` | 下注輪 | to-amount 語義 |
| 6 | `C_LEAVE` | – | 任意 | 天然 |
| 7 | `C_JOIN_DECIDE` | `player_id u8, allow u8` | 局間(僅莊家) | set 語義 |
| 8 | `C_READY_NEXT` | – | 局間(僅莊家) | 天然 |
| 9 | `C_CEREMONY_SKIP` | – | 儀式期(僅莊家/臨時 Master) | 天然 |

`action`:1=CHECK 2=CALL 3=BET 4=RAISE 5=FOLD 6=ALLIN。
`amount` 僅 BET/RAISE 有效,語義為**「本輪投注總額加到多少」(raise-to)**,天然冪等。

## 11. 事件目錄(EVT,Master → 全體)

```c
typedef struct __attribute__((packed)) {
    uint16_t seq;
    uint8_t  evt;            // 下表;0x01–0x7F 狀態事件,0x80+ 裝飾性事件(§15)
    uint8_t  _pad;           // 0
    uint32_t play_at;        // 桌面時鐘;0 = 不需排程播放
} pn_evt_hdr_t;               // 8 bytes
```

| evt | 名稱 | 負載結構 | goal 步驟 |
|---|---|---|---|
| 1 | `E_ROSTER` | §11.1 | 2 |
| 2 | `E_DEALER_CALL` | – | 3 |
| 3 | `E_DEALER_SET` | `{u8 player_id}` | 3 |
| 4 | `E_MASTER_HANDOFF` | `{u8 new_master_player; u16 new_epoch}` | 3、18 |
| 5 | `E_SEAT_PROMPT` | `{u8 seat_no}` | 4 |
| 6 | `E_SEAT_SET` | `{u8 seat_no; u8 player_id; u8 auto}` | 4 |
| 7 | `E_SEATING_DONE` | – | 4 |
| 8 | `E_CHIPS_PROMPT` | – | 5 |
| 9 | `E_CHIPS_SET` | `{u8 player_id; u16 amount; u8 auto}` | 5 |
| 10 | `E_BLINDS_PROMPT` | – | 6 |
| 11 | `E_TABLE_CONFIG` | `{u8 sb; u8 bb; u16 bet_cap; u8 auto}` | 6 |
| 12 | `E_HAND_START` | §11.2 | 7、9、10、11 |
| 13 | `E_STREET` | §11.3 | 12、14、15 |
| 14 | `E_ACTION_REQ` | §11.3 | 13 |
| 15 | `E_ACTION` | §11.3 | 13 |
| 16 | `E_HAND_RESULT` | §11.4 | 16、17 |
| 17 | `E_HAND_ABORT` | §11.6 | 容錯 |
| 18 | `E_PLAYER_JOINED` | `{u8 player_id; u8 mac[6]; u8 device_class; char name[8]}` | 20 |
| 19 | `E_PLAYER_LEFT` | `{u8 player_id; u8 reason}`(1=QUIT 2=KICKED) | 20 |
| 20 | `E_PLAYER_OFFLINE` / 21 `E_PLAYER_BACK` | `{u8 player_id}` | 容錯 |
| 22 | `E_JOIN_PENDING` | `{u8 cand_id; u8 mac[6]; u8 device_class; char name[8]}` | 20 |
| 23 | `E_MASTER_TAKEOVER` | `{u16 new_epoch; u8 new_master_player; u8 dead_master_player}` | 容錯 |
| 24 | `E_GAME_PAUSE` / 25 `E_GAME_RESUME` | – | 容錯 |
| 26 | `E_JOIN_DECIDED` | `{u8 cand_id; u8 allow; u8 player_id}`(allow=1 時 player_id 有效) | 20 |
| 0x80 | `E_REMIND` | `{u8 target_seat; u8 kind}`(裝飾性:催行動/重播提示) | – |

**E_PLAYER_JOINED 必含 MAC**:每個成員都要維護完整 peer 表(未來可能接任 Master)。

### 11.1 `E_ROSTER`

```c
typedef struct __attribute__((packed)) {
    uint8_t n;
    struct __attribute__((packed)) {
        uint8_t  player_id;
        uint8_t  mac[6];
        uint8_t  device_class;
        uint8_t  seat;          // 0xFF=未排
        uint8_t  flags;         // 同 §6.6
        uint16_t chips;
        char     name[8];
    } m[10];
} pn_evt_roster_t;               // 1 + 20×10 = 201 bytes(+8+14 = 223,單包 OK)
```

### 11.2 `E_HAND_START` —— 一包發完整局(goal 步驟 9、11)

Master 洗牌(熵源 `esp_random()`,Fisher–Yates)後,**整局所有牌一次廣播**:

```c
typedef struct __attribute__((packed)) {
    uint8_t  hand_no;
    uint8_t  button_seat;
    uint8_t  sb_seat, bb_seat;   // heads-up 時 sb_seat == button_seat(§11.7)
    uint8_t  n_dealt;            // 本局實際發牌人數
    uint8_t  board[5];           // 5 張公共牌(先發、蓋著;E_STREET 才翻示)
    struct __attribute__((packed)) {
        uint8_t  seat;
        uint8_t  hole[2];
        uint16_t chips;          // 扣盲後的權威籌碼
        uint16_t bet_hand;       // 已投入(=已扣的盲;不足額全下時=其全部身家)
        uint8_t  flags;          // bit2 all-in(貼盲即全下)bit4 有發牌
    } deal[10];                  // 未發牌座位不出現;不足 10 槽清零
    uint16_t pot;                // 實收盲注總和(短盲時 ≠ SB+BB)
} pn_evt_hand_start_t;            // 92 bytes(+8+14 = 114,單包 OK)
```

- 只對 `flags.淘汰=0 且在線(或離線但籌碼>0)` 的玩家發牌;離線者照發、由代打規則處理。
- **短盲(盲注不足)**:籌碼 < 應繳盲注 → 全額投入、置 all-in 旗標;`pot` 為實收。
- 每台裝置只顯示自己的底牌,其餘存記憶體(goal 需求 11)。
- 齊播序列(*"Dealing hole cards"* → *"Pre-flop betting"*)由 `play_at` + 產品層排程。

### 11.3 下注輪:`E_ACTION_REQ` / `E_ACTION` / `E_STREET`

```c
typedef struct __attribute__((packed)) {
    uint8_t  seat;            // 輪到誰
    uint16_t call_amt;        // 跟注需補到的總額(to-amount)
    uint16_t min_raise_to;    // 最小加注後總額(=cur_bet+min_raise;0=不可加注)
    uint16_t max_raise_to;    // 最大加注後總額(受 bet_cap 與身家限制)
    uint8_t  can_check;       // 1=可過牌
} pn_evt_action_req_t;

typedef struct __attribute__((packed)) {
    uint8_t  seat;
    uint8_t  action;          // 同 C_ACTION;bit7=Master 代打(auto)
    uint16_t amount;          // 本輪投注總額(to-amount)
    // ---- 權威結果(接收端直接覆寫)----
    uint16_t chips_left;      // 該玩家剩餘籌碼
    uint16_t bet_round;       // 該玩家本輪已投注
    uint16_t bet_hand;        // 該玩家本局累計投入
    uint16_t pot;             // 已收池(本輪中不變,交叉檢查用)
    uint16_t cur_bet;         // 本輪最高注
    uint8_t  raise_count;     // 本輪已加注次數
    uint8_t  next_seat;       // 下一位行動者;0xFF=本輪結束(等 E_STREET/E_HAND_RESULT)
} pn_evt_action_t;

typedef struct __attribute__((packed)) {
    uint8_t  street;          // 進入的街道:1=flop 2=turn 3=river 4=showdown
    uint16_t pot;             // 收池後權威池底(本事件才是收池時點)
    uint8_t  first_seat;      // 本街第一位行動者;0xFF=無(全 all-in 直接發完)
} pn_evt_street_t;
```

**限注語義(bet_cap,goal 步驟 6)**:
- `bet_cap` 限制**單次增量**:BET 的 `amount ≤ bet_cap`;RAISE 的 `amount - cur_bet ≤ bet_cap`。
- **每街加注次數上限 `RAISE_PER_STREET` = 4**(bet+3 raises,限注慣例);
  達上限後只可 call/fold(`E_ACTION_REQ.min_raise_to = 0`)。
- **All-in 例外**:身家不足跟注 → 永遠可全下;不足最小加注額的全下**不重開加注權**
  (不增加 `raise_count`、不更新 `min_raise`,標準規則)。All-in 金額不受 `bet_cap` 約束
  (身家小於 cap 時的全下本來就 ≤ cap;身家大於 call 但小於 min_raise_to 的全下屬「不足額加注」)。
- `min_raise` 初始 = BB,之後 = 上一次加注增量與 `bet_cap` 取小。
- **收池時點**:`pot` 只在 `E_STREET` / `E_HAND_RESULT` 更新(把各家 `bet_round` 掃入),
  `E_ACTION.pot` 在街中恆為已收池值。

### 11.4 `E_HAND_RESULT` —— 結算(goal 步驟 16、17)

```c
typedef struct __attribute__((packed)) {
    uint8_t  hand_no;
    uint8_t  reason;            // 0=攤牌 1=全棄只剩一人(不揭牌)
    uint8_t  n_show;            // 攤牌者數
    struct __attribute__((packed)) {
        uint8_t seat;
        uint8_t rank_cat;       // 0=高牌 … 8=同花順 9=皇家同花順(播報用)
        uint8_t is_winner;
    } show[10];                 // 依播報順序排列(slot k = 第 k 筆)
    struct __attribute__((packed)) {
        uint8_t  seat;          // 0xFF=空槽
        uint16_t win;           // 分得(側池已結算;奇數籌碼歸最靠按鈕左手的贏家)
    } payout[10];
    struct __attribute__((packed)) {
        uint8_t  seat;          // 0xFF=空槽(座位稀疏安全)
        uint16_t chips;         // 權威餘額
    } chips_after[10];
    uint16_t slot_gap_ms;       // 逐座播報間隔(預設 2500)
} pn_evt_hand_result_t;          // 96 bytes(+8+14 = 118,單包 OK)
```

- **側池**:Master 依各家 `bet_hand` 切池結算(每個 all-in 額度一池),`payout` 為最終結果;
  接收端不重算,只照表顯示/播報。**奇數籌碼**:分池除不盡的餘數歸按鈕左手最近的贏家(慣例)。
- 播報:攤牌者按 `slot` 順序各自播自己的牌型(*"Full house"* 等),
  最後贏家裝置播 *"I won"*;每台裝置隨後本機播報自己的最新籌碼(goal 步驟 8)。

### 11.5 局間統一時序(修正 v1.0 的 §9.2/§11.4 矛盾)

結算後到下一局開始,**唯一合法順序**(全程由**本局 Master** 主持,它此刻仍是莊家裝置):

```
E_HAND_RESULT
  → (有淘汰者想補籌) C_SET_CHIPS → E_CHIPS_SET            [可選]
  → (有加入候選)   E_JOIN_PENDING → C_JOIN_DECIDE → E_JOIN_DECIDED
                    → 若批准:重跑排座 §8.4(E_SEAT_PROMPT…E_SEATING_DONE)
                      + 新玩家 C_SET_CHIPS → E_CHIPS_SET     [可選]
  → 莊家 C_READY_NEXT(30s 超時自動視同按下)
  → E_MASTER_HANDOFF(交接給下任莊家;§9.3 握手;低電豁免見 §9.4)
  → 新 Master:E_HAND_START(button = 自己的座位)
```

`C_JOIN_DECIDE`/`C_READY_NEXT` 的「莊家」= **本局(即將卸任)莊家**,也就是現任 Master 本人
—— 命令不跨機,無歧義。

### 11.6 `E_HAND_ABORT` —— 局中接管的作廢退注

```c
typedef struct __attribute__((packed)) {
    uint8_t  hand_no;
    uint8_t  reason;            // 0=Master 死亡接管 1=其他
    struct __attribute__((packed)) {
        uint8_t  seat;          // 0xFF=空槽
        uint16_t refund;        // 退還(= 該玩家 bet_hand)
        uint16_t chips_after;   // 權威餘額
    } refund[10];
} pn_evt_hand_abort_t;           // 52 bytes
```

退注表 = 每人 `bet_hand` 原數退還 —— 新 Master 從被複製的權威狀態即可確定性計算
(v1.0 算不出來,v1.1 靠 §6.6 的 `bet_hand` 修復)。

### 11.7 Heads-up(2 人)特例(標準規則,明文固定)

- `sb_seat = button_seat`(按鈕兼小盲),另一人為 BB。
- **Preflop:按鈕(SB)先行動;Flop 之後:BB 先行動**(即按鈕永遠最後行動)。
- 換莊照常左移(兩人輪流)。
- 3 人縮到 2 人的當局照原規則打完,下一局起適用本節。

## 12. goal 20 步 ↔ 協定對照表

| goal 步驟 | 協定機制 |
|---|---|
| 1–2 加入(10s) | §8.1 發現/建桌/`T_JOIN_WINDOW`(+延長規則) |
| 3 確認莊家 | §8.3 `E_DEALER_CALL`+`C_DEALER_CLAIM` 搶序,§9.3 交接給莊家 |
| 4 座次 | §8.4 逐位認領(+超時/跳過) |
| 5 籌碼 | §8.5 `C_SET_CHIPS`/`E_CHIPS_SET`+本機播報 |
| 6 盲注/上限 | §8.5 `C_SET_BLINDS`/`E_TABLE_CONFIG`(+驗證) |
| 7 莊家=主設備 | §9.4 常態不變式(低電豁免例外) |
| 8 播報籌碼 | `E_CHIPS_SET`/`E_HAND_RESULT.chips_after` 本機播報 |
| 9 公共牌+防故障 | `E_HAND_START` 全量廣播 + §9.5 三階段接管 + §11.6 作廢退注 |
| 10–12 發牌/齊播 | `E_HAND_START` + §7 桌面時鐘 `play_at` |
| 13 行動 | `E_ACTION_REQ`/`C_ACTION`/`E_ACTION`(權威數值) |
| 14–15 街道推進 | `E_STREET`(收池時點) |
| 16–17 結果/播報 | `E_HAND_RESULT` slot 播報(含側池/奇數籌碼) |
| 18–19 換莊循環 | §11.5 統一局間時序 + §9.3 握手交接 |
| 20 退出/加入 | §8.2/§8.6 + `E_JOIN_PENDING`/`C_JOIN_DECIDE`/`E_JOIN_DECIDED` |

## 13. 常數表

| 常數 | 值 | 說明 |
|---|---|---|
| `PN_VERSION` | 1 | 協定主版本(不符拒絕互通) |
| `T_SCAN` | 1500 ms | 開機監聽既有牌桌 |
| `T_JOIN_WINDOW` | 10 s | 加入窗口(<3 人 +5s,至多 4 次) |
| `T_ANNOUNCE` / `T_HELLO` | 300 ms | 招募/加入信標間隔(PENDING 後 HELLO 降頻 2s) |
| `T_HEARTBEAT` | 500 ms | Master 心跳 |
| `T_STATUS` | 2 s | 成員回報 |
| `T_EVT_REBCAST` | 40 ms | 事件二次廣播間隔(齊播事件 0/40/200ms 三次+立即心跳) |
| `T_GAP_DELAY` | 60 ± rand(30) ms | 缺洞等待冗餘期(含抖動) |
| `GAP_RETRY` | 4 次 / 200 ms | 補洞重試 |
| `T_CMD_RETRY` | 150 ms ×5 | 命令重試 |
| `T_MASTER_DEAD` | 3 s | Master 死亡判定 |
| `T_CLAIM_STAGGER` | 400 ms/順位 | 接管宣告錯峰 |
| `T_CLAIM_SETTLE` | 800 ms | 接管日誌同步窗口 |
| `T_TAKEOVER_GRACE` | 300 ms | 上任後緩衝(收殘餘 CLAIM_INFO) |
| `T_HANDOFF_ACK` | 1.5 s | 交接握手超時(舊任收回) |
| `T_PEER_DEAD` | 5 s | 成員離線判定 |
| `T_ACT_OFFLINE` | 15 s | 離線者代打等待 |
| `T_ACT_REMIND` | 30 s | 在線不行動催促 |
| `T_CEREMONY_REMIND` | 15 s | 儀式無回應重播提示 |
| `T_CHIPS_AUTO` | 60 s | 籌碼/盲注設定超時代設 |
| `T_READY_AUTO` | 30 s | C_READY_NEXT 超時自動 |
| `T_AUDIO_LEAD` | 700 ms | 齊播提前量(涵蓋一輪補洞,非最壞情況;遲到降級立即播) |
| `RAISE_PER_STREET` | 4 | 每街 bet+raise 次數上限 |
| `BATT_MASTER_MIN` | 15 % | 低電豁免門檻(§9.4) |
| `EVT_LOG_KEEP` | 64 / 32 筆 | Master / 成員事件日誌保留 |
| `MAX_PLAYERS` | 10 | |

## 14. 上層介面(協定層的「小介面」)

```c
// ============ pbus.h —— 協定層對遊戲邏輯的完整介面 ============
typedef struct {
    void (*on_event)(const pn_evt_hdr_t *e, const void *body, size_t len);
        // 嚴格按 (epoch, seq) 順序交付;絕不亂序、絕不缺洞、絕不重複。
    void (*on_role)(bool i_am_master);
        // 本機成為/卸任 Master(Master 端需啟用「決策引擎」:驗證命令、推進狀態機)。
    void (*on_link)(pn_link_state_t s);   // JOINED / PENDING / LINK_LOST / RESYNCED / DISSOLVED
    uint32_t (*get_battery_pct)(void);    // 由韌體提供(0xFF=未知),協定層放入 HELLO/STATUS
} pbus_callbacks_t;

esp_err_t pbus_start(const pbus_callbacks_t *cb, const char *name);
                                            // 掃描 → 建桌或加入(§8.1)
esp_err_t pbus_submit_cmd(uint8_t cmd, const void *arg, size_t len);
                                            // 可靠送 Master(§6.3;自動處理 NOT_MASTER 轉投)
esp_err_t pbus_publish_evt(uint8_t evt, const void *body, size_t len,
                           uint32_t play_at);  // 僅 Master:編序+廣播(自動 2/3 次冗餘)
uint32_t  pbus_table_now(void);             // 桌面時鐘(§7)
uint32_t  pbus_local_time_for(uint32_t table_ms);  // play_at → 本地時刻
const pn_table_state_t *pbus_state(void);   // 唯讀權威狀態

// —— 僅 Master 端(決策引擎)使用:接收玩家命令與回覆 ——
typedef void (*pbus_cmd_handler_t)(const uint8_t mac[6], uint16_t cmd_id,
                                   uint8_t cmd, const void *arg, size_t len);
esp_err_t pbus_set_cmd_handler(pbus_cmd_handler_t h);   // 協定層已完成 (MAC,cmd_id) 去重
esp_err_t pbus_reply_cmd(const uint8_t mac[6], uint16_t cmd_id,
                         uint8_t result, uint8_t reason);  // 發 CMD_ACK(§6.3)
```

七個函式 + 四個回呼 —— epoch、補洞、重試、去重、心跳、快照、時鐘、接管全部不可見。
遊戲邏輯(Master 端決策 + 全員狀態機)寫一次,四種裝置共用;裝置差異只在 HAL
(《產品開發文件》定義)。

## 15. 版本與擴充

- `version` 不符:直接丟棄封包;HELLO 的版本不符時回 `JOIN_ACK(REJECT_VERSION)`。
- **事件 ID 分區**:`0x01–0x7F` = 狀態事件(影響權威狀態/CRC)——同一 `PN_VERSION` 內
  集合固定,**新增狀態事件必須升主版本**;`0x80–0xFF` = 裝飾性事件(提示音、UI 提醒)——
  舊裝置對未知裝飾性事件**忽略但推進 seq**(前向相容)。
- 新裝置類別:`device_class` 僅供顯示,協定行為與硬體無關。

## 16. 安全性聲明

線下面對面娛樂用途:無加密、無防作弊(所有底牌廣播全桌,有心人抓包即可看牌)。
此為 goal 明示的刻意取捨,不屬缺陷。

## 17. 已知限制與硬體注意

1. 局中 Master 死亡 → 本局作廢退注(§11.6),不做殘局重演。
2. 全裝置斷電 → 牌局不持久化(不寫 NVS),重開即新局。
3. 接管分叉窗口內的 UI/語音副作用不保證一致(狀態保證收斂,§9.5)。
4. 同房兩桌:靠 `table_id` + HELLO 指名隔離,但共用 channel 1 空中頻寬。
5. **StickS3**:250mAh + `WIFI_PS_NONE` 續航有限(估 2–4 小時),長場次建議插電;
   電池供電時揚聲器音量須 ≤75%(硬體限制);韌體**必須**於啟動時改寫 M5PM1 BTN_CFG
   停用「單擊電源鍵復位」,否則誤觸=瞬間離線,容錯路徑被高頻觸發;
   插 USB 時 VBAT 可能讀 0,電量一律回報 0xFF(未知)而非 0。
6. **zuowei-c3**:GPIO13 電源自鎖 —— 使用者長按關機等同裝置消失,體驗上與斷電相同,
   產品層應在關機前若為 Master 先觸發 §9.3 交接。
7. 齊播保證為「±10ms(穩態)/ 最壞降級為遲到即播」;`T_AUDIO_LEAD` 不涵蓋補洞最壞情況。

## 18. 結構 sizeof 總表(不含 14-byte 通用標頭;EVT 另加 8-byte 事件頭)

| 結構 | 負載 bytes | 空中總長 |
|---|---|---|
| `pn_hello_t` | 11 | 25 |
| `pn_announce_t` | 4 | 18 |
| `pn_join_ack_t` | 10 | 24 |
| `pn_heartbeat_t` | 10 | 24 |
| `pn_status_t` | 7 | 21 |
| `pn_gap_req_t` | 4 | 18 |
| `pn_snapshot_t` | 189 | 203 |
| `pn_table_state_t` | 185 | –(嵌入快照) |
| `pn_evt_roster_t` | 201 | 223 |
| `pn_evt_hand_start_t` | 92 | 114 |
| `pn_evt_action_req_t` | 8 | 30 |
| `pn_evt_action_t` | 16 | 38 |
| `pn_evt_street_t` | 4 | 26 |
| `pn_evt_hand_result_t` | 95 | 117 |
| `pn_evt_hand_abort_t` | 52 | 74 |
| `MASTER_CLAIM` `{u16 epoch; u16 last_seq; u8 player_id}` | 5 | 19 |
| `CLAIM_INFO` `{u16 last_seq}` | 2 | 16 |
| `TABLE_DISSOLVE` `{u16 target_table_id; u8 target_mac[6]}` | 8 | 22 |

全部 ≤ 250,無分片需求。
(實作必須以 `_Static_assert(sizeof(x)==N)` 鎖定上表數值,防止編譯器差異。)
