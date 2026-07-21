/* hal_input.c -- Cardputer 全鍵盤方案(v2.1:按鍵→抽象意圖映射在裝置端)。
 * TCA8418 鍵盤,直接單擊(免 Fn;絲印 ;=↑ ,=← .=↓ /=→):
 *   Enter          → UI_OK
 *   / (→) ; (↑)    → UI_UP   (+1/下一項)
 *   . (↓) , (←)    → UI_DOWN   (-1/上一項)
 *   ` (ESC 位)     → UI_BACK   (取消/返回上一層)
 *   Tab            → UI_MENU   (系統選單)
 *   G0(BtnA)      → UI_OK     (並聯,實體鍵)
 * 鍵盤鍵位豐富,每個意圖獨立單鍵,無需點/雙/長組合。電氣解碼表在 tca8418.c。 */
#include "hal/hal_input.h"
#include "board_config.h"
#include "board_i2c.h"
#include "tca8418.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdbool.h>

static const char *TAG = "hal_input";
static hal_ui_cb_t s_cb;
static void emit(hal_ui_event_t ev) { if (s_cb) s_cb(ev); }

static void on_key(uint8_t r, uint8_t c, bool pressed)
{
    if (!pressed) return;                       /* 鍵盤:按下沿觸發即可 */
    uint8_t k = tca8418_key_decode(r, c);
#ifdef PK_KEYMAP_DUMP
    ESP_LOGI(TAG, "r=%u c=%u -> 0x%02x('%c')", r, c, k, (k >= 0x20 && k < 0x7F) ? k : '?');
#endif
    switch (k) {
    case TCA_KEY_ENTER: emit(UI_OK);   break;
    case '/': case ';': emit(UI_UP); break;   /* → / ↑ */
    case '.': case ',': emit(UI_DOWN); break;   /* ↓ / ← */
    case '`':           emit(UI_BACK); break;   /* ESC 位 */
    case TCA_KEY_TAB:   emit(UI_MENU); break;
    default: break;
    }
}

/* G0(BtnA)並聯 UI_OK:10ms 輪詢去抖 */
static void g0_task(void *a)
{
    (void)a;
    bool stable = false, last = false;
    for (;;) {
        bool raw = gpio_get_level(BTN_G0_PIN) == 0;
        if (raw == last && raw != stable) { stable = raw; if (raw) emit(UI_OK); }
        last = raw;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t hal_input_init(hal_ui_cb_t cb)
{
    s_cb = cb;
    ESP_ERROR_CHECK(tca8418_init(board_i2c_bus(), KBD_INT_PIN, on_key));
    gpio_config_t io = { .pin_bit_mask = BIT64(BTN_G0_PIN),
                         .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&io);
    xTaskCreatePinnedToCore(g0_task, "btn_g0", 2048, NULL, 6, NULL, 0);
    ESP_LOGI(TAG, "keyboard input ready");
    return ESP_OK;
}

const char *hal_input_hint(hal_ui_event_t ev)
{
    switch (ev) {
    case UI_OK:   return "ok";     /* Adv 鍵盤上 Enter 鍵的絲印即 "ok" */
    case UI_UP: return "Up";
    case UI_DOWN: return "Down";
    case UI_BACK: return "ESC";
    case UI_MENU: return "Tab";
    default:      return "?";
    }
}
