#pragma once
/*
 * app_ui.h -- app_flow.c / app_screens.c 共享的 UI 內部狀態(私有)。
 */
#include "game_state.h"
#include "hal/hal_display.h"
#include "voice.h"

typedef enum {
    SCR_BOOT, SCR_SCANNING, SCR_LOBBY, SCR_PENDING_JOIN,
    SCR_DEALER_CALL, SCR_SEATING, SCR_WAIT,
    SCR_CHIPS_SET, SCR_BLINDS,
    SCR_HAND_DEALT, SCR_MY_TURN, SCR_AMOUNT_PICK, SCR_WAIT_TURN, SCR_SPECTATE,
    SCR_RESULT, SCR_ABORT, SCR_INTERMISSION, SCR_INTERMISSION_DEALER, SCR_REBUY,
    SCR_PAUSED, SCR_SYSMENU,
} screen_t;

/* 動作選單項(依合法性動態) */
enum { AS_CHECKCALL, AS_BETRAISE, AS_FOLD, AS_ALLIN, AS_MAX };

typedef struct {
    screen_t screen;
    /* 選單/數值編輯 */
    int      cursor;
    uint16_t edit_value;
    int      blinds_field;          /* 0=sb 1=bb 2=cap */
    uint8_t  bf_sb, bf_bb;
    uint16_t bf_cap;
    /* 二確認 */
    bool     confirm_pending;
    uint8_t  confirm_action;        /* ACT_FOLD/ACT_ALLIN,或 0xFE=LEAVE */
    /* 系統選單 */
    bool     sysmenu_open;
    int      sysmenu_cursor;
    /* 設定(NVS) */
    uint8_t  volume, brightness;
    /* MY_TURN 進場防誤觸 */
    int64_t  guard_until_us;
    /* 動作選單合法項 */
    uint8_t  act_items[AS_MAX];
    int      act_n;
    /* RESULT 分頁 */
    int      result_slot;
    int64_t  result_next_us;
    bool     result_summary;
    /* 斷線橫幅 */
    bool     link_lost;
    /* 電量快取 */
    uint8_t  batt_pct;
    bool     batt_charging;
    int64_t  batt_next_us;
} ui_ctx_t;

extern ui_ctx_t g_ui;

/* app_screens.c */
screen_t screen_from_state(void);
void app_render_current(void);      /* 依 g_ui.screen 組 hal_screen_t / showdown 並 render */

/* app_flow.c 供 screens 使用的小工具 */
const char *reject_phrase(uint8_t reason);
const pn_evt_hand_result_t *app_result(void);   /* 最近結算(無則 NULL);v1.3 攤牌頁用 */
