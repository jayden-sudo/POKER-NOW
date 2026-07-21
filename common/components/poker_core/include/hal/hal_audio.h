#pragma once
/*
 * hal_audio.h -- PCM 輸出 + 排程(產品 §2.3)。解碼在共用層(拉取式)。
 */
#include <stdint.h>
#include "esp_err.h"
#include "voice_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t hal_audio_init(void);
void hal_audio_play(voice_id_t id);                    /* 入佇列儘快播(串接不打斷) */
void hal_audio_play_at(voice_id_t id, uint32_t local_ms);  /* 排程齊播 */
void hal_audio_stop(void);
void hal_audio_set_volume(uint8_t pct);                /* 0-100(StickS3 電池鉗 ≤75) */
uint16_t hal_audio_path_latency_ms(void);

#ifdef __cplusplus
}
#endif
