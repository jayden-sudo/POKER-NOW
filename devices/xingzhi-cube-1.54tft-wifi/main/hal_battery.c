/* hal_battery.c -- ADC2_CH6(GPIO17) + 充電檢測 GPIO38(指南 §8)。
 * ADC2 與 Wi-Fi 衝突:開機(Wi-Fi 前)讀基準,之後讀失敗回快取;充電中回 0xFF。 */
#include "hal/hal_battery.h"
#include "hal_battery_board.h"
#include "board_config.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "hal_battery";
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static bool s_cali_ok, s_inited;
static bool s_adc_ok;               /* ADC 初始化成功;失敗則回快取/0xFF 而非 panic 整機 */
static uint8_t s_cache = 0xFF;      /* 最後一次成功換算的 pct(Wi-Fi 佔用 ADC2 時回它) */
static int64_t s_next_us;

static void ensure_init(void)
{
    if (s_inited) return;
    s_inited = true;   /* 提前設,初始化失敗也不反覆重試 */

    gpio_config_t io = { .pin_bit_mask = 1ULL << CHARGE_DETECT_PIN, .mode = GPIO_MODE_INPUT };
    gpio_config(&io);

    /* 電量感測失效不應 brick 整台:檢查回傳而非 ESP_ERROR_CHECK(#7)。 */
    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = BAT_ADC_UNIT };
    if (adc_oneshot_new_unit(&unit_cfg, &s_adc) != ESP_OK) {
        ESP_LOGE(TAG, "ADC unit init failed; battery unavailable");
        return;
    }
    adc_oneshot_chan_cfg_t ch_cfg = { .atten = BAT_ADC_ATTEN, .bitwidth = ADC_BITWIDTH_12 };
    if (adc_oneshot_config_channel(s_adc, BAT_ADC_CHANNEL, &ch_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "ADC channel config failed; battery unavailable");
        return;
    }
    s_adc_ok = true;
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = BAT_ADC_UNIT, .chan = BAT_ADC_CHANNEL,
        .atten = BAT_ADC_ATTEN, .bitwidth = ADC_BITWIDTH_12,
    };
    s_cali_ok = (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) == ESP_OK);
#endif
    ESP_LOGI(TAG, "battery ADC2_CH6 ready (cali=%d)", (int)s_cali_ok);
}

static int try_read_bat_mv(void)            /* 成功回電池 mV,失敗回 -1(demo battery.c 邏輯) */
{
    if (!s_adc_ok) return -1;
    long acc = 0; int ok = 0, r = 0;
    for (int i = 0; i < 16; i++) {
        if (adc_oneshot_read(s_adc, BAT_ADC_CHANNEL, &r) == ESP_OK) { acc += r; ok++; }
    }
    if (ok == 0) return -1;                 /* Wi-Fi 佔用:ESP_ERR_TIMEOUT,非亂數 */
    int raw = (int)(acc / ok), mv;
    if (!(s_cali_ok && adc_cali_raw_to_voltage(s_cali, raw, &mv) == ESP_OK))
        mv = raw * 3100 / 4095;             /* 校準缺席或換算失敗 → 線性近似(#5) */
    return (int)(mv * BAT_DIVIDER);         /* float 乘積顯式轉 int(zuowei §16.1-4) */
}

static uint8_t pct_from_mv(int mv)
{
    int pct = (mv - BAT_EMPTY_MV) * 100 / (BAT_FULL_MV - BAT_EMPTY_MV);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return (uint8_t)pct;
}

void hal_battery_boot_baseline(void)        /* app_main 於 app_flow_start(=Wi-Fi)前呼叫 */
{
    ensure_init();
    int mv = try_read_bat_mv();
    if (mv > 0) s_cache = pct_from_mv(mv);
    ESP_LOGI(TAG, "boot baseline: %d mV -> %u%%", mv, (unsigned)s_cache);
}

uint8_t hal_battery_pct(void)
{
    ensure_init();
    if (gpio_get_level(CHARGE_DETECT_PIN)) return 0xFF;   /* 充電中:讀數虛高 → 未知(§8.1-3) */
    int64_t now = esp_timer_get_time();
    if (now < s_next_us) return s_cache;                  /* 1s 節流(pbus 2s + app_flow 10s 都來要) */
    s_next_us = now + 1000000;
    int mv = try_read_bat_mv();
    if (mv > 0) s_cache = pct_from_mv(mv);                /* 失敗:保持快取(開機基準) */
    return s_cache;
}

bool hal_battery_charging(void)
{
    ensure_init();
    return gpio_get_level(CHARGE_DETECT_PIN) == 1;        /* 高=充電中【实测】 */
}
