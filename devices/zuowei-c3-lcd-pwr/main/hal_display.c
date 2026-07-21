/* hal_display.c -- esp_lcd ST7789 + esp_lvgl_port + LEDC 背光(指南 §8)。
 * = demo display.c 初始化(CS=GPIO12、spi_mode 3、80MHz、單緩衝 20 行、swap_bytes)
 * + demo backlight.c(併入,加 10% 下限鉗)+ StickS3 render 語義(物件一次建成、
 * render 只改屬性)+ 240×240 佈局(§8.2)。物件集/render 邏輯與 StickS3 逐行同構。 */
#include "hal/hal_display.h"
#include "board_config.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_common.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "display";
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;


/* ---- 背光 LEDC(demo backlight.c 參數原樣:5kHz/10-bit/ch0/GPIO5)---- */
static void bl_init_off(void)
{
    ledc_timer_config_t t = { .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = LEDC_TIMER_0,
                              .duty_resolution = LEDC_TIMER_10_BIT, .freq_hz = 5000,
                              .clk_cfg = LEDC_AUTO_CLK };
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    ledc_channel_config_t c = { .gpio_num = LCD_PIN_BL, .speed_mode = LEDC_LOW_SPEED_MODE,
                                .channel = LEDC_CHANNEL_0, .timer_sel = LEDC_TIMER_0,
                                .duty = 0, .hpoint = 0,
                                .flags = { .output_invert = BL_OUTPUT_INVERT } };
    ESP_ERROR_CHECK(ledc_channel_config(&c));
}
void hal_display_set_brightness(uint8_t pct)
{
    if (pct < 10) pct = 10;              /* 產品 §2.2:PWM 佔空比勿低於 10%(防調到全黑找不回選單) */
    if (pct > 100) pct = 100;
    uint32_t duty = (1023u * pct) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

/* ---- 靜態 UI(§8.2 的 240×240 佈局)---- */


esp_err_t hal_display_init(void)
{
    bl_init_off();                            /* 佔空 0 起步;init 完成後才點亮到 80% */

    spi_bus_config_t bus = {
        .mosi_io_num = LCD_PIN_MOSI, .sclk_io_num = LCD_PIN_SCLK,
        .miso_io_num = GPIO_NUM_NC, .quadwp_io_num = GPIO_NUM_NC, .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_H_RES * 80 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = LCD_PIN_CS,            /* GPIO12 —— 缺它整屏全黑(§8.1;conflict 警告忽略) */
        .dc_gpio_num = LCD_PIN_DC,
        .spi_mode = LCD_SPI_MODE,             /* 本板 mode 3 */
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .trans_queue_depth = 10, .lcd_cmd_bits = 8, .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_SPI_HOST, &io_cfg, &s_io));

    esp_lcd_panel_dev_config_t pcfg = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB, .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(s_io, &pcfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, LCD_INVERT_COLOR));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel, LCD_SWAP_XY));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, LCD_MIRROR_X, LCD_MIRROR_Y));
    esp_lcd_panel_set_gap(s_panel, LCD_OFFSET_X, LCD_OFFSET_Y);
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    port.task_priority = 4; port.task_stack = 8192;   /* §3:C3 單核,不設 task_affinity(用預設 -1) */
    ESP_ERROR_CHECK(lvgl_port_init(&port));
    lvgl_port_display_cfg_t disp = {
        .io_handle = s_io, .panel_handle = s_panel,
        .buffer_size = LCD_H_RES * 20,        /* 單緩衝 20 行 ≈ 9.6KB(§8.1;C3 無 PSRAM) */
        .double_buffer = false,
        .hres = LCD_H_RES, .vres = LCD_V_RES,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = { .swap_xy = LCD_SWAP_XY, .mirror_x = LCD_MIRROR_X, .mirror_y = LCD_MIRROR_Y },
        .flags = { .buff_dma = true, .swap_bytes = true },
    };
    if (!lvgl_port_add_disp(&disp)) return ESP_FAIL;

    if (lvgl_port_lock(0)) { ui_lvgl_attach(LCD_H_RES, LCD_V_RES); lvgl_port_unlock(); }
    hal_display_set_brightness(PK_DEFAULT_BRIGHTNESS_PCT);           /* NVS 值由共用碼稍後 set_brightness 蓋上 */
    ESP_LOGI(TAG, "ST7789 + LVGL ready %dx%d (CS=%d)", LCD_H_RES, LCD_V_RES, LCD_PIN_CS);
    return ESP_OK;
}




