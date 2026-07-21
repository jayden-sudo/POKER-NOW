#pragma once
/*
 * hal_power.h -- 關機攔截(產品 §2.5)。
 * app_prepare_poweroff() 由 poker_core 實作、板級在關機前呼叫。
 */
#ifdef __cplusplus
extern "C" {
#endif

void app_prepare_poweroff(void);   /* 若本機為 Master → 觸發 §9.3 交接(最多等 2s) */

#ifdef __cplusplus
}
#endif
