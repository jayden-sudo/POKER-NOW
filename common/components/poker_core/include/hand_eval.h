#pragma once
/*
 * hand_eval.h -- 7 選 5 手牌評估器(純函式,host 可測)。
 * card = rank*4 + suit(協定 §5):rank 0=2..12=A;suit 0=c 1=d 2=h 3=s。
 */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { uint8_t cat; uint8_t kick[5]; } hand_rank_t;  /* cat 同協定 §11.4;9=皇家 */

hand_rank_t hand_eval7(const uint8_t cards[7]);
int hand_rank_cmp(const hand_rank_t *a, const hand_rank_t *b);  /* >0:a 勝;0:平 */

#ifdef __cplusplus
}
#endif
