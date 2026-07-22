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
    /* st 指向本 view 自帶的 snap 快照(見下),而非 pbus 的 live state。
       UI task 只讀 st,故整份狀態在發佈時一次 memcpy 定格,消除對正被 pbus task
       改寫之權威狀態的撕裂讀取(併發規範 §7)。 */
    const pn_table_state_t *st;
    uint8_t  my_player_id, my_seat;
    bool     my_turn;
    uint16_t call_amt, min_raise_to, max_raise_to;
    bool     can_check;
    uint8_t  revealed_streets;
    uint8_t  cmd_inflight;
    uint8_t  last_reject_reason;      /* 讀取即清零 */
    pn_table_state_t snap;            /* st 指向此;發佈時定格的狀態快照 */
} game_view_t;

void game_init(const char *my_name);  /* 掛 callbacks + reducer + cmd handler,呼叫 pbus_start;init 期一次 */
/* game_view():UI task 呼叫,回傳最新一份定格快照(內含自帶 snap,可安全跨幀解參考)。
   下方 game_submit_* 由 UI task 於使用者操作時呼叫(內部走 pbus_submit_cmd,執行緒安全)。 */
const game_view_t *game_view(void);

void game_submit_action(uint8_t action, uint16_t raise_to);
void game_submit_chips(uint16_t amount);
void game_submit_blinds(uint8_t sb, uint8_t bb, uint16_t cap);
void game_submit_dealer_claim(void);
void game_submit_seat_claim(uint8_t seat_no);
void game_submit_join_decide(uint8_t cand_id, bool allow);
void game_submit_ready_next(void);
void game_submit_leave(void);
void game_submit_ceremony_skip(void);

/* 純 reducer:僅 pbus task 的交付路徑內呼叫(單一寫者);host 可編譯測試。
   len = 交付的 body 長度,短於該事件結構者不套用(防截斷/惡意封包)。 */
void game_state_apply(pn_table_state_t *st, const pn_evt_hdr_t *e,
                      const void *body, size_t len);

/* game_view 雙緩衝發佈:應僅由 pbus task 呼叫(單一寫者,維持雙緩衝不變量);host 測試不需要。 */
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
