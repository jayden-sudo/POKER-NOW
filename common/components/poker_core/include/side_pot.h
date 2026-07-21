#pragma once
/*
 * side_pot.h -- 側池切池結算(協定 §11.4,純函式,host 可測)。
 */
#include "pbus_proto.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 每個座位的結算輸入 */
typedef struct {
    bool     present;       /* 該座位有玩家 */
    bool     folded;
    uint16_t bet_hand;      /* 本局總投入(含已收池) */
    uint8_t  hole[2];       /* 底牌;folded 者可為 0xFF */
} sp_player_t;

/*
 * settle:切池 + 評牌 + 分池(含奇數籌碼歸按鈕左手最近贏家)。
 *   players[10]  座位索引,present=false 表空座
 *   board[5]     公共牌
 *   button_seat  按鈕座(奇數籌碼與播報順序的參考)
 *   out_win[10]  每座分得(結算前清零)
 *   out_winner[10] 每座是否為任一池贏家(供 show/播報)
 * 回傳分池總額(== Σ bet_hand,恆等)。
 */
uint16_t side_pot_settle(const sp_player_t players[10], const uint8_t board[5],
                         uint8_t button_seat,
                         uint16_t out_win[10], bool out_winner[10]);

#ifdef __cplusplus
}
#endif
