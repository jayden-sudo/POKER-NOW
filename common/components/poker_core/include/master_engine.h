#pragma once
/*
 * master_engine.h -- Master 決策引擎(僅本機為 Master 時生效)。
 * 執行緒:以下全部只在 pbus task 內呼叫(掛在 pbus 的 cmd_handler / idle_hook / 事件路徑),
 *         函式內部一律用 pbus_state() 同步讀 live 狀態、以 pn_publish_locked 同步套 reducer。
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
