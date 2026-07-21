#pragma once
/*
 * hal_display.h -- 語義級繪圖(產品 §2.2)。
 */
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t hal_card_t;                    /* 協定 §5 卡牌編碼;0xFF=空位 */
enum { TC_NORMAL, TC_ALERT, TC_TURN, TC_WIN }; /* title_color */

/* title_flags 位 */
enum {
    TF_BATT_LOW  = (1u << 0),
    TF_LINK_LOST = (1u << 1),
    TF_DEALER_NE_MASTER = (1u << 2),
    TF_SUBMITTING = (1u << 3),
};

/* v1.5:非遊戲畫面的裝飾模式(渲染層依此套版) */
enum { DECO_NONE = 0,      /* 遊戲中五區版式(不加裝飾) */
       DECO_SPLASH,        /* 開機/掃描/大廳:TEXAS HOLD'EM 招牌 + 花色列 + 轉圈 */
       DECO_PANEL,         /* 儀式/等待/暫停:毛氈面板 + 置中大字 */
       DECO_STANDINGS };   /* 結算/局間:排行榜面板 */

typedef struct {
    const char *title;
    uint8_t     title_color;
    uint8_t     title_flags;
    uint8_t     battery_pct;      /* 0-100;0xFF=未知(顯示 USB) */
    uint8_t     deco;             /* v1.5:裝飾模式(上方 enum) */
    const char *guide;            /* v1.3:頂部動作指引行("PRESS OK TO ...");NULL=無 */
    const char *lines[6];
    const char *big;
    hal_card_t  cards[7];         /* 自己 2 + 公共 5;0xFF=空 */
    uint8_t     cards_face_up;    /* bitmask */
    uint8_t     show_stakes;      /* v1.3:1=遊戲中五區版式(顯示牌桌/POT/MY CHIPS 區) */
    uint16_t    pot;              /* 桌面總籌碼(池底+在途注) */
    uint16_t    my_chips;         /* 我的籌碼 */
    const char *hint_ok;
    const char *hint_next;
    uint8_t     progress;         /* 0-100;0xFF=不顯示 */
    /* v1.6:行動滾輪(MY_TURN)。任一非 NULL → big 改以「金邊閃爍晶片」呈現,
       上下以小字淡色顯示前/後一個動作(環形滾動示意);"" = 該側留空 */
    const char *act_prev;
    const char *act_next;
    /* v1.8:模態對話框。modal_title 非 NULL → 於最上層畫置中對話框(壓暗背景),
       其餘畫面元素一律不接受注意力。用於二次確認(Fold/All-in/離桌)與下注金額選擇。 */
    const char *modal_title;      /* 標題/問句,如 "FOLD?" / "RAISE TO" */
    const char *modal_value;      /* 大字內容(金額 "$8");NULL=無 */
    const char *modal_hint[3];    /* 底部提示行,由上而下垂直堆疊、置中、互不重疊;NULL=該行空 */
    uint8_t     modal_danger;     /* 1=紅色警示框(破壞性動作) */
} hal_screen_t;

typedef struct {
    const char *title;
    uint8_t     seat;
    const char *name;
    hal_card_t  hole[2];
    const char *rank_text;
    uint16_t    win_amount;
    bool        is_me;
} hal_showdown_page_t;

esp_err_t hal_display_init(void);       /* per-device:面板 bring-up + lvgl_port 掛接 + ui_lvgl_attach */
void      hal_display_render(const hal_screen_t *s);            /* v1.3 起由共用 ui_lvgl.c 提供 */
void      hal_display_render_showdown(const hal_showdown_page_t *p); /* 同上 */
void      hal_display_set_brightness(uint8_t pct);  /* 10-100;裁定 R9(per-device) */

/* v1.3:共用 LVGL 渲染器(ui_lvgl.c)。per-device hal_display_init 在
   lvgl_port_add_disp 之後、於 lvgl_port_lock 內呼叫一次;之後 render 皆走共用實作。 */
void ui_lvgl_attach(uint16_t hres, uint16_t vres);

#ifdef __cplusplus
}
#endif
