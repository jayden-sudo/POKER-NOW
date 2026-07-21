/*
 * ui_lvgl.c -- v1.3 共用 LVGL 渲染器(產品 §2.2a)。
 * 四機皆 LVGL → 渲染由此單一實作;per-device hal_display.c 只留面板 bring-up 與亮度。
 * 兩檔佈局:normal(240×240)/ compact(240×135;StickS3 已軟旋轉、Cardputer 原生)。
 * 物件一次建成、render 只改屬性;新增 SRAM ≈ 6KB(物件池)+ 576B(花色圖)。
 */
#include "hal/hal_display.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include <string.h>
#include <stdio.h>

/* ---------------- 花色圖示(12×12 A8,遊戲王牌面級簡化像素圖) ---------------- */
static const char *SUIT_ART[4][12] = {
    { /* 0=♣ club */
      "....####....", "...######...", "...######...", "....####....",
      ".####..####.", "############", "############", ".####..####.",
      ".....##.....", "....####....", "...######...", "............" },
    { /* 1=♦ diamond */
      ".....##.....", "....####....", "...######...", "..########..",
      ".##########.", ".##########.", "..########..", "...######...",
      "....####....", ".....##.....", "............", "............" },
    { /* 2=♥ heart */
      "..##....##..", ".####..####.", "############", "############",
      "############", ".##########.", "..########..", "...######...",
      "....####....", ".....##.....", "............", "............" },
    { /* 3=♠ spade */
      ".....##.....", "....####....", "...######...", "..########..",
      ".##########.", ".##########.", "..########..", "...#.##.#...",
      ".....##.....", "....####....", "...######...", "............" },
};
static uint8_t        s_suit_px[4][12 * 12];
static lv_image_dsc_t s_suit_dsc[4];

static void suits_init(void)
{
    for (int s = 0; s < 4; s++) {
        for (int y = 0; y < 12; y++)
            for (int x = 0; x < 12; x++)
                s_suit_px[s][y * 12 + x] = (SUIT_ART[s][y][x] == '#') ? 0xFF : 0x00;
        memset(&s_suit_dsc[s], 0, sizeof(lv_image_dsc_t));
        s_suit_dsc[s].header.magic = LV_IMAGE_HEADER_MAGIC;
        s_suit_dsc[s].header.cf = LV_COLOR_FORMAT_A8;
        s_suit_dsc[s].header.w = 12;
        s_suit_dsc[s].header.h = 12;
        s_suit_dsc[s].header.stride = 12;
        s_suit_dsc[s].data = s_suit_px[s];
        s_suit_dsc[s].data_size = sizeof(s_suit_px[s]);
    }
}

/* ---------------- 佈局參數 ---------------- */
static bool     s_compact;
static uint16_t s_w, s_h;

#define COL_BG        lv_color_hex(0x0a3d1e)   /* 牌桌綠 */
#define COL_CARD      lv_color_hex(0xf8f8f4)
#define COL_CARD_EDGE lv_color_hex(0x202020)
#define COL_BACK      lv_color_hex(0x18306e)   /* 牌背深藍 */
#define COL_BACK_EDGE lv_color_hex(0x4a6ab8)
#define COL_BACK_MOTIF lv_color_hex(0x2a4a96)
#define COL_RED       lv_color_hex(0xc82020)
#define COL_BLACK     lv_color_hex(0x181818)
#define COL_GOLD      lv_color_hex(0xf0c020)
#define COL_DIM       lv_color_hex(0x9aa89a)
#define COL_OK        lv_color_hex(0x40c040)
#define COL_NEXT      lv_color_hex(0x60a0ff)
#define COL_EMPTY     lv_color_hex(0x2a5a3a)

/* ---------------- 仿真紙牌 widget ---------------- */
typedef struct {
    lv_obj_t *root, *rank, *suit, *motif;   /* motif = 牌背花樣(置中小圖) */
} card_w_t;

static card_w_t mk_card(lv_obj_t *parent, int w, int h, bool big)
{
    card_w_t c;
    c.root = lv_obj_create(parent);
    lv_obj_set_size(c.root, w, h);
    lv_obj_set_style_radius(c.root, big ? 5 : 4, 0);
    lv_obj_set_style_border_width(c.root, 1, 0);
    lv_obj_set_style_border_color(c.root, COL_CARD_EDGE, 0);
    lv_obj_set_style_bg_color(c.root, COL_CARD, 0);
    lv_obj_set_style_pad_all(c.root, 0, 0);
    lv_obj_clear_flag(c.root, LV_OBJ_FLAG_SCROLLABLE);

    c.rank = lv_label_create(c.root);
    lv_obj_set_style_text_font(c.rank, &lv_font_montserrat_14, 0);
    lv_obj_align(c.rank, LV_ALIGN_TOP_LEFT, 2, 0);

    c.suit = lv_image_create(c.root);
    lv_obj_set_style_image_recolor_opa(c.suit, LV_OPA_COVER, 0);
    lv_obj_align(c.suit, LV_ALIGN_BOTTOM_MID, 0, big ? -6 : -3);

    c.motif = lv_image_create(c.root);       /* 牌背:置中花樣 */
    lv_image_set_src(c.motif, &s_suit_dsc[1]);   /* ♦ 形花樣 */
    lv_obj_set_style_image_recolor_opa(c.motif, LV_OPA_COVER, 0);
    lv_obj_set_style_image_recolor(c.motif, COL_BACK_MOTIF, 0);
    lv_obj_center(c.motif);
    return c;
}

/* card: 0xFF=空槽;face_up=false → 牌背 */
static void card_set(card_w_t *c, hal_card_t card, bool face_up)
{
    if (card == 0xFF) {   /* 空槽:虛框 */
        lv_obj_set_style_bg_color(c->root, COL_EMPTY, 0);
        lv_obj_set_style_bg_opa(c->root, LV_OPA_40, 0);
        lv_obj_set_style_border_color(c->root, COL_DIM, 0);
        lv_obj_add_flag(c->rank, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(c->suit, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(c->motif, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_set_style_bg_opa(c->root, LV_OPA_COVER, 0);
    if (!face_up) {       /* 牌背 */
        lv_obj_set_style_bg_color(c->root, COL_BACK, 0);
        lv_obj_set_style_border_color(c->root, COL_BACK_EDGE, 0);
        lv_obj_add_flag(c->rank, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(c->suit, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(c->motif, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    static const char *RK[13] = { "2","3","4","5","6","7","8","9","10","J","Q","K","A" };
    uint8_t rank = card >> 2, suit = card & 3;
    lv_color_t col = (suit == 1 || suit == 2) ? COL_RED : COL_BLACK;
    lv_obj_set_style_bg_color(c->root, COL_CARD, 0);
    lv_obj_set_style_border_color(c->root, COL_CARD_EDGE, 0);
    lv_obj_clear_flag(c->rank, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(c->suit, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(c->motif, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(c->rank, rank < 13 ? RK[rank] : "?");
    lv_obj_set_style_text_color(c->rank, col, 0);
    lv_image_set_src(c->suit, &s_suit_dsc[suit]);
    lv_obj_set_style_image_recolor(c->suit, col, 0);
}

/* ---------------- 畫面物件 ---------------- */
static lv_obj_t *s_scr;
static lv_obj_t *s_title, *s_batt, *s_guide;
/* v1.6 行動滾輪 */
static lv_obj_t *s_act_chip, *s_act_cur, *s_act_prev, *s_act_next;
static uint32_t  s_frame;
/* v1.8 模態對話框 */
static lv_obj_t *s_dim, *s_modal, *s_modal_t, *s_modal_v, *s_modal_h[3];
/* v1.5 裝飾層 */
static lv_obj_t *s_panel;                       /* 毛氈面板(PANEL/STANDINGS) */
static lv_obj_t *s_spin;                        /* 轉圈(SPLASH) */
static lv_obj_t *s_sp_t1, *s_sp_t2, *s_sp_sub;  /* TEXAS / HOLD'EM / 副標 */
static lv_obj_t *s_sp_suit[4];                  /* 花色列 */
static card_w_t  s_board[5], s_hand[2];
static lv_obj_t *s_pot_lbl, *s_pot_val, *s_my_lbl, *s_my_val;   /* v1.4:標籤淡色+數字亮色 */
static lv_obj_t *s_lines[6], *s_big, *s_hint_ok, *s_hint_next, *s_prog;
/* 攤牌頁 */
static lv_obj_t *s_sd;                 /* 全屏容器 */
static lv_obj_t *s_sd_title, *s_sd_name, *s_sd_rank, *s_sd_win, *s_sd_you;
static card_w_t  s_sd_hole[2];

static lv_obj_t *mk_label(lv_obj_t *p, const lv_font_t *f, lv_color_t col)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_label_set_text(l, "");
    return l;
}

void ui_lvgl_attach(uint16_t hres, uint16_t vres)   /* 呼叫方須持有 lvgl_port_lock */
{
    s_w = hres; s_h = vres;
    s_compact = (vres < 200);
    suits_init();

    s_scr = lv_scr_act();
    lv_obj_set_style_bg_color(s_scr, COL_BG, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* 尺寸表 */
    const int bw  = s_compact ? 22 : 30, bh  = s_compact ? 32 : 42;   /* 小牌 */
    const int hw  = s_compact ? 30 : 44, hh  = s_compact ? 44 : 62;   /* 大牌 */
    const int bgap = s_compact ? 4 : 6,  hgap = s_compact ? 12 : 16;

    /* 頂列:狀態 + 電量(最右上) */
    s_title = mk_label(s_scr, &lv_font_montserrat_14, lv_color_white());
    lv_obj_set_pos(s_title, 4, s_compact ? 0 : 2);
    s_batt = mk_label(s_scr, &lv_font_montserrat_14, COL_DIM);
    lv_obj_align(s_batt, LV_ALIGN_TOP_RIGHT, -4, s_compact ? 0 : 2);
    /* 指引行 */
    s_guide = mk_label(s_scr, &lv_font_montserrat_14, COL_GOLD);
    lv_obj_set_pos(s_guide, 4, s_compact ? 14 : 20);

    /* 公共牌列 */
    int bx = (s_w - (5 * bw + 4 * bgap)) / 2;
    int by = s_compact ? 27 : 40;
    for (int i = 0; i < 5; i++) {
        s_board[i] = mk_card(s_scr, bw, bh, false);
        lv_obj_set_pos(s_board[i].root, bx + i * (bw + bgap), by);
    }
    /* POT / MY CHIPS */
    /* v1.4:POT/MY CHIPS 標籤淡色、$金額亮色(標籤字 14,金額字 normal=20/compact=14) */
    s_pot_lbl = mk_label(s_scr, &lv_font_montserrat_14, COL_DIM);
    s_pot_val = mk_label(s_scr, s_compact ? &lv_font_montserrat_14 : &lv_font_montserrat_20, COL_GOLD);
    s_my_lbl  = mk_label(s_scr, &lv_font_montserrat_14, COL_DIM);
    s_my_val  = mk_label(s_scr, s_compact ? &lv_font_montserrat_14 : &lv_font_montserrat_20, lv_color_white());
    lv_label_set_text(s_pot_lbl, "POT");
    lv_label_set_text(s_my_lbl, s_compact ? "ME" : "MY CHIPS");
    if (s_compact) {   /* v1.7:同列靠左排(右欄整欄保留給行動滾輪,消除重疊) */
        lv_obj_set_pos(s_pot_lbl, 8, 64);
        lv_obj_align_to(s_pot_val, s_pot_lbl, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
        lv_obj_set_pos(s_my_lbl, 82, 64);
        lv_obj_align_to(s_my_val, s_my_lbl, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
    } else {           /* v1.7:左欄固定(手牌/滾輪左右分區) */
        lv_obj_set_pos(s_pot_lbl, 24, 94);
        lv_obj_align_to(s_pot_val, s_pot_lbl, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
        lv_obj_set_pos(s_my_lbl, 24, 118);
        lv_obj_align_to(s_my_val, s_my_lbl, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    }
    /* 手牌列 */
    int hx = s_compact ? 8 : 24;             /* v1.7:手牌固定左下,右側讓給滾輪 */
    int hy = s_compact ? 80 : 140;
    for (int i = 0; i < 2; i++) {
        s_hand[i] = mk_card(s_scr, hw, hh, true);
        lv_obj_set_pos(s_hand[i].root, hx + i * (hw + hgap), hy);
    }

    /* 一般內容(非五區畫面):6 行 + 大字 + 進度條 */
    for (int i = 0; i < 6; i++) {
        s_lines[i] = mk_label(s_scr, &lv_font_montserrat_14, lv_color_hex(0xd8d8d8));
        lv_obj_set_pos(s_lines[i], 6, (s_compact ? 30 : 52) + i * (s_compact ? 15 : 20));
    }
    s_big = mk_label(s_scr, s_compact ? &lv_font_montserrat_20 : &lv_font_montserrat_28,
                     lv_color_white());
    /* big:五區版式時作為行動選單(底部);一般畫面時置中大字 —— render 時動態擺位 */
    /* ---- v1.5 裝飾層 ---- */
    s_panel = lv_obj_create(s_scr);             /* 毛氈面板:深綠 + 金邊 */
    if (s_compact) { lv_obj_set_size(s_panel, 220, 86); lv_obj_set_pos(s_panel, 10, 30); }
    else           { lv_obj_set_size(s_panel, 208, 160); lv_obj_set_pos(s_panel, 16, 42); }
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(0x06280f), 0);
    lv_obj_set_style_radius(s_panel, 10, 0);
    lv_obj_set_style_border_width(s_panel, 2, 0);
    lv_obj_set_style_border_color(s_panel, COL_GOLD, 0);
    lv_obj_set_style_border_opa(s_panel, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(s_panel, 0, 0);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(s_panel);            /* 內容(lines/big)壓在面板上 */

    s_spin = lv_spinner_create(s_scr);          /* 轉圈動畫(LVGL 自驅動) */
    lv_obj_set_size(s_spin, s_compact ? 22 : 30, s_compact ? 22 : 30);
    lv_obj_set_style_arc_color(s_spin, COL_GOLD, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_spin, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_spin, lv_color_hex(0x1a4a2a), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_spin, 3, LV_PART_MAIN);
    /* v1.9.1:移到右下角 —— 原底部置中會與 LOBBY 的「N/10」人數大字重疊(使用者回報) */
    lv_obj_align(s_spin, LV_ALIGN_BOTTOM_RIGHT, s_compact ? -8 : -12, s_compact ? -8 : -12);
    lv_obj_add_flag(s_spin, LV_OBJ_FLAG_HIDDEN);

    s_sp_t1 = mk_label(s_scr, s_compact ? &lv_font_montserrat_20 : &lv_font_montserrat_28,
                       lv_color_hex(0xf5f0e0));
    lv_label_set_text(s_sp_t1, "TEXAS");
    s_sp_t2 = mk_label(s_scr, s_compact ? &lv_font_montserrat_20 : &lv_font_montserrat_28, COL_GOLD);
    lv_label_set_text(s_sp_t2, "HOLD'EM");
    s_sp_sub = mk_label(s_scr, &lv_font_montserrat_14, COL_DIM);
    lv_label_set_text(s_sp_sub, "ESP-NOW POKER TABLE");
    lv_obj_align(s_sp_t1, LV_ALIGN_TOP_MID, 0, s_compact ? 26 : 58);
    lv_obj_align(s_sp_t2, LV_ALIGN_TOP_MID, 0, s_compact ? 48 : 92);
    lv_obj_align(s_sp_sub, LV_ALIGN_TOP_MID, 0, s_compact ? 96 : 176);
    for (int i = 0; i < 4; i++) {               /* 花色列 ♣♦♥♠ */
        s_sp_suit[i] = lv_image_create(s_scr);
        lv_image_set_src(s_sp_suit[i], &s_suit_dsc[i]);
        lv_obj_set_style_image_recolor_opa(s_sp_suit[i], LV_OPA_COVER, 0);
        lv_obj_set_style_image_recolor(s_sp_suit[i],
            (i == 1 || i == 2) ? COL_RED : lv_color_hex(0xe8e8e0), 0);
        lv_obj_align(s_sp_suit[i], LV_ALIGN_TOP_MID, (i - 2) * 26 + 13,
                     s_compact ? 74 : 140);
    }
    for (int i = 0; i < 4; i++) lv_obj_add_flag(s_sp_suit[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_sp_t1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_sp_t2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_sp_sub, LV_OBJ_FLAG_HIDDEN);

    /* ---- v1.6 行動滾輪(MY_TURN:金邊閃爍晶片 + 前/後動作) ---- */
    s_act_chip = lv_obj_create(s_scr);
    lv_obj_set_size(s_act_chip, s_compact ? 92 : 96, s_compact ? 24 : 30);
    lv_obj_set_style_bg_color(s_act_chip, lv_color_hex(0x06280f), 0);
    lv_obj_set_style_radius(s_act_chip, 8, 0);
    lv_obj_set_style_border_width(s_act_chip, 2, 0);
    lv_obj_set_style_border_color(s_act_chip, COL_GOLD, 0);
    lv_obj_set_style_pad_all(s_act_chip, 0, 0);
    lv_obj_clear_flag(s_act_chip, LV_OBJ_FLAG_SCROLLABLE);
    s_act_cur = mk_label(s_act_chip, s_compact ? &lv_font_montserrat_14 : &lv_font_montserrat_20,
                         lv_color_white());
    lv_obj_center(s_act_cur);
    s_act_prev = mk_label(s_scr, &lv_font_montserrat_14, COL_DIM);
    s_act_next = mk_label(s_scr, &lv_font_montserrat_14, COL_DIM);
    if (s_compact) {   /* v1.7:右下直欄(x≥136,與手牌 x≤68 / ME 列 x≤150 分離) */
        lv_obj_align(s_act_chip, LV_ALIGN_BOTTOM_RIGHT, -6, -26);
        lv_obj_align(s_act_prev, LV_ALIGN_BOTTOM_RIGHT, -12, -54);
        lv_obj_align(s_act_next, LV_ALIGN_BOTTOM_RIGHT, -12, -4);
    } else {           /* v1.7.1:晶片 x=140..236,右手牌止於 x=128 → 12px 實隙(照片實測修) */
        lv_obj_align(s_act_chip, LV_ALIGN_BOTTOM_RIGHT, -4, -30);
        lv_obj_align(s_act_prev, LV_ALIGN_BOTTOM_RIGHT, -10, -66);
        lv_obj_align(s_act_next, LV_ALIGN_BOTTOM_RIGHT, -10, -6);
    }
    lv_obj_add_flag(s_act_chip, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_act_prev, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_act_next, LV_OBJ_FLAG_HIDDEN);

    s_prog = lv_bar_create(s_scr);
    lv_obj_set_size(s_prog, s_w - 40, 6);
    lv_obj_align(s_prog, LV_ALIGN_BOTTOM_MID, 0, s_compact ? -14 : -30);
    lv_obj_add_flag(s_prog, LV_OBJ_FLAG_HIDDEN);

    /* 底部提示 */
    s_hint_ok = mk_label(s_scr, &lv_font_montserrat_14, COL_OK);
    lv_obj_align(s_hint_ok, LV_ALIGN_BOTTOM_LEFT, 4, -1);
    s_hint_next = mk_label(s_scr, &lv_font_montserrat_14, COL_NEXT);
    lv_obj_align(s_hint_next, LV_ALIGN_BOTTOM_RIGHT, -4, -1);

    /* ---- v1.8 模態對話框(最上層;壓暗背景 + 置中框) ---- */
    s_dim = lv_obj_create(s_scr);
    lv_obj_set_size(s_dim, s_w, s_h);
    lv_obj_set_pos(s_dim, 0, 0);
    lv_obj_set_style_bg_color(s_dim, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_dim, LV_OPA_60, 0);
    lv_obj_set_style_border_width(s_dim, 0, 0);
    lv_obj_set_style_radius(s_dim, 0, 0);
    lv_obj_clear_flag(s_dim, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_dim, LV_OBJ_FLAG_HIDDEN);

    s_modal = lv_obj_create(s_dim);
    if (s_compact) { lv_obj_set_size(s_modal, 200, 118); }
    else           { lv_obj_set_size(s_modal, 210, 150); }
    lv_obj_center(s_modal);
    lv_obj_set_style_bg_color(s_modal, lv_color_hex(0x10331c), 0);
    lv_obj_set_style_radius(s_modal, 12, 0);
    lv_obj_set_style_border_width(s_modal, 3, 0);
    lv_obj_set_style_border_color(s_modal, COL_GOLD, 0);
    lv_obj_set_style_pad_all(s_modal, 6, 0);
    lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);
    s_modal_t = mk_label(s_modal, &lv_font_montserrat_20, lv_color_white());
    lv_obj_align(s_modal_t, LV_ALIGN_TOP_MID, 0, 2);
    s_modal_v = mk_label(s_modal, &lv_font_montserrat_28, COL_GOLD);
    lv_obj_align(s_modal_v, LV_ALIGN_TOP_MID, 0, s_compact ? 26 : 34);
    /* v2.2:底部提示改「垂直堆疊、置中」3 行,由下而上排(index 0 在最上)—— 徹底消除重疊 */
    for (int i = 0; i < 3; i++) {
        s_modal_h[i] = mk_label(s_modal, &lv_font_montserrat_14, COL_DIM);
        lv_obj_align(s_modal_h[i], LV_ALIGN_BOTTOM_MID, 0, -2 - (2 - i) * 16);
    }

    /* 攤牌全屏頁 */
    s_sd = lv_obj_create(s_scr);
    lv_obj_set_size(s_sd, s_w, s_h);
    lv_obj_set_pos(s_sd, 0, 0);
    lv_obj_set_style_bg_color(s_sd, lv_color_hex(0x0a1e3d), 0);
    lv_obj_set_style_radius(s_sd, 0, 0);
    lv_obj_set_style_border_width(s_sd, 0, 0);
    lv_obj_set_style_pad_all(s_sd, 0, 0);
    lv_obj_clear_flag(s_sd, LV_OBJ_FLAG_SCROLLABLE);
    s_sd_title = mk_label(s_sd, &lv_font_montserrat_14, COL_DIM);
    lv_obj_set_pos(s_sd_title, 4, 0);
    s_sd_name = mk_label(s_sd, &lv_font_montserrat_20, lv_color_white());
    lv_obj_set_pos(s_sd_name, 4, s_compact ? 16 : 24);
    s_sd_you = mk_label(s_sd, &lv_font_montserrat_14, COL_GOLD);
    lv_obj_align(s_sd_you, LV_ALIGN_TOP_RIGHT, -4, s_compact ? 16 : 24);
    for (int i = 0; i < 2; i++) {
        s_sd_hole[i] = mk_card(s_sd, hw, hh, true);
        lv_obj_set_pos(s_sd_hole[i].root, (s_compact ? 8 : 40) + i * (hw + hgap),
                       s_compact ? 40 : 60);
    }
    s_sd_rank = mk_label(s_sd, &lv_font_montserrat_20, lv_color_white());
    if (s_compact) lv_obj_set_pos(s_sd_rank, 8 + 2 * hw + hgap + 10, 52);
    else           lv_obj_align(s_sd_rank, LV_ALIGN_BOTTOM_LEFT, 6, -26);
    s_sd_win = mk_label(s_sd, &lv_font_montserrat_20, COL_GOLD);
    lv_obj_align(s_sd_win, LV_ALIGN_BOTTOM_RIGHT, -6, s_compact ? -2 : -26);
    lv_obj_add_flag(s_sd, LV_OBJ_FLAG_HIDDEN);
}

static void set_or_clear(lv_obj_t *l, const char *t)
{
    if (t) { lv_label_set_text(l, t); lv_obj_clear_flag(l, LV_OBJ_FLAG_HIDDEN); }
    else   { lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN); }
}

void hal_display_render(const hal_screen_t *s)
{
    if (!s_scr || !lvgl_port_lock(0)) return;
    lv_obj_add_flag(s_sd, LV_OBJ_FLAG_HIDDEN);

    /* 標題 + 旗標 */
    static char tb[48];
    snprintf(tb, sizeof(tb), "%s%s%s", s->title ? s->title : "",
             (s->title_flags & TF_LINK_LOST) ? " [RECONNECT]" : "",
             (s->title_flags & TF_SUBMITTING) ? " ..." : "");
    lv_label_set_text(s_title, tb);
    lv_color_t tc = lv_color_white();
    if (s->title_color == TC_ALERT) tc = lv_color_hex(0xf05050);
    else if (s->title_color == TC_TURN) tc = COL_GOLD;
    else if (s->title_color == TC_WIN) tc = COL_OK;
    lv_obj_set_style_text_color(s_title, tc, 0);

    /* 電量(最右上) */
    static char bb[12];
    if (s->battery_pct == 0xFF) snprintf(bb, sizeof(bb), "USB");
    else snprintf(bb, sizeof(bb), "%u%%", s->battery_pct);
    lv_label_set_text(s_batt, bb);
    lv_obj_set_style_text_color(s_batt,
        (s->title_flags & TF_BATT_LOW) ? lv_color_hex(0xf05050) : COL_DIM, 0);

    set_or_clear(s_guide, s->guide);

    /* 五區 vs 一般 */
    bool stakes = s->show_stakes;
    for (int i = 0; i < 5; i++) {
        if (stakes) {
            lv_obj_clear_flag(s_board[i].root, LV_OBJ_FLAG_HIDDEN);
            card_set(&s_board[i], s->cards[2 + i], s->cards_face_up & (1u << (2 + i)));
        } else lv_obj_add_flag(s_board[i].root, LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < 2; i++) {
        if (stakes) {
            lv_obj_clear_flag(s_hand[i].root, LV_OBJ_FLAG_HIDDEN);
            card_set(&s_hand[i], s->cards[i], s->cards_face_up & (1u << i));
        } else lv_obj_add_flag(s_hand[i].root, LV_OBJ_FLAG_HIDDEN);
    }
    static char pb[16], mb[16];
    if (stakes) {   /* v1.4:$ 前綴 + 標籤/數字分離;數字更新後重新貼齊 */
        snprintf(pb, sizeof(pb), "$%u", s->pot);
        snprintf(mb, sizeof(mb), "$%u", s->my_chips);
        set_or_clear(s_pot_lbl, "x"); lv_label_set_text(s_pot_lbl, "POT");
        set_or_clear(s_my_lbl, "x");  lv_label_set_text(s_my_lbl, s_compact ? "ME" : "MY CHIPS");
        set_or_clear(s_pot_val, pb);
        set_or_clear(s_my_val, mb);
        lv_obj_align_to(s_pot_val, s_pot_lbl, LV_ALIGN_OUT_RIGHT_MID, s_compact ? 5 : 8, 0);
        lv_obj_align_to(s_my_val, s_my_lbl, LV_ALIGN_OUT_RIGHT_MID, s_compact ? 5 : 8, 0);
    } else {
        set_or_clear(s_pot_lbl, NULL); set_or_clear(s_pot_val, NULL);
        set_or_clear(s_my_lbl, NULL);  set_or_clear(s_my_val, NULL);
    }

    /* ---- v1.5 裝飾層切換 ---- */
    uint8_t deco = stakes ? DECO_NONE : s->deco;
    bool splash = (deco == DECO_SPLASH);
    bool panel  = (deco == DECO_PANEL || deco == DECO_STANDINGS);
    lv_obj_add_flag(s_sp_t1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_sp_t2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_sp_sub, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 4; i++) lv_obj_add_flag(s_sp_suit[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_spin, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    if (splash) {
        lv_obj_clear_flag(s_sp_t1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_sp_t2, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < 4; i++) lv_obj_clear_flag(s_sp_suit[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_spin, LV_OBJ_FLAG_HIDDEN);
        if (!s->big) lv_obj_clear_flag(s_sp_sub, LV_OBJ_FLAG_HIDDEN);  /* 有大字(如 3/10)則讓位 */
    } else if (panel) {
        lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    }

    /* 行內容(依 deco 擺位) */
    for (int i = 0; i < 6; i++)
        set_or_clear(s_lines[i], stakes ? (i == 0 ? s->lines[0] : NULL)
                                        : (splash ? NULL : s->lines[i]));
    if (stakes && s->lines[0]) {   /* 五區版式的補充行貼在指引下 */
        lv_obj_set_pos(s_lines[0], 6, s_compact ? 14 : 20);
        if (s->guide) lv_obj_set_pos(s_lines[0], 6, s_compact ? 66 : 100); /* 讓位 */
    } else if (!stakes && !splash) {
        int lx = panel ? (s_compact ? 20 : 30) : 6;
        int ly = panel ? (s_compact ? 36 : 54) : (s_compact ? 30 : 52);
        int lp = panel ? (s_compact ? 13 : 21) : (s_compact ? 15 : 20);
        for (int i = 0; i < 6; i++) lv_obj_set_pos(s_lines[i], lx, ly + i * lp);
    }

    /* v1.6 行動滾輪:act_prev/act_next 任一非 NULL → 晶片模式(閃爍) */
    s_frame++;
    bool wheel = stakes && (s->act_prev || s->act_next) && s->big;
    if (wheel) {
        lv_obj_clear_flag(s_act_chip, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_act_cur, s->big);
        set_or_clear(s_act_prev, (s->act_prev && s->act_prev[0]) ? s->act_prev : NULL);
        set_or_clear(s_act_next, (s->act_next && s->act_next[0]) ? s->act_next : NULL);
        bool on = ((s_frame / 3) & 1) == 0;          /* ~1.7Hz 呼吸閃爍 */
        lv_obj_set_style_border_opa(s_act_chip, on ? LV_OPA_COVER : LV_OPA_40, 0);
        lv_obj_set_style_text_opa(s_act_cur, on ? LV_OPA_COVER : LV_OPA_70, 0);
    } else {
        lv_obj_add_flag(s_act_chip, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_act_prev, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_act_next, LV_OBJ_FLAG_HIDDEN);
    }

    /* big:五區=底部行動列;splash=招牌下方;panel=面板置中;其他=畫面置中 */
    if (s->big && !wheel) {
        lv_label_set_text(s_big, s->big);
        lv_obj_clear_flag(s_big, LV_OBJ_FLAG_HIDDEN);
        if (stakes) {
            if (s_compact) lv_obj_align(s_big, LV_ALIGN_BOTTOM_RIGHT, -6, -16);
            else           lv_obj_align(s_big, LV_ALIGN_BOTTOM_MID, 0, -22);
        } else if (splash) {
            lv_obj_align(s_big, LV_ALIGN_TOP_MID, 0, s_compact ? 94 : 172);
            lv_obj_set_style_text_color(s_big, COL_GOLD, 0);
        } else if (panel && deco == DECO_PANEL) {
            lv_obj_align_to(s_big, s_panel, LV_ALIGN_CENTER, 0, 0);
        } else if (panel) {   /* STANDINGS:大字(我的籌碼)貼面板底 */
            lv_obj_align_to(s_big, s_panel, LV_ALIGN_BOTTOM_MID, 0, -4);
        } else {
            lv_obj_align(s_big, LV_ALIGN_CENTER, 0, s_compact ? 0 : -10);
        }
        if (!splash) lv_obj_set_style_text_color(s_big, lv_color_white(), 0);
    } else lv_obj_add_flag(s_big, LV_OBJ_FLAG_HIDDEN);   /* wheel 模式亦走此(晶片代顯) */

    set_or_clear(s_hint_ok, s->hint_ok);
    set_or_clear(s_hint_next, s->hint_next);

    if (s->progress != 0xFF) {
        lv_bar_set_value(s_prog, s->progress, LV_ANIM_OFF);
        lv_obj_clear_flag(s_prog, LV_OBJ_FLAG_HIDDEN);
    } else lv_obj_add_flag(s_prog, LV_OBJ_FLAG_HIDDEN);

    /* v1.8 模態對話框(最上層) */
    if (s->modal_title) {
        lv_obj_clear_flag(s_dim, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_dim);
        lv_color_t edge = s->modal_danger ? lv_color_hex(0xe03030) : COL_GOLD;
        lv_obj_set_style_border_color(s_modal, edge, 0);
        lv_label_set_text(s_modal_t, s->modal_title);
        lv_obj_set_style_text_color(s_modal_t, s->modal_danger ? lv_color_hex(0xff8080)
                                                               : lv_color_white(), 0);
        set_or_clear(s_modal_v, s->modal_value);
        for (int i = 0; i < 3; i++) set_or_clear(s_modal_h[i], s->modal_hint[i]);
    } else {
        lv_obj_add_flag(s_dim, LV_OBJ_FLAG_HIDDEN);
    }

    lvgl_port_unlock();
}

void hal_display_render_showdown(const hal_showdown_page_t *p)
{
    if (!s_scr || !lvgl_port_lock(0)) return;
    lv_obj_clear_flag(s_sd, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_sd);
    set_or_clear(s_sd_title, p->title ? p->title : "SHOWDOWN");
    set_or_clear(s_sd_name, p->name);
    set_or_clear(s_sd_you, p->is_me ? "YOU" : NULL);
    for (int i = 0; i < 2; i++)
        card_set(&s_sd_hole[i], p->hole[i], p->hole[i] != 0xFF);
    set_or_clear(s_sd_rank, p->rank_text);
    static char wb[16];
    if (p->win_amount) {
        snprintf(wb, sizeof(wb), "+$%u", p->win_amount);
        set_or_clear(s_sd_win, wb);
        lv_obj_set_style_bg_color(s_sd, lv_color_hex(0x143d14), 0);   /* 贏家頁綠底 */
    } else {
        set_or_clear(s_sd_win, NULL);
        lv_obj_set_style_bg_color(s_sd, lv_color_hex(0x0a1e3d), 0);
    }
    lvgl_port_unlock();
}
