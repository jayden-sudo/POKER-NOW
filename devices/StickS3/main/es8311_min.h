#pragma once
/* es8311_min -- 最小 ES8311 codec 初始化(I2C 0x18,MCLK 模式,16k/16bit)。
 * 避開 espressif/es8311 元件的網路與 legacy-i2c 相依(指南 §2.4 預案:自帶驅動)。 */
#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>

esp_err_t es8311_min_init(i2c_master_bus_handle_t bus, uint32_t mclk_hz, uint32_t sample_rate);
void es8311_min_set_volume(uint8_t vol_0_100);   /* codec 固定音量;軟體增益另在 hal_audio */
