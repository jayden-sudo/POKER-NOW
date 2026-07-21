/* tca8418.h -- TCA8418 鍵盤掃描晶片驅動(I2C 0x34,INT=G11 低有效)。
 * 上報電氣座標 (row 0-6, col 0-7) 的按下/釋放事件,並提供電氣座標 → 鍵值
 * 解碼表(§17.5 R16 實機校準);撲克語義在 hal_input.c(§7)。 */
#pragma once
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

typedef void (*tca8418_key_cb_t)(uint8_t row, uint8_t col, bool pressed);

/* 解碼後鍵值:可列印鍵 = ASCII(普通層字元);特殊鍵 = 以下常數。 */
enum {
    TCA_KEY_NONE  = 0,
    TCA_KEY_BS    = 0x08,
    TCA_KEY_TAB   = 0x09,
    TCA_KEY_ENTER = 0x0D,
    TCA_KEY_FN    = 0x80,
    TCA_KEY_SHIFT = 0x81,
    TCA_KEY_CTRL  = 0x82,
    TCA_KEY_OPT   = 0x83,
    TCA_KEY_ALT   = 0x84,
};

/* 電氣 (row 0-6, col 0-7) → 鍵值(普通層);範圍外回 TCA_KEY_NONE。 */
uint8_t tca8418_key_decode(uint8_t row, uint8_t col);

/* 初始化 7×8 矩陣 + FIFO + 中斷,並建立 kbd task(core0, prio6, 3072)。
 * bus = board_i2c_bus();cb 於 kbd task 上下文被呼叫。 */
esp_err_t tca8418_init(i2c_master_bus_handle_t bus, gpio_num_t int_pin, tca8418_key_cb_t cb);
