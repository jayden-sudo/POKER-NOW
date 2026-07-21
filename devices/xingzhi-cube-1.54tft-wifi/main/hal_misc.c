/* hal_misc.c -- 雜項(產品 §2.6)+ 設定 NVS 持久化(產品 §5.6 v1.2)+ 裝置類別。
 * = StickS3 hal_misc.c 照抄,唯一改動:device_class → 2(協定 §8.1;S3 §21.3 R12)。 */
#include "board_config.h"
#include "hal/hal_misc.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

uint32_t hal_rand(void) { return esp_random(); }
void hal_yield_watchdog(void) { vTaskDelay(1); }

/* xingzhi device_class = 2(協定 §8.1);覆寫 poker_core 的弱符號 */
uint8_t pk_board_device_class(void) { return 2; }

#define NVS_NS "poker"

void hal_settings_load(uint8_t *volume, uint8_t *brightness)
{
    if (volume) *volume = 0;
    if (brightness) *brightness = 0;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    if (volume)     { uint8_t v; if (nvs_get_u8(h, "vol", &v) == ESP_OK) *volume = v; }
    if (brightness) { uint8_t b; if (nvs_get_u8(h, "bri", &b) == ESP_OK) *brightness = b; }
    nvs_close(h);
}

static void save_u8(const char *key, uint8_t v)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, key, v);
    nvs_commit(h);
    nvs_close(h);
}
void hal_settings_save_volume(uint8_t volume)      { save_u8("vol", volume); }
void hal_settings_save_brightness(uint8_t brightness) { save_u8("bri", brightness); }

/* v1.4:板級預設音量/亮度(覆寫共用碼 weak;值在 board_config.h) */
uint8_t pk_board_default_volume(void)     { return PK_DEFAULT_VOLUME_PCT; }
uint8_t pk_board_default_brightness(void) { return PK_DEFAULT_BRIGHTNESS_PCT; }
