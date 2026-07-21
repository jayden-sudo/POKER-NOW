#pragma once
/*
 * hal_battery.h -- 電量(產品 §2.4)。
 */
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t hal_battery_pct(void);      /* 0-100;0xFF=未知(插 USB 必回 0xFF) */
bool    hal_battery_charging(void); /* 不可知時回 false */

#ifdef __cplusplus
}
#endif
