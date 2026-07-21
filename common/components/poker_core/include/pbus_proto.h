#pragma once
/*
 * pbus_proto.h -- POKER-NOW 線上格式(協定 §5/§6/§8/§11/§18 全部 packed 結構與枚舉)。
 * 位元組序:小端(全同族 ESP32);全部 __attribute__((packed))。
 * sizeof 依裁定 R1 鎖定(state=185/snapshot=189/hand_result=95)。
 * 本檔對 esp_* 依賴為零(host 測試可 include)。
 */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================= §5.1 通用標頭(14 bytes) ================= */
typedef struct __attribute__((packed)) {
    uint16_t magic;         /* PN_MAGIC */
    uint8_t  version;       /* PN_VERSION */
    uint8_t  type;          /* pn_pkt_type_t */
    uint16_t table_id;
    uint16_t epoch;
    uint16_t len;           /* 標頭之後的負載長度 */
    uint32_t table_ms;
} pn_hdr_t;

/* ================= §5.2 封包類型 ================= */
typedef enum {
    PN_PKT_HELLO = 1, PN_PKT_ANNOUNCE = 2, PN_PKT_JOIN_ACK = 3,
    PN_PKT_CMD = 4, PN_PKT_CMD_ACK = 5, PN_PKT_EVT = 6, PN_PKT_EVT_RTX = 7,
    PN_PKT_GAP_REQ = 8, PN_PKT_HEARTBEAT = 9, PN_PKT_STATUS = 10,
    PN_PKT_SNAP_REQ = 11, PN_PKT_SNAPSHOT = 12, PN_PKT_MASTER_CLAIM = 13,
    PN_PKT_CLAIM_INFO = 14, PN_PKT_TABLE_DISSOLVE = 15,
} pn_pkt_type_t;

/* ================= §6.6 phase ================= */
typedef enum {
    PN_PH_LOBBY = 0, PN_PH_DEALER_CALL = 1, PN_PH_SEATING = 2, PN_PH_CHIPS = 3,
    PN_PH_BLINDS = 4, PN_PH_HAND = 5, PN_PH_INTERMISSION = 6, PN_PH_PAUSED = 7,
} pn_phase_t;

/* 成員 flags 位定義(§6.6) */
enum {
    PN_PF_ONLINE   = (1u << 0),
    PN_PF_FOLDED   = (1u << 1),
    PN_PF_ALLIN    = (1u << 2),
    PN_PF_OUT      = (1u << 3),   /* 淘汰(chips=0 等待補籌) */
    PN_PF_DEALT    = (1u << 4),   /* 本局有發牌 */
};

/* ================= §6.6 權威狀態(185 bytes) ================= */
typedef struct __attribute__((packed)) {
    uint8_t  mac[6];
    uint8_t  seat;          /* 0xFF=未定 */
    uint8_t  flags;
    uint16_t chips;
    uint16_t bet_round;
    uint16_t bet_hand;
    uint8_t  hole[2];
} pn_player_t;

typedef struct __attribute__((packed)) {
    /* 桌面配置(7) */
    uint16_t table_id;
    uint8_t  sb, bb;
    uint16_t bet_cap;
    uint8_t  n_players;
    /* 桌面進度(4) */
    uint8_t  phase;
    uint8_t  hand_no;
    uint8_t  button_seat;   /* 0xFF=尚無 */
    uint8_t  raise_count;
    /* 成員(16×10) */
    pn_player_t p[10];
    /* 本局(14) */
    uint8_t  board[5];
    uint8_t  street;        /* 0=preflop 1=flop 2=turn 3=river 4=showdown */
    uint8_t  to_act_seat;   /* 0xFF=無 */
    uint16_t pot;
    uint16_t cur_bet;
    uint16_t min_raise;
    uint8_t  _pad;
} pn_table_state_t;

/* ================= §6.5 快照(189 bytes) ================= */
typedef struct __attribute__((packed)) {
    uint16_t as_of_seq;
    uint16_t epoch;
    pn_table_state_t state;
} pn_snapshot_t;

/* ================= §6.2 補洞 ================= */
typedef struct __attribute__((packed)) {
    uint16_t from_seq;
    uint16_t to_seq;
} pn_gap_req_t;

/* ================= §6.3 命令 ================= */
typedef struct __attribute__((packed)) {
    uint16_t cmd_id;
    uint8_t  cmd;
    uint8_t  _pad;
} pn_cmd_hdr_t;

typedef struct __attribute__((packed)) {
    uint16_t cmd_id;
    uint8_t  result;        /* pn_cmd_result_t */
    uint8_t  reason;        /* pn_reject_reason_t */
    /* NOT_MASTER 時尾端附 uint8_t master_mac[6] */
} pn_cmd_ack_t;

typedef enum {
    PN_CMD_OK = 0, PN_CMD_REJECT = 1, PN_CMD_STALE = 2, PN_CMD_NOT_MASTER = 3,
} pn_cmd_result_t;

typedef enum {
    PN_RJ_NOT_YOUR_TURN = 1, PN_RJ_BAD_AMOUNT = 2, PN_RJ_CAP_EXCEEDED = 3,
    PN_RJ_RAISE_LIMIT = 4, PN_RJ_NOT_DEALER = 5, PN_RJ_SEAT_TAKEN = 6,
    PN_RJ_TABLE_FULL = 7, PN_RJ_BAD_CONFIG = 8, PN_RJ_WRONG_PHASE = 9,
} pn_reject_reason_t;

/* 命令 ID(§10) */
typedef enum {
    C_DEALER_CLAIM = 1, C_SEAT_CLAIM = 2, C_SET_CHIPS = 3, C_SET_BLINDS = 4,
    C_ACTION = 5, C_LEAVE = 6, C_JOIN_DECIDE = 7, C_READY_NEXT = 8,
    C_CEREMONY_SKIP = 9,
} pn_cmd_t;

/* action 值(§10) */
enum { ACT_CHECK = 1, ACT_CALL = 2, ACT_BET = 3, ACT_RAISE = 4, ACT_FOLD = 5, ACT_ALLIN = 6 };
#define ACT_AUTO_FLAG 0x80

/* 命令參數負載 */
typedef struct __attribute__((packed)) { uint8_t seat_no; } pn_cmd_seat_t;
typedef struct __attribute__((packed)) { uint16_t amount; } pn_cmd_chips_t;
typedef struct __attribute__((packed)) { uint8_t sb, bb; uint16_t bet_cap; } pn_cmd_blinds_t;
typedef struct __attribute__((packed)) { uint8_t action; uint16_t amount; } pn_cmd_action_t;
typedef struct __attribute__((packed)) { uint8_t player_id, allow; } pn_cmd_join_decide_t;

/* ================= §6.4 心跳/狀態 ================= */
typedef struct __attribute__((packed)) {
    uint16_t last_seq;
    uint32_t state_crc;
    uint8_t  phase;
    uint8_t  hand_no;
    uint8_t  master_player;
    uint8_t  battery_pct;
} pn_heartbeat_t;

typedef struct __attribute__((packed)) {
    uint16_t last_recv_seq;
    uint32_t state_crc;
    uint8_t  battery_pct;
} pn_status_t;

/* ================= §8.1 發現/建桌 ================= */
typedef struct __attribute__((packed)) {
    uint8_t device_class;   /* 1=zuowei-c3 2=xingzhi 3=cardputer 4=sticks3 0=其他 */
    uint8_t battery_pct;
    uint8_t fw_ver;
    char    name[8];
} pn_hello_t;

typedef struct __attribute__((packed)) {
    uint8_t  phase;
    uint8_t  n_players;
    uint16_t join_close_ms;
} pn_announce_t;

typedef enum {
    JA_ACCEPT = 0, JA_PENDING = 1, JA_REJECT_FULL = 2, JA_REJECT_VERSION = 3,
    JA_ACCEPT_BACK = 4,
} pn_join_result_t;

typedef struct __attribute__((packed)) {
    uint8_t  result;
    uint8_t  player_id;
    uint16_t table_id;
    uint16_t epoch;
    uint32_t table_ms;
} pn_join_ack_t;

/* ================= 接管 §9.5 ================= */
typedef struct __attribute__((packed)) {
    uint16_t epoch;         /* proposed_epoch = cur+1 */
    uint16_t last_seq;
    uint8_t  player_id;
} pn_master_claim_t;

typedef struct __attribute__((packed)) {
    uint16_t last_seq;
} pn_claim_info_t;

typedef struct __attribute__((packed)) {
    uint16_t target_table_id;
    uint8_t  target_mac[6];
} pn_table_dissolve_t;

/* ================= §11 事件 ================= */
typedef struct __attribute__((packed)) {
    uint16_t seq;
    uint8_t  evt;
    uint8_t  _pad;
    uint32_t play_at;
} pn_evt_hdr_t;

/* 事件 ID(§11) */
typedef enum {
    E_ROSTER = 1, E_DEALER_CALL = 2, E_DEALER_SET = 3, E_MASTER_HANDOFF = 4,
    E_SEAT_PROMPT = 5, E_SEAT_SET = 6, E_SEATING_DONE = 7, E_CHIPS_PROMPT = 8,
    E_CHIPS_SET = 9, E_BLINDS_PROMPT = 10, E_TABLE_CONFIG = 11, E_HAND_START = 12,
    E_STREET = 13, E_ACTION_REQ = 14, E_ACTION = 15, E_HAND_RESULT = 16,
    E_HAND_ABORT = 17, E_PLAYER_JOINED = 18, E_PLAYER_LEFT = 19,
    E_PLAYER_OFFLINE = 20, E_PLAYER_BACK = 21, E_JOIN_PENDING = 22,
    E_MASTER_TAKEOVER = 23, E_GAME_PAUSE = 24, E_GAME_RESUME = 25,
    E_JOIN_DECIDED = 26,
    E_REMIND = 0x80,
} pn_evt_t;

typedef struct __attribute__((packed)) {
    uint8_t n;
    struct __attribute__((packed)) {
        uint8_t  player_id;
        uint8_t  mac[6];
        uint8_t  device_class;
        uint8_t  seat;
        uint8_t  flags;
        uint16_t chips;
        char     name[8];
    } m[10];
} pn_evt_roster_t;

typedef struct __attribute__((packed)) { uint8_t player_id; } pn_evt_dealer_set_t;
typedef struct __attribute__((packed)) { uint8_t new_master_player; uint16_t new_epoch; } pn_evt_handoff_t;
typedef struct __attribute__((packed)) { uint8_t seat_no; } pn_evt_seat_prompt_t;
typedef struct __attribute__((packed)) { uint8_t seat_no; uint8_t player_id; uint8_t is_auto; } pn_evt_seat_set_t;
typedef struct __attribute__((packed)) { uint8_t player_id; uint16_t amount; uint8_t is_auto; } pn_evt_chips_set_t;
typedef struct __attribute__((packed)) { uint8_t sb, bb; uint16_t bet_cap; uint8_t is_auto; } pn_evt_table_config_t;

typedef struct __attribute__((packed)) {
    uint8_t  hand_no;
    uint8_t  button_seat;
    uint8_t  sb_seat, bb_seat;
    uint8_t  n_dealt;
    uint8_t  board[5];
    struct __attribute__((packed)) {
        uint8_t  seat;
        uint8_t  hole[2];
        uint16_t chips;
        uint16_t bet_hand;
        uint8_t  flags;
    } deal[10];
    uint16_t pot;
} pn_evt_hand_start_t;

typedef struct __attribute__((packed)) {
    uint8_t  seat;
    uint16_t call_amt;
    uint16_t min_raise_to;
    uint16_t max_raise_to;
    uint8_t  can_check;
} pn_evt_action_req_t;

typedef struct __attribute__((packed)) {
    uint8_t  seat;
    uint8_t  action;        /* bit7=auto */
    uint16_t amount;
    uint16_t chips_left;
    uint16_t bet_round;
    uint16_t bet_hand;
    uint16_t pot;
    uint16_t cur_bet;
    uint8_t  raise_count;
    uint8_t  next_seat;     /* 0xFF=本輪結束 */
} pn_evt_action_t;

typedef struct __attribute__((packed)) {
    uint8_t  street;
    uint16_t pot;
    uint8_t  first_seat;    /* 0xFF=無(全 all-in) */
} pn_evt_street_t;

typedef struct __attribute__((packed)) {
    uint8_t  hand_no;
    uint8_t  reason;        /* 0=攤牌 1=全棄只剩一人 */
    uint8_t  n_show;
    struct __attribute__((packed)) {
        uint8_t seat;
        uint8_t rank_cat;   /* 0..9(9=皇家) */
        uint8_t is_winner;
    } show[10];
    struct __attribute__((packed)) {
        uint8_t  seat;      /* 0xFF=空槽 */
        uint16_t win;
    } payout[10];
    struct __attribute__((packed)) {
        uint8_t  seat;      /* 0xFF=空槽 */
        uint16_t chips;
    } chips_after[10];
    uint16_t slot_gap_ms;
} pn_evt_hand_result_t;

typedef struct __attribute__((packed)) {
    uint8_t  hand_no;
    uint8_t  reason;        /* 0=Master 死亡接管 1=其他 */
    struct __attribute__((packed)) {
        uint8_t  seat;      /* 0xFF=空槽 */
        uint16_t refund;
        uint16_t chips_after;
    } refund[10];
} pn_evt_hand_abort_t;

typedef struct __attribute__((packed)) {
    uint8_t player_id;
    uint8_t mac[6];
    uint8_t device_class;
    char    name[8];
} pn_evt_player_joined_t;

typedef struct __attribute__((packed)) { uint8_t player_id; uint8_t reason; } pn_evt_player_left_t;
typedef struct __attribute__((packed)) { uint8_t player_id; } pn_evt_player_id_t;

typedef struct __attribute__((packed)) {
    uint8_t cand_id;
    uint8_t mac[6];
    uint8_t device_class;
    char    name[8];
} pn_evt_join_pending_t;

typedef struct __attribute__((packed)) {
    uint16_t new_epoch;
    uint8_t  new_master_player;
    uint8_t  dead_master_player;
} pn_evt_takeover_t;

typedef struct __attribute__((packed)) {
    uint8_t cand_id;
    uint8_t allow;
    uint8_t player_id;      /* allow=1 時有效 */
} pn_evt_join_decided_t;

/* E_REMIND(0x80,裁定 R8) */
typedef struct __attribute__((packed)) {
    uint8_t target_seat;    /* 0xFF=全體 */
    uint8_t kind;           /* 1=催行動(重播 V_YOUR_TURN) 2=儀式重播提示 */
} pn_evt_remind_t;

/* ================= §18 sizeof 鎖定(裁定 R1) ================= */
_Static_assert(sizeof(pn_hdr_t)              == 14,  "pn_hdr_t");
_Static_assert(sizeof(pn_table_state_t)      == 185, "pn_table_state_t (R1: not 167)");
_Static_assert(sizeof(pn_snapshot_t)         == 189, "pn_snapshot_t (R1: not 171)");
_Static_assert(sizeof(pn_evt_roster_t)       == 201, "pn_evt_roster_t");
_Static_assert(sizeof(pn_evt_hand_start_t)   == 92,  "pn_evt_hand_start_t");
_Static_assert(sizeof(pn_evt_action_req_t)   == 8,   "pn_evt_action_req_t");
_Static_assert(sizeof(pn_evt_action_t)       == 16,  "pn_evt_action_t");
_Static_assert(sizeof(pn_evt_street_t)       == 4,   "pn_evt_street_t");
_Static_assert(sizeof(pn_evt_hand_result_t)  == 95,  "pn_evt_hand_result_t (R1: not 96)");
_Static_assert(sizeof(pn_evt_hand_abort_t)   == 52,  "pn_evt_hand_abort_t");
_Static_assert(sizeof(pn_heartbeat_t)        == 10,  "pn_heartbeat_t");
_Static_assert(sizeof(pn_status_t)           == 7,   "pn_status_t");
_Static_assert(sizeof(pn_hello_t)            == 11,  "pn_hello_t");
_Static_assert(sizeof(pn_announce_t)         == 4,   "pn_announce_t");
_Static_assert(sizeof(pn_join_ack_t)         == 10,  "pn_join_ack_t");
_Static_assert(sizeof(pn_evt_hdr_t)          == 8,   "pn_evt_hdr_t");
_Static_assert(sizeof(pn_gap_req_t)          == 4,   "pn_gap_req_t");
_Static_assert(sizeof(pn_master_claim_t)     == 5,   "pn_master_claim_t");
_Static_assert(sizeof(pn_claim_info_t)       == 2,   "pn_claim_info_t");
_Static_assert(sizeof(pn_table_dissolve_t)   == 8,   "pn_table_dissolve_t");
_Static_assert(sizeof(pn_evt_remind_t)       == 2,   "pn_evt_remind_t");

#ifdef __cplusplus
}
#endif
