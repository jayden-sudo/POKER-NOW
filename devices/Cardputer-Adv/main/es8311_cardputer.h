/* es8311_cardputer.h -- ES8311 最小播放路徑(無 MCLK:SCLK 作時鐘源)。 */
#pragma once
#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>

esp_err_t es8311_cardputer_init(i2c_master_bus_handle_t bus, uint32_t sample_rate);
void es8311_cardputer_set_volume(uint8_t vol_0_100);   /* codec 音量;軟體增益另在 hal_audio */
