/* es8311_cardputer.c -- ES8311 DAC 播放路徑最小初始化(Cardputer-Adv 版,指南 §8.2)。
 * 差異(vs StickS3 es8311_min.c):本板 MCLK 未接主控 → REG01 bit7=1 以 SCLK 為
 * 內部時鐘源;SCLK=512kHz(16k×16bit×2slot)→ REG02 預倍頻 ×8 = 4.096MHz(256fs)。
 * codec 為 I2S slave;寄存器序列參照 ES8311 DS + espressif/es8311 預設路徑;
 * 實機微調記入 §17。 */
#include "es8311_cardputer.h"
#include "esp_log.h"

static const char *TAG = "es8311";
static i2c_master_dev_handle_t s_dev;

static esp_err_t wr(uint8_t reg, uint8_t val)
{
    uint8_t f[2] = { reg, val };
    return i2c_master_transmit(s_dev, f, 2, 100);
}

typedef struct { uint8_t reg, val; } rv_t;

esp_err_t es8311_cardputer_init(i2c_master_bus_handle_t bus, uint32_t sample_rate)
{
    (void)sample_rate;                         /* 鎖定 16k;變更需重推 REG02(§8.1) */
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x18,                /* CE 接地(參考 §3.3) */
        .scl_speed_hz = 100 * 1000,
    };
    esp_err_t r = i2c_master_bus_add_device(bus, &cfg, &s_dev);
    if (r != ESP_OK) return r;

    static const rv_t seq[] = {
        { 0x00, 0x1F }, { 0x00, 0x00 },   /* reset */
        { 0x01, 0xBF },                   /* ★ v1.4 修:bit7 SCLK 源 + 全時鐘 ON ——
                                   *   原 0xB0 低半位元組=0 → DAC/ADC 內部時鐘全關(無聲) */                   /* ★ clkmgr:bit7 MCLK_SEL=1(SCLK 作時鐘源)
                                           *   + MCLK/BCLK on(0x30)。StickS3 此處 0x30 */
        { 0x02, 0x18 },                   /* ★ clkmgr:pre_div=1,pre_multi=×8
                                           *   (512kHz->4.096MHz=256fs)。StickS3 此處 0x10 */
        { 0x03, 0x10 }, { 0x16, 0x24 }, { 0x04, 0x10 }, { 0x05, 0x00 },
        { 0x06, 0x03 }, { 0x07, 0x00 }, { 0x08, 0xFF },
        { 0x09, 0x0C },                   /* SDP In:16bit I2S */
        { 0x0A, 0x0C },                   /* SDP Out:16bit I2S(錄音不用,寫上無害) */
        { 0x0B, 0x00 }, { 0x0C, 0x00 },
        { 0x10, 0x1F }, { 0x11, 0x7F },   /* system power up analog */
        { 0x00, 0x80 },                   /* power up, slave mode */
        { 0x0D, 0x01 },                   /* system power up */
        { 0x0E, 0x02 },                   /* ★ v1.4 補:analog power(對齊 StickS3 真機驗證序列) */
        { 0x12, 0x00 },                   /* ★ v1.4 補:DAC power up(原缺 → 類比級沒電) */
        { 0x13, 0x10 },                   /* ★ v1.4 補:輸出級 enable(DAC→HP drive) */
        { 0x1C, 0x6A },                   /* ★ v1.4 補:ADC/eq 配套 */
        { 0x37, 0x08 },                   /* DAC ramp */
        { 0x45, 0x00 },
        { 0x31, 0x00 },                   /* ★ v1.4 補:DAC 解除靜音(0x31 預設含 mute) */
        { 0x32, 0xBF },                   /* DAC volume(codec 固定 0dB,音量走軟體增益) */
    };
    for (size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
        esp_err_t e = wr(seq[i].reg, seq[i].val);
        if (e != ESP_OK) { ESP_LOGW(TAG, "reg 0x%02x write failed (%d)", seq[i].reg, e); }
        /* codec 寫失敗只 LOGW 不中斷(S3 §21.2-2 慣例:無硬體時仍可跑) */
    }
    ESP_LOGI(TAG, "ES8311 init done (SCLK-derived clock, no MCLK)");
    return ESP_OK;
}

void es8311_cardputer_set_volume(uint8_t vol_0_100)
{
    if (vol_0_100 > 100) vol_0_100 = 100;
    uint8_t code = (uint8_t)((uint32_t)vol_0_100 * 191 / 100);   /* 0x32:0xBF=0dB 滿刻度
                                                                (255 會到 +32dB 破音域) */
    wr(0x32, code);
}
