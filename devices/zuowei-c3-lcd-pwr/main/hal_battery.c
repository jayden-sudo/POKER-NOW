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
static bool s_adc_ok;      /* ADC 單元/通道初始化成功;失敗則電量回 0xFF 而非讓整機 panic */
static bool s_inited;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void ensure_init(void)
{
    if (s_inited) return;
    s_inited = true;   /* 提前設,初始化失敗也不反覆重試 */

    /* USB 偵測腳與 ADC 無關,先配置,確保即使 ADC 起不來 hal_battery_pct 仍能判 USB。 */
    gpio_config_t io = { .pin_bit_mask = 1ULL << USB_DETECT_PIN, .mode = GPIO_MODE_INPUT };
    gpio_config(&io);

    /* 電量感測失效不應 brick 整台遊戲機:檢查回傳而非 ESP_ERROR_CHECK(#7)。 */
    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = BAT_ADC_UNIT };
    if (adc_oneshot_new_unit(&unit_cfg, &s_adc) != ESP_OK) {
        ESP_LOGE(TAG, "ADC unit init failed; battery unavailable");
        return;
    }
    adc_oneshot_chan_cfg_t ch_cfg = { .atten = BAT_ADC_ATTEN, .bitwidth = ADC_BITWIDTH_DEFAULT };
    if (adc_oneshot_config_channel(s_adc, BAT_ADC_CHANNEL, &ch_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "ADC channel config failed; battery unavailable");
        return;
    }
    s_adc_ok = true;
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = BAT_ADC_UNIT, .chan = BAT_ADC_CHANNEL,
        .atten = BAT_ADC_ATTEN, .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    s_cali_ok = (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) == ESP_OK);
#endif
    ESP_LOGI(TAG, "battery ADC ready (cali=%d)", (int)s_cali_ok);
}

static int read_bat_mv(void)                  /* 成功回電池 mV,ADC 不可用/全失敗回 -1 */
{
    if (!s_adc_ok) return -1;
    long acc = 0; int ok = 0, r = 0;
    for (int i = 0; i < 16; i++)              /* 只累加成功的讀數,勿讓失敗(r=0)拉低均值(#6) */
        if (adc_oneshot_read(s_adc, BAT_ADC_CHANNEL, &r) == ESP_OK) { acc += r; ok++; }
    if (ok == 0) return -1;
    int raw = (int)(acc / ok), mv;
    if (!(s_cali_ok && adc_cali_raw_to_voltage(s_cali, raw, &mv) == ESP_OK))
        mv = raw * 3100 / 4095;               /* 校準缺席或換算失敗 → 線性近似(#5) */
    return (int)(mv * BAT_DIVIDER);
}

uint8_t hal_battery_pct(void)
{
    ensure_init();
    if (gpio_get_level(USB_DETECT_PIN)) return 0xFF;   /* 插 USB:一律 0xFF(協定 §5) */
    static uint32_t cache_t; static uint8_t cache_v = 0xFF;
    if (cache_v != 0xFF && now_ms() - cache_t < 1000) return cache_v;   /* 1s 快取 */
    int bat_mv = read_bat_mv();
    if (bat_mv < 0) return 0xFF;                                        /* ADC 不可用/讀取失敗 */
    int pct = (bat_mv - BAT_EMPTY_MV) * 100 / (BAT_FULL_MV - BAT_EMPTY_MV);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    cache_v = (uint8_t)pct;
    cache_t = now_ms();
    return cache_v;
}

bool hal_battery_charging(void) { return false; }   /* 板上無 CHRG 狀態腳(產品 §2.4) */
