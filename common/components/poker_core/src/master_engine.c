/*
 * master_engine.c -- Master 決策引擎(協定 §8/§9/§11,產品 §3.4)。
 * 只有 enable(true) 後生效;不持有狀態副本,一律讀 pbus_state()。
 * 命令驗證 → 直接 pn_publish_locked(同步套用 reducer)→ 讀回 st 續推。
 */
#include "master_engine.h"
#include "pbus_int.h"
#include "side_pot.h"
#include "hand_eval.h"
#include "hal/hal_misc.h"

static bool     s_enabled;
static uint8_t  s_deck[52];
static int      s_deck_pos;
static uint8_t  s_seat_prompt;     /* 排座:目前提示的 seat */
static bool     s_hand_active;
static uint8_t  s_acted[10];       /* 本街已行動(seat 索引) */
static int64_t  s_deadline;        /* 通用超時錨 (us) */
static bool     s_pending_abort;

/* pn_publish_locked 為同步:套 reducer + 廣播 */
#define ME_PUB(evt, body, len, at) pn_publish_locked((evt), (body), (len), (at))

static uint32_t now_table(void) { return pn_clock_table_now(); }

/* ---- 座位/玩家 helper(讀 pbus_state) ---- */
static const pn_table_state_t *ST(void) { return pbus_state(); }
static int seat_pid(uint8_t seat)
{
    const pn_table_state_t *st = ST();
    if (seat == 0xFF) return -1;
    for (int i = 0; i < st->n_players && i < 10; i++)
        if (st->p[i].seat == seat) return i;
    return -1;
}
static bool seat_active(uint8_t seat)   /* 有玩家、未淘汰 */
{
    int pid = seat_pid(seat);
    if (pid < 0) return false;
    return !(ST()->p[pid].flags & PN_PF_OUT);
}
static uint8_t max_seat(void)
{
    const pn_table_state_t *st = ST();
    uint8_t m = 0;
    for (int i = 0; i < st->n_players && i < 10; i++)
        if (st->p[i].seat != 0xFF && st->p[i].seat > m) m = st->p[i].seat;
    return m;
}
/* 環序找下一個活躍座位(給定 from,不含 from) */
static uint8_t next_active_seat(uint8_t from)
{
    uint8_t n = (uint8_t)(max_seat() + 1);
    for (uint8_t off = 1; off <= n; off++) {
        uint8_t s = (uint8_t)((from + off) % n);
        if (seat_active(s)) return s;
    }
    return from;
}

/* ============ 儀式:莊家確認 → 排座 → 籌碼 → 盲注 ============ */
void master_engine_begin(void)
{
    if (!s_enabled) return;
    s_hand_active = false;
    ME_PUB(E_DEALER_CALL, NULL, 0, now_table() + PN_T_AUDIO_LEAD_MS);
    s_deadline = pn_now_us() + (int64_t)PN_T_CEREMONY_REMIND_MS * 1000;
}

static void start_seating(void)
{
    /* 莊家 seat 0 免認領;從 seat 1 開始逐位提示 */
    s_seat_prompt = 1;
    pn_evt_seat_prompt_t sp = { .seat_no = s_seat_prompt };
    ME_PUB(E_SEAT_PROMPT, &sp, sizeof(sp), now_table() + PN_T_AUDIO_LEAD_MS);
    s_deadline = pn_now_us() + (int64_t)PN_T_CEREMONY_REMIND_MS * 1000;
}

static bool all_seated(void)
{
    const pn_table_state_t *st = ST();
    for (int i = 0; i < st->n_players && i < 10; i++)
        if (st->p[i].seat == 0xFF) return false;
    return true;
}

static void start_chips(void)
{
    ME_PUB(E_SEATING_DONE, NULL, 0, 0);
    ME_PUB(E_CHIPS_PROMPT, NULL, 0, now_table() + PN_T_AUDIO_LEAD_MS);
    s_deadline = pn_now_us() + (int64_t)PN_T_CHIPS_AUTO_MS * 1000;
}

static bool all_chips_set(void)
{
    const pn_table_state_t *st = ST();
    for (int i = 0; i < st->n_players && i < 10; i++)
        if (st->p[i].chips == 0 && !(st->p[i].flags & PN_PF_OUT)) return false;
    return true;
}

static void start_blinds(void)
{
    ME_PUB(E_BLINDS_PROMPT, NULL, 0, now_table() + PN_T_AUDIO_LEAD_MS);
    s_deadline = pn_now_us() + (int64_t)PN_T_CHIPS_AUTO_MS * 1000;
}

/* ============ 發牌 ============ */
static void shuffle(void)
{
    for (int i = 0; i < 52; i++) s_deck[i] = (uint8_t)i;
    for (int i = 51; i > 0; i--) {
        int j = (int)(hal_rand() % (uint32_t)(i + 1));
        uint8_t t = s_deck[i]; s_deck[i] = s_deck[j]; s_deck[j] = t;
    }
    s_deck_pos = 0;
}
static uint8_t draw(void) { return s_deck[s_deck_pos++]; }

static void settle_hand(void);
static void advance_action(void);
static void advance_action_from(uint8_t start);

static uint8_t count_active(void)
{
    const pn_table_state_t *st = ST();
    uint8_t c = 0;
    for (int i = 0; i < st->n_players && i < 10; i++)
        if (st->p[i].seat != 0xFF && !(st->p[i].flags & PN_PF_OUT) && st->p[i].chips > 0) c++;
    return c;
}

static void begin_hand(void)
{
    const pn_table_state_t *st = ST();
    if (count_active() < PK_PAUSE_MIN_ALIVE) {   /* 修:原 `&& <1` 把 <2 判斷廢掉,
                                                    只剩 1 人有籌碼時會誤發 1 人局;應直接暫停 */
        ME_PUB(E_GAME_PAUSE, NULL, 0, 0);
        return;
    }
    shuffle();
    memset(s_acted, 0, sizeof(s_acted));

    /* 按鈕:首局 seat 0,其後左移 */
    uint8_t button = (st->button_seat == 0xFF) ? 0 : next_active_seat(st->button_seat);
    uint8_t ns = (uint8_t)(max_seat() + 1);
    (void)ns;

    /* 決定發牌對象(活躍且有籌碼) */
    uint8_t dealt_seats[10]; int nd = 0;
    for (uint8_t s = 0; s <= max_seat(); s++)
        if (seat_active(s) && ST()->p[seat_pid(s)].chips > 0) dealt_seats[nd++] = s;

    uint8_t sb_seat, bb_seat;
    if (nd == 2) { sb_seat = button; bb_seat = next_active_seat(button); }  /* heads-up */
    else { sb_seat = next_active_seat(button); bb_seat = next_active_seat(sb_seat); }

    /* 組 E_HAND_START */
    pn_evt_hand_start_t hs;
    memset(&hs, 0, sizeof(hs));
    hs.hand_no = (uint8_t)(st->hand_no + 1);
    hs.button_seat = button;
    hs.sb_seat = sb_seat; hs.bb_seat = bb_seat;
    hs.n_dealt = (uint8_t)nd;
    for (int i = 0; i < 5; i++) hs.board[i] = draw();

    uint16_t pot = 0;
    for (int i = 0; i < nd; i++) {
        uint8_t s = dealt_seats[i];
        int pid = seat_pid(s);
        uint16_t chips = ST()->p[pid].chips;
        uint16_t post = 0;
        if (nd >= 2 && s == sb_seat) post = st->sb;
        if (nd >= 2 && s == bb_seat) post = st->bb;
        uint8_t fl = PN_PF_DEALT;
        if (post >= chips) { post = chips; fl |= PN_PF_ALLIN; }  /* 短盲全下 */
        hs.deal[i].seat = s;
        hs.deal[i].hole[0] = draw();
        hs.deal[i].hole[1] = draw();
        hs.deal[i].chips = (uint16_t)(chips - post);
        hs.deal[i].bet_hand = post;
        hs.deal[i].flags = fl;
        pot += post;
    }
    hs.pot = 0;   /* 盲注仍為各家 bet_round 在途,收池時點在 E_STREET(§11.3) */
    (void)pot;
    ME_PUB(E_HAND_START, &hs, sizeof(hs), now_table() + PN_T_AUDIO_LEAD_MS);

    s_hand_active = true;
    memset(s_acted, 0, sizeof(s_acted));

    if (nd < 2) {   /* SOLO/單人:只剩一人未棄 → 立即結算 reason=1 */
        settle_hand();
        return;
    }
    /* preflop 首行動 = bb 左手(heads-up:button=sb 先動)(§11.3/§11.7;
       B2 修:原 advance_action() 從 button 掃起,4+ 人局首行動者錯成按鈕位) */
    advance_action_from((nd == 2) ? sb_seat : next_active_seat(bb_seat));
}

/* ============ 行動輪 ============ */
static bool need_act(uint8_t seat)   /* 該座位仍需行動 */
{
    int pid = seat_pid(seat);
    if (pid < 0) return false;
    const pn_player_t *p = &ST()->p[pid];
    if (p->flags & (PN_PF_FOLDED | PN_PF_ALLIN | PN_PF_OUT)) return false;
    if (!(p->flags & PN_PF_DEALT)) return false;
    if (!s_acted[seat]) return true;
    return p->bet_round < ST()->cur_bet;    /* 未跟到最高注 */
}
static uint8_t count_in_hand(void)   /* 未棄未淘汰且有牌 */
{
    const pn_table_state_t *st = ST();
    uint8_t c = 0;
    for (int i = 0; i < st->n_players && i < 10; i++)
        if ((st->p[i].flags & PN_PF_DEALT) && !(st->p[i].flags & PN_PF_FOLDED)) c++;
    return c;
}
static uint8_t count_can_act(void)
{
    const pn_table_state_t *st = ST();
    uint8_t c = 0;
    for (int i = 0; i < st->n_players && i < 10; i++)
        if ((st->p[i].flags & PN_PF_DEALT) && !(st->p[i].flags & (PN_PF_FOLDED | PN_PF_ALLIN))) c++;
    return c;
}

static void collect_and_next_street(void);

static uint8_t first_actor_of_street(void)
{
    const pn_table_state_t *st = ST();
    uint8_t button = st->button_seat;
    /* postflop:按鈕左手第一個仍在局者;preflop 由 begin 另處理 */
    uint8_t n = (uint8_t)(max_seat() + 1);
    for (uint8_t off = 1; off <= n; off++) {
        uint8_t s = (uint8_t)((button + off) % n);
        if (need_act(s) || (seat_pid(s) >= 0 && (st->p[seat_pid(s)].flags & PN_PF_DEALT) &&
                            !(st->p[seat_pid(s)].flags & PN_PF_FOLDED))) return s;
    }
    return 0xFF;
}

static void issue_action_req(uint8_t seat)
{
    const pn_table_state_t *st = ST();
    int pid = seat_pid(seat);
    if (pid < 0) return;
    const pn_player_t *p = &st->p[pid];
    pn_evt_action_req_t rq;
    rq.seat = seat;
    rq.call_amt = st->cur_bet;
    rq.can_check = (p->bet_round >= st->cur_bet) ? 1 : 0;
    uint16_t allin_ceiling = (uint16_t)(p->bet_round + p->chips);
    uint32_t cap_ceiling = (st->bet_cap == 0) ? 0xFFFFu : (uint32_t)st->cur_bet + st->bet_cap;
    uint16_t maxr = allin_ceiling; if (maxr > cap_ceiling) maxr = (uint16_t)cap_ceiling;
    rq.max_raise_to = maxr;
    /* 標準規則:已行動過(s_acted,自上次「足額加注」後未被重置)的玩家,若只是因不足額
       全下(未重開加注權)而被再次詢問,只能跟注/棄牌,不得再加注。s_acted 於足額加注時
       memset 清零,故此處 s_acted[seat] 仍為 1 即代表「面對不完整加注的補跟」→ 禁加注。 */
    if (st->raise_count >= PN_RAISE_PER_STREET || s_acted[seat]) rq.min_raise_to = 0;
    else { uint16_t m = (uint16_t)(st->cur_bet + st->min_raise); rq.min_raise_to = (m > maxr) ? 0 : m; }
    ME_PUB(E_ACTION_REQ, &rq, sizeof(rq), 0);
    s_deadline = pn_now_us() + (int64_t)PN_T_ACT_REMIND_MS * 1000;
}

/* 決定下一步:發 E_ACTION_REQ 或收池進街 或結算(B2:可指定掃描起點) */
static void advance_action_from(uint8_t start)
{
    if (count_in_hand() <= 1) { settle_hand(); return; }   /* 只剩一人 */
    if (count_can_act() == 0) { collect_and_next_street(); return; }  /* 全 all-in → runout */

    uint8_t n = (uint8_t)(max_seat() + 1);
    for (uint8_t off = 0; off <= n; off++) {
        uint8_t s = (uint8_t)((start + off) % n);
        if (need_act(s)) { issue_action_req(s); return; }
    }
    /* 無人需行動 → 收池進下一街 */
    collect_and_next_street();
}

static void advance_action(void)
{
    const pn_table_state_t *st = ST();
    advance_action_from((st->to_act_seat != 0xFF) ? st->to_act_seat : st->button_seat);
}

static void collect_and_next_street(void)
{
    const pn_table_state_t *st = ST();
    /* 收池:pot += Σ bet_round */
    uint32_t pot = st->pot;
    for (int i = 0; i < st->n_players && i < 10; i++) pot += st->p[i].bet_round;

    if (st->street >= 3) { settle_hand(); return; }   /* river 結束 → 攤牌 */

    uint8_t next_street = (uint8_t)(st->street + 1);
    uint8_t first = (count_can_act() >= 1) ? first_actor_of_street() : 0xFF;
    uint32_t play_at = now_table() + PN_T_AUDIO_LEAD_MS;
    pn_evt_street_t ev = { .street = next_street, .pot = (uint16_t)pot, .first_seat = first };
    ME_PUB(E_STREET, &ev, sizeof(ev), play_at);
    memset(s_acted, 0, sizeof(s_acted));

    if (first == 0xFF) {
        /* runout:繼續發完剩餘街道(遞迴,play_at 間隔 ≥3500ms 由呼叫時序近似) */
        if (next_street < 3) { collect_and_next_street(); }
        else settle_hand();
    } else {
        issue_action_req(first);
    }
}

/* ============ 結算(側池) ============ */
static void settle_hand(void)
{
    const pn_table_state_t *st = ST();
    sp_player_t sp[10];
    memset(sp, 0, sizeof(sp));
    for (int s = 0; s < 10; s++) {
        int pid = seat_pid((uint8_t)s);
        if (pid < 0) continue;
        const pn_player_t *p = &st->p[pid];
        if (!(p->flags & PN_PF_DEALT)) continue;
        sp[s].present = true;
        sp[s].folded = (p->flags & PN_PF_FOLDED) ? true : false;
        /* 尚未收池的 bet_round 也算入 bet_hand */
        sp[s].bet_hand = p->bet_hand;
        sp[s].hole[0] = p->hole[0];
        sp[s].hole[1] = p->hole[1];
    }
    uint16_t win[10]; bool winner[10];
    side_pot_settle(sp, st->board, st->button_seat, win, winner);

    uint8_t in_hand = count_in_hand();

    pn_evt_hand_result_t r;
    memset(&r, 0, sizeof(r));
    r.hand_no = st->hand_no;
    r.reason = (in_hand <= 1) ? 1 : 0;
    r.slot_gap_ms = PK_SLOT_GAP_MS_DEFAULT;

    int nshow = 0;
    for (int i = 0; i < 10; i++) { r.payout[i].seat = 0xFF; r.chips_after[i].seat = 0xFF; }

    int oi = 0;
    for (int s = 0; s < 10; s++) {
        if (!sp[s].present) continue;
        int pid = seat_pid((uint8_t)s);
        r.payout[oi].seat = (uint8_t)s;
        r.payout[oi].win = win[s];
        r.chips_after[oi].seat = (uint8_t)s;
        r.chips_after[oi].chips = (uint16_t)(st->p[pid].chips + win[s]);
        oi++;
        if (r.reason == 0 && !sp[s].folded) {
            uint8_t c7[7] = { sp[s].hole[0], sp[s].hole[1],
                              st->board[0], st->board[1], st->board[2], st->board[3], st->board[4] };
            hand_rank_t hr = hand_eval7(c7);
            r.show[nshow].seat = (uint8_t)s;
            r.show[nshow].rank_cat = hr.cat;
            r.show[nshow].is_winner = winner[s] ? 1 : 0;
            nshow++;
        }
    }
    r.n_show = (uint8_t)nshow;

    ME_PUB(E_HAND_RESULT, &r, sizeof(r), now_table() + PN_T_AUDIO_LEAD_MS);
    s_hand_active = false;
    s_deadline = pn_now_us() + (int64_t)PN_T_READY_AUTO_MS * 1000;
}

/* ============ 局間 → 下一局 ============ */
static void start_next_hand(void)
{
    /* 低電豁免略:此處直接由本機續任 Master 開新局(§11.5 簡化) */
    begin_hand();
}

/* ============ 命令裁決 ============ */
static pbus_cmd_verdict_t verdict(uint8_t result, uint8_t reason)
{
    pbus_cmd_verdict_t v = { result, reason }; return v;
}

pbus_cmd_verdict_t master_engine_on_cmd(uint8_t player_id, uint8_t cmd,
                                        const void *arg, size_t len)
{
    (void)len;
    if (!s_enabled) return verdict(PN_CMD_NOT_MASTER, 0);
    const pn_table_state_t *st = ST();

    switch (cmd) {
    case C_DEALER_CLAIM:
        if (st->phase != PN_PH_DEALER_CALL) return verdict(PN_CMD_STALE, 0);
        {
            pn_evt_dealer_set_t ds = { .player_id = player_id };
            ME_PUB(E_DEALER_SET, &ds, sizeof(ds), 0);
            pn_evt_seat_set_t ss = { .seat_no = 0, .player_id = player_id, .is_auto = 0 };
            ME_PUB(E_SEAT_SET, &ss, sizeof(ss), 0);   /* 莊家 = seat 0 */
            if (all_seated()) start_chips(); else start_seating();
        }
        return verdict(PN_CMD_OK, 0);

    case C_SEAT_CLAIM: {
        if (st->phase != PN_PH_SEATING) return verdict(PN_CMD_STALE, 0);
        const pn_cmd_seat_t *sc = (const pn_cmd_seat_t *)arg;
        if (player_id < 10 && st->p[player_id].seat != 0xFF) return verdict(PN_CMD_REJECT, PN_RJ_SEAT_TAKEN);
        if (seat_pid(sc->seat_no) >= 0) return verdict(PN_CMD_REJECT, PN_RJ_SEAT_TAKEN);
        pn_evt_seat_set_t ss = { .seat_no = sc->seat_no, .player_id = player_id, .is_auto = 0 };
        ME_PUB(E_SEAT_SET, &ss, sizeof(ss), 0);
        if (all_seated()) start_chips();
        else {
            s_seat_prompt = (uint8_t)(s_seat_prompt + 1);
            pn_evt_seat_prompt_t sp = { .seat_no = s_seat_prompt };
            ME_PUB(E_SEAT_PROMPT, &sp, sizeof(sp), now_table() + PN_T_AUDIO_LEAD_MS);
        }
        return verdict(PN_CMD_OK, 0);
    }

    case C_SET_CHIPS: {
        const pn_cmd_chips_t *cc = (const pn_cmd_chips_t *)arg;
        if (cc->amount < 1 || cc->amount > PK_CHIPS_MAX) return verdict(PN_CMD_REJECT, PN_RJ_BAD_AMOUNT);
        pn_evt_chips_set_t cs = { .player_id = player_id, .amount = cc->amount, .is_auto = 0 };
        ME_PUB(E_CHIPS_SET, &cs, sizeof(cs), 0);
        if (st->phase == PN_PH_CHIPS && all_chips_set()) start_blinds();
        return verdict(PN_CMD_OK, 0);
    }

    case C_SET_BLINDS: {
        if (st->phase != PN_PH_BLINDS) return verdict(PN_CMD_STALE, 0);
        const pn_cmd_blinds_t *cb = (const pn_cmd_blinds_t *)arg;
        if (cb->sb < 1 || cb->bb < cb->sb || (cb->bet_cap != 0 && cb->bet_cap < cb->bb))
            return verdict(PN_CMD_REJECT, PN_RJ_BAD_CONFIG);
        pn_evt_table_config_t tc = { .sb = cb->sb, .bb = cb->bb, .bet_cap = cb->bet_cap, .is_auto = 0 };
        ME_PUB(E_TABLE_CONFIG, &tc, sizeof(tc), 0);
        begin_hand();
        return verdict(PN_CMD_OK, 0);
    }

    case C_ACTION: {
        if (st->phase != PN_PH_HAND) return verdict(PN_CMD_STALE, 0);
        const pn_cmd_action_t *ca = (const pn_cmd_action_t *)arg;
        int pid = player_id;
        if (pid < 0 || pid >= 10) return verdict(PN_CMD_REJECT, PN_RJ_WRONG_PHASE);
        uint8_t seat = st->p[pid].seat;
        if (seat != st->to_act_seat) return verdict(PN_CMD_REJECT, PN_RJ_NOT_YOUR_TURN);

        const pn_player_t *p = &st->p[pid];
        uint16_t to_amt = p->bet_round;         /* 本輪投注最終總額 */
        uint8_t act = ca->action;
        uint16_t chips = p->chips;

        if (act == ACT_FOLD) {
            to_amt = p->bet_round;
        } else if (act == ACT_CHECK) {
            if (p->bet_round < st->cur_bet) return verdict(PN_CMD_REJECT, PN_RJ_NOT_YOUR_TURN);
        } else if (act == ACT_CALL) {
            uint16_t need = (st->cur_bet > p->bet_round) ? (uint16_t)(st->cur_bet - p->bet_round) : 0;
            if (need > chips) need = chips;      /* 不足額全下 */
            to_amt = (uint16_t)(p->bet_round + need);
        } else if (act == ACT_BET || act == ACT_RAISE) {
            to_amt = ca->amount;
            uint16_t ceiling = (uint16_t)(p->bet_round + chips);
            if (to_amt > ceiling) to_amt = ceiling;
            if (to_amt <= st->cur_bet && to_amt < ceiling) return verdict(PN_CMD_REJECT, PN_RJ_BAD_AMOUNT);
            if (st->raise_count >= PN_RAISE_PER_STREET && to_amt < ceiling)
                return verdict(PN_CMD_REJECT, PN_RJ_RAISE_LIMIT);
            /* 權威防線:已行動過(自上次足額加注未重置)又想加注 → 面對不完整全下時只能跟/棄
               (成員 UI 從複製狀態自算 min_raise_to、不知 s_acted,故引擎必須擋) */
            if (s_acted[seat] && to_amt > st->cur_bet && to_amt < ceiling)
                return verdict(PN_CMD_REJECT, PN_RJ_RAISE_LIMIT);
        } else if (act == ACT_ALLIN) {
            to_amt = (uint16_t)(p->bet_round + chips);
        } else {
            return verdict(PN_CMD_REJECT, PN_RJ_BAD_AMOUNT);
        }

        /* 計算權威結果 */
        uint16_t added = (to_amt > p->bet_round) ? (uint16_t)(to_amt - p->bet_round) : 0;
        if (added > chips) added = chips;
        uint16_t new_chips = (uint16_t)(chips - added);
        uint16_t new_bet_round = (uint16_t)(p->bet_round + added);
        uint16_t new_bet_hand = (uint16_t)(p->bet_hand + added);
        uint16_t new_cur = st->cur_bet;
        uint8_t  new_rc = st->raise_count;
        bool aggressive = (act == ACT_BET || act == ACT_RAISE ||
                           (act == ACT_ALLIN && new_bet_round > st->cur_bet));
        if (new_bet_round > new_cur) {
            /* 是否足額加注(重開加注權) */
            if (new_bet_round - st->cur_bet >= st->min_raise || act == ACT_BET) {
                if (aggressive) { new_rc = (uint8_t)(st->raise_count + 1); memset(s_acted, 0, sizeof(s_acted)); }
            }
            new_cur = new_bet_round;
        }

        pn_evt_action_t ev;
        ev.seat = seat;
        ev.action = act;
        ev.amount = to_amt;
        ev.chips_left = new_chips;
        ev.bet_round = new_bet_round;
        ev.bet_hand = new_bet_hand;
        ev.pot = st->pot;
        ev.cur_bet = new_cur;
        ev.raise_count = new_rc;
        s_acted[seat] = 1;
        ev.next_seat = 0xFF;   /* 由 reducer 後 advance_action 決定;先填 0xFF */
        ME_PUB(E_ACTION, &ev, sizeof(ev), 0);

        advance_action();
        return verdict(PN_CMD_OK, 0);
    }

    case C_READY_NEXT:
        if (st->phase != PN_PH_INTERMISSION) return verdict(PN_CMD_STALE, 0);
        start_next_hand();
        return verdict(PN_CMD_OK, 0);

    case C_JOIN_DECIDE: {
        const pn_cmd_join_decide_t *jd = (const pn_cmd_join_decide_t *)arg;
        pn_evt_join_decided_t ev = { .cand_id = jd->player_id, .allow = jd->allow, .player_id = jd->player_id };
        ME_PUB(E_JOIN_DECIDED, &ev, sizeof(ev), 0);
        return verdict(PN_CMD_OK, 0);
    }

    case C_LEAVE: {
        pn_evt_player_left_t ev = { .player_id = player_id, .reason = 1 };
        ME_PUB(E_PLAYER_LEFT, &ev, sizeof(ev), 0);
        return verdict(PN_CMD_OK, 0);
    }

    case C_CEREMONY_SKIP: {
        if (st->phase == PN_PH_SEATING) {
            /* 依 player_id 順序自動指派剩餘座位 */
            uint8_t next = s_seat_prompt;
            for (int i = 0; i < st->n_players && i < 10; i++) {
                if (st->p[i].seat == 0xFF) {
                    pn_evt_seat_set_t ss = { .seat_no = next++, .player_id = (uint8_t)i, .is_auto = 1 };
                    ME_PUB(E_SEAT_SET, &ss, sizeof(ss), 0);
                }
            }
            start_chips();
        }
        return verdict(PN_CMD_OK, 0);
    }

    default:
        return verdict(PN_CMD_REJECT, PN_RJ_WRONG_PHASE);
    }
}

/* ============ tick:超時/自動 ============ */
void master_engine_enable(bool on)
{
    s_enabled = on;
    if (on) {
        /* 接管上任:若局中 → 作廢退注(§9.5/§11.6) */
        if (ST()->phase == PN_PH_HAND) s_pending_abort = true;
    }
}

static void do_abort(void)
{
    const pn_table_state_t *st = ST();
    pn_evt_hand_abort_t ab;
    memset(&ab, 0, sizeof(ab));
    for (int i = 0; i < 10; i++) ab.refund[i].seat = 0xFF;   /* B3:空槽哨兵(seat=0 會誤清 seat0 籌碼,真機踩中) */
    ab.hand_no = st->hand_no;
    ab.reason = 0;
    int oi = 0;
    for (int s = 0; s < 10; s++) {
        int pid = seat_pid((uint8_t)s);
        if (pid < 0) continue;
        const pn_player_t *p = &st->p[pid];
        if (!(p->flags & PN_PF_DEALT)) continue;
        ab.refund[oi].seat = (uint8_t)s;
        ab.refund[oi].refund = p->bet_hand;
        ab.refund[oi].chips_after = (uint16_t)(p->chips + p->bet_hand);
        oi++;
    }
    ME_PUB(E_HAND_ABORT, &ab, sizeof(ab), now_table() + PN_T_AUDIO_LEAD_MS);
    s_hand_active = false;
}

void master_engine_tick(void)
{
    if (!s_enabled) return;
    if (s_pending_abort) { s_pending_abort = false; do_abort(); return; }

    int64_t now = pn_now_us();
    const pn_table_state_t *st = ST();

    switch (st->phase) {
    case PN_PH_DEALER_CALL:
        if (now > s_deadline) {   /* 儀式重播提示 */
            pn_evt_remind_t rm = { .target_seat = 0xFF, .kind = 2 };
            ME_PUB(E_REMIND, &rm, sizeof(rm), now_table() + PN_T_AUDIO_LEAD_MS);
            s_deadline = now + (int64_t)PN_T_CEREMONY_REMIND_MS * 1000;
        }
        break;
    case PN_PH_CHIPS:
        if (now > s_deadline) {   /* 60s 代設預設 15 */
            for (int i = 0; i < st->n_players && i < 10; i++) {
                if (st->p[i].chips == 0 && !(st->p[i].flags & PN_PF_OUT)) {
                    pn_evt_chips_set_t cs = { .player_id = (uint8_t)i, .amount = PK_CHIPS_DEFAULT, .is_auto = 1 };
                    ME_PUB(E_CHIPS_SET, &cs, sizeof(cs), 0);
                }
            }
            if (all_chips_set()) start_blinds();
        }
        break;
    case PN_PH_BLINDS:
        if (now > s_deadline) {   /* 代設預設 1/2/10 */
            pn_evt_table_config_t tc = { .sb = 1, .bb = 2, .bet_cap = 10, .is_auto = 1 };
            ME_PUB(E_TABLE_CONFIG, &tc, sizeof(tc), 0);
            begin_hand();
        }
        break;
    case PN_PH_HAND:
        if (s_hand_active && st->to_act_seat != 0xFF && now > s_deadline) {
            /* 在線催促(不代打);離線代打從略,發提醒 */
            pn_evt_remind_t rm = { .target_seat = st->to_act_seat, .kind = 1 };
            ME_PUB(E_REMIND, &rm, sizeof(rm), 0);
            s_deadline = now + (int64_t)PN_T_ACT_REMIND_MS * 1000;
        }
        break;
    case PN_PH_INTERMISSION:
        if (now > s_deadline) start_next_hand();   /* C_READY_NEXT 30s 自動 */
        break;
    default: break;
    }
}
