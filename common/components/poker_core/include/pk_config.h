#pragma once
/*
 * pk_config.h -- 全專案常數總表。
 * 協定 §13 常數照抄為 PN_T_* / PN_* 巨集(值不得改);指南新增者見下方。
 * 本檔對 esp_* 依賴為零(host 測試可 include)。
 */

/* ---- 協定 §5.1 通用標頭常數 ---- */
#define PN_MAGIC            0x504B      /* "PK" 線上位元組序 4B 50 */
#define PN_VERSION          1

/* ---- 協定 §13 常數表(時間單位:毫秒,除非另註)---- */
#define PN_T_SCAN_MS            1500
#define PN_T_JOIN_WINDOW_MS     10000
#define PN_T_JOIN_EXTEND_MS     5000
#define PN_JOIN_EXTEND_MAX      4
#define PN_T_ANNOUNCE_MS        300
#define PN_T_ANNOUNCE_INGAME_MS 1000   /* v1.1 真機修訂:成桌後 Master 續發 ANNOUNCE(§8.2 中途加入/回歸) */
#define PN_T_JOIN_GIVEUP_MS     5000   /* v1.1 真機修訂:JOINING 無 JOIN_ACK 超時回掃描(防幽靈桌) */
#define PN_T_HELLO_MS           300
#define PN_T_HELLO_PENDING_MS   2000
#define PN_T_HEARTBEAT_MS       500
#define PN_T_STATUS_MS          2000
#define PN_T_EVT_REBCAST_MS     40
#define PN_T_GAP_DELAY_MS       60
#define PN_T_GAP_JITTER_MS      30
#define PN_GAP_RETRY            4
#define PN_T_GAP_RETRY_MS       200
#define PN_T_CMD_RETRY_MS       150
#define PN_CMD_RETRY            5
#define PN_T_MASTER_DEAD_MS     3000
#define PN_T_CLAIM_STAGGER_MS   400
#define PN_T_CLAIM_SETTLE_MS    800
#define PN_T_TAKEOVER_GRACE_MS  300
#define PN_T_HANDOFF_ACK_MS     1500
#define PN_T_PEER_DEAD_MS       5000
#define PN_T_ACT_OFFLINE_MS     15000
#define PN_T_ACT_REMIND_MS      30000
#define PN_T_CEREMONY_REMIND_MS 15000
#define PN_T_CHIPS_AUTO_MS      60000
#define PN_T_READY_AUTO_MS      30000
#define PN_T_AUDIO_LEAD_MS      700
#define PN_RAISE_PER_STREET     4
#define PN_BATT_MASTER_MIN      15
#define PN_MAX_PLAYERS          10
#define PN_SNAP_REQ_RETRY       5
#define PN_T_SNAP_REQ_MS        1000

/* 事件日誌保留量(Master / 成員);槽位定長見 PK_EVT_SLOT_BYTES */
#define PK_EVT_LOG_MASTER       64
#define PK_EVT_LOG_MEMBER       32

/* ---- 指南 §3.1 新增常數 ---- */
#define PK_CHIPS_MAX        9999          /* 產品 §3.2 */
#define PK_CHIPS_DEFAULT    15
#define PK_EVT_SLOT_BYTES   210           /* 事件日誌定長槽(產品 §8.1:容 E_ROSTER 201B) */
#define PK_OOO_BUF_SLOTS    8             /* 亂序緩衝(協定 §6.2) */
#define PK_GESTURE_LONG_MS  600           /* 產品 §2.1(裁定 R5) */
#define PK_GESTURE_REPEAT_MS 120
#define PK_GESTURE_X10_MS   2000

/* 產品 §6.3 排程常數(全裝置一致) */
#define PK_GAP_DEAL_PREFLOP_MS  2000
#define PK_RUNOUT_STREET_MS     3500
#define PK_SLOT_GAP_MS_DEFAULT  2500
#define PK_SPLIT_SLOT_MS        1200
#define PK_CHIP_REPORT_STAGGER_MS 1500

/* 系統選單預設(裁定 R9 / 產品 §5.6) */
#define PK_VOL_DEFAULT      70
#define PK_BRI_DEFAULT      80

#ifdef PK_DEBUG_SOLO                      /* §20.3 單機冒煙(裁定 R6) */
#define PK_MIN_PLAYERS      1
#define PK_PAUSE_MIN_ALIVE  1
#else
#define PK_MIN_PLAYERS      3             /* 協定 §8.1 */
#define PK_PAUSE_MIN_ALIVE  2             /* 協定 §8.6 */
#endif
