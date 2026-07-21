/*
 * hand_eval.c -- 7 選 5 手牌評估(純函式,host 可編譯)。
 * 只依賴 string.h + 自家標頭(協定 §11.4 / 產品 §3.3)。
 */
#include "hand_eval.h"
#include <string.h>
#include <stdbool.h>

/* 直方圖:每 rank 出現次數,排序鍵 = (count desc, rank desc)。 */
typedef struct { uint8_t cnt, rank; } grp_t;

static hand_rank_t eval5(const uint8_t c[5])
{
    uint8_t rank[5], suit[5];
    uint8_t cnt[13] = {0};
    for (int i = 0; i < 5; i++) {
        rank[i] = c[i] >> 2;
        suit[i] = c[i] & 3;
        cnt[rank[i]]++;
    }

    bool flush = (suit[0] == suit[1]) && (suit[0] == suit[2]) &&
                 (suit[0] == suit[3]) && (suit[0] == suit[4]);

    uint16_t mask = 0;
    for (int i = 0; i < 5; i++) mask |= (uint16_t)(1u << rank[i]);

    int straight_hi = -1;
    for (int hi = 12; hi >= 4; hi--)
        if (((mask >> (hi - 4)) & 0x1F) == 0x1F) { straight_hi = hi; break; }
    /* A2345 輪子:A(12)+2..5(0..3);高牌記 5(rank index 3) */
    if (straight_hi < 0 && (mask & 0x100F) == 0x100F) straight_hi = 3;
    bool straight = (straight_hi >= 0);

    /* 建 groups:cnt>0 的 rank,依 (cnt desc, rank desc) 排序 */
    grp_t g[13]; int ng = 0;
    for (int r = 12; r >= 0; r--)
        if (cnt[r]) { g[ng].cnt = cnt[r]; g[ng].rank = (uint8_t)r; ng++; }
    /* 穩定插入排序:cnt 遞減;同 cnt 保持原輸入序(rank 遞減) */
    for (int i = 1; i < ng; i++) {
        grp_t key = g[i]; int j = i - 1;
        while (j >= 0 && g[j].cnt < key.cnt) { g[j + 1] = g[j]; j--; }
        g[j + 1] = key;
    }

    hand_rank_t out;
    memset(&out, 0, sizeof(out));

    if (straight && flush) {
        out.cat = (straight_hi == 12) ? 9 : 8;   /* 皇家 vs 一般同花順 */
        out.kick[0] = (uint8_t)straight_hi;
        return out;
    }
    if (g[0].cnt == 4) {
        out.cat = 7;
        out.kick[0] = g[0].rank; out.kick[1] = g[1].rank;
        return out;
    }
    if (g[0].cnt == 3 && ng >= 2 && g[1].cnt == 2) {
        out.cat = 6;
        out.kick[0] = g[0].rank; out.kick[1] = g[1].rank;
        return out;
    }
    if (flush) {
        out.cat = 5;
        for (int i = 0; i < 5; i++) out.kick[i] = g[i].rank;
        return out;
    }
    if (straight) {
        out.cat = 4;
        out.kick[0] = (uint8_t)straight_hi;
        return out;
    }
    /* 展開:每 group 記 rank 一次 */
    if (g[0].cnt == 3) out.cat = 3;
    else if (g[0].cnt == 2 && ng >= 2 && g[1].cnt == 2) out.cat = 2;
    else if (g[0].cnt == 2) out.cat = 1;
    else out.cat = 0;
    for (int i = 0; i < ng && i < 5; i++) out.kick[i] = g[i].rank;
    return out;
}

hand_rank_t hand_eval7(const uint8_t cards[7])
{
    static const uint8_t idx[21][5] = {
        {0,1,2,3,4},{0,1,2,3,5},{0,1,2,3,6},{0,1,2,4,5},{0,1,2,4,6},
        {0,1,2,5,6},{0,1,3,4,5},{0,1,3,4,6},{0,1,3,5,6},{0,1,4,5,6},
        {0,2,3,4,5},{0,2,3,4,6},{0,2,3,5,6},{0,2,4,5,6},{0,3,4,5,6},
        {1,2,3,4,5},{1,2,3,4,6},{1,2,3,5,6},{1,2,4,5,6},{1,3,4,5,6},
        {2,3,4,5,6},
    };
    hand_rank_t best; best.cat = 0xFF;
    for (int k = 0; k < 21; k++) {
        uint8_t five[5];
        for (int j = 0; j < 5; j++) five[j] = cards[idx[k][j]];
        hand_rank_t r = eval5(five);
        if (best.cat == 0xFF || hand_rank_cmp(&r, &best) > 0) best = r;
    }
    return best;
}

int hand_rank_cmp(const hand_rank_t *a, const hand_rank_t *b)
{
    /* cat + kick[5] 字典序;struct 無 padding(全 u8) */
    return memcmp(a, b, 6);
}
