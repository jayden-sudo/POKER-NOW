/* app_main.c -- StickS3 德州撲克進入點(指南 §12.2)。 */
#include "board_i2c.h"
#include "m5pm1.h"
#include "hal/hal_display.h"
#include "hal/hal_audio.h"
#include "app_flow.h"
#include "esp_mac.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "app_main";

void app_main(void)
{
    ESP_ERROR_CHECK(board_i2c_init());                 /* i2c_new_master_bus(G47/G48) */
    ESP_ERROR_CHECK(m5pm1_init(board_i2c_bus()));
    ESP_ERROR_CHECK(m5pm1_enable_l3b_domain());        /* ★ 必須先於 hal_display_init,否則黑屏 */
    ESP_ERROR_CHECK(m5pm1_poker_btn_cfg());            /* ★ 停用單擊復位 */

    ESP_ERROR_CHECK(hal_display_init());
    ESP_ERROR_CHECK(hal_audio_init());                 /* 含 voice 分區 mmap → voice_init */

    char name[8];
    uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(name, sizeof(name), "S3-%02X%02X", mac[4], mac[5]);
#ifdef PK_DEBUG_SOLO
    ESP_LOGW(TAG, "[SOLO] smoke build");
#endif
    app_flow_start(name);                              /* 內部 game_init → pbus_start */
    ESP_LOGI(TAG, "boot complete: %s", name);
}
