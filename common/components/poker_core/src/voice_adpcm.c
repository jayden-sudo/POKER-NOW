/*
 * voice_adpcm.c -- IMA-ADPCM 拉取式解碼 + 資產索引(產品 §6.1;禁整段解碼)。
 * 解析格式 = wav2adpcm.py 打包格式(指南 §19.2,鎖死)。
 */
#include "voice.h"
#include <string.h>
#include <stdbool.h>
#include "esp_log.h"

static const char *TAG = "voice";

static const int16_t STEP_TABLE[89] = {
    7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,
    88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,
    544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,
    2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,
    10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767 };
static const int8_t INDEX_TABLE[16] = { -1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8 };

typedef struct { uint32_t data_off, adpcm_bytes, pcm_samples; } idx_ent_t;

static const uint8_t *s_base;
static size_t s_len;
static uint16_t s_count;

#define MAX_STREAMS 4
struct voice_stream {
    bool     used;
    const uint8_t *nib;      /* nibble 流起點 */
    uint32_t nib_bytes;
    uint32_t nib_pos;        /* 已消耗的 nibble 數(半位元組) */
    int32_t  predictor;
    int      step_index;
    uint32_t samples_left;
    bool     first_pending;  /* 首樣本(狀態頭)尚未輸出 */
};
static struct voice_stream s_streams[MAX_STREAMS];

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t rd16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }

static idx_ent_t idx_of(voice_id_t id)
{
    idx_ent_t e = {0, 0, 0};
    if (!s_base || (uint16_t)id >= s_count) return e;
    const uint8_t *ip = s_base + 8 + (size_t)id * 12;
    e.data_off = rd32(ip);
    e.adpcm_bytes = rd32(ip + 4);
    e.pcm_samples = rd32(ip + 8);
    return e;
}

esp_err_t voice_init(const void *bin, size_t len)
{
    if (!bin || len < 8) return ESP_ERR_INVALID_ARG;
    const uint8_t *p = (const uint8_t *)bin;
    if (memcmp(p, "PKV1", 4) != 0) {
        ESP_LOGE(TAG, "bad voice.bin magic");
        s_base = NULL;
        return ESP_ERR_INVALID_STATE;
    }
    s_count = rd16(p + 4);
    s_base = p;
    s_len = len;
    ESP_LOGI(TAG, "voice.bin ok: %u clips, %u bytes", (unsigned)s_count, (unsigned)len);
    return ESP_OK;
}

voice_stream_t *voice_open(voice_id_t id)
{
    if (!s_base) return NULL;
    idx_ent_t e = idx_of(id);
    if (e.pcm_samples == 0 || e.adpcm_bytes < 4) return NULL;
    if (e.data_off + e.adpcm_bytes > s_len) return NULL;

    struct voice_stream *h = NULL;
    for (int i = 0; i < MAX_STREAMS; i++)
        if (!s_streams[i].used) { h = &s_streams[i]; break; }
    if (!h) return NULL;

    const uint8_t *clip = s_base + e.data_off;
    h->used = true;
    h->predictor = (int16_t)rd16(clip);      /* s16 initial_predictor */
    h->step_index = clip[2];                 /* u8 initial_step_index */
    if (h->step_index > 88) h->step_index = 88;
    h->nib = clip + 4;
    h->nib_bytes = e.adpcm_bytes - 4;
    h->nib_pos = 0;
    h->samples_left = e.pcm_samples;
    h->first_pending = true;
    return h;
}

static int16_t decode_nibble(struct voice_stream *h, uint8_t delta)
{
    int step = STEP_TABLE[h->step_index];
    int vpdiff = step >> 3;
    if (delta & 4) vpdiff += step;
    if (delta & 2) vpdiff += step >> 1;
    if (delta & 1) vpdiff += step >> 2;
    if (delta & 8) h->predictor -= vpdiff; else h->predictor += vpdiff;
    if (h->predictor > 32767) h->predictor = 32767;
    if (h->predictor < -32768) h->predictor = -32768;
    h->step_index += INDEX_TABLE[delta];
    if (h->step_index < 0) h->step_index = 0;
    if (h->step_index > 88) h->step_index = 88;
    return (int16_t)h->predictor;
}

int voice_read(voice_stream_t *h, int16_t *pcm, int max_samples)
{
    if (!h || !h->used) return 0;
    int n = 0;
    while (n < max_samples && h->samples_left > 0) {
        if (h->first_pending) {
            pcm[n++] = (int16_t)h->predictor;   /* 狀態頭首樣本 */
            h->first_pending = false;
            h->samples_left--;
            continue;
        }
        if (h->nib_pos >= h->nib_bytes * 2) break;  /* nibble 用盡 */
        uint8_t byte = h->nib[h->nib_pos >> 1];
        uint8_t delta = (h->nib_pos & 1) ? (byte >> 4) : (byte & 0x0F);  /* 低 nibble 先 */
        h->nib_pos++;
        pcm[n++] = decode_nibble(h, delta);
        h->samples_left--;
    }
    return n;
}

void voice_close(voice_stream_t *h)
{
    if (h && h->used) { h->used = false; }
}

uint32_t voice_duration_ms(voice_id_t id)
{
    idx_ent_t e = idx_of(id);
    return e.pcm_samples / 16;   /* 16 samples/ms @16kHz */
}
