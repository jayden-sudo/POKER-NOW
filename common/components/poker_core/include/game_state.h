#pragma once
/*
 * game_state.h -- 狀態鏡像 reducer + game_view(產品 §3.2)。
 */
#include "pbus_proto.h"
#include "pbus.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const pn_table_state_t *st;
    uint8_t  my_player_id, my_seat;
    bool     my_turn;
    uint16_t call_amt, min_raise_to, max_raise_to;
    bool     can_check;
    uint8_t  revealed_streets;
    uint8_t  cmd_inflight;
    uint8_t  last_reject_reason;      /* 讀取即清零 */
} game_view_t;

void game_init(const char *my_name);  /* 掛 callbacks + reducer + cmd handler,呼叫 pbus_start */
const game_view_t *game_view(void);   /* UI task 讀 */

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

/* game_view 雙緩衝發佈(pbus task 每次交付後呼叫);host 測試不需要 */
void game_view_publish(void);

/* app_flow 觀察者掛勾(共用碼內部;供播報排程/畫面反應) */
typedef void (*game_event_hook_t)(const pn_evt_hdr_t *e, const void *body, size_t len);
typedef void (*game_link_hook_t)(pn_link_state_t s);
typedef void (*game_role_hook_t)(bool i_am_master);
void game_set_event_hook(game_event_hook_t hook);
void game_set_link_hook(game_link_hook_t hook);
void game_set_role_hook(game_role_hook_t hook);

#ifdef __cplusplus
}
#endif
