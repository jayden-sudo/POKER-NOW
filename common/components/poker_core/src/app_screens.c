/*
 * app_screens.c -- screen_from_state()(產品 §4.5)+ 各畫面 → hal_screen_t 組裝(§2.2)。
 */
#include "app_ui.h"
#include "pbus.h"
#include "pk_config.h"
#include "esp_log.h"
#include "hal/hal_input.h"
#include <stdio.h>
#include <string.h>

/* ---- UI 語義 trace(串口測試通道 TX 面;每次 render 一行,主機端可全自動觀測)---- */
static const char *SCR_NAME[] = {
    "BOOT","SCANNING","LOBBY","PENDING_JOIN","DEALER_CALL","SEATING","WAIT",
    "CHIPS_SET","BLINDS","HAND_DEALT","MY_TURN","AMOUNT_PICK","WAIT_TURN","SPECTATE",
    "RESULT","ABORT","INTERMISSION","INTERMISSION_DEALER","REBUY","PAUSED","SYSMENU",
};
static void card_txt(hal_card_t c, bool up, char out[4])
{
    if (c == 0xFF) { out[0]='-'; out[1]='-'; out[2]=0; return; }
    if (!up)       { out[0]='#'; out[1]='#'; out[2]=0; return; }
    out[0] = "23456789TJQKA"[c >> 2]; out[1] = "cdhs"[c & 3]; out[2] = 0;
}
static void ui_trace(const hal_screen_t *sc)
{
    char cd[7][4];
    for (int i = 0; i < 7; i++) card_txt(sc->cards[i], sc->cards_face_up & (1u << i), cd[i]);
    ESP_LOGI("ui", "[%s] t=\"%s\" g=\"%s\" big=\"%s\" pot=%u my=%u hand=%s,%s board=%s,%s,%s,%s,%s ok=\"%s\" nx=\"%s\" L0=\"%s\"",
             SCR_NAME[g_ui.screen], sc->title ? sc->title : "",
             sc->guide ? sc->guide : "", sc->big ? sc->big : "",
             sc->pot, sc->my_chips, cd[0], cd[1], cd[2], cd[3], cd[4], cd[5], cd[6],
             sc->hint_ok ? sc->hint_ok : "", sc->hint_next ? sc->hint_next : "",
             sc->lines[0] ? sc->lines[0] : "");
    if (sc->modal_title)
        ESP_LOGI("ui", "  MODAL t=\"%s\" v=\"%s\" h0=\"%s\" h1=\"%s\" h2=\"%s\" danger=%u",
                 sc->modal_title, sc->modal_value ? sc->modal_value : "",
                 sc->modal_hint[0] ? sc->modal_hint[0] : "",
                 sc->modal_hint[1] ? sc->modal_hint[1] : "",
                 sc->modal_hint[2] ? sc->modal_hint[2] : "", sc->modal_danger);
}


/* v2.1:用「裝置自報的觸發標籤」組合提示文字(2 鍵/3 鍵/鍵盤各異) */
static const char *H(hal_ui_event_t ev) { return hal_input_hint(ev); }
static const char *hint2(const char *m1, hal_ui_event_t e1, const char *m2, hal_ui_event_t e2)
{
    static char b[4][48]; static int bi; char *o = b[bi = (bi + 1) & 3];
    snprintf(o, 48, "%s:%s   %s:%s", H(e1), m1, H(e2), m2);
    return o;
}
static const char *hint1(const char *m1, hal_ui_event_t e1)
{
    static char b[2][40]; static int bi; char *o = b[bi = (bi + 1) & 1];
    snprintf(o, 40, "%s:%s", H(e1), m1);
    return o;
}

static char s_title[40], s_big[16];
static char s_line[6][40];

static const game_view_t *V(void) { return game_view(); }

static uint16_t my_chips(const game_view_t *v)
{
    if (v->my_player_id < 10) return v->st->p[v->my_player_id].chips;
    return 0;
}
static bool my_flag(const game_view_t *v, uint8_t f)
{
    return v->my_player_id < 10 && (v->st->p[v->my_player_id].flags & f);
}
static bool my_eliminated(const game_view_t *v) { return my_flag(v, PN_PF_OUT); }
static bool my_folded(const game_view_t *v)     { return my_flag(v, PN_PF_FOLDED); }
static bool my_dealt(const game_view_t *v)      { return my_flag(v, PN_PF_DEALT); }

const char *reject_phrase(uint8_t reason)
{
    switch (reason) {
    case PN_RJ_NOT_YOUR_TURN: return "Not your turn";
    case PN_RJ_BAD_AMOUNT:    return "Invalid amount";
    case PN_RJ_CAP_EXCEEDED:  return "Over the cap";
    case PN_RJ_RAISE_LIMIT:   return "Raise limit";
    case PN_RJ_NOT_DEALER:    return "Dealer only";
    case PN_RJ_SEAT_TAKEN:    return "Seat taken";
    case PN_RJ_TABLE_FULL:    return "Table full";
    case PN_RJ_BAD_CONFIG:    return "Invalid blinds";
    case PN_RJ_WRONG_PHASE:   return "Not now";
    default: return NULL;
    }
}

screen_t screen_from_state(void)
{
    const game_view_t *v = V();
    const pn_table_state_t *st = v->st;
    /* v1.1 真機修訂:未入桌(掃描/加入中/被解散後)一律顯示 SCANNING ——
       否則清零狀態的 phase=0 會被誤畫成「LOBBY 0 players」(真機實測踩中)。 */
    if (v->my_player_id == 0xFF) return SCR_SCANNING;
    /* B1 修:首局 button_seat 尚未設定(發牌才定),此前「莊家」= 排座 seat 0 ——
       否則 BLINDS 畫面無人可達,全桌卡到 60s 超時代設。 */
    uint8_t btn = (st->button_seat != 0xFF) ? st->button_seat : 0;
    bool dealer = (v->my_seat != 0xFF && v->my_seat == btn);
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

/* 卡牌 face_up:preflop 0,flop 3,turn 4,river/showdown 5 張公共牌 */
static uint8_t board_faceup_count(uint8_t street)
{
    switch (street) { case 0: return 0; case 1: return 3; case 2: return 4; default: return 5; }
}

static void fill_cards(hal_screen_t *sc, const game_view_t *v, bool show_hole)
{
    for (int i = 0; i < 7; i++) sc->cards[i] = 0xFF;
    sc->cards_face_up = 0;
    if (show_hole && v->my_player_id < 10) {
        sc->cards[0] = v->st->p[v->my_player_id].hole[0];
        sc->cards[1] = v->st->p[v->my_player_id].hole[1];
        if (sc->cards[0] != 0xFF) sc->cards_face_up |= (1u << 0);
        if (sc->cards[1] != 0xFF) sc->cards_face_up |= (1u << 1);
    }
    uint8_t up = board_faceup_count(v->revealed_streets);
    for (int i = 0; i < 5; i++) {
        sc->cards[2 + i] = v->st->board[i];
        if (i < up) sc->cards_face_up |= (1u << (2 + i));
    }
}

static void base_screen(hal_screen_t *sc)
{
    memset(sc, 0, sizeof(*sc));
    sc->title = s_title;
    sc->title_color = TC_NORMAL;
    sc->battery_pct = g_ui.batt_pct;
    if (g_ui.link_lost) sc->title_flags |= TF_LINK_LOST;
    if (game_view()->cmd_inflight) sc->title_flags |= TF_SUBMITTING;
    for (int i = 0; i < 7; i++) sc->cards[i] = 0xFF;
    sc->progress = 0xFF;
    for (int i = 0; i < 6; i++) sc->lines[i] = NULL;
}

/* v1.3:遊戲中五區版式資料(POT=池底+在途注;MY CHIPS) */
static void fill_stakes(hal_screen_t *sc, const game_view_t *v)
{
    const pn_table_state_t *st = v->st;
    uint32_t pot = st->pot;
    for (int i = 0; i < st->n_players && i < 10; i++) pot += st->p[i].bet_round;
    sc->show_stakes = 1;
    sc->pot = (uint16_t)pot;
    sc->my_chips = my_chips(v);
}

/* 總覽壓縮:兩人一行(§4.4 簡化) */
__attribute__((unused)) static void fill_overview(hal_screen_t *sc, const game_view_t *v)  /* v1.3 後備用 */
{
    const pn_table_state_t *st = v->st;
    static char ov[3][40];
    int line = 0, col = 0; ov[0][0] = ov[1][0] = ov[2][0] = 0;
    char cell[20];
    for (int s = 0; s <= 9 && line < 3; s++) {
        int pid = -1;
        for (int i = 0; i < st->n_players && i < 10; i++) if (st->p[i].seat == s) { pid = i; break; }
        if (pid < 0) continue;
        char mk = ' ';
        uint8_t f = st->p[pid].flags;
        if (f & PN_PF_OUT) mk = 'X'; else if (f & PN_PF_FOLDED) mk = 'F';
        else if (f & PN_PF_ALLIN) mk = 'A'; else if (!(f & PN_PF_ONLINE)) mk = '?';
        else if (st->to_act_seat == s) mk = '*';
        snprintf(cell, sizeof(cell), "P%d:%u%c ", s, st->p[pid].chips, mk);
        strncat(ov[line], cell, sizeof(ov[line]) - strlen(ov[line]) - 1);
        if (++col >= 2) { col = 0; sc->lines[line] = ov[line]; line++; }
    }
    if (col && line < 3) sc->lines[line] = ov[line];
}


/* v1.5:排行榜(結算/局間;籌碼降序,標記 YOU 與本局贏額) */
static void fill_standings(hal_screen_t *sc, const game_view_t *v)
{
    const pn_table_state_t *st = v->st;
    const pn_evt_hand_result_t *res = app_result();
    struct { int pid; uint16_t chips; } r[10];
    int n = 0;
    for (int i = 0; i < st->n_players && i < 10; i++)
        if (st->p[i].seat != 0xFF) { r[n].pid = i; r[n].chips = st->p[i].chips; n++; }
    for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++)
            if (r[b].chips > r[a].chips) {
                int tp = r[a].pid; uint16_t tc = r[a].chips;
                r[a].pid = r[b].pid; r[a].chips = r[b].chips;
                r[b].pid = tp; r[b].chips = tc;
            }
    for (int i = 0; i < n && i < 6; i++) {
        uint8_t seat = st->p[r[i].pid].seat;
        uint16_t win = 0;
        if (res)
            for (int k = 0; k < 10; k++)
                if (res->payout[k].seat == seat) { win = res->payout[k].win; break; }
        int len = snprintf(s_line[i], sizeof(s_line[i]), "%d. P%u   $%u%s",
                           i + 1, seat, r[i].chips,
                           (r[i].pid == v->my_player_id) ? "  YOU" : "");
        if (win && len > 0 && len < (int)sizeof(s_line[i]) - 8)
            snprintf(s_line[i] + len, sizeof(s_line[i]) - len, "  +$%u", win);
        sc->lines[i] = s_line[i];
    }
    sc->deco = DECO_STANDINGS;
}

void app_render_current(void)
{
    const game_view_t *v = V();
    hal_screen_t sc;
    base_screen(&sc);

    switch (g_ui.screen) {
    case SCR_BOOT:
        snprintf(s_title, sizeof(s_title), "POKER");
        sc.deco = DECO_SPLASH; break;
    case SCR_SCANNING:
        snprintf(s_title, sizeof(s_title), "%s", "SCANNING");
        sc.guide = "SEARCHING NEARBY TABLES...";
        sc.deco = DECO_SPLASH;
        sc.hint_ok = NULL; sc.hint_next = NULL; break;
    case SCR_LOBBY:
        snprintf(s_title, sizeof(s_title), "LOBBY");
        snprintf(s_big, sizeof(s_big), "%d / 10", v->st->n_players);
        sc.big = s_big;
        sc.deco = DECO_SPLASH;
        sc.guide = "WAITING FOR PLAYERS..."; break;
    case SCR_PENDING_JOIN:
        snprintf(s_title, sizeof(s_title), "JOINING");
        sc.guide = "WAITING FOR DEALER APPROVAL";
        sc.deco = DECO_SPLASH;
        sc.hint_next = hint1("Menu", UI_MENU); break;
    case SCR_DEALER_CALL:
        snprintf(s_title, sizeof(s_title), "CONFIRM DEALER");
        sc.deco = DECO_PANEL;
        sc.big = "I'M DEALER";
        sc.lines[5] = hint1("I am the dealer", UI_OK); break;   /* 面板最底行(遠離置中大字,不重疊) */
    case SCR_SEATING:
        snprintf(s_title, sizeof(s_title), "SEATING");
        sc.guide = hint1("Claim this seat", UI_OK);
        sc.deco = DECO_PANEL;
        sc.lines[1] = "Claim in seating order,";
        sc.lines[2] = "starting left of dealer"; break;
    case SCR_WAIT:
        snprintf(s_title, sizeof(s_title), "WAIT");
        sc.guide = "PLEASE WAIT...";
        sc.deco = DECO_PANEL;
        sc.lines[1] = "Other players setting up"; break;
    case SCR_CHIPS_SET: {   /* v2.3:數值輸入統一走模態版式(底部提示堆疊,永不與金額重疊) */
        snprintf(s_title, sizeof(s_title), "YOUR CHIPS");
        snprintf(s_big, sizeof(s_big), "$%u", g_ui.edit_value);
        sc.modal_title = "SET CHIPS";
        sc.modal_value = s_big;
        static char c_adj[36];
        snprintf(c_adj, sizeof(c_adj), "%s +1   %s -1", H(UI_UP), H(UI_DOWN));
        sc.modal_hint[0] = hint1("Confirm", UI_OK);
        sc.modal_hint[1] = c_adj;
        break;
    }
    case SCR_BLINDS: {
        const char *fn = g_ui.blinds_field == 0 ? "SMALL BLIND" :
                         g_ui.blinds_field == 1 ? "BIG BLIND" : "BET CAP";
        snprintf(s_title, sizeof(s_title), "BLINDS");
        uint16_t val = g_ui.blinds_field == 0 ? g_ui.bf_sb : g_ui.blinds_field == 1 ? g_ui.bf_bb : g_ui.bf_cap;
        snprintf(s_big, sizeof(s_big), "$%u", val);
        sc.modal_title = fn;
        sc.modal_value = s_big;
        static char b_ok[28], b_adj[36], b_bk[28];
        snprintf(b_ok, sizeof(b_ok), "%s: %s", H(UI_OK), g_ui.blinds_field < 2 ? "Next" : "Start");
        snprintf(b_adj, sizeof(b_adj), "%s +1   %s -1", H(UI_UP), H(UI_DOWN));
        snprintf(b_bk, sizeof(b_bk), "%s: %s", H(UI_BACK), g_ui.blinds_field == 0 ? "Menu" : "Back");
        sc.modal_hint[0] = b_bk;
        sc.modal_hint[1] = b_ok;
        sc.modal_hint[2] = b_adj;
        break;
    }
    case SCR_HAND_DEALT:
        snprintf(s_title, sizeof(s_title), "HAND %d", v->st->hand_no);
        fill_cards(&sc, v, true); fill_stakes(&sc, v);
        sc.guide = "HOLE CARDS DEALT"; sc.hint_ok = H(UI_OK); break;
    case SCR_MY_TURN: {
        snprintf(s_title, sizeof(s_title), "YOUR TURN");
        sc.title_color = TC_TURN;
        fill_cards(&sc, v, true); fill_stakes(&sc, v);
        if (v->call_amt) snprintf(s_line[0], sizeof(s_line[0]), "CALL $%u", v->call_amt);
        else             snprintf(s_line[0], sizeof(s_line[0]), "CHECK FREE");
        sc.guide = s_line[0];   /* v1.7:縮短(操作性由滾輪自身表達,消除頂部兩行長黃字) */
        const char *labels[AS_MAX] = { v->can_check ? "Check" : "Call", "Bet/Raise", "Fold", "All-in" };
        int cur = (g_ui.cursor < g_ui.act_n) ? g_ui.cursor : 0;
        int it = g_ui.act_items[cur];
        snprintf(s_big, sizeof(s_big), "%s", labels[it]);
        sc.big = s_big;
        /* v1.6 行動滾輪:上下顯示前/後動作(環形) */
        if (g_ui.act_n > 1) {
            sc.act_prev = labels[g_ui.act_items[(cur + g_ui.act_n - 1) % g_ui.act_n]];
            sc.act_next = labels[g_ui.act_items[(cur + 1) % g_ui.act_n]];
        } else { sc.act_prev = ""; sc.act_next = ""; }
        sc.hint_ok = NULL; sc.hint_next = NULL;   /* v1.6:滾輪+指引行已足,去雜訊 */
        if (g_ui.confirm_pending) {   /* v1.8:破壞性動作 → 模態確認框(2 行堆疊) */
            sc.modal_title = (g_ui.confirm_action == ACT_FOLD) ? "FOLD?" : "ALL-IN?";
            sc.modal_hint[0] = hint1("Yes, do it", UI_OK);
            sc.modal_hint[1] = hint1("No, cancel", UI_BACK);
            sc.modal_danger = 1;
        }
        break;
    }
    case SCR_AMOUNT_PICK: {  /* v2.2:下注金額 → 乾淨模態,提示 3 行堆疊不重疊 */
        snprintf(s_title, sizeof(s_title), "YOUR TURN");
        sc.title_color = TC_TURN;
        fill_cards(&sc, v, true); fill_stakes(&sc, v);
        snprintf(s_big, sizeof(s_big), "$%u", g_ui.edit_value);
        sc.modal_title = "RAISE TO";
        sc.modal_value = s_big;
        static char mo[28], ma[36];
        snprintf(mo, sizeof(mo), "%s: Confirm", H(UI_OK));
        snprintf(ma, sizeof(ma), "%s +1   %s -1", H(UI_UP), H(UI_DOWN));
        sc.modal_hint[0] = hint1("Cancel", UI_BACK);   /* 上:取消 */
        sc.modal_hint[1] = mo;                          /* 中:確認 */
        sc.modal_hint[2] = ma;                          /* 下:±金額 */
        break;
    }
    case SCR_WAIT_TURN:
        snprintf(s_title, sizeof(s_title), "WAITING");
        fill_cards(&sc, v, true); fill_stakes(&sc, v);
        snprintf(s_line[3], sizeof(s_line[3]), "WAITING FOR P%d...",
                 v->st->to_act_seat == 0xFF ? 0 : v->st->to_act_seat);
        sc.guide = v->st->to_act_seat == 0xFF ? "WAITING..." : s_line[3];
        sc.hint_ok = H(UI_OK); break;
    case SCR_SPECTATE:
        snprintf(s_title, sizeof(s_title), "SPECTATING");
        fill_cards(&sc, v, false); fill_stakes(&sc, v);
        sc.guide = my_eliminated(v) ? "YOU ARE OUT - WATCHING" : "FOLDED - WATCHING"; break;
    case SCR_ABORT:
        snprintf(s_title, sizeof(s_title), "HAND CANCELLED");
        sc.title_color = TC_ALERT;
        sc.deco = DECO_PANEL;
        sc.big = "CANCELLED";
        sc.guide = "BETS RETURNED"; break;
    case SCR_INTERMISSION:
        snprintf(s_title, sizeof(s_title), "NEXT HAND SOON");
        sc.guide = "WAITING FOR DEALER TO START";
        fill_standings(&sc, v); break;
    case SCR_INTERMISSION_DEALER:
        snprintf(s_title, sizeof(s_title), "INTERMISSION");
        sc.guide = hint1("Start next hand", UI_OK);
        fill_standings(&sc, v); break;
    case SCR_REBUY: {
        snprintf(s_title, sizeof(s_title), "REBUY");
        snprintf(s_big, sizeof(s_big), "$%u", g_ui.edit_value);
        sc.modal_title = "BUY IN";
        sc.modal_value = s_big;
        static char r_adj[36];
        snprintf(r_adj, sizeof(r_adj), "%s +1   %s -1", H(UI_UP), H(UI_DOWN));
        sc.modal_hint[0] = hint1("Buy in", UI_OK);
        sc.modal_hint[1] = r_adj;
        break;
    }
    case SCR_PAUSED:
        snprintf(s_title, sizeof(s_title), "PAUSED");
        sc.title_color = TC_ALERT;
        sc.deco = DECO_PANEL;
        sc.big = "PAUSED";
        sc.guide = "NOT ENOUGH PLAYERS - WAITING"; break;
    case SCR_SYSMENU: {
        snprintf(s_title, sizeof(s_title), "MENU  BATT %s",
                 g_ui.batt_pct == 0xFF ? "USB" : "");
        if (g_ui.batt_pct != 0xFF)
            snprintf(s_title, sizeof(s_title), "MENU  BATT %d%%%s", g_ui.batt_pct,
                     g_ui.batt_charging ? " CHG" : "");
        static const char *items[] = { "RESUME", "VOLUME", "BRIGHTNESS", "LEAVE TABLE" };
        for (int i = 0; i < 4; i++) {
            snprintf(s_line[i], sizeof(s_line[i]), "%c %s", i == g_ui.sysmenu_cursor ? '>' : ' ', items[i]);
            sc.lines[i] = s_line[i];
        }
        snprintf(s_line[4], sizeof(s_line[4]), "Vol %d  Bri %d", g_ui.volume, g_ui.brightness);
        sc.lines[4] = s_line[4];
        sc.deco = DECO_PANEL;
        sc.hint_ok = hint2("Select", UI_OK, "Move", UI_UP);
        if (g_ui.confirm_pending && g_ui.confirm_action == 0xFE) {   /* v1.8:離桌 → 模態確認 */
            sc.modal_title = "LEAVE TABLE?";
            sc.modal_hint[0] = hint1("Yes, leave", UI_OK);
            sc.modal_hint[1] = hint1("No, stay", UI_BACK);
            sc.modal_danger = 1;
        }
        break;
    }
    case SCR_RESULT: {
        const pn_table_state_t *st = v->st;
        if (g_ui.result_summary) {
            snprintf(s_title, sizeof(s_title), "RESULT");
            snprintf(s_big, sizeof(s_big), "$%u", my_chips(v));
            sc.big = s_big;
            sc.guide = "HAND FINISHED";
            fill_standings(&sc, v);
            ui_trace(&sc);
            hal_display_render(&sc);
            return;
        }
        /* 逐位翻牌子畫面(v1.3:slot = show[] 索引,補牌型/彩金) */
        hal_showdown_page_t pg;
        memset(&pg, 0, sizeof(pg));
        pg.title = "SHOWDOWN";
        const pn_evt_hand_result_t *res = app_result();
        static const char *RANKS[10] = {
            "HIGH CARD", "ONE PAIR", "TWO PAIR", "THREE OF A KIND", "STRAIGHT",
            "FLUSH", "FULL HOUSE", "FOUR OF A KIND", "STRAIGHT FLUSH", "ROYAL FLUSH" };
        int slot = g_ui.result_slot;
        int seat = -1;
        if (res && slot < res->n_show) {
            seat = res->show[slot].seat;
            if (res->show[slot].rank_cat < 10) pg.rank_text = RANKS[res->show[slot].rank_cat];
            for (int i = 0; i < 10; i++)
                if (res->payout[i].seat == seat) { pg.win_amount = res->payout[i].win; break; }
        }
        int pid = -1;
        for (int i = 0; i < st->n_players && i < 10; i++) if (st->p[i].seat == seat) pid = i;
        if (pid >= 0) {
            static char nm[8]; snprintf(nm, sizeof(nm), "P%d", seat);
            pg.seat = (uint8_t)seat; pg.name = nm;
            pg.hole[0] = st->p[pid].hole[0]; pg.hole[1] = st->p[pid].hole[1];
            pg.is_me = (pid == v->my_player_id);
        }
        ESP_LOGI("ui", "[RESULT.page] slot=%d seat=%d rank=\"%s\" win=%u me=%d",
                 g_ui.result_slot, (int)pg.seat, pg.rank_text ? pg.rank_text : "",
                 pg.win_amount, (int)pg.is_me);
        hal_display_render_showdown(&pg);
        return;
    }
    default:
        snprintf(s_title, sizeof(s_title), "...");
        break;
    }

    /* REJECT 短語 */
    uint8_t rj = v->last_reject_reason;
    if (rj) {
        const char *p = reject_phrase(rj);
        if (p) { sc.title_color = TC_ALERT; sc.lines[5] = p; }
    }
    ui_trace(&sc);
    hal_display_render(&sc);
}
