/* hal_audio.c -- I2S1 直推功放(指南 §6)。
 * = StickS3 hal_audio.c ≈95% 原樣照抄;本板無 codec/無 I2C/無 MCLK/無功放控制腳,
 * 音量 = 純軟體增益。輸出流常開靜音填充(§6.3);play_at 樣本級切入。 */
#include "hal/hal_audio.h"
#include "board_config.h"
#include "voice.h"
#include "driver/i2s_std.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>

static const char *TAG = "hal_audio";
#define FRAME 320                              /* 20ms @16k */

typedef struct { voice_id_t id; uint32_t at_ms; bool sched; } aq_item_t;
static QueueHandle_t s_q;
static i2s_chan_handle_t s_tx;
static uint8_t s_vol = AUDIO_VOL_DEFAULT;

static aq_item_t s_pend[4]; static int s_npend;
static aq_item_t s_imm[8];  static int s_nimm;
static voice_stream_t *s_cur;
static uint32_t s_head_ms;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static bool time_ge(uint32_t a, uint32_t b) { return (int32_t)(a - b) >= 0; }

void hal_audio_play(voice_id_t id)                { aq_item_t i = { id, 0, false }; if (s_q) xQueueSend(s_q, &i, 0); }
void hal_audio_play_at(voice_id_t id, uint32_t t) { aq_item_t i = { id, t, true };  if (s_q) xQueueSend(s_q, &i, 0); }
void hal_audio_stop(void) { if (s_cur) { voice_close(s_cur); s_cur = NULL; } s_nimm = 0; s_npend = 0; }
uint16_t hal_audio_path_latency_ms(void) { return AUDIO_PATH_LATENCY_MS; }
void hal_audio_set_volume(uint8_t pct) { if (pct > 100) pct = 100; s_vol = pct; }

static uint8_t effective_volume(void)
{
    return s_vol;                              /* 本板直推小功放,無電池音量鉗(§6.2 diff) */
}

static void drain_queue(void)
{
    aq_item_t it;
    while (xQueueReceive(s_q, &it, 0) == pdTRUE) {
        if (it.sched) {
            if (s_npend < 4) {
                /* 依 at_ms 升序插入 */
                int i = s_npend++;
                while (i > 0 && s_pend[i - 1].at_ms > it.at_ms) { s_pend[i] = s_pend[i - 1]; i--; }
                s_pend[i] = it;
            }
        } else {
            if (s_nimm < 8) s_imm[s_nimm++] = it;
        }
    }
}
static void pop_pend(void)
{
    for (int i = 1; i < s_npend; i++) s_pend[i - 1] = s_pend[i];
    if (s_npend) s_npend--;
}
static voice_stream_t *next_immediate(void)
{
    while (s_nimm > 0) {
        aq_item_t it = s_imm[0];
        for (int i = 1; i < s_nimm; i++) s_imm[i - 1] = s_imm[i];
        s_nimm--;
        voice_stream_t *h = voice_open(it.id);
        if (h) return h;
    }
    return NULL;
}

static void apply_volume(int16_t *buf, int n, uint8_t vol)
{
    if (vol >= 100) return;
    int32_t g = (int32_t)vol;
    for (int i = 0; i < n; i++) {
        int32_t v = (int32_t)buf[i] * g / 100;
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        buf[i] = (int16_t)v;
    }
}

static void audio_task(void *arg)
{
    (void)arg;
    int16_t buf[FRAME];
    s_head_ms = now_ms() + AUDIO_PATH_LATENCY_MS;
    for (;;) {
        drain_queue();
        memset(buf, 0, sizeof(buf));                       /* 1) 常開靜音填充 */
        if (s_npend && time_ge(s_head_ms + 20, s_pend[0].at_ms)) {   /* 2) 排程搶佔 */
            int off = time_ge(s_pend[0].at_ms, s_head_ms)
                    ? (int)((s_pend[0].at_ms - s_head_ms) * 16) : 0;
            if (off > FRAME) off = FRAME;
            if (s_cur) { voice_close(s_cur); }
            s_cur = voice_open(s_pend[0].id);
            pop_pend();
            if (s_cur && off < FRAME) voice_read(s_cur, buf + off, FRAME - off);
        } else if (s_cur) {                                /* 3) 續播 */
            int n = voice_read(s_cur, buf, FRAME);
            if (n < FRAME) { voice_close(s_cur); s_cur = next_immediate();
                             if (s_cur) voice_read(s_cur, buf + (n > 0 ? n : 0), FRAME - (n > 0 ? n : 0)); }
        } else {                                           /* 4) 即播佇列 */
            s_cur = next_immediate();
            if (s_cur) voice_read(s_cur, buf, FRAME);
        }
        apply_volume(buf, FRAME, effective_volume());
        size_t wr;
        i2s_channel_write(s_tx, buf, sizeof(buf), &wr, portMAX_DELAY);
        s_head_ms += 20;
    }
}

static esp_err_t voice_partition_load(void)
{
    const esp_partition_t *p = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "voice");
    if (!p) { ESP_LOGE(TAG, "voice partition not found"); return ESP_ERR_NOT_FOUND; }
    const void *base; esp_partition_mmap_handle_t h;
    esp_err_t r = esp_partition_mmap(p, 0, p->size, ESP_PARTITION_MMAP_DATA, &base, &h);
    if (r != ESP_OK) { ESP_LOGE(TAG, "voice mmap failed"); return r; }
    return voice_init(base, p->size);          /* 失敗 → 播放呼叫靜默 no-op */
}

esp_err_t hal_audio_init(void)
{
    s_q = xQueueCreate(16, sizeof(aq_item_t));

    i2s_chan_config_t ch = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_SPK_I2S_PORT, I2S_ROLE_MASTER);
    ch.dma_desc_num = 4; ch.dma_frame_num = 240;          /* 4×240 ≈ 60ms 環形深度(不改!
                                                           * AUDIO_PATH_LATENCY_MS=60 由此推導) */
    ESP_ERROR_CHECK(i2s_new_channel(&ch, &s_tx, NULL));
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),   /* 16k;無 codec 不需 MCLK */
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = { .mclk = I2S_GPIO_UNUSED, .bclk = AUDIO_SPK_BCLK, .ws = AUDIO_SPK_WS,
                      .dout = AUDIO_SPK_DOUT, .din = I2S_GPIO_UNUSED,
                      .invert_flags = { 0 } },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx, &std));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx));            /* ★ 常開,之後永不 disable(§6.3) */

    /* 本板無 codec/功放控制腳:功放常通電,靜音 = 全零樣本(§6.3) */

    voice_partition_load();                               /* 與 StickS3 逐行相同(subtype 0x40,"voice") */

    xTaskCreatePinnedToCore(audio_task, "audio", 4096, NULL, 15, NULL, 1);   /* §3 任務表 */
    ESP_LOGI(TAG, "audio ready (I2S1 direct, latency=%dms)", AUDIO_PATH_LATENCY_MS);
    return ESP_OK;
}
