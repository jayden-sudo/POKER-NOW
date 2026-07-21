/* hal_input.c -- xingzhi 3 鍵方案(v2.1:按鍵→抽象意圖映射在裝置端)。
 * 物理(上拉低有效):上 G39 / 中 G40 / 下 G0(BOOT)。開關機為專用硬體電源鍵,與此無關。
 *   中 G40 點擊 → UI_OK
 *   上 G39 點擊 → UI_UP (+1/下一項)   上長按 → UI_MENU
 *   下 G0  點擊 → UI_DOWN (-1/上一項)   下長按 → UI_BACK
 * 3 鍵大多單擊即足,返回/選單用上下鍵長按。 */
#include "hal/hal_input.h"
#include "board_config.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#define LONG_MS 600
#define POLL_MS 10

static hal_ui_cb_t s_cb;
static void emit(int ev) { if (ev >= 0 && s_cb) s_cb((hal_ui_event_t)ev); }

typedef struct { bool down, prev_raw, stable, long_fired; int64_t press_us; } keyst_t;

static void btn_task(void *a)
{
    (void)a;
    /* 0=中(OK) 1=上(NEXT) 2=下(PREV) */
    const gpio_num_t pin[3]    = { BTN_INCDEC_PIN, BTN_OK_PIN, BTN_NEXT_PIN };
    const int        single[3] = { UI_OK,          UI_UP,    UI_DOWN };
    const int        longp[3]  = { -1,             UI_MENU,    UI_BACK };
    keyst_t ks[3] = {0};
    for (;;) {
        int64_t now = esp_timer_get_time();
        for (int i = 0; i < 3; i++) {
            keyst_t *k = &ks[i];
            bool raw = gpio_get_level(pin[i]) == 0;
            if (raw == k->prev_raw && raw != k->stable) {
                k->stable = raw;
                if (raw) { k->down = true; k->press_us = now; k->long_fired = false; }
                else { k->down = false; if (!k->long_fired) emit(single[i]); }
            }
            k->prev_raw = raw;
            if (k->down && !k->long_fired && longp[i] >= 0 &&
                (now - k->press_us) >= LONG_MS * 1000) {
                emit(longp[i]); k->long_fired = true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

esp_err_t hal_input_init(hal_ui_cb_t cb)
{
    s_cb = cb;
    gpio_config_t io = {
        .pin_bit_mask = BIT64(BTN_OK_PIN) | BIT64(BTN_NEXT_PIN) | BIT64(BTN_INCDEC_PIN),
        .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    xTaskCreatePinnedToCore(btn_task, "btns", 2560, NULL, 6, NULL, 0);
    return ESP_OK;
}

const char *hal_input_hint(hal_ui_event_t ev)
{
    switch (ev) {
    case UI_OK:   return "MID";
    case UI_UP: return "UP";
    case UI_DOWN: return "DOWN";
    case UI_BACK: return "hold DOWN";
    case UI_MENU: return "hold UP";
    default:      return "?";
    }
}
