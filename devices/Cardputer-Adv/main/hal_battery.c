/* hal_battery.c -- G10 ADC1 oneshot + 100k/100k 分壓 ×2(指南 §10)。
 * 整數百分比運算(zuowei §16.1-4 慣例);1s 快取;充電狀態不可知恆 false。 */
#include "hal/hal_battery.h"
#include "board_config.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_timer.h"
#include <stdbool.h>

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static bool s_inited;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void ensure_init(void)
{
    if (s_inited) return;
    adc_oneshot_unit_init_cfg_t ucfg = { .unit_id = BAT_ADC_UNIT };
    if (adc_oneshot_new_unit(&ucfg, &s_adc) != ESP_OK) return;
    adc_oneshot_chan_cfg_t ccfg = { .atten = BAT_ADC_ATTEN, .bitwidth = ADC_BITWIDTH_DEFAULT };
    adc_oneshot_config_channel(s_adc, BAT_ADC_CHANNEL, &ccfg);
    adc_cali_curve_fitting_config_t cal = {
        .unit_id = BAT_ADC_UNIT, .chan = BAT_ADC_CHANNEL,
        .atten = BAT_ADC_ATTEN, .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cal, &s_cali) != ESP_OK) s_cali = NULL;
    s_inited = true;
}

static int read_bat_mv(void)                        /* 8 均值 + 校準 + ×2 */
{
    int acc = 0, n = 0;
    for (int i = 0; i < 8; i++) {
        int raw, mv;
        if (adc_oneshot_read(s_adc, BAT_ADC_CHANNEL, &raw) != ESP_OK) continue;
        if (s_cali && adc_cali_raw_to_voltage(s_cali, raw, &mv) == ESP_OK) { acc += mv; n++; }
    }
    if (n == 0) return -1;
    return (acc / n) * BAT_DIVIDER_NUM;             /* 全整數,無 float 轉型問題 */
}

uint8_t hal_battery_pct(void)
{
    ensure_init();
    if (!s_inited) return 0xFF;                     /* ADC 不可用:未知(協定 §5,勿回 0) */
    static uint32_t cache_t; static uint8_t cache_v = 0xFF;
    if (cache_v != 0xFF && now_ms() - cache_t < 1000) return cache_v;   /* 1s 快取 */
    int mv = read_bat_mv();
    if (mv < 0) return cache_v;                     /* 瞬時失敗:回快取(可能 0xFF) */
    if (mv < 2500) {                                /* v1.7:未裝電池(USB 直供)ADC 讀近 0
                                                       → 回未知,UI 顯示 "USB" 而非 "0%" */
        cache_v = 0xFF; cache_t = now_ms();
        return 0xFF;
    }
    int pct = (mv - BAT_EMPTY_MV) * 100 / (BAT_FULL_MV - BAT_EMPTY_MV);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    cache_v = (uint8_t)pct;
    cache_t = now_ms();
    return cache_v;
}

bool hal_battery_charging(void) { return false; }   /* TP4057 STAT 未引出(參考 §3.1) */
