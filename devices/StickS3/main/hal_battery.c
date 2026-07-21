/* hal_battery.c -- 電量(產品 §2.4,指南 §13.4)。插 USB VBAT 讀 0 → 回 0xFF。 */
#include "hal/hal_battery.h"
#include "m5pm1.h"
#include "esp_timer.h"

static uint8_t lipo_pct_from_mv(uint16_t mv)
{
    static const struct { uint16_t mv; uint8_t pct; } curve[] = {
        { 4150,100 },{ 4050,90 },{ 3950,80 },{ 3870,70 },{ 3800,60 },{ 3750,50 },
        { 3700,40 },{ 3650,30 },{ 3600,20 },{ 3500,10 },{ 3400,5 },{ 3300,0 },
    };
    if (mv >= curve[0].mv) return 100;
    for (size_t i = 1; i < sizeof(curve) / sizeof(curve[0]); i++) {
        if (mv >= curve[i].mv) {
            uint16_t span_mv = curve[i - 1].mv - curve[i].mv;
            uint8_t span_pct = curve[i - 1].pct - curve[i].pct;
            return curve[i].pct + (uint8_t)((uint32_t)(mv - curve[i].mv) * span_pct / span_mv);
        }
    }
    return 0;
}

uint8_t hal_battery_pct(void)
{
    static int64_t s_next; static uint8_t s_cache = 0xFF;
    int64_t now = esp_timer_get_time();
    if (now < s_next) return s_cache;
    s_next = now + 1000000;   /* 1s 節流 */

    uint16_t vin = 0, vbat = 0;
    if (m5pm1_get_vin_mv(&vin) != ESP_OK) { s_cache = 0xFF; return 0xFF; }
    if (vin > 4000) { s_cache = 0xFF; return 0xFF; }   /* 插 USB:一律 0xFF(嚴禁回 0) */
    /* v1.7 修:插 USB 時 VBAT 非零殘值(數百 mV)會繞過 ==0 判斷 → 顯示 0%。
       運作中的鋰電不可能 <3.0V,低於即視為讀值無效 → 未知(顯示 USB)。 */
    if (m5pm1_get_battery_mv(&vbat) != ESP_OK || vbat < 3000) { s_cache = 0xFF; return 0xFF; }
    s_cache = lipo_pct_from_mv(vbat);
    return s_cache;
}

bool hal_battery_charging(void) { return m5pm1_is_charging(); }
