/*
 * app_flow.c -- 畫面狀態機 + 手勢消費 + 播報排程(產品 §4/§5/§6)。
 * 只碰 HAL 與 core 介面。UI task ≤10fps 輪詢 game_view。
 */
#include "app_flow.h"
#include "app_ui.h"
#include "pk_config.h"
#include "hal/hal_input.h"
#include "pbus.h"
#include "voice.h"
#include "hal/hal_display.h"
#include "hal/hal_audio.h"
#include "hal/hal_battery.h"
#include "hal/hal_misc.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "app_flow";
ui_ctx_t g_ui;

static uint8_t s_prompt_seat = 1;          /* 最近 E_SEAT_PROMPT 座位 */
static pn_evt_hand_result_t s_result;      /* 最近結算 */
static bool s_have_result;
static int64_t s_abort_until;

static uint32_t local_of(uint32_t table_ms) { return pbus_local_time_for(table_ms); }

/* ---- 播報 ---- */
static void play_number_report(uint16_t n)
{
    hal_audio_play(V_YOU_HAVE);
    voice_id_t seq[8];
    int k = voice_expand_number(n, seq);
    for (int i = 0; i < k; i++) hal_audio_play(seq[i]);
    hal_audio_play(V_CHIPS);
}

static void schedule_result_audio(const pn_evt_hand_result_t *r, uint32_t play_at)
{
    const game_view_t *v = game_view();
    /* 找自己在 show[] 的索引 → 序列齊播 */
    for (int i = 0; i < r->n_show; i++) {
        int pid = -1;
        const pn_table_state_t *st = v->st;
        for (int j = 0; j < st->n_players && j < 10; j++)
            if (st->p[j].seat == r->show[i].seat) pid = j;
        if (pid == v->my_player_id) {
            uint32_t at = play_at + (uint32_t)i * r->slot_gap_ms;
            hal_audio_play_at((voice_id_t)(V_RANK_0 + r->show[i].rank_cat), local_of(at));
            if (r->show[i].is_winner)
                hal_audio_play_at(V_I_WON, local_of(at + 800));
        }
    }
}

/* pbus event hook(pbus task 上下文) */
static void on_evt(const pn_evt_hdr_t *e, const void *body, size_t len)
{
    (void)len;
    const game_view_t *v = game_view();
    switch (e->evt) {
    case E_DEALER_CALL: hal_audio_play_at(V_CONFIRM_DEALER, local_of(e->play_at)); break;
    case E_SEAT_PROMPT: {
        const pn_evt_seat_prompt_t *sp = body;
        s_prompt_seat = sp->seat_no;
        hal_audio_play_at(sp->seat_no <= 1 ? V_SEAT_PROMPT : V_NEXT_SEAT, local_of(e->play_at));
        break;
    }
    case E_CHIPS_PROMPT: hal_audio_play_at(V_SET_CHIPS, local_of(e->play_at)); break;
    case E_CHIPS_SET: {
        const pn_evt_chips_set_t *cs = body;
        if (cs->player_id == v->my_player_id) play_number_report(cs->amount);
        break;
    }
    case E_HAND_START: {
        hal_audio_play_at(V_DEAL, local_of(e->play_at));
        hal_audio_play_at(V_PREFLOP, local_of(e->play_at + PK_GAP_DEAL_PREFLOP_MS));
        const pn_evt_hand_start_t *hs = body;
        if (v->my_seat == hs->button_seat) hal_audio_play(V_NEW_DEALER);
        break;
    }
    case E_STREET: {
        const pn_evt_street_t *s = body;
        voice_id_t id = s->street == 1 ? V_FLOP : s->street == 2 ? V_TURN :
                        s->street == 3 ? V_RIVER : V_SHOWDOWN;
        hal_audio_play_at(id, local_of(e->play_at));
        break;
    }
    case E_ACTION_REQ: {
        const pn_evt_action_req_t *a = body;
        if (a->seat == v->my_seat) hal_audio_play(V_YOUR_TURN);
        break;
    }
    case E_ACTION: {
        const pn_evt_action_t *a = body;
        if (a->seat == v->my_seat) {
            uint8_t act = a->action & 0x7F;
            voice_id_t id = act == ACT_CHECK ? V_CHECK : act == ACT_CALL ? V_CALL :
                            act == ACT_BET ? V_BET : act == ACT_RAISE ? V_RAISE :
                            act == ACT_FOLD ? V_FOLD : V_ALLIN;
            hal_audio_play(id);
        }
        break;
    }
    case E_HAND_RESULT:
        memcpy(&s_result, body, sizeof(s_result));
        s_have_result = true;
        g_ui.screen = SCR_RESULT;
        g_ui.result_slot = 0;
        g_ui.result_summary = false;
        g_ui.result_next_us = esp_timer_get_time() + (int64_t)local_of(e->play_at) * 0; /* 立即起頁 */
        g_ui.result_next_us = esp_timer_get_time() + (int64_t)s_result.slot_gap_ms * 1000;
        schedule_result_audio(&s_result, e->play_at);
        break;
    case E_HAND_ABORT:
        hal_audio_play_at(V_HAND_ABORT, local_of(e->play_at));
        g_ui.screen = SCR_ABORT;
        s_abort_until = esp_timer_get_time() + 3000000;
        break;
    case E_GAME_PAUSE:  hal_audio_play(V_PAUSED); break;
    case E_GAME_RESUME: hal_audio_play(V_RESUMED); break;
    case E_PLAYER_JOINED: hal_audio_play(V_PLAYER_JOINED); break;
    case E_PLAYER_LEFT:   hal_audio_play(V_PLAYER_LEFT); break;
    case E_REMIND: {
        const pn_evt_remind_t *rm = body;
        if (rm->kind == 1 && (rm->target_seat == 0xFF || rm->target_seat == v->my_seat))
            hal_audio_play(V_YOUR_TURN);
        break;
    }
    default: break;
    }
}

static void on_link(pn_link_state_t s)
{
    switch (s) {
    case PBUS_LINK_LOST:   g_ui.link_lost = true; break;
    case PBUS_LINK_JOINED:
    case PBUS_LINK_RESYNCED: g_ui.link_lost = false; break;
    case PBUS_LINK_PENDING: g_ui.screen = SCR_PENDING_JOIN; break;
    case PBUS_LINK_DISSOLVED: g_ui.screen = SCR_SCANNING; break;
    }
}

/* ---- 動作選單合法項 ---- */
static void build_actions(void)
{
    const game_view_t *v = game_view();
    g_ui.act_n = 0;
    g_ui.act_items[g_ui.act_n++] = AS_CHECKCALL;
    if (v->min_raise_to > 0) g_ui.act_items[g_ui.act_n++] = AS_BETRAISE;
    g_ui.act_items[g_ui.act_n++] = AS_FOLD;
    if (v->my_player_id < 10 && v->st->p[v->my_player_id].chips > 0)
        g_ui.act_items[g_ui.act_n++] = AS_ALLIN;
    g_ui.cursor = 0;
}

static void enter_screen(screen_t s)
{
    const game_view_t *v = game_view();
    g_ui.screen = s;
    g_ui.cursor = 0;
    g_ui.confirm_pending = false;
    switch (s) {
    case SCR_CHIPS_SET:
    case SCR_REBUY:
        g_ui.edit_value = PK_CHIPS_DEFAULT;
        break;
    case SCR_BLINDS:
        g_ui.blinds_field = 0; g_ui.bf_sb = v->st->sb ? v->st->sb : 1;
        g_ui.bf_bb = v->st->bb ? v->st->bb : 2; g_ui.bf_cap = v->st->bet_cap ? v->st->bet_cap : 10;
        break;
    case SCR_MY_TURN:
        /* guard:進場 150ms 吞掉跨畫面殘留按壓(人類刻意操作 ~300ms+ 不受影響) */
        g_ui.guard_until_us = esp_timer_get_time() + 150000;
        build_actions();
        break;
    default: break;
    }
    (void)s;
}

/* ---- 數值步進(+1/-1,繞回)---- */
static uint16_t bump(uint16_t val, int dir, uint16_t lo, uint16_t hi)
{
    if (dir > 0) { uint32_t nv = (uint32_t)val + 1; return (uint16_t)(nv > hi ? lo : nv); }
    return (uint16_t)(val <= lo ? hi : val - 1);   /* -1,繞回上限 */
}

/* ---- 系統選單 ---- */
static void sysmenu_open(void)
{
    g_ui.sysmenu_open = true;
    g_ui.sysmenu_cursor = 0;
    g_ui.screen = SCR_SYSMENU;
}
static void sysmenu_handle(hal_ui_event_t ev)
{
    static const uint8_t VOLS[3] = { 40, 70, 100 };
    static const uint8_t BRIS[3] = { 30, 60, 100 };
    if (ev == UI_DOWN) { g_ui.sysmenu_cursor = (g_ui.sysmenu_cursor + 1) % 4; return; }   /* 下移一項 */
    if (ev == UI_UP)   { g_ui.sysmenu_cursor = (g_ui.sysmenu_cursor + 3) % 4; return; }   /* 上移一項 */
    if (ev == UI_BACK || ev == UI_MENU) { g_ui.sysmenu_open = false; enter_screen(screen_from_state()); return; }
    if (ev != UI_OK) return;
    switch (g_ui.sysmenu_cursor) {
    case 0: /* RESUME */ g_ui.sysmenu_open = false; enter_screen(screen_from_state()); break;
    case 1: { /* VOLUME */
        int idx = 0; for (int i = 0; i < 3; i++) if (VOLS[i] == g_ui.volume) idx = i;
        g_ui.volume = VOLS[(idx + 1) % 3];
        hal_audio_set_volume(g_ui.volume);
        hal_settings_save_volume(g_ui.volume);
        hal_audio_play(V_BEEP);
        break;
    }
    case 2: { /* BRIGHTNESS */
        int idx = 0; for (int i = 0; i < 3; i++) if (BRIS[i] == g_ui.brightness) idx = i;
        g_ui.brightness = BRIS[(idx + 1) % 3];
        hal_display_set_brightness(g_ui.brightness);
        hal_settings_save_brightness(g_ui.brightness);
        break;
    }
    case 3: /* LEAVE */ g_ui.confirm_pending = true; g_ui.confirm_action = 0xFE; break;
    }
}

static void confirm_handle(hal_ui_event_t ev)
{
    if (ev == UI_OK) {
        const game_view_t *v = game_view();
        if (g_ui.confirm_action == ACT_FOLD) game_submit_action(ACT_FOLD, 0);
        else if (g_ui.confirm_action == ACT_ALLIN) game_submit_action(ACT_ALLIN, v->max_raise_to);
        else if (g_ui.confirm_action == 0xFE) { game_submit_leave(); g_ui.sysmenu_open = false; }
        g_ui.confirm_pending = false;
    } else if (ev == UI_BACK || ev == UI_UP || ev == UI_DOWN) {
        g_ui.confirm_pending = false;   /* 取消(任何非 OK 意圖) */
    }
}

const pn_evt_hand_result_t *app_result(void)   /* 攤牌頁渲染用 */
{
    return s_have_result ? &s_result : NULL;
}

/* v1.4:板級預設音量/亮度 —— 各板 hal_misc.c 以強符號覆寫;weak 後備 = 共用預設 */
__attribute__((weak)) uint8_t pk_board_default_volume(void)     { return PK_VOL_DEFAULT; }
__attribute__((weak)) uint8_t pk_board_default_brightness(void) { return PK_BRI_DEFAULT; }

/* ---- 意圖分派(v2.1:common 只認識抽象意圖,物理按鍵映射在各裝置 hal_input.c)---- */
static void ui_event_cb(hal_ui_event_t ev)
{
    if (g_ui.confirm_pending) { confirm_handle(ev); return; }
    if (g_ui.sysmenu_open)    { sysmenu_handle(ev); return; }

    const game_view_t *v = game_view();

    /* 全域:UI_MENU 開系統選單(數值輸入畫面 UI_BACK 另有返回語義,不在此) */
    if (ev == UI_MENU) { sysmenu_open(); return; }

    switch (g_ui.screen) {
    case SCR_DEALER_CALL:
        if (ev == UI_OK) game_submit_dealer_claim();
        else if (ev == UI_BACK) sysmenu_open();
        break;
    case SCR_SEATING:
        if (ev == UI_OK) game_submit_seat_claim(s_prompt_seat);
        else if (ev == UI_BACK) sysmenu_open();
        break;
    case SCR_CHIPS_SET:
    case SCR_REBUY:
        if (ev == UI_OK)        game_submit_chips(g_ui.edit_value);
        else if (ev == UI_UP) g_ui.edit_value = bump(g_ui.edit_value, +1, 1, PK_CHIPS_MAX);
        else if (ev == UI_DOWN) g_ui.edit_value = bump(g_ui.edit_value, -1, 1, PK_CHIPS_MAX);
        else if (ev == UI_BACK) sysmenu_open();
        break;
    case SCR_BLINDS: {
        uint16_t cur = g_ui.blinds_field == 0 ? g_ui.bf_sb : g_ui.blinds_field == 1 ? g_ui.bf_bb : g_ui.bf_cap;
        uint16_t lo = (g_ui.blinds_field == 2) ? 0 : 1;
        if (ev == UI_UP || ev == UI_DOWN) {
            cur = bump(cur, ev == UI_UP ? +1 : -1, lo, 200);
            if (g_ui.blinds_field == 0) g_ui.bf_sb = (uint8_t)cur;
            else if (g_ui.blinds_field == 1) g_ui.bf_bb = (uint8_t)cur;
            else g_ui.bf_cap = cur;
        } else if (ev == UI_OK) {
            if (g_ui.blinds_field < 2) g_ui.blinds_field++;
            else game_submit_blinds(g_ui.bf_sb, g_ui.bf_bb, g_ui.bf_cap);
        } else if (ev == UI_BACK) {
            if (g_ui.blinds_field > 0) g_ui.blinds_field--;   /* 回上一欄 */
            else sysmenu_open();                              /* 首欄 → 系統選單(逃生) */
        }
        break;
    }
    case SCR_MY_TURN: {
        if (esp_timer_get_time() < g_ui.guard_until_us) break;   /* 進場防誤觸 */
        /* 滾輪為垂直清單(prev 在上、next 在下):DOWN 移到下方那項(cursor+1),UP 移到上方(cursor-1) */
        if (ev == UI_DOWN) { if (g_ui.act_n) g_ui.cursor = (g_ui.cursor + 1) % g_ui.act_n; break; }
        if (ev == UI_UP) { if (g_ui.act_n) g_ui.cursor = (g_ui.cursor + g_ui.act_n - 1) % g_ui.act_n; break; }
        if (ev == UI_BACK) { sysmenu_open(); break; }
        if (ev != UI_OK) break;
        uint8_t it = g_ui.act_items[g_ui.cursor];
        if (it == AS_CHECKCALL) game_submit_action(v->can_check ? ACT_CHECK : ACT_CALL, 0);
        else if (it == AS_BETRAISE) { enter_screen(SCR_AMOUNT_PICK); g_ui.edit_value = v->min_raise_to ? v->min_raise_to : v->call_amt; }
        else if (it == AS_FOLD) { g_ui.confirm_pending = true; g_ui.confirm_action = ACT_FOLD; }
        else if (it == AS_ALLIN) { g_ui.confirm_pending = true; g_ui.confirm_action = ACT_ALLIN; }
        break;
    }
    case SCR_AMOUNT_PICK:
        if (ev == UI_OK) {
            game_submit_action(ACT_RAISE, g_ui.edit_value);
            enter_screen(SCR_WAIT_TURN);   /* 送出後主動離開(AMOUNT_PICK 排除於畫面重整) */
        }
        else if (ev == UI_UP) g_ui.edit_value = bump(g_ui.edit_value, +1, v->min_raise_to, v->max_raise_to);
        else if (ev == UI_DOWN) g_ui.edit_value = bump(g_ui.edit_value, -1, v->min_raise_to, v->max_raise_to);
        else if (ev == UI_BACK) enter_screen(SCR_MY_TURN);   /* 取消返回動作選擇 */
        break;
    case SCR_INTERMISSION_DEALER:
        if (ev == UI_OK) game_submit_ready_next();
        else if (ev == UI_BACK) sysmenu_open();
        break;
    default:
        if (ev == UI_BACK) sysmenu_open();   /* 其餘畫面 BACK 也開選單(逃生) */
        break;
    }
}

void pk_test_ui(hal_ui_event_t ev) { ui_event_cb(ev); }   /* 測試通道(pk_testio) */

/* ---- UI task ---- */
static void refresh_battery(int64_t now)
{
    if (now < g_ui.batt_next_us) return;
    g_ui.batt_pct = hal_battery_pct();
    g_ui.batt_charging = hal_battery_charging();
    g_ui.batt_next_us = now + 10000000;   /* 10s */
}

static void ui_task(void *arg)
{
    (void)arg;
    enter_screen(SCR_BOOT);
    for (;;) {
        int64_t now = esp_timer_get_time();
        refresh_battery(now);

        if (g_ui.screen == SCR_RESULT) {
            if (now >= g_ui.result_next_us) {
                if (!g_ui.result_summary) {
                    g_ui.result_slot++;
                    if (g_ui.result_slot >= s_result.n_show || s_result.reason == 1) {
                        g_ui.result_summary = true;
                        g_ui.result_next_us = now + 3000000;
                        /* 錯時本機播報自己新籌碼(§6.3) */
                        const game_view_t *v = game_view();
                        play_number_report(v->my_player_id < 10 ? v->st->p[v->my_player_id].chips : 0);
                    } else {
                        g_ui.result_next_us = now + (int64_t)s_result.slot_gap_ms * 1000;
                    }
                } else {
                    s_have_result = false;
                    enter_screen(screen_from_state());   /* → INTERMISSION */
                }
            }
        } else if (g_ui.screen == SCR_ABORT) {
            if (now >= s_abort_until) enter_screen(screen_from_state());
        } else if (!g_ui.sysmenu_open && !g_ui.confirm_pending &&
                   g_ui.screen != SCR_PENDING_JOIN && g_ui.screen != SCR_AMOUNT_PICK) {
            screen_t base = screen_from_state();
            if (base != g_ui.screen) enter_screen(base);
        }

        app_render_current();
        vTaskDelay(pdMS_TO_TICKS(100));   /* ≤10fps */
    }
}

void app_flow_start(const char *device_name)
{
    memset(&g_ui, 0, sizeof(g_ui));
    g_ui.batt_pct = 0xFF;
    hal_settings_load(&g_ui.volume, &g_ui.brightness);
    if (g_ui.volume == 0) g_ui.volume = pk_board_default_volume();
    if (g_ui.brightness == 0) g_ui.brightness = pk_board_default_brightness();
    hal_audio_set_volume(g_ui.volume);
    hal_display_set_brightness(g_ui.brightness);

    game_set_event_hook(on_evt);
    game_set_link_hook(on_link);
    hal_input_init(ui_event_cb);          /* v2.1:裝置端映射物理按鍵 → 抽象意圖 */
    extern void pk_testio_start(void);
    pk_testio_start();                    /* 串口測試通道(注入按鍵;UI trace 見 app_screens) */

    game_init(device_name);   /* 內部 pbus_start */

    xTaskCreatePinnedToCore(ui_task, "ui", 8192, NULL, 5, NULL, 0);
    ESP_LOGI(TAG, "app_flow started: %s", device_name);
}
