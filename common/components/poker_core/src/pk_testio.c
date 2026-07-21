/*
 * pk_testio.c -- 串口測試通道(v2.1:注入抽象意圖,與裝置物理按鍵無關)。
 * RX:經 USB-Serial-JTAG 收單字元 → 注入 UI 意圖,主機端可全自動驅動 UI:
 *   o=UI_OK  u=UI_UP  d=UI_DOWN  b=UI_BACK  m=UI_MENU
 * TX:UI 語義 trace 由 app_screens.c 於每次 render 輸出(tag="ui")。
 * 常駐啟用:本產品 console 即實體 USB,無安全疑慮(協定 §16)。
 */
#include "hal/hal_input.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "testio";

void pk_test_ui(hal_ui_event_t ev);   /* app_flow.c 提供 */

static void testio_task(void *arg)
{
    (void)arg;
    uint8_t buf[8];
    for (;;) {
        int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(200));
        for (int i = 0; i < n; i++) {
            int ev = -1;
            switch (buf[i]) {
            case 'o': ev = UI_OK;   break;
            case 'u': ev = UI_UP;   break;
            case 'd': ev = UI_DOWN; break;
            case 'b': ev = UI_BACK; break;
            case 'm': ev = UI_MENU; break;
            default: break;   /* 換行等雜訊忽略 */
            }
            if (ev >= 0) { ESP_LOGI(TAG, "inject %d", ev); pk_test_ui((hal_ui_event_t)ev); }
        }
    }
}

void pk_testio_start(void)
{
    usb_serial_jtag_driver_config_t cfg = {
        .rx_buffer_size = 256,
        .tx_buffer_size = 256,   /* 驅動要求非 0;console 日誌 TX 不經此驅動 */
    };
    esp_err_t r = usb_serial_jtag_driver_install(&cfg);
    if (r != ESP_OK && r != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "usb_serial_jtag driver install failed: %d (injection disabled)", r);
        return;
    }
    xTaskCreate(testio_task, "testio", 3072, NULL, 3, NULL);
    ESP_LOGI(TAG, "test injection ready (o/n/p/b/m)");
}
