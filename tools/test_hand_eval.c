/*
 * test_hand_eval.c -- host 端單元測試(指南 §20.2)。
 * 編譯:
 *   cc -std=c11 -DPK_HOST_TEST -I components/poker_core/include \
 *      tools/test_hand_eval.c components/poker_core/src/hand_eval.c \
 *      components/poker_core/src/side_pot.c -o /tmp/he && /tmp/he
 */
#include "hand_eval.h"
#include "side_pot.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } \
} while (0)

/* card = rank*4 + suit;rank 0=2..12=A;suit 0=c 1=d 2=h 3=s */
static uint8_t pc(const char *s)
{
    static const char *ranks = "23456789TJQKA";
    int r = (int)(strchr(ranks, s[0]) - ranks);
    int suit;
    switch (s[1]) { case 'c': suit=0; break; case 'd': suit=1; break;
                    case 'h': suit=2; break; default: suit=3; break; }
    return (uint8_t)(r * 4 + suit);
}
/* 7 張:以空白分隔的字串 */
static void p7(const char *str, uint8_t out[7])
{
    char buf[64]; strncpy(buf, str, sizeof(buf) - 1); buf[63] = 0;
    int i = 0;
    for (char *t = strtok(buf, " "); t && i < 7; t = strtok(NULL, " ")) out[i++] = pc(t);
}

static hand_rank_t ev(const char *str) { uint8_t c[7]; p7(str, c); return hand_eval7(c); }

static void test_hand_eval(void)
{
    printf("== hand_eval ==\n");
    hand_rank_t r;

    /* 1. 皇家同花順 → cat 9 */
    r = ev("Ah Kh Qh Jh Th 2c 3d");
    CHECK(r.cat == 9, "royal flush cat=%d exp 9", r.cat);

    /* 2. 同花順(9 高)→ cat 8 */
    r = ev("5s 6s 7s 8s 9s 2c 3d");
    CHECK(r.cat == 8 && r.kick[0] == 7 /* '9' rank idx */, "straight flush cat=%d k0=%d", r.cat, r.kick[0]);

    /* 3. 鋼輪 A2345 同花順 → cat 8,kick[0]=3(記 5) */
    r = ev("Ac 2c 3c 4c 5c 9d Kh");
    CHECK(r.cat == 8 && r.kick[0] == 3, "steel wheel cat=%d k0=%d exp 8/3", r.cat, r.kick[0]);

    /* 4. 四條 → cat 7 */
    r = ev("9h 9d 9c 9s Kc 2d 3h");
    CHECK(r.cat == 7 && r.kick[0] == 7 && r.kick[1] == 11, "quads cat=%d k=%d,%d", r.cat, r.kick[0], r.kick[1]);

    /* 5. 葫蘆 → cat 6 */
    r = ev("Qh Qd Qc 4s 4c 2d 7h");
    CHECK(r.cat == 6 && r.kick[0] == 10 && r.kick[1] == 2, "full house cat=%d", r.cat);

    /* 6. 同花 → cat 5 */
    r = ev("2h 5h 9h Jh Kh 3c 4d");
    CHECK(r.cat == 5, "flush cat=%d", r.cat);

    /* 7. 順子(百老匯 AKQJT) → cat 4 */
    r = ev("Ah Kd Qc Js Tc 2d 3h");
    CHECK(r.cat == 4 && r.kick[0] == 12, "broadway cat=%d k0=%d", r.cat, r.kick[0]);

    /* 8. A2345 順子 < 23456 順子 */
    {
        hand_rank_t w = ev("Ah 2d 3c 4s 5c 9d Kh");   /* wheel, hi=5(idx3) */
        hand_rank_t s = ev("2h 3d 4c 5s 6c 9d Kh");   /* 23456, hi=6(idx4) */
        CHECK(w.cat == 4 && s.cat == 4, "both straights");
        CHECK(hand_rank_cmp(&s, &w) > 0, "23456 beats wheel");
        CHECK(w.kick[0] == 3, "wheel hi=5 idx=%d", w.kick[0]);
    }

    /* 9. 三條 → cat 3 + 兩踢腳降序 */
    r = ev("7h 7d 7c Ks 9c 2d 4h");
    CHECK(r.cat == 3 && r.kick[0] == 5 && r.kick[1] == 11 && r.kick[2] == 7, "trips cat=%d", r.cat);

    /* 10. 兩對 → cat 2 */
    r = ev("Jh Jd 4c 4s Ac 2d 7h");
    CHECK(r.cat == 2 && r.kick[0] == 9 && r.kick[1] == 2 && r.kick[2] == 12, "two pair cat=%d", r.cat);

    /* 11. 一對 → cat 1 */
    r = ev("Th Td 4c 8s Ac 2d 7h");
    CHECK(r.cat == 1 && r.kick[0] == 8, "one pair cat=%d", r.cat);

    /* 12. 高牌 → cat 0 */
    r = ev("Ah Kd 9c 7s 5c 2d 3h");
    CHECK(r.cat == 0 && r.kick[0] == 12 && r.kick[1] == 11, "high card cat=%d", r.cat);

    /* 13. 同兩對比踢腳 */
    {
        hand_rank_t a = ev("Kh Kd 3c 3s Ac 2d 7h");   /* KK 33 A */
        hand_rank_t b = ev("Kh Kd 3c 3s Qc 2d 7h");   /* KK 33 Q */
        CHECK(hand_rank_cmp(&a, &b) > 0, "two pair kicker A beats Q");
    }

    /* 14. 葫蘆 > 同花 */
    {
        hand_rank_t fh = ev("Qh Qd Qc 4s 4c 2h 7h");
        hand_rank_t fl = ev("2h 5h 9h Jh Kh 3c 4d");
        CHECK(fh.cat > fl.cat, "full house(%d) > flush(%d)", fh.cat, fl.cat);
    }

    /* 15. 四條帶踢腳比較(同四條,不同踢腳) */
    {
        hand_rank_t a = ev("5h 5d 5c 5s Ah 2d 3h");   /* quad 5 + A */
        hand_rank_t b = ev("5h 5d 5c 5s Kh 2d 3h");   /* quad 5 + K */
        CHECK(a.cat == 7 && hand_rank_cmp(&a, &b) > 0, "quad kicker A>K");
    }

    /* 16. 7 張中 6 張同花 → 取最大五張同花 */
    r = ev("2h 4h 6h 8h Th Qh 9c");
    CHECK(r.cat == 5 && r.kick[0] == 10 /* Q */ && r.kick[4] == 2 /* 4 */, "6-flush picks top5 k0=%d k4=%d", r.kick[0], r.kick[4]);

    /* 17. board plays 平分(雙方僅用公共牌,同結果) */
    {
        uint8_t board[5] = { pc("Ah"), pc("Ad"), pc("Ac"), pc("As"), pc("Kh") };
        uint8_t p1[7] = { pc("2c"), pc("3d"), board[0], board[1], board[2], board[3], board[4] };
        uint8_t p2[7] = { pc("2h"), pc("4d"), board[0], board[1], board[2], board[3], board[4] };
        hand_rank_t a = hand_eval7(p1), b = hand_eval7(p2);
        CHECK(a.cat == 7 && hand_rank_cmp(&a, &b) == 0, "board plays: tie (quad aces + K)");
    }

    /* 18. 同花順擊敗四條(cat 8 > 7) */
    {
        hand_rank_t sf = ev("6h 7h 8h 9h Th Ac Kd");
        hand_rank_t q  = ev("Ah Ad Ac As Kh 2d 3h");
        CHECK(hand_rank_cmp(&sf, &q) > 0, "straight flush beats quads");
    }
}

/* ---- side_pot helpers ---- */
static sp_player_t mk(bool folded, uint16_t bh, const char *h0, const char *h1)
{
    sp_player_t p; memset(&p, 0, sizeof(p));
    p.present = true; p.folded = folded; p.bet_hand = bh;
    p.hole[0] = h0 ? pc(h0) : 0xFF;
    p.hole[1] = h1 ? pc(h1) : 0xFF;
    return p;
}

static void test_side_pot(void)
{
    printf("== side_pot ==\n");
    uint16_t win[10]; bool ww[10];

    /* S1. 單池單贏家 */
    {
        sp_player_t p[10]; memset(p, 0, sizeof(p));
        uint8_t board[5] = { pc("2c"), pc("7d"), pc("9h"), pc("Jc"), pc("Ks") };
        p[0] = mk(false, 100, "Ah", "Ad");   /* 一對 A */
        p[1] = mk(false, 100, "3c", "4d");   /* 高牌 */
        uint16_t tot = side_pot_settle(p, board, 0, win, ww);
        CHECK(tot == 200, "S1 total=%d exp 200", tot);
        CHECK(win[0] == 200 && win[1] == 0, "S1 seat0 wins all (%d/%d)", win[0], win[1]);
    }

    /* S2. 三層 all-in 側池 */
    {
        sp_player_t p[10]; memset(p, 0, sizeof(p));
        uint8_t board[5] = { pc("2c"), pc("7d"), pc("9h"), pc("Th"), pc("Kd") };
        /* seat0 短籌 25 對子 K(用公共 K + 手 K)最強;seat1 50;seat2 100 */
        p[0] = mk(false, 25,  "Kh", "Ks");   /* trips K -> 三條 K */
        p[1] = mk(false, 50,  "9c", "9d");   /* trips 9 */
        p[2] = mk(false, 100, "7h", "7s");   /* trips 7 */
        uint16_t tot = side_pot_settle(p, board, 2, win, ww);
        /* 主池 25*3=75 → seat0(K三條)贏;邊池1 (50-25)*2=50 → seat1 贏;邊池2 (100-50)=50 → seat2 退回 */
        CHECK(tot == 175, "S2 total=%d exp 175", tot);
        CHECK(win[0] == 75, "S2 seat0=%d exp 75", win[0]);
        CHECK(win[1] == 50, "S2 seat1=%d exp 50", win[1]);
        CHECK(win[2] == 50, "S2 seat2=%d exp 50 (uncalled return)", win[2]);
    }

    /* S3. 棄牌者貢獻(死錢入池) */
    {
        sp_player_t p[10]; memset(p, 0, sizeof(p));
        uint8_t board[5] = { pc("2c"), pc("7d"), pc("9h"), pc("Th"), pc("Kd") };
        p[0] = mk(false, 60, "Ah", "Ad");    /* 一對 A */
        p[1] = mk(false, 60, "3c", "4d");    /* 高牌 */
        p[2] = mk(true,  30, "5c", "5d");    /* 棄牌但已投 30 */
        uint16_t tot = side_pot_settle(p, board, 0, win, ww);
        CHECK(tot == 150, "S3 total=%d exp 150", tot);
        CHECK(win[0] == 150 && win[2] == 0, "S3 seat0 takes 150 incl dead money (%d)", win[0]);
    }

    /* S4. 不叫注自動回退(超額棄牌者過投) */
    {
        sp_player_t p[10]; memset(p, 0, sizeof(p));
        uint8_t board[5] = { pc("2c"), pc("7d"), pc("9h"), pc("Th"), pc("Kd") };
        p[0] = mk(false, 50, "Ah", "Ad");    /* all-in 50,唯一未棄 */
        p[1] = mk(true,  80, "3c", "4d");    /* 棄牌卻投 80 → 超額 30 應退回 */
        uint16_t tot = side_pot_settle(p, board, 0, win, ww);
        CHECK(tot == 130, "S4 total=%d exp 130 (invariant)", tot);
        CHECK(win[0] == 100, "S4 seat0=%d exp 100 (50+50 called)", win[0]);
        CHECK(win[1] == 30, "S4 seat1 refund uncalled 30 (%d)", win[1]);
    }

    /* S5. 奇數籌碼歸按鈕左手最近贏家 */
    {
        sp_player_t p[10]; memset(p, 0, sizeof(p));
        uint8_t board[5] = { pc("Ah"), pc("Ad"), pc("Kc"), pc("Qd"), pc("Jh") };
        /* seat1 與 seat3 平手(都用公共牌兩對 A K),池底 25(奇數) */
        p[1] = mk(false, 25, "2c", "3d");
        p[3] = mk(false, 25, "2h", "3s");   /* 完全同結果,平分 */
        p[5] = mk(true,  0, NULL, NULL);
        /* button=0 → 左手起環序 seat1 先於 seat3 → 奇數的 +1 歸 seat1 */
        uint16_t tot = side_pot_settle(p, board, 0, win, ww);
        CHECK(tot == 50, "S5 total=%d exp 50", tot);
        CHECK(win[1] == 25 && win[3] == 25, "S5 split 50/2 (%d/%d)", win[1], win[3]);
    }

    /* S6. 奇數籌碼:池底 51,平分 3 家,button 左手起前 rem 名 +1 */
    {
        sp_player_t p[10]; memset(p, 0, sizeof(p));
        uint8_t board[5] = { pc("Ah"), pc("Ad"), pc("Ac"), pc("Kd"), pc("Qh") };
        p[0] = mk(false, 17, "2c", "3d");   /* 全平手(公共牌三條A + KQ) */
        p[1] = mk(false, 17, "2h", "3s");
        p[2] = mk(false, 17, "4c", "5d");
        /* total 51, /3 = 17, rem 0 → 各 17 */
        uint16_t tot = side_pot_settle(p, board, 2, win, ww);
        CHECK(tot == 51, "S6 total=%d exp 51", tot);
        CHECK(win[0] == 17 && win[1] == 17 && win[2] == 17, "S6 even 3-way (%d/%d/%d)", win[0], win[1], win[2]);
    }

    /* S7. 奇數餘數分配:池底 100 三家平分 → 34/33/33,+1 給按鈕左手起前 1 名 */
    {
        sp_player_t p[10]; memset(p, 0, sizeof(p));
        uint8_t board[5] = { pc("Ah"), pc("Ad"), pc("Ac"), pc("Kd"), pc("Qh") };
        p[2] = mk(false, 34, "2c", "3d");
        p[4] = mk(false, 33, "2h", "3s");
        p[6] = mk(false, 33, "4c", "5d");
        /* total 100, /3=33 rem 1 → 按鈕(=1)左手起環序 seat2 先得 +1 */
        uint16_t tot = side_pot_settle(p, board, 1, win, ww);
        CHECK(tot == 100, "S7 total=%d exp 100", tot);
        CHECK(win[2] == 34 && win[4] == 33 && win[6] == 33, "S7 odd remainder to button-left (%d/%d/%d)", win[2], win[4], win[6]);
    }
}

int main(void)
{
    test_hand_eval();
    test_side_pot();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
