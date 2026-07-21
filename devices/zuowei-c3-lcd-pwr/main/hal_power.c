/* hal_power.c -- GPIO13 電源自鎖 + 關機攔截(產品 §2.5;S3 §21.3 R11)。 */
#include "hal/hal_power.h"
#include "hal_power_board.h"
#include "board_config.h"
#include "pbus.h"                 /* pbus_is_master / pbus_my_player_id(凍結介面) */
#include "game_state.h"           /* game_submit_leave(凍結介面) */
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "hal_power";

void power_latch_on(void)         /* demo main.c 原樣遷入【实机验证】 */
{
    gpio_reset_pin(PWR_KEEP_PIN);
    gpio_set_direction(PWR_KEEP_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(PWR_KEEP_PIN, 1);
}

/* R11:main 提供單一定義。本機真的會走到這裡(中鍵長按),非 StickS3 式空殼。
 * 語義(產品 §2.5):若本機為 Master → 觸發協定 §9.3 交接,最多等 2s → 返回後才允許斷電。
 * 凍結介面下的實作路徑:C_LEAVE(game_submit_leave)→ Master 端(可能就是本機引擎)
 * 完成 E_PLAYER_LEFT 後按 §9.3.5 交接;本機以 pbus_is_master() 轉 false 為交接完成訊號。
 * 局中/2s 超時交接未完 → 照樣斷電,協定 §9.5 故障接管(≤6s)兜底 —— 不比拔電池差。 */
void app_prepare_poweroff(void)
{
    if (pbus_my_player_id() != 0xFF)
        game_submit_leave();                       /* 已入桌才有 LEAVE 可送 */
    if (!pbus_is_master()) { vTaskDelay(pdMS_TO_TICKS(150)); return; }  /* 讓 C_LEAVE 出去 */
    for (int i = 0; i < 20 && pbus_is_master(); i++)
        vTaskDelay(pdMS_TO_TICKS(100));            /* ≤2s 等 E_MASTER_HANDOFF 生效 */
    ESP_LOGW(TAG, "poweroff: master=%d after handoff wait", (int)pbus_is_master());
}

void board_poweroff(void)
{
    app_prepare_poweroff();                        /* ★ 關機前必走(產品 §2.5 硬性) */
    ESP_LOGW(TAG, "power off (PWR_KEEP -> LOW)");
    vTaskDelay(pdMS_TO_TICKS(50));                 /* 讓日誌從 USB-JTAG 出去 */
    gpio_set_level(PWR_KEEP_PIN, 0);               /* 整機斷電(USB/電池皆然) */
    vTaskDelay(portMAX_DELAY);                     /* 插著 USB 時斷不掉?見指南 §7.2 注意 2 */
}
