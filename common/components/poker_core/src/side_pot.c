/*
 * side_pot.c -- 側池切池結算(協定 §11.4,純函式,host 可編譯)。
 * 每個下注額度切一池;棄牌者按投入貢獻但永不 eligible;
 * 無 eligible 的層(僅棄牌者/不叫注超額)退還其貢獻者 → 保 Σpayout==Σbet_hand。
 * 奇數籌碼歸按鈕左手最近的贏家(慣例)。
 */
#include "side_pot.h"
#include "hand_eval.h"
#include <string.h>

static uint16_t min_u16(uint16_t a, uint16_t b) { return a < b ? a : b; }

uint16_t side_pot_settle(const sp_player_t players[10], const uint8_t board[5],
                         uint8_t button_seat,
                         uint16_t out_win[10], bool out_winner[10])
{
    memset(out_win, 0, sizeof(uint16_t) * 10);
    for (int i = 0; i < 10; i++) out_winner[i] = false;

    /* 收集所有正 bet_hand 值(含棄牌者)為切池邊界 */
    uint16_t levels[10]; int nlev = 0;
    for (int i = 0; i < 10; i++) {
        if (!players[i].present || players[i].bet_hand == 0) continue;
        uint16_t v = players[i].bet_hand;
        bool dup = false;
        for (int j = 0; j < nlev; j++) if (levels[j] == v) { dup = true; break; }
        if (!dup) levels[nlev++] = v;
    }
    /* 升序排序 */
    for (int i = 0; i < nlev; i++)
        for (int j = i + 1; j < nlev; j++)
            if (levels[j] < levels[i]) { uint16_t t = levels[i]; levels[i] = levels[j]; levels[j] = t; }

    /* 預評牌(non-folded 才需要) */
    hand_rank_t rank[10];
    for (int i = 0; i < 10; i++) {
        if (players[i].present && !players[i].folded) {
            uint8_t c7[7] = { players[i].hole[0], players[i].hole[1],
                              board[0], board[1], board[2], board[3], board[4] };
            rank[i] = hand_eval7(c7);
        } else {
            rank[i].cat = 0xFF;
        }
    }

    uint16_t total = 0;
    uint16_t prev = 0;
    for (int li = 0; li < nlev; li++) {
        uint16_t L = levels[li];

        /* 本層池底 = 全體(含棄牌)在 [prev,L] 的貢獻 */
        uint32_t pot_L = 0;
        for (int i = 0; i < 10; i++) {
            if (!players[i].present) continue;
            uint16_t bh = players[i].bet_hand;
            pot_L += min_u16(bh, L) - min_u16(bh, prev);
        }

        /* eligible = non-folded 且 bet_hand >= L */
        int elig_cnt = 0;
        hand_rank_t best; best.cat = 0xFF;
        for (int i = 0; i < 10; i++) {
            if (players[i].present && !players[i].folded && players[i].bet_hand >= L) {
                elig_cnt++;
                if (best.cat == 0xFF || hand_rank_cmp(&rank[i], &best) > 0) best = rank[i];
            }
        }

        if (elig_cnt == 0) {
            /* 只有棄牌者/超額不叫注 → 退還本層貢獻者 */
            for (int i = 0; i < 10; i++) {
                if (!players[i].present) continue;
                uint16_t bh = players[i].bet_hand;
                uint16_t contrib = min_u16(bh, L) - min_u16(bh, prev);
                out_win[i] += contrib;
                total += contrib;
            }
            prev = L;
            continue;
        }

        /* 找贏家(rank==best),依按鈕左手起環序排列 */
        int winners[10]; int nwin = 0;
        for (int off = 1; off <= 10; off++) {
            int s = (button_seat + off) % 10;
            if (players[s].present && !players[s].folded && players[s].bet_hand >= L &&
                hand_rank_cmp(&rank[s], &best) == 0) {
                winners[nwin++] = s;
                out_winner[s] = true;
            }
        }

        uint16_t share = (uint16_t)(pot_L / nwin);
        uint16_t rem   = (uint16_t)(pot_L % nwin);
        for (int w = 0; w < nwin; w++) {
            uint16_t amt = share + (w < (int)rem ? 1 : 0);
            out_win[winners[w]] += amt;
            total += amt;
        }
        prev = L;
    }
    return total;
}
