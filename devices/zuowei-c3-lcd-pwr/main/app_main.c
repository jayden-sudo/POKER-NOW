/* app_main.c -- zuowei-c3 德州撲克進入點。 */
#include "board_config.h"
#include "hal_power_board.h"       /* power_latch_on 的私有宣告(見 §7.1) */
#include "hal/hal_display.h"
#include "hal/hal_audio.h"
#include "app_flow.h"
#include "esp_mac.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "app_main";

void app_main(void)
{
    power_latch_on();          /* ★★ 必須是第一行(§7.2):電池開機時使用者長按中鍵,
                                *   鬆手前不自鎖 = 直接斷電。任何 init 都不得排在它前面。 */

    ESP_ERROR_CHECK(hal_display_init());   /* §8:含背光 LEDC(預設亮度由共用碼經 NVS 設回) */
    ESP_ERROR_CHECK(hal_audio_init());     /* §6:vb6824_init + voice 分區 mmap + audio task */
    /* hal_input_init 由共用 input_gesture_init 內部呼叫;
       hal_battery 為惰性初始化(首次讀取時 init,§10),此處無事可做 */

    char name[8];
    uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(name, sizeof(name), "C3-%02X%02X", mac[4], mac[5]);
#ifdef PK_DEBUG_SOLO
    ESP_LOGW(TAG, "[SOLO] smoke build");
#endif
    app_flow_start(name);                  /* 內部 game_init → pbus_start;不返回業務控制權 */
    ESP_LOGI(TAG, "boot complete: %s", name);
}
