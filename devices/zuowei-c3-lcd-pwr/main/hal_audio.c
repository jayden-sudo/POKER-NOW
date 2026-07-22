/* hal_audio.c -- VB6824 UART PCM 下行(指南 §6)。
 * 輸出流常開靜音填充(tx ring 永不斷流);play_at 樣本級切入;無電池音量鉗。
 * 佇列/排程/切入邏輯與 StickS3 hal_audio.c 逐行同構(凍結語義),只換底層寫入與節拍源。 */
#include "hal/hal_audio.h"
#include "board_config.h"
#include "voice.h"
#include "vb6824.h"
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
static aq_item_t s_pend[4]; static int s_npend;      /* 排程單,按 at_ms 升序 */
static aq_item_t s_imm[8];  static int s_nimm;       /* 即播 FIFO */
static voice_stream_t *s_cur;
static uint32_t s_head_ms;                           /* 下一幀首樣本的「出聲」本地時刻 */

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static bool time_ge(uint32_t a, uint32_t b) { return (int32_t)(a - b) >= 0; }

void hal_audio_play(voice_id_t id)                { aq_item_t i = { .id = id, .sched = false }; if (s_q) xQueueSend(s_q, &i, 0); }
void hal_audio_play_at(voice_id_t id, uint32_t t) { aq_item_t i = { .id = id, .at_ms = t, .sched = true }; if (s_q) xQueueSend(s_q, &i, 0); }
/* stop 經佇列傳遞,清理只在 audio_task 內做,消除對 s_cur/計數器的資料競爭(UAF/double-free,#1)。 */
void hal_audio_stop(void) { aq_item_t i = { .stop = true }; if (s_q) xQueueSend(s_q, &i, 0); }
uint16_t hal_audio_path_latency_ms(void) { return AUDIO_PATH_LATENCY_MS; }
void hal_audio_set_volume(uint8_t pct)
{
    if (pct > 100) pct = 100;
    vb6824_audio_set_output_volume(pct);   /* UART 命令幀,~10B;僅選單變更時呼叫,勿每幀 */
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

static void audio_task(void *arg)
{
    (void)arg;
    static int16_t buf[FRAME];                       /* static:C3 上勿佔任務棧 */

    /* 預灌水位:3 幀靜音(60ms),之後節拍恆定 → 佔用 40–60ms 震盪 */
    memset(buf, 0, sizeof(buf));
    for (int i = 0; i < AUDIO_PREFILL_FRAMES; i++)
        vb6824_audio_write((uint8_t *)buf, sizeof(buf));

    s_head_ms = now_ms() + AUDIO_PATH_LATENCY_MS;    /* 出聲時刻 = 現在 + 全路徑延遲 */
    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        drain_queue();
        memset(buf, 0, sizeof(buf));                 /* 1) 預設本幀 = 靜音(常開填充) */
        if (s_npend && time_ge(s_head_ms + 20, s_pend[0].at_ms)) {   /* 2) 排程搶佔 */
            int off = time_ge(s_pend[0].at_ms, s_head_ms)
                    ? (int)((s_pend[0].at_ms - s_head_ms) * 16) : 0; /* 樣本級切入;遲到 off=0 */
            if (off > FRAME) off = FRAME;
            if (s_cur) voice_close(s_cur);           /* 齊播優先,打斷即播型 */
            s_cur = voice_open(s_pend[0].id);
            pop_pend();
            if (s_cur && off < FRAME) voice_read(s_cur, buf + off, FRAME - off);
        } else if (s_cur) {                          /* 3) 續播當前流 */
            int n = voice_read(s_cur, buf, FRAME);
            if (n < FRAME) { voice_close(s_cur); s_cur = next_immediate();
                             if (s_cur) voice_read(s_cur, buf + (n > 0 ? n : 0), FRAME - (n > 0 ? n : 0)); }
        } else {                                     /* 4) 即播佇列 */
            s_cur = next_immediate();
            if (s_cur) voice_read(s_cur, buf, FRAME);
        }
        vb6824_audio_write((uint8_t *)buf, sizeof(buf));  /* 水位 <180ms,恆不阻塞 */
        s_head_ms += 20;
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(20));   /* 節拍源:FreeRTOS tick(1kHz) */
    }
}

static esp_err_t voice_partition_load(void)   /* 與 StickS3 逐行相同(subtype 0x40,"voice") */
{
    const esp_partition_t *p = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "voice");
    if (!p) { ESP_LOGE(TAG, "voice partition not found"); return ESP_ERR_NOT_FOUND; }
    const void *base; esp_partition_mmap_handle_t h;
    esp_err_t r = esp_partition_mmap(p, 0, p->size, ESP_PARTITION_MMAP_DATA, &base, &h);
    if (r != ESP_OK) { ESP_LOGE(TAG, "voice mmap failed"); return r; }
    return voice_init(base, p->size);         /* 驗 magic 失敗 → 播放靜默 no-op,遊戲照跑 */
}

esp_err_t hal_audio_init(void)
{
    s_q = xQueueCreate(16, sizeof(aq_item_t));
    if (!s_q) { ESP_LOGE(TAG, "audio queue alloc failed"); return ESP_ERR_NO_MEM; }

    vb6824_init(VB_PIN_TX, VB_PIN_RX);        /* UART1@2Mbps + 內部兩 task(prio 9) */
    /* 不註冊 voice_command_cb、不 enable_input:撲克不用喚醒詞與麥克風 */
    vb6824_audio_enable_output(true);         /* ★ 常開,之後永不關(§6.2) */
    vb6824_audio_set_output_volume(PK_DEFAULT_VOLUME_PCT);   /* 開機預設;NVS 值由共用碼稍後蓋上 */

    voice_partition_load();

    xTaskCreate(audio_task, "audio", 4096, NULL, 15, NULL);   /* §3 任務表 */
    ESP_LOGI(TAG, "audio ready (VB6824 uart, latency=%dms)", AUDIO_PATH_LATENCY_MS);
    return ESP_OK;
}
