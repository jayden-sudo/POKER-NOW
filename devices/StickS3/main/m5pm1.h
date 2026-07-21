#pragma once
/* M5PM1 電源管理驅動(I2C 0x6E)。基於 ble-page-turner 真機驗證實作 + 指南 §13 追加。 */
#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

typedef enum {
    M5PM1_PWR_SRC_5VIN = 0,
    M5PM1_PWR_SRC_5VINOUT = 1,
    M5PM1_PWR_SRC_BATTERY = 2,
} m5pm1_pwr_src_t;

esp_err_t m5pm1_init(i2c_master_bus_handle_t bus);
esp_err_t m5pm1_enable_l3b_domain(void);         /* 坑1:LCD 供電 */
esp_err_t m5pm1_poker_btn_cfg(void);             /* 坑2:停用單擊復位 */
esp_err_t m5pm1_restore_pwr_btn_defaults(void);  /* 刷回其他韌體前呼叫 */
esp_err_t m5pm1_get_battery_mv(uint16_t *mv);
esp_err_t m5pm1_get_vin_mv(uint16_t *mv);
esp_err_t m5pm1_get_power_source(m5pm1_pwr_src_t *src);
bool      m5pm1_is_external_powered(void);
bool      m5pm1_is_charging(void);
esp_err_t m5pm1_amp_set(uint8_t on);             /* AW8737 功放:0=關,非0=開(GPIO3 推挽恆高,含讀回自證) */
void      m5pm1_power_off(void);
