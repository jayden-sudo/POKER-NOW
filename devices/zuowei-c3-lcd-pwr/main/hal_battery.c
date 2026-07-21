/* hal_battery.c -- 電量(產品 §2.4;指南 §10)。
 * demo battery.c 改造:ADC1 oneshot + curve-fitting 校準 + 16 均值 + 分壓 6.08。
 * 插 USB → ADC 讀充電電壓非電池 OCV,一律回 0xFF(顯示 USB;嚴禁回 0)。惰性初始化。 */
#include "hal/hal_battery.h"
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
static bool s_cali_ok;
static bool s_inited;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void ensure_init(void)
{
    if (s_inited) return;
    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = BAT_ADC_UNIT };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_adc));
    adc_oneshot_chan_cfg_t ch_cfg = { .atten = BAT_ADC_ATTEN, .bitwidth = ADC_BITWIDTH_DEFAULT };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, BAT_ADC_CHANNEL, &ch_cfg));
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = BAT_ADC_UNIT, .chan = BAT_ADC_CHANNEL,
        .atten = BAT_ADC_ATTEN, .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    s_cali_ok = (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) == ESP_OK);
#endif
    gpio_config_t io = { .pin_bit_mask = 1ULL << USB_DETECT_PIN, .mode = GPIO_MODE_INPUT };
    gpio_config(&io);
    s_inited = true;
    ESP_LOGI(TAG, "battery ADC ready (cali=%d)", (int)s_cali_ok);
}

static int read_bat_mv(void)                  /* = demo battery_read 的 16 均值 + 校準 + ×6.08 */
{
    int acc = 0;
    for (int i = 0; i < 16; i++) { int r = 0; adc_oneshot_read(s_adc, BAT_ADC_CHANNEL, &r); acc += r; }
    int raw = acc / 16;
    int mv;
    if (s_cali_ok) adc_cali_raw_to_voltage(s_cali, raw, &mv);
    else mv = raw * 3100 / 4095;              /* 12dB/12bit 線性近似 */
    return (int)(mv * BAT_DIVIDER);
}

uint8_t hal_battery_pct(void)
{
    ensure_init();
    if (gpio_get_level(USB_DETECT_PIN)) return 0xFF;   /* 插 USB:一律 0xFF(協定 §5) */
    static uint32_t cache_t; static uint8_t cache_v = 0xFF;
    if (cache_v != 0xFF && now_ms() - cache_t < 1000) return cache_v;   /* 1s 快取 */
    int bat_mv = read_bat_mv();
    int pct = (bat_mv - BAT_EMPTY_MV) * 100 / (BAT_FULL_MV - BAT_EMPTY_MV);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    cache_v = (uint8_t)pct;
    cache_t = now_ms();
    return cache_v;
}

bool hal_battery_charging(void) { return false; }   /* 板上無 CHRG 狀態腳(產品 §2.4) */
