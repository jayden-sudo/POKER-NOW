/* board_i2c.c -- 共享 I2C 匯流排(G47/G48,IDF v6 新驅動 i2c_new_master_bus) */
#include "board_i2c.h"
#include "board_config.h"
#include "esp_check.h"

static const char *TAG = "board_i2c";
static i2c_master_bus_handle_t s_bus;

esp_err_t board_i2c_init(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg, &s_bus), TAG, "i2c_new_master_bus failed");
    return ESP_OK;
}

i2c_master_bus_handle_t board_i2c_bus(void) { return s_bus; }
