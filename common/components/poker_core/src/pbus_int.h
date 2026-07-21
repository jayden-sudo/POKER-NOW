#pragma once
/*
 * pbus_int.h -- pbus 私有共享狀態與內部函式(不對外公開)。
 * 全部欄位只在 pbus task 上下文觸碰(併發規範見指南 §5/§7)。
 */
#include "pbus.h"
#include "pk_config.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* 收包項(Wi-Fi task → pbus task) */
typedef struct { uint8_t src[6]; uint16_t len; uint8_t pkt[250]; } pn_rx_item_t;

/* 發送項(任意 task → pbus task:submit_cmd / publish_evt) */
typedef enum { TXREQ_CMD, TXREQ_EVT } txreq_kind_t;
typedef struct {
    txreq_kind_t kind;
    uint8_t  code;                 /* cmd 或 evt id */
    uint32_t play_at;              /* evt 用 */
    uint16_t len;
    uint8_t  body[210];
} pn_txreq_t;

typedef enum {
    ROLE_SCAN, ROLE_JOINING, ROLE_PENDING, ROLE_TEMP_MASTER,
    ROLE_MEMBER, ROLE_MASTER, ROLE_TAKEOVER_CLAIM,
} pbus_role_t;

typedef struct {
    uint8_t  mac[6];
    uint16_t last_cmd_id;
    uint8_t  last_result, last_reason;
    bool     valid;
} cmd_dedup_t;

typedef struct {
    /* 身分/會話 */
    char     name[8];
    uint8_t  self_mac[6];
    uint8_t  device_class;
    uint16_t table_id, epoch;
    uint8_t  master_mac[6];
    uint8_t  my_player_id;         /* 0xFF=未入桌 */
    pbus_role_t role;

    /* 可靠層 */
    uint16_t next_expected_seq;    /* 成員側 */
    uint16_t seq_alloc;            /* Master 側:下一個 seq */
    uint16_t last_recv_seq;        /* 已連續交付到 */

    struct { bool used; uint16_t seq; uint16_t len; uint8_t pkt[250]; } ooo[PK_OOO_BUF_SLOTS];
    /* 事件日誌:存完整空中包(hdr+evt_hdr+body) */
    struct { uint16_t seq; uint16_t len; uint8_t pkt[PK_EVT_SLOT_BYTES]; } log[PK_EVT_LOG_MASTER];
    int      log_head;             /* 環形寫指標 */
    int      log_count;
    uint32_t crc_ring[8];

    /* 時鐘 */
    int64_t  clock_offset_q8;      /* (table_ms - local_ms) << 8 */
    bool     clock_inited;
    uint8_t  clock_freeze_hb;

    /* CMD 客戶端 */
    uint16_t cmd_id_next;
    bool     cmd_inflight;
    uint16_t cmd_inflight_id;
    uint8_t  cmd_inflight_code, cmd_inflight_len;
    uint8_t  cmd_inflight_body[210];
    int64_t  cmd_last_send_us;
    int      cmd_retries;

    /* Master 端命令去重表 */
    cmd_dedup_t dedup[PN_MAX_PLAYERS];

    /* 定時錨點(us) */
    int64_t  t_last_hb_rx;         /* 最近收到 Master 心跳 */
    int64_t  t_last_hb_tx;         /* 最近發心跳 */
    int64_t  t_last_status_tx;
    int64_t  t_last_beacon_tx;     /* HELLO/ANNOUNCE */
    int64_t  t_scan_start;
    int64_t  t_role_enter;      /* v1.1 真機修訂:角色進入時刻(JOINING 超時等用) */
    int64_t  t_join_open;          /* 加入窗口起點(Master) */
    int      join_extends;
    int64_t  t_gap_arm;            /* gap timer 到期(0=未武裝) */
    int      gap_retries;

    /* 接管 */
    int64_t  t_claim_start;
    uint16_t claim_best_seq;
    uint8_t  claim_my_rank;     /* B6:接管順位(距死亡 Master 座位環距) */
    bool     claim_sent;        /* B6:錯峰宣告是否已發 */

    /* 權威狀態鏡像 */
    pn_table_state_t st;

    /* 回呼與 hooks */
    pbus_callbacks_t cb;
    pbus_cmd_handler_t cmd_handler;
    void (*idle_hook)(void);

    QueueHandle_t rxq;
    QueueHandle_t txq;
    bool     started;
} pbus_t;

extern pbus_t g_pb;

/* ---- transport ---- */
esp_err_t pn_transport_init(void);
void pn_send(const uint8_t dst[6], const void *data, size_t len);   /* dst=NULL → 廣播 */
void pn_add_peer(const uint8_t mac[6]);
uint32_t pn_local_ms(void);
int64_t  pn_now_us(void);
uint32_t pn_crc32(const void *data, size_t len);

/* 組包 helper:填標頭並發送。type 見 pn_pkt_type_t */
void pn_send_typed(const uint8_t dst[6], uint8_t type, const void *payload, uint16_t plen);

/* ---- clock ---- */
void pn_clock_ingest(uint32_t table_ms);       /* 收到現任 Master 包時 */
uint32_t pn_clock_table_now(void);
uint32_t pn_clock_local_for(uint32_t table_ms);
void pn_clock_reset(void);

/* ---- reliab ---- */
void pn_deliver(const uint8_t *pkt, uint16_t len);   /* 交付一個已排序 EVT 包 */
void pn_reliab_on_evt(const uint8_t *pkt, uint16_t len);  /* 交付路徑入口(亂序處理) */
void pn_log_store(uint16_t seq, const uint8_t *pkt, uint16_t len);
bool pn_log_get(uint16_t seq, const uint8_t **pkt, uint16_t *len);
void pn_gap_tick(void);
void pn_gap_arm_if_needed(void);
void pn_reliab_reset(void);

/* ---- session ---- */
void pn_session_tick(void);                    /* 定時器輪:心跳/status/beacon/掃描/接管 */
void pn_session_on_pkt(const pn_rx_item_t *rx); /* 非 EVT 的控制包處理 */
void pn_publish_locked(uint8_t evt, const void *body, uint16_t len, uint32_t play_at);
void pn_send_snapshot(const uint8_t dst[6]);
void pn_apply_snapshot(const pn_snapshot_t *snap);
void pn_roster_add_peers(void);
void pn_set_role(pbus_role_t r);
