/* app_main.c -- xingzhi-cube 德州撲克進入點(指南 §4.2)。 */
#include "board_config.h"
#include "hal_battery_board.h"     /* hal_battery_boot_baseline 的私有宣告(§8) */
#include "hal/hal_display.h"
#include "hal/hal_audio.h"
#include "app_flow.h"
#include "driver/rtc_io.h"
#include "esp_mac.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "app_main";

/* demo main.c 原樣遷入【实测】:GPIO21 是 RTC 腳,照出廠固件用 rtc_gpio_* 拉高自鎖。
 * 本板真正開關機由第 4 顆硬體電源鍵完成;此腳僅「開機後維持供電」(電池供電時需要,
 * 保守保留),拉低它並不能可靠關機(插 USB 時 5V 旁路自鎖,HARDWARE.md §2)。 */
static void power_latch_on(void)
{
    rtc_gpio_init(PWR_KEEP_PIN);
    rtc_gpio_set_direction(PWR_KEEP_PIN, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_hold_dis(PWR_KEEP_PIN);
    rtc_gpio_set_level(PWR_KEEP_PIN, 1);
}

void app_main(void)
{
    power_latch_on();                       /* 第一行(電池供電兜底;demo 同序) */

    ESP_ERROR_CHECK(hal_display_init());    /* §7 */
    ESP_ERROR_CHECK(hal_audio_init());      /* §6(含 voice 分區 mmap → voice_init) */
    hal_battery_boot_baseline();            /* ★ §8:Wi-Fi 啟動前讀 ADC2 基準,次序硬性 */

    char name[8];
    uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(name, sizeof(name), "XZ-%02X%02X", mac[4], mac[5]);
#ifdef PK_DEBUG_SOLO
    ESP_LOGW(TAG, "[SOLO] smoke build");
#endif
    app_flow_start(name);                   /* 內部 game_init→pbus_start(Wi-Fi 在此才啟) */
    ESP_LOGI(TAG, "boot complete: %s", name);
}
