/* tca8418.c -- TCA8418 驅動實作(指南 §6)。
 * 資料手冊:TCA8418(TI);鍵碼 = row*10 + col + 1(bit7 = press)。 */
#include "tca8418.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "tca8418";

/* ---- 寄存器(僅列本驅動用到的)---- */
#define REG_CFG          0x01   /* [7]AI [4]INT_CFG [3]OVR_FLOW_IEN [0]KE_IEN */
#define REG_INT_STAT     0x02   /* [4]CAD [3]OVR_FLOW [2]K_LCK [1]GPI [0]K_INT(寫1清除) */
#define REG_KEY_LCK_EC   0x03   /* [3:0] FIFO 內事件數 */
#define REG_KEY_EVENT_A  0x04   /* FIFO 讀口:0=空;[7]press [6:0]keycode */
#define REG_KP_GPIO1     0x1D   /* ROW0-7 撥入鍵盤矩陣(1=矩陣) */
#define REG_KP_GPIO2     0x1E   /* COL0-7 */
#define REG_KP_GPIO3     0x1F   /* COL8-9(本板不用,寫 0) */

#define CFG_KE_IEN       0x01
#define CFG_OVR_FLOW_IEN 0x08
#define CFG_INT_CFG      0x10   /* 中斷處理期間有新事件 → INT 再拉低(電平語義友好) */
#define INT_K_INT        0x01
#define INT_OVR_FLOW     0x08

/* ---- 電氣 (row,col) → 鍵值解碼表(§17.5 R16,2026-07-21 依官方源碼定案)----
 * 依據(兩處官方實作交叉一致,同一 remap 公式):
 *  1) github.com/m5stack/M5Cardputer @master(v1.2.x,支援 ADV)
 *     src/utility/Keyboard/KeyboardReader/TCA8418.cpp::remap():
 *       x = row*2 + (col>3 ? 1 : 0);   y = col % 4;    // x=0..13, y=0..3
 *     再查 src/utility/Keyboard/Keyboard.h::_key_value_map[y][x](y=0 = 頂列)。
 *  2) github.com/m5stack/M5Cardputer-UserDemo @CardputerADV
 *     main/hal/keyboard/keyboard.cpp:remap() 與 _key_value_map 完全同構。
 * 幾何直覺:每條電氣 ROW 佈線對應鍵盤面上「相鄰兩豎條」(4 實體行 × 2 邏輯列):
 * COL0-3 = 左條(邏輯列 2*row)由頂至底,COL4-7 = 右條(邏輯列 2*row+1)。
 * 舊版 row-major 推定(idx=r*8+c 橫掃 4×14)與此正交,故整表錯位(R16)。 */
static const uint8_t s_keymap[7][8] = {
    /*        COL0  COL1         COL2        COL3          COL4  COL5  COL6           COL7        */
    /*ROW0*/{ '`',  TCA_KEY_TAB, TCA_KEY_FN, TCA_KEY_CTRL, '1',  'q',  TCA_KEY_SHIFT, TCA_KEY_OPT },
    /*ROW1*/{ '2',  'w',         'a',        TCA_KEY_ALT,  '3',  'e',  's',           'z'         },
    /*ROW2*/{ '4',  'r',         'd',        'x',          '5',  't',  'f',           'c'         },
    /*ROW3*/{ '6',  'y',         'g',        'v',          '7',  'u',  'h',           'b'         },
    /*ROW4*/{ '8',  'i',         'j',        'n',          '9',  'o',  'k',           'm'         },
    /*ROW5*/{ '0',  'p',         'l',        ',',          '-',  '[',  ';',           '.'         },
    /*ROW6*/{ '=',  ']',         '\'',       '/',          TCA_KEY_BS, '\\', TCA_KEY_ENTER, ' '   },
};

uint8_t tca8418_key_decode(uint8_t row, uint8_t col)
{
    return (row < 7 && col < 8) ? s_keymap[row][col] : TCA_KEY_NONE;
}

static i2c_master_dev_handle_t s_dev;
static gpio_num_t         s_int_pin;
static tca8418_key_cb_t   s_cb;
static SemaphoreHandle_t  s_sem;

static esp_err_t wr(uint8_t reg, uint8_t val)
{
    uint8_t f[2] = { reg, val };
    return i2c_master_transmit(s_dev, f, 2, 100);
}
static esp_err_t rd(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 100);
}

static void IRAM_ATTR int_isr(void *arg)
{
    (void)arg;
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_sem, &hp);
    if (hp) portYIELD_FROM_ISR();
}

/* 讀空 FIFO 並分發事件;回傳處理的事件數 */
static int drain_fifo(void)
{
    int n = 0;
    for (;;) {
        uint8_t ev = 0;
        if (rd(REG_KEY_EVENT_A, &ev) != ESP_OK) break;   /* I2C 失敗:下輪重試 */
        if (ev == 0) break;                              /* FIFO 空 */
        uint8_t code = ev & 0x7F;                        /* 1..80 */
        if (code >= 1) {
            uint8_t row = (uint8_t)((code - 1) / 10);
            uint8_t col = (uint8_t)((code - 1) % 10);
            if (row < 7 && col < 8 && s_cb)
                s_cb(row, col, (ev & 0x80) != 0);
        }
        n++;
        if (n >= 16) break;                              /* 防病態迴圈(FIFO 深 10) */
    }
    /* 寫 1 清除中斷位(必須在 FIFO 讀空後,否則 K_INT 立即重置) */
    wr(REG_INT_STAT, INT_K_INT | INT_OVR_FLOW);
    return n;
}

static void kbd_task(void *arg)
{
    (void)arg;
    for (;;) {
        xSemaphoreTake(s_sem, pdMS_TO_TICKS(50));        /* 中斷喚醒;50ms 輪詢兜底 */
        if (gpio_get_level(s_int_pin) == 0)              /* 電平判定,免疫丟邊沿 */
            drain_fifo();
    }
}

esp_err_t tca8418_init(i2c_master_bus_handle_t bus, gpio_num_t int_pin, tca8418_key_cb_t cb)
{
    s_int_pin = int_pin;
    s_cb = cb;

    const i2c_device_config_t dcfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x34,
        .scl_speed_hz = 100 * 1000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dcfg, &s_dev), TAG, "add dev");

    /* 1) 7×8 矩陣:ROW0-6、COL0-7 撥入鍵盤掃描(參考 §3.2);去抖預設開啟不動 */
    ESP_RETURN_ON_ERROR(wr(REG_KP_GPIO1, 0x7F), TAG, "rows");
    ESP_RETURN_ON_ERROR(wr(REG_KP_GPIO2, 0xFF), TAG, "cols");
    ESP_RETURN_ON_ERROR(wr(REG_KP_GPIO3, 0x00), TAG, "cols89");

    /* 2) 清殘留:讀空 FIFO(上電/重啟殘鍵)+ 清全部中斷位 */
    for (int i = 0; i < 12; i++) { uint8_t ev; if (rd(REG_KEY_EVENT_A, &ev) != ESP_OK || ev == 0) break; }
    ESP_RETURN_ON_ERROR(wr(REG_INT_STAT, 0x1F), TAG, "int clr");

    /* 3) 開鍵事件中斷 + 溢出中斷 */
    ESP_RETURN_ON_ERROR(wr(REG_CFG, CFG_KE_IEN | CFG_OVR_FLOW_IEN | CFG_INT_CFG), TAG, "cfg");

    /* 4) INT 腳 + 負緣中斷(晶片側已有 3.3k 硬上拉) */
    s_sem = xSemaphoreCreateBinary();
    gpio_config_t io = {
        .pin_bit_mask = BIT64(int_pin),
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "int gpio");
    esp_err_t r = gpio_install_isr_service(0);
    if (r != ESP_OK && r != ESP_ERR_INVALID_STATE) return r;   /* 已安裝則沿用 */
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(int_pin, int_isr, NULL), TAG, "isr add");

    xTaskCreatePinnedToCore(kbd_task, "kbd", 3072, NULL, 6, NULL, 0);
    ESP_LOGI(TAG, "TCA8418 ready (7x8 matrix, INT=G%d)", (int)int_pin);
    return ESP_OK;
}
