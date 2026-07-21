#pragma once
/*
 * voice.h -- ADPCM 拉取式解碼 + 數字合成(產品 §6)。
 */
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "voice_ids.h"     /* 生成物:typedef enum {...} voice_id_t; VOICE_COUNT */

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t voice_init(const void *bin, size_t len);   /* 傳入 voice.bin 映射位址 */

typedef struct voice_stream voice_stream_t;
voice_stream_t *voice_open(voice_id_t id);            /* NULL=id 無效 */
int  voice_read(voice_stream_t *h, int16_t *pcm, int max_samples);  /* 回實際樣本數;0=結束 */
void voice_close(voice_stream_t *h);
uint32_t voice_duration_ms(voice_id_t id);

int voice_expand_number(uint16_t n, voice_id_t out[8]);  /* 0..9999 → 片段序列,回長度(≤6) */

#ifdef __cplusplus
}
#endif
