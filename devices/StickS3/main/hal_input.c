/* hal_input.c -- StickS3 2 鍵方案(v2.1:按鍵→抽象意圖映射在裝置端)。
 * 物理:KEY1(G11)=OK 鍵,KEY2(G12)=NEXT 鍵(上拉低有效)。左側電源鍵不接 MCU。
 * 以「點擊 / 雙擊 / 長按」組合出 5 種意圖:
 *   OK   點擊 → UI_OK        OK   雙擊 → UI_MENU
 *   NEXT 點擊 → UI_UP      NEXT 雙擊 → UI_DOWN      NEXT 長按 → UI_BACK
 * (OK 長按保留未用)。手勢合成(點/雙/長)本地完成,不在 common。 */
#include "hal/hal_input.h"
#include "board_config.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#define LONG_MS   600
#define DOUBLE_MS 350
#define POLL_MS   10

static hal_ui_cb_t s_cb;

/* 每鍵手勢偵測狀態機 */
typedef struct {
    bool     down, prev_raw, stable;
    int64_t  press_us, release_us;
    bool     long_fired, click_pending;
} keyst_t;

/* 手勢 → 意圖:idx0=OK 鍵,idx1=NEXT 鍵;-1=不觸發 */
static const int MAP_SINGLE[2] = { UI_OK,   UI_UP };
static const int MAP_DOUBLE[2] = { UI_MENU, UI_DOWN };
static const int MAP_LONG[2]   = { -1,      UI_BACK };

static void emit(int ev) { if (ev >= 0 && s_cb) s_cb((hal_ui_event_t)ev); }

static void btn_task(void *a)
{
    (void)a;
    const gpio_num_t pin[2] = { BTN_OK_PIN, BTN_NEXT_PIN };
    keyst_t ks[2] = {0};
    for (;;) {
        int64_t now = esp_timer_get_time();
        for (int i = 0; i < 2; i++) {
            keyst_t *k = &ks[i];
            bool raw = gpio_get_level(pin[i]) == 0;
            /* 去抖:連續兩拍一致才視為穩定電平 */
            if (raw == k->prev_raw && raw != k->stable) {
                k->stable = raw;
                if (raw) {                          /* 按下沿 */
                    k->down = true; k->press_us = now; k->long_fired = false;
                } else {                            /* 放開沿 */
                    k->down = false;
                    if (!k->long_fired) {
                        if (k->click_pending && (now - k->release_us) < DOUBLE_MS * 1000) {
                            emit(MAP_DOUBLE[i]); k->click_pending = false;   /* 雙擊 */
                        } else {
                            k->click_pending = true; k->release_us = now;   /* 待定單擊 */
                        }
                    }
                }
            }
            k->prev_raw = raw;
            /* 長按:按住超過門檻即觸發一次(且不再視為單/雙擊) */
            if (k->down && !k->long_fired && (now - k->press_us) >= LONG_MS * 1000) {
                emit(MAP_LONG[i]); k->long_fired = true; k->click_pending = false;
            }
            /* 待定單擊逾時 → 確認為單擊 */
            if (k->click_pending && (now - k->release_us) >= DOUBLE_MS * 1000) {
                emit(MAP_SINGLE[i]); k->click_pending = false;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

esp_err_t hal_input_init(hal_ui_cb_t cb)
{
    s_cb = cb;
    gpio_config_t io = {
        .pin_bit_mask = BIT64(BTN_OK_PIN) | BIT64(BTN_NEXT_PIN),
        .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
    xTaskCreatePinnedToCore(btn_task, "btns", 2560, NULL, 6, NULL, 0);
    return ESP_OK;
}

const char *hal_input_hint(hal_ui_event_t ev)
{
    switch (ev) {
    case UI_OK:   return "OK";
    case UI_UP: return "NEXT";
    case UI_DOWN: return "NEXT x2";
    case UI_BACK: return "hold NEXT";
    case UI_MENU: return "OK x2";
    default:      return "?";
    }
}
