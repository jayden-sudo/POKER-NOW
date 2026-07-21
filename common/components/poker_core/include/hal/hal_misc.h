#pragma once
/*
 * hal_misc.h -- 雜項(產品 §2.6)。
 */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t hal_rand(void);          /* esp_random() 包裝(Master 洗牌熵源) */
void     hal_yield_watchdog(void);

/* 亮度/音量 NVS 持久化(產品 §5.6 v1.2;實作在各裝置 HAL 或共用 nvs 皆可,
 * 為求四機一致此處由共用碼呼叫,實作在 hal_misc.c per-device) */
void     hal_settings_load(uint8_t *volume, uint8_t *brightness);
void     hal_settings_save_volume(uint8_t volume);
void     hal_settings_save_brightness(uint8_t brightness);

#ifdef __cplusplus
}
#endif
