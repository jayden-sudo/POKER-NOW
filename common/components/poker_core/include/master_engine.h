#pragma once
/*
 * master_engine.h -- Master 決策引擎(僅本機為 Master 時生效)。
 */
#include "pbus.h"

#ifdef __cplusplus
extern "C" {
#endif

void master_engine_enable(bool on);        /* game_init 掛在 on_role 上 */
void master_engine_begin(void);            /* 由 pbus_session 於 E_ROSTER 後呼叫,啟動莊家確認儀式 */
pbus_cmd_verdict_t master_engine_on_cmd(uint8_t player_id, uint8_t cmd,
                                        const void *arg, size_t len);
void master_engine_tick(void);             /* 掛 pbus idle hook */

#ifdef __cplusplus
}
#endif
