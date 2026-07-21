/* m5pm1.c -- M5PM1 驅動(指南 §13;暫存器對照手冊 V1.9 + 官方 Arduino 庫 + 真機驗證) */
#include "m5pm1.h"
#include "board_config.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "m5pm1";

#define PM1_REG_DEVICE_ID   0x00
#define PM1_REG_PWR_SRC     0x04
#define PM1_REG_SYS_CMD     0x0C
#define PM1_REG_GPIO_MODE   0x10
#define PM1_REG_GPIO_OUT    0x11
#define PM1_REG_GPIO_IN     0x12
#define PM1_REG_GPIO_DRV    0x13
#define PM1_REG_GPIO_PUPD0  0x14
#define PM1_REG_GPIO_FUNC0  0x16
#define PM1_REG_VBAT_L      0x22
#define PM1_REG_VBAT_H      0x23
#define PM1_REG_VIN_L       0x24
#define PM1_REG_VIN_H       0x25
#define PM1_REG_BTN_CFG_1   0x49
#define PM1_REG_BTN_CFG_2   0x4A
#define PM1_BTN_CFG_1_DEFAULT 0x2A
#define SYS_CMD_SHUTDOWN    0xA1
#define I2C_TIMEOUT_MS      100

static i2c_master_dev_handle_t s_dev;
static bool s_chg_ready;

static esp_err_t pm1_read(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, I2C_TIMEOUT_MS);
}
static esp_err_t pm1_read_n(uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, n, I2C_TIMEOUT_MS);
}
static esp_err_t pm1_write(uint8_t reg, uint8_t val)
{
    uint8_t f[2] = { reg, val };
    return i2c_master_transmit(s_dev, f, 2, I2C_TIMEOUT_MS);
}

esp_err_t m5pm1_init(i2c_master_bus_handle_t bus)
{
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = I2C_ADDR_M5PM1,
        .scl_speed_hz = 100 * 1000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &s_dev), TAG, "add device failed");
    uint8_t id = 0;
    ESP_RETURN_ON_ERROR(pm1_read(PM1_REG_DEVICE_ID, &id), TAG, "no response");
    ESP_LOGI(TAG, "found M5PM1, device id 0x%02x", id);
    return ESP_OK;
}

esp_err_t m5pm1_enable_l3b_domain(void)
{
    uint8_t r;
    ESP_RETURN_ON_ERROR(pm1_read(PM1_REG_GPIO_FUNC0, &r), TAG, "");
    ESP_RETURN_ON_ERROR(pm1_write(PM1_REG_GPIO_FUNC0, r & ~(3u << 4)), TAG, "");
    ESP_RETURN_ON_ERROR(pm1_read(PM1_REG_GPIO_OUT, &r), TAG, "");
    ESP_RETURN_ON_ERROR(pm1_write(PM1_REG_GPIO_OUT, r | (1u << 2)), TAG, "");
    ESP_RETURN_ON_ERROR(pm1_read(PM1_REG_GPIO_DRV, &r), TAG, "");
    ESP_RETURN_ON_ERROR(pm1_write(PM1_REG_GPIO_DRV, r & ~(1u << 2)), TAG, "");
    ESP_RETURN_ON_ERROR(pm1_read(PM1_REG_GPIO_MODE, &r), TAG, "");
    ESP_RETURN_ON_ERROR(pm1_write(PM1_REG_GPIO_MODE, r | (1u << 2)), TAG, "");
    ESP_LOGI(TAG, "L3B power domain enabled");
    return ESP_OK;
}

esp_err_t m5pm1_poker_btn_cfg(void)
{
    /* 出廠 0x2A + SINGLE_RST_DIS(bit0)=1 → 0x2B:單擊不再復位 */
    ESP_RETURN_ON_ERROR(pm1_write(PM1_REG_BTN_CFG_1, PM1_BTN_CFG_1_DEFAULT | 0x01), TAG, "");
    ESP_RETURN_ON_ERROR(pm1_write(PM1_REG_BTN_CFG_2, 0x00), TAG, "");
    ESP_LOGI(TAG, "poker BTN_CFG: single-press reset disabled");
    return ESP_OK;
}

esp_err_t m5pm1_restore_pwr_btn_defaults(void)
{
    ESP_RETURN_ON_ERROR(pm1_write(PM1_REG_BTN_CFG_1, PM1_BTN_CFG_1_DEFAULT), TAG, "");
    ESP_RETURN_ON_ERROR(pm1_write(PM1_REG_BTN_CFG_2, 0x00), TAG, "");
    return ESP_OK;
}

esp_err_t m5pm1_get_battery_mv(uint16_t *mv)
{
    uint8_t raw[2];
    ESP_RETURN_ON_ERROR(pm1_read_n(PM1_REG_VBAT_L, raw, 2), TAG, "");
    *mv = (uint16_t)raw[0] | ((uint16_t)(raw[1] & 0x0F) << 8);
    return ESP_OK;
}
esp_err_t m5pm1_get_vin_mv(uint16_t *mv)
{
    uint8_t raw[2];
    ESP_RETURN_ON_ERROR(pm1_read_n(PM1_REG_VIN_L, raw, 2), TAG, "");
    *mv = (uint16_t)raw[0] | ((uint16_t)(raw[1] & 0x0F) << 8);
    return ESP_OK;
}
esp_err_t m5pm1_get_power_source(m5pm1_pwr_src_t *src)
{
    uint8_t raw = 0;
    ESP_RETURN_ON_ERROR(pm1_read(PM1_REG_PWR_SRC, &raw), TAG, "");
    *src = (m5pm1_pwr_src_t)(raw & 0x07);
    return ESP_OK;
}
bool m5pm1_is_external_powered(void)
{
    m5pm1_pwr_src_t s;
    return m5pm1_get_power_source(&s) == ESP_OK && s != M5PM1_PWR_SRC_BATTERY;
}

static esp_err_t chg_stat_configure(void)
{
    uint8_t r;
    ESP_RETURN_ON_ERROR(pm1_read(PM1_REG_GPIO_FUNC0, &r), TAG, "");
    ESP_RETURN_ON_ERROR(pm1_write(PM1_REG_GPIO_FUNC0, r & ~(3u << 0)), TAG, "");
    ESP_RETURN_ON_ERROR(pm1_read(PM1_REG_GPIO_MODE, &r), TAG, "");
    ESP_RETURN_ON_ERROR(pm1_write(PM1_REG_GPIO_MODE, r & ~(1u << 0)), TAG, "");
    ESP_RETURN_ON_ERROR(pm1_read(PM1_REG_GPIO_PUPD0, &r), TAG, "");   /* 無上拉(坑) */
    ESP_RETURN_ON_ERROR(pm1_write(PM1_REG_GPIO_PUPD0, r & ~(3u << 0)), TAG, "");
    return ESP_OK;
}
bool m5pm1_is_charging(void)
{
    if (!s_chg_ready) { if (chg_stat_configure() != ESP_OK) return false; s_chg_ready = true; }
    if (!m5pm1_is_external_powered()) return false;
    uint8_t in;
    return pm1_read(PM1_REG_GPIO_IN, &in) == ESP_OK && !(in & 1);   /* CHG_STAT 低有效 */
}

esp_err_t m5pm1_amp_set(uint8_t on)
{
    /* AW8737 EN 由 PM1 GPIO3(PYG3_SPK_Pulse)驅動。官方 M5PM1 教程與
     * M5Unified 的做法:GPIO3 設普通 GPIO、推挽輸出、恆高=開 / 恆低=關。
     * 手冊 V1.9:GPIO_DRV(0x13) 上電預設 0x1F(全開漏),板上無外部上拉,
     * 不先改推挽則 EN 永遠拉不高(0x53 脈衝寄存器手冊也明言須先設推挽)。 */
    uint8_t r;
    ESP_RETURN_ON_ERROR(pm1_read(PM1_REG_GPIO_FUNC0, &r), TAG, "");
    ESP_RETURN_ON_ERROR(pm1_write(PM1_REG_GPIO_FUNC0, r & ~(3u << 6)), TAG, "");  /* GPIO3=普通GPIO */
    ESP_RETURN_ON_ERROR(pm1_read(PM1_REG_GPIO_DRV, &r), TAG, "");
    ESP_RETURN_ON_ERROR(pm1_write(PM1_REG_GPIO_DRV, r & ~(1u << 3)), TAG, "");    /* 推挽(根因)*/
    ESP_RETURN_ON_ERROR(pm1_read(PM1_REG_GPIO_OUT, &r), TAG, "");
    ESP_RETURN_ON_ERROR(pm1_write(PM1_REG_GPIO_OUT,
                                  on ? (r | (1u << 3)) : (r & ~(1u << 3))), TAG, "");
    ESP_RETURN_ON_ERROR(pm1_read(PM1_REG_GPIO_MODE, &r), TAG, "");
    ESP_RETURN_ON_ERROR(pm1_write(PM1_REG_GPIO_MODE, r | (1u << 3)), TAG, "");    /* 輸出 */

    /* 寄存器讀回自證:MODE.3=1、DRV.3=0、FUNC0[7:6]=00、OUT.3/IN.3=on */
    uint8_t mode, out, drv, func, in;
    ESP_RETURN_ON_ERROR(pm1_read(PM1_REG_GPIO_MODE, &mode), TAG, "");
    ESP_RETURN_ON_ERROR(pm1_read(PM1_REG_GPIO_OUT, &out), TAG, "");
    ESP_RETURN_ON_ERROR(pm1_read(PM1_REG_GPIO_DRV, &drv), TAG, "");
    ESP_RETURN_ON_ERROR(pm1_read(PM1_REG_GPIO_FUNC0, &func), TAG, "");
    ESP_RETURN_ON_ERROR(pm1_read(PM1_REG_GPIO_IN, &in), TAG, "");
    const bool ok = ((mode >> 3) & 1) == 1 && ((drv >> 3) & 1) == 0 &&
                    ((func >> 6) & 3) == 0 && ((out >> 3) & 1) == (on ? 1 : 0) &&
                    ((in >> 3) & 1) == (on ? 1 : 0);
    ESP_LOGI(TAG, "AW8737 amp %s %s: MODE=0x%02x OUT=0x%02x DRV=0x%02x FUNC0=0x%02x IN=0x%02x",
             on ? "ON" : "OFF", ok ? "verified" : "MISMATCH", mode, out, drv, func, in);
    return ok ? ESP_OK : ESP_ERR_INVALID_STATE;
}

void m5pm1_power_off(void)
{
    ESP_LOGW(TAG, "powering off");
    pm1_write(PM1_REG_SYS_CMD, SYS_CMD_SHUTDOWN);
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
