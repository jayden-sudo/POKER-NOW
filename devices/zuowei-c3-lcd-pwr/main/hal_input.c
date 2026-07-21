/* hal_input.c -- zuowei 3 鍵方案(v2.1:按鍵→抽象意圖映射在裝置端)。
 * 物理(上拉低有效):左 G8 / 中 G9 / 右 G7。
 *   中 G9 點擊 → UI_OK   (中鍵長按 ≥BTN_PWR_HOLD_MS = 關機,沿用原行為,不作 UI 意圖)
 *   左 G8 點擊 → UI_UP (+1/下一項)   左長按 → UI_MENU
 *   右 G7 點擊 → UI_DOWN (-1/上一項)   右長按 → UI_BACK
 * 3 鍵大多單擊即足,返回/選單用左右鍵長按(中鍵長按已被關機佔用,故不碰)。 */
#include "hal/hal_input.h"
#include "hal_power_board.h"
#include "board_config.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#define LONG_MS 600
#define POLL_MS 10

static hal_ui_cb_t s_cb;
static void emit(int ev) { if (ev >= 0 && s_cb) s_cb((hal_ui_event_t)ev); }

/* 左/右鍵:單擊 + 長按(長按觸發一次,放開不再補單擊) */
typedef struct { bool down, prev_raw, stable, long_fired; int64_t press_us; } keyst_t;

static void btn_task(void *a)
{
    (void)a;
    const gpio_num_t lr_pin[2] = { BTN_OK_PIN /*左 G8=NEXT*/, BTN_NEXT_PIN /*右 G7=PREV*/ };
    const int lr_single[2]     = { UI_UP, UI_DOWN };
    const int lr_long[2]       = { UI_MENU, UI_BACK };
    keyst_t lr[2] = {0};
    /* 中鍵(G9):短放=UI_OK,長按=關機 */
    bool mid_prev = false, mid_stable = false, mid_down = false; int mid_ms = 0;
    for (;;) {
        int64_t now = esp_timer_get_time();
        for (int i = 0; i < 2; i++) {
            keyst_t *k = &lr[i];
            bool raw = gpio_get_level(lr_pin[i]) == 0;
            if (raw == k->prev_raw && raw != k->stable) {
                k->stable = raw;
                if (raw) { k->down = true; k->press_us = now; k->long_fired = false; }
                else { k->down = false; if (!k->long_fired) emit(lr_single[i]); }
            }
            k->prev_raw = raw;
            if (k->down && !k->long_fired && (now - k->press_us) >= LONG_MS * 1000) {
                emit(lr_long[i]); k->long_fired = true;
            }
        }
        /* 中鍵:去抖 + 短放=OK / 長按=關機 */
        bool mraw = gpio_get_level(BTN_PWR_PIN) == 0;
        if (mraw == mid_prev && mraw != mid_stable) {
            mid_stable = mraw;
            if (mraw) { mid_down = true; mid_ms = 0; }
            else { if (mid_down && mid_ms < BTN_PWR_HOLD_MS) emit(UI_OK); mid_down = false; }
        }
        mid_prev = mraw;
        if (mid_down) { mid_ms += POLL_MS; if (mid_ms >= BTN_PWR_HOLD_MS) board_poweroff(); }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

esp_err_t hal_input_init(hal_ui_cb_t cb)
{
    s_cb = cb;
    gpio_config_t io = { .pin_bit_mask = BIT64(BTN_OK_PIN) | BIT64(BTN_NEXT_PIN) | BIT64(BTN_PWR_PIN),
                         .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    ESP_ERROR_CHECK(gpio_config(&io));
    xTaskCreate(btn_task, "buttons", 2560, NULL, 6, NULL);
    return ESP_OK;
}

const char *hal_input_hint(hal_ui_event_t ev)
{
    switch (ev) {
    case UI_OK:   return "MID";
    case UI_UP: return "LEFT";
    case UI_DOWN: return "RIGHT";
    case UI_BACK: return "hold RIGHT";
    case UI_MENU: return "hold LEFT";
    default:      return "?";
    }
}
