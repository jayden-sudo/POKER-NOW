/* es8311_min.c -- ES8311 DAC 播放路徑最小初始化。
 * 16kHz mono、MCLK 由 G18 提供(256fs=4.096MHz)、codec 為 I2S slave。
 * 寄存器序列對齊 espressif/es8311 官方元件(esp-bsp)coeff 表
 * {4096000,16000}:pre_div=1 pre_multi=x1 bclk_div=4 lrck=256 osr=0x10。
 * §21.R:舊序列 0x01=0x30 未開 DAC 時鐘、缺 0x12/0x13 輸出級 —— 已修正。 */
#include "es8311_min.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "es8311";
static i2c_master_dev_handle_t s_dev;

static esp_err_t wr(uint8_t reg, uint8_t val)
{
    uint8_t f[2] = { reg, val };
    return i2c_master_transmit(s_dev, f, 2, 100);
}
static esp_err_t rd(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 100);
}

typedef struct { uint8_t reg, val; } rv_t;

esp_err_t es8311_min_init(i2c_master_bus_handle_t bus, uint32_t mclk_hz, uint32_t sample_rate)
{
    (void)mclk_hz; (void)sample_rate;
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x18,
        .scl_speed_hz = 100 * 1000,
    };
    esp_err_t r = i2c_master_bus_add_device(bus, &cfg, &s_dev);
    if (r != ESP_OK) return r;

    /* 復位(官方元件:0x1F → 20ms → 0x00 → 0x80 CSM 上電、slave) */
    wr(0x00, 0x1F);
    vTaskDelay(pdMS_TO_TICKS(20));
    wr(0x00, 0x00);
    wr(0x00, 0x80);

    static const rv_t seq[] = {
        { 0x01, 0x3F },                   /* clkmgr:MCLK 取自 MCLK 腳,全部時鐘使能(舊值 0x30 DAC 時鐘關) */
        { 0x02, 0x00 },                   /* pre_div=1、pre_multi=x1(舊值 0x10 = x4,分頻錯) */
        { 0x03, 0x10 },                   /* fs_mode=0,ADC OSR */
        { 0x16, 0x24 },
        { 0x04, 0x10 },                   /* DAC OSR */
        { 0x05, 0x00 },                   /* adc_div=1 dac_div=1 */
        { 0x06, 0x03 },                   /* bclk_div=4 */
        { 0x07, 0x00 }, { 0x08, 0xFF },   /* LRCK 分頻 256 */
        { 0x09, 0x0C },                   /* SDP In:16bit I2S */
        { 0x0A, 0x0C },                   /* SDP Out:16bit I2S */
        { 0x0B, 0x00 }, { 0x0C, 0x00 },
        { 0x10, 0x1F }, { 0x11, 0x7F },   /* system power up analog */
        { 0x0D, 0x01 },                   /* 類比電路上電 */
        { 0x0E, 0x02 },                   /* PGA/ADC modulator 使能 */
        { 0x12, 0x00 },                   /* DAC 上電(舊序列缺) */
        { 0x13, 0x10 },                   /* DAC 輸出接 HP drive(舊序列缺,輸出級斷路) */
        { 0x1C, 0x6A },                   /* ADC EQ bypass */
        { 0x37, 0x08 },                   /* DAC EQ bypass / ramp */
        { 0x45, 0x00 },
        { 0x31, 0x00 },                   /* DAC unmute */
        { 0x32, 0xBF },                   /* DAC volume(初值,後由 set_volume 覆寫) */
    };
    for (size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
        esp_err_t e = wr(seq[i].reg, seq[i].val);
        if (e != ESP_OK) { ESP_LOGW(TAG, "reg 0x%02x write failed (%d)", seq[i].reg, e); }
    }

    /* 關鍵寄存器讀回自證(時鐘/格式/DAC 路徑/輸出級) */
    static const rv_t chk[] = {
        { 0x01, 0x3F }, { 0x02, 0x00 }, { 0x06, 0x03 }, { 0x09, 0x0C },
        { 0x0A, 0x0C }, { 0x0D, 0x01 }, { 0x12, 0x00 }, { 0x13, 0x10 },
        { 0x31, 0x00 }, { 0x32, 0xBF },
    };
    bool ok = true;
    for (size_t i = 0; i < sizeof(chk) / sizeof(chk[0]); i++) {
        uint8_t v = 0xEE;
        if (rd(chk[i].reg, &v) != ESP_OK || v != chk[i].val) {
            ESP_LOGW(TAG, "readback reg 0x%02x = 0x%02x, expect 0x%02x", chk[i].reg, v, chk[i].val);
            ok = false;
        }
    }
    ESP_LOGI(TAG, "ES8311 init done, readback %s", ok ? "verified (10/10)" : "MISMATCH");
    return ESP_OK;
}

void es8311_min_set_volume(uint8_t vol_0_100)
{
    if (vol_0_100 > 100) vol_0_100 = 100;
    uint8_t code = (uint8_t)((uint32_t)vol_0_100 * 191 / 100);   /* 0x32:0xBF=0dB 滿刻度
                                                                (255 會到 +32dB 破音域) */
    wr(0x32, code);
}
