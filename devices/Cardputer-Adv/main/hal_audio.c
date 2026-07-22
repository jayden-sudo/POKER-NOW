/* hal_audio.c -- ES8311(無 MCLK,SCLK 時鐘源)+ NS4150B + I2S(指南 §8)。
 * 輸出流常開靜音填充;play_at 樣本級切入。以 StickS3 hal_audio.c 為底本(§8.4 diff):
 * 佇列/排程/樣本級切入/常開填充/audio task 主迴圈逐行保持(三機驗證的凍結參考語義)。 */
#include "hal/hal_audio.h"
#include "board_config.h"
#include "board_i2c.h"
#include "es8311_cardputer.h"
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

typedef struct { voice_id_t id; uint32_t at_ms; bool sched; bool stop; } aq_item_t;
static QueueHandle_t s_q;
static i2s_chan_handle_t s_tx;
static uint8_t s_vol = AUDIO_VOL_DEFAULT;

static aq_item_t s_pend[4]; static int s_npend;
static aq_item_t s_imm[8];  static int s_nimm;
static voice_stream_t *s_cur;
static uint32_t s_head_ms;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static bool time_ge(uint32_t a, uint32_t b) { return (int32_t)(a - b) >= 0; }

void hal_audio_play(voice_id_t id)                { aq_item_t i = { .id = id, .sched = false }; if (s_q) xQueueSend(s_q, &i, 0); }
void hal_audio_play_at(voice_id_t id, uint32_t t) { aq_item_t i = { .id = id, .at_ms = t, .sched = true }; if (s_q) xQueueSend(s_q, &i, 0); }
/* stop 經佇列傳遞,清理只在 audio_task 內做,消除對 s_cur/計數器的資料競爭(UAF/double-free,#1)。 */
void hal_audio_stop(void) { aq_item_t i = { .stop = true }; if (s_q) xQueueSend(s_q, &i, 0); }
uint16_t hal_audio_path_latency_ms(void) { return AUDIO_PATH_LATENCY_MS; }
void hal_audio_set_volume(uint8_t pct) { if (pct > 100) pct = 100; s_vol = pct; }

/* 無電池鉗(NS4150B 1W + 1750mAh,無 StickS3 的 PM1 過流限制) */
static uint8_t effective_volume(void)
{
    return s_vol;
}

static void drain_queue(void)
{
    aq_item_t it;
    while (xQueueReceive(s_q, &it, 0) == pdTRUE) {
        if (it.stop) {   /* 在 audio_task 內清理,無競爭(#1) */
            if (s_cur) { voice_close(s_cur); s_cur = NULL; }
            s_npend = 0; s_nimm = 0;
            continue;
        }
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
        static int16_t sbuf[FRAME * 2];                    /* v1.4:單聲道 → 立體聲複製 */
        for (int i = 0; i < FRAME; i++) { sbuf[2 * i] = buf[i]; sbuf[2 * i + 1] = buf[i]; }
        size_t wr;
        i2s_channel_write(s_tx, sbuf, sizeof(sbuf), &wr, portMAX_DELAY);
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
    if (!s_q) { ESP_LOGE(TAG, "audio queue alloc failed"); return ESP_ERR_NO_MEM; }

    i2s_chan_config_t ch = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ch.dma_desc_num = 4; ch.dma_frame_num = 240;          /* 4×240 ≈ 60ms 環形深度(不改!
                                                           * AUDIO_PATH_LATENCY_MS=62 由此推導) */
    ESP_ERROR_CHECK(i2s_new_channel(&ch, &s_tx, NULL));
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),   /* 無 MCLK 腳,預設即可 */
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        /* ★ v1.4 無聲根因:MONO slot 令 BCLK=256kHz,而 ES8311 無 MCLK 模式的分頻
           (REG02 pre_multi×8)以 BCLK=512kHz 推 4.096MHz/256fs —— 時鐘錯一倍,DAC 死寂。
           官方 coeff 表 16k 最低內部時鐘 1.024MHz,MONO 的 256k×8=2.048M 亦可但需改表;
           取 STEREO(左右聲道重複單聲道樣本)與既有寄存器一致。 */
        .gpio_cfg = { .mclk = I2S_GPIO_UNUSED,            /* ★ 本板無 MCLK 腳(vs StickS3 G18) */
                      .bclk = I2S_PIN_BCLK, .ws = I2S_PIN_LRCK,
                      .dout = I2S_PIN_DOUT, .din = I2S_GPIO_UNUSED,
                      .invert_flags = { 0 } },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx, &std));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx));            /* ★ 常開,之後永不 disable */

    esp_err_t codec = es8311_cardputer_init(board_i2c_bus(), AUDIO_SAMPLE_RATE);
    if (codec != ESP_OK) ESP_LOGE(TAG, "ES8311 init failed (%s); audio may be silent", esp_err_to_name(codec));
    es8311_cardputer_set_volume(100);   /* v1.4:codec 固定 0dB —— 音量單一由軟體增益(apply_volume)
                    控制,修「codec+軟體雙重衰減 ≈ -24dB 近無聲」bug */
    /* NS4150B 無使能腳:耳機插入由硬體自動切換揚聲器/耳機,軟體不管(§8.1) */

    voice_partition_load();

    xTaskCreatePinnedToCore(audio_task, "audio", 4096, NULL, 15, NULL, 1);
    ESP_LOGI(TAG, "audio ready");
    return ESP_OK;
}
