/* app_main.c -- Cardputer-Adv 德州撲克進入點(指南 §4.2)。 */
#include "board_config.h"
#include "board_i2c.h"
#include "hal/hal_display.h"
#include "hal/hal_audio.h"
#include "app_flow.h"
#include "driver/gpio.h"
#include "esp_mac.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "app_main";

/* 共享 SPI 匯流排衛生(§13):本專案不初始化 microSD / Cap LoRa,但兩者共享
 * G40/G14/G39;若 Cap 模組在位、CS 懸浮,SX1262 可能亂駕 MISO(參考 §13.6:
 * Launcher 在 Adv 上需拉高 G5 的既往經驗)。開機把兩個片選鎖定為「未選中」。 */
static void bus_hygiene(void)
{
    gpio_config_t io = {
        .pin_bit_mask = BIT64(UNUSED_CS_LORA) | BIT64(UNUSED_CS_SDCARD),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(UNUSED_CS_LORA, 1);
    gpio_set_level(UNUSED_CS_SDCARD, 1);
}

void app_main(void)
{
    bus_hygiene();

    ESP_ERROR_CHECK(board_i2c_init());      /* i2c_new_master_bus(G8/G9);鍵盤/codec 共用 */
    ESP_ERROR_CHECK(hal_display_init());    /* §9 */
    ESP_ERROR_CHECK(hal_audio_init());      /* §8(含 voice 分區 mmap → voice_init) */

    char name[8];
    uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(name, sizeof(name), "CP-%02X%02X", mac[4], mac[5]);
#ifdef PK_DEBUG_SOLO
    ESP_LOGW(TAG, "[SOLO] smoke build");
#endif
    app_flow_start(name);                   /* 內部 game_init→pbus_start;鍵盤亦在此鏈上初始化 */
    ESP_LOGI(TAG, "boot complete: %s", name);
}
