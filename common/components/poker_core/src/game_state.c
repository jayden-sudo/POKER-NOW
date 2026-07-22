/*
 * game_state.c -- 狀態鏡像 reducer + game_view(產品 §3.2/§3.1)。
 * reducer:凡事件內含權威數值一律直接覆寫;未用槽位清零(參與 CRC)。
 * game_view:純由 st 推導 + 雙緩衝發佈(併發規範 §7)。
 */
#include "game_state.h"
#include "pbus.h"
#include "master_engine.h"
#include "pk_config.h"
#include "hal/hal_battery.h"
#include <string.h>
#include <stdatomic.h>

/* ---------------- 雙緩衝 view ---------------- */
static game_view_t s_view[2];
static _Atomic int s_idx = 0;
static uint8_t s_local_reject;

static game_event_hook_t s_evt_hook;
static game_link_hook_t  s_link_hook;
static game_role_hook_t  s_role_hook;

static int seat_to_pid(const pn_table_state_t *st, uint8_t seat)
{
    if (seat == 0xFF) return -1;
    for (int i = 0; i < st->n_players && i < 10; i++)
        if (st->p[i].seat == seat) return i;
    return -1;
}

void game_view_publish(void)
{
    const pn_table_state_t *st = pbus_state();
    int cur = atomic_load(&s_idx);
    int nxt = 1 - cur;
    game_view_t *v = &s_view[nxt];
    memset(v, 0, sizeof(*v));
    /* 快照整份權威狀態進 view;之後一律讀本地 snap,不再指向 pbus 的 live state。 */
    memcpy(&v->snap, st, sizeof(v->snap));
    st = &v->snap;
    v->st = st;
    v->my_player_id = pbus_my_player_id();
    uint8_t my_seat = 0xFF;
    if (v->my_player_id < st->n_players && v->my_player_id < 10)
        my_seat = st->p[v->my_player_id].seat;
    v->my_seat = my_seat;
    v->revealed_streets = st->street;
    v->cmd_inflight = pbus_cmd_inflight() ? 1 : 0;

    bool my_turn = (st->phase == PN_PH_HAND && my_seat != 0xFF && st->to_act_seat == my_seat);
    v->my_turn = my_turn;
    /* 以下下注上限算式為成員側預覽,與 master_engine.c:issue_action_req() 的權威計算「必須一致」;
       唯一刻意差異:引擎另有 s_acted「補跟禁加注」規則(view 讀不到 s_acted)。同步改動(#14)。 */
    if (my_turn && v->my_player_id < 10) {
        const pn_player_t *me = &st->p[v->my_player_id];
        v->call_amt = st->cur_bet;
        v->can_check = (me->bet_round >= st->cur_bet);
        uint16_t allin_ceiling = (uint16_t)(me->bet_round + me->chips);
        uint32_t cap_ceiling = (st->bet_cap == 0) ? 0xFFFFu : (uint32_t)st->cur_bet + st->bet_cap;
        uint16_t maxr = allin_ceiling;
        if (maxr > cap_ceiling) maxr = (uint16_t)cap_ceiling;
        v->max_raise_to = maxr;
        if (st->raise_count >= PN_RAISE_PER_STREET) v->min_raise_to = 0;
        else {
            uint16_t minr = (uint16_t)(st->cur_bet + st->min_raise);
            v->min_raise_to = (minr > maxr) ? 0 : minr;
        }
    }

    uint8_t rj = s_local_reject; s_local_reject = 0;
    if (!rj) rj = pbus_consume_reject();
    v->last_reject_reason = rj;

    atomic_store(&s_idx, nxt);
}

const game_view_t *game_view(void)
{
    return &s_view[atomic_load(&s_idx)];
}

static void set_local_reject(uint8_t r) { s_local_reject = r; game_view_publish(); }

/* 各事件最小 body 長度。事件一律以 sizeof(struct) 全長送出(見 pbus_session/master_engine
   的 pn_publish_locked/ME_PUB 呼叫),故合法事件必 >= 此值;截斷/惡意封包在此擋下,避免
   reducer 讀進封包尾端未初始化位元組而汙染權威狀態(#5)。只列會解參考 body 的事件。 */
static size_t evt_min_body(uint8_t evt)
{
    switch (evt) {
    case E_ROSTER:        return sizeof(pn_evt_roster_t);
    case E_SEAT_SET:      return sizeof(pn_evt_seat_set_t);
    case E_CHIPS_SET:     return sizeof(pn_evt_chips_set_t);
    case E_TABLE_CONFIG:  return sizeof(pn_evt_table_config_t);
    case E_HAND_START:    return sizeof(pn_evt_hand_start_t);
    case E_ACTION_REQ:    return sizeof(pn_evt_action_req_t);
    case E_ACTION:        return sizeof(pn_evt_action_t);
    case E_STREET:        return sizeof(pn_evt_street_t);
    case E_HAND_RESULT:   return sizeof(pn_evt_hand_result_t);
    case E_HAND_ABORT:    return sizeof(pn_evt_hand_abort_t);
    case E_PLAYER_JOINED: return sizeof(pn_evt_player_joined_t);
    case E_PLAYER_LEFT:   return sizeof(pn_evt_player_left_t);
    case E_PLAYER_OFFLINE:
    case E_PLAYER_BACK:   return sizeof(pn_evt_player_id_t);
    default:              return 0;   /* 無 body 或 reducer 不解參考 body 的事件 */
    }
}

/* ---------------- 純 reducer ---------------- */
void game_state_apply(pn_table_state_t *st, const pn_evt_hdr_t *e,
                      const void *body, size_t len)
{
    if (len < evt_min_body(e->evt)) return;   /* 截斷/惡意事件:不套用 */
    switch (e->evt) {
    case E_ROSTER: {
        const pn_evt_roster_t *r = (const pn_evt_roster_t *)body;
        /* r->n 來自線上,可能因損毀/惡意超過 10;夾限以免流入顯示與比較(索引迴圈另有 i<10 護欄)。 */
        st->n_players = (r->n > PN_MAX_PLAYERS) ? PN_MAX_PLAYERS : r->n;
        for (int i = 0; i < 10; i++) {
            pn_player_t *p = &st->p[i];
            if (i < r->n) {
                memcpy(p->mac, r->m[i].mac, 6);
                p->seat = r->m[i].seat;
                p->flags = r->m[i].flags;
                p->chips = r->m[i].chips;
            } else {
                memset(p, 0, sizeof(*p));
                p->seat = 0xFF;
            }
            p->bet_round = 0; p->bet_hand = 0;
            p->hole[0] = p->hole[1] = 0xFF;
        }
        break;
    }
    case E_DEALER_CALL: st->phase = PN_PH_DEALER_CALL; break;
    case E_SEAT_PROMPT:  st->phase = PN_PH_SEATING; break;
    case E_SEAT_SET: {
        const pn_evt_seat_set_t *s = (const pn_evt_seat_set_t *)body;
        if (s->player_id < 10) st->p[s->player_id].seat = s->seat_no;
        break;
    }
    case E_CHIPS_PROMPT: st->phase = PN_PH_CHIPS; break;
    case E_CHIPS_SET: {
        const pn_evt_chips_set_t *c = (const pn_evt_chips_set_t *)body;
        if (c->player_id < 10) {
            st->p[c->player_id].chips = c->amount;
            if (c->amount > 0) st->p[c->player_id].flags &= ~PN_PF_OUT;
        }
        break;
    }
    case E_BLINDS_PROMPT: st->phase = PN_PH_BLINDS; break;
    case E_TABLE_CONFIG: {
        const pn_evt_table_config_t *t = (const pn_evt_table_config_t *)body;
        st->sb = t->sb; st->bb = t->bb; st->bet_cap = t->bet_cap;
        break;
    }
    case E_HAND_START: {
        const pn_evt_hand_start_t *hs = (const pn_evt_hand_start_t *)body;
        st->phase = PN_PH_HAND;
        st->hand_no = hs->hand_no;
        st->button_seat = hs->button_seat;
        memcpy(st->board, hs->board, 5);
        st->street = 0; st->pot = hs->pot; st->raise_count = 0;
        st->min_raise = st->bb ? st->bb : 2;
        st->to_act_seat = 0xFF;
        for (int i = 0; i < st->n_players && i < 10; i++) {
            st->p[i].bet_round = 0; st->p[i].bet_hand = 0;
            st->p[i].flags &= ~(PN_PF_FOLDED | PN_PF_ALLIN | PN_PF_DEALT);
            st->p[i].hole[0] = st->p[i].hole[1] = 0xFF;
        }
        uint16_t cur = 0;
        for (int d = 0; d < hs->n_dealt && d < 10; d++) {
            int pid = seat_to_pid(st, hs->deal[d].seat);
            if (pid < 0) continue;
            pn_player_t *p = &st->p[pid];
            p->chips = hs->deal[d].chips;
            p->bet_hand = hs->deal[d].bet_hand;
            p->bet_round = hs->deal[d].bet_hand;
            p->hole[0] = hs->deal[d].hole[0];
            p->hole[1] = hs->deal[d].hole[1];
            p->flags |= PN_PF_DEALT;
            if (hs->deal[d].flags & PN_PF_ALLIN) p->flags |= PN_PF_ALLIN;
            if (p->bet_round > cur) cur = p->bet_round;
        }
        st->cur_bet = cur;
        break;
    }
    case E_ACTION_REQ: {
        const pn_evt_action_req_t *a = (const pn_evt_action_req_t *)body;
        st->to_act_seat = a->seat;
        break;
    }
    case E_ACTION: {
        const pn_evt_action_t *a = (const pn_evt_action_t *)body;
        int pid = seat_to_pid(st, a->seat);
        if (pid >= 0) {
            pn_player_t *p = &st->p[pid];
            p->chips = a->chips_left;
            p->bet_round = a->bet_round;
            p->bet_hand = a->bet_hand;
            uint8_t act = a->action & 0x7F;
            if (act == ACT_FOLD) p->flags |= PN_PF_FOLDED;
            if (act == ACT_ALLIN || a->chips_left == 0) p->flags |= PN_PF_ALLIN;
        }
        st->pot = a->pot;
        st->cur_bet = a->cur_bet;
        st->raise_count = a->raise_count;
        st->to_act_seat = a->next_seat;
        break;
    }
    case E_STREET: {
        const pn_evt_street_t *s = (const pn_evt_street_t *)body;
        st->street = s->street;
        st->pot = s->pot;
        st->to_act_seat = s->first_seat;
        st->cur_bet = 0;
        st->min_raise = st->bb ? st->bb : 2;
        st->raise_count = 0;
        for (int i = 0; i < st->n_players && i < 10; i++) st->p[i].bet_round = 0;
        break;
    }
    case E_HAND_RESULT: {
        const pn_evt_hand_result_t *r = (const pn_evt_hand_result_t *)body;
        for (int i = 0; i < 10; i++) {
            if (r->chips_after[i].seat == 0xFF) continue;
            int pid = seat_to_pid(st, r->chips_after[i].seat);
            if (pid < 0) continue;
            st->p[pid].chips = r->chips_after[i].chips;
            if (st->p[pid].chips == 0) st->p[pid].flags |= PN_PF_OUT;
        }
        st->phase = PN_PH_INTERMISSION;
        st->pot = 0; st->to_act_seat = 0xFF;
        break;
    }
    case E_HAND_ABORT: {
        const pn_evt_hand_abort_t *r = (const pn_evt_hand_abort_t *)body;
        for (int i = 0; i < 10; i++) {
            if (r->refund[i].seat == 0xFF) continue;
            int pid = seat_to_pid(st, r->refund[i].seat);
            if (pid < 0) continue;
            st->p[pid].chips = r->refund[i].chips_after;
            st->p[pid].flags &= ~(PN_PF_FOLDED | PN_PF_ALLIN);
        }
        st->phase = PN_PH_INTERMISSION;
        st->pot = 0; st->to_act_seat = 0xFF;
        break;
    }
    case E_PLAYER_JOINED: {
        const pn_evt_player_joined_t *j = (const pn_evt_player_joined_t *)body;
        if (j->player_id < 10) {
            pn_player_t *p = &st->p[j->player_id];
            memcpy(p->mac, j->mac, 6);
            p->seat = 0xFF; p->flags = PN_PF_ONLINE; p->chips = 0;
            p->bet_round = p->bet_hand = 0;
            p->hole[0] = p->hole[1] = 0xFF;
            if (j->player_id + 1 > st->n_players) st->n_players = j->player_id + 1;
        }
        break;
    }
    case E_PLAYER_LEFT: {
        const pn_evt_player_left_t *l = (const pn_evt_player_left_t *)body;
        if (l->player_id < 10) st->p[l->player_id].flags &= ~PN_PF_ONLINE;
        break;
    }
    case E_PLAYER_OFFLINE: {
        const pn_evt_player_id_t *o = (const pn_evt_player_id_t *)body;
        if (o->player_id < 10) st->p[o->player_id].flags &= ~PN_PF_ONLINE;
        break;
    }
    case E_PLAYER_BACK: {
        const pn_evt_player_id_t *o = (const pn_evt_player_id_t *)body;
        if (o->player_id < 10) st->p[o->player_id].flags |= PN_PF_ONLINE;
        break;
    }
    case E_GAME_PAUSE:  st->phase = PN_PH_PAUSED; break;
    case E_GAME_RESUME: st->phase = PN_PH_INTERMISSION; break;
    default: break;   /* E_DEALER_SET/SEATING_DONE/HANDOFF/TAKEOVER/JOIN/REMIND: no st change */
    }
}

/* ---------------- 提交(本地預驗證 + pbus_submit_cmd) ---------------- */
void game_submit_action(uint8_t action, uint16_t raise_to)
{
    pn_cmd_action_t a = { .action = action, .amount = raise_to };
    pbus_submit_cmd(C_ACTION, &a, sizeof(a));
}

void game_submit_chips(uint16_t amount)
{
    if (amount < 1 || amount > PK_CHIPS_MAX) { set_local_reject(PN_RJ_BAD_AMOUNT); return; }
    pn_cmd_chips_t c = { .amount = amount };
    pbus_submit_cmd(C_SET_CHIPS, &c, sizeof(c));
}

void game_submit_blinds(uint8_t sb, uint8_t bb, uint16_t cap)
{
    if (sb < 1 || bb < sb || (cap != 0 && cap < bb)) { set_local_reject(PN_RJ_BAD_CONFIG); return; }
    pn_cmd_blinds_t b = { .sb = sb, .bb = bb, .bet_cap = cap };
    pbus_submit_cmd(C_SET_BLINDS, &b, sizeof(b));
}

void game_submit_dealer_claim(void) { pbus_submit_cmd(C_DEALER_CLAIM, NULL, 0); }
void game_submit_seat_claim(uint8_t seat_no)
{
    pn_cmd_seat_t s = { .seat_no = seat_no };
    pbus_submit_cmd(C_SEAT_CLAIM, &s, sizeof(s));
}
void game_submit_join_decide(uint8_t cand_id, bool allow)
{
    pn_cmd_join_decide_t j = { .player_id = cand_id, .allow = allow ? 1 : 0 };
    pbus_submit_cmd(C_JOIN_DECIDE, &j, sizeof(j));
}
void game_submit_ready_next(void)   { pbus_submit_cmd(C_READY_NEXT, NULL, 0); }
void game_submit_leave(void)        { pbus_submit_cmd(C_LEAVE, NULL, 0); pbus_leave(); }
void game_submit_ceremony_skip(void){ pbus_submit_cmd(C_CEREMONY_SKIP, NULL, 0); }

/* ---------------- 觀察者掛勾 ---------------- */
void game_set_event_hook(game_event_hook_t hook) { s_evt_hook = hook; }
void game_set_link_hook(game_link_hook_t hook)   { s_link_hook = hook; }
void game_set_role_hook(game_role_hook_t hook)   { s_role_hook = hook; }

/* ---------------- pbus 回呼 ---------------- */
static void cb_on_event(const pn_evt_hdr_t *e, const void *b, size_t l)
{
    if (s_evt_hook) s_evt_hook(e, b, l);
}
static void cb_on_role(bool i_am_master)
{
    master_engine_enable(i_am_master);
    if (s_role_hook) s_role_hook(i_am_master);
}
static void cb_on_link(pn_link_state_t s)
{
    if (s == PBUS_LINK_RESYNCED) game_view_publish();
    if (s_link_hook) s_link_hook(s);
}
static uint32_t cb_get_batt(void) { return (uint32_t)hal_battery_pct(); }   /* 裁定 R3 轉接 */

void game_init(const char *my_name)
{
    static pbus_callbacks_t cb;
    cb.on_event = cb_on_event;
    cb.on_role = cb_on_role;
    cb.on_link = cb_on_link;
    cb.get_battery_pct = cb_get_batt;
    game_view_publish();               /* 初始空 view */
    pbus_start(&cb, my_name);
    /* v1.1 真機修訂:必須在 pbus_start 之後註冊 —— pbus_start 開頭 memset(g_pb) 會把
       先註冊的 handler/hook 抹掉(真機實測:所有 CMD 被 REJECT(WRONG_PHASE)、引擎 tick 不跑)。 */
    pbus_set_cmd_handler(master_engine_on_cmd);
    pbus_set_idle_hook(master_engine_tick);
}
