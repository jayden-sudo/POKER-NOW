// StickS3 板級配置 —— 唯一允許出現 GPIO 編號的檔案(參照 sticks3-reference.md)
#pragma once
#include "driver/gpio.h"

// ---- 共享 I2C(內部匯流排:ES8311/BMI270/M5PM1)----
#define BOARD_I2C_SDA        GPIO_NUM_47
#define BOARD_I2C_SCL        GPIO_NUM_48
#define I2C_ADDR_ES8311      0x18
#define I2C_ADDR_BMI270      0x68        // 注意:Cardputer-Adv 是 0x69,本機 0x68
#define I2C_ADDR_M5PM1       0x6E

// ---- LCD (ST7789P3, 實體 135×240 直向 → 邏輯 240×135 橫向) ----
#define LCD_SPI_HOST         SPI3_HOST
#define LCD_PIN_MOSI         GPIO_NUM_39
#define LCD_PIN_SCLK         GPIO_NUM_40
#define LCD_PIN_CS           GPIO_NUM_41
#define LCD_PIN_DC           GPIO_NUM_45   // RS
#define LCD_PIN_RST          GPIO_NUM_21
#define LCD_PIN_BL           GPIO_NUM_38   // LEDC PWM
#define LCD_H_RES            240           // 邏輯橫向
#define LCD_V_RES            135
#define LCD_PANEL_W          135           // 面板原生
#define LCD_PANEL_H          240
#define LCD_PIXEL_CLOCK_HZ   (40 * 1000 * 1000)
#define LCD_INVERT_COLOR     true          // 【實測】sticks3-reference §11.8
#define LCD_SWAP_XY          true          // 橫向
#define LCD_MIRROR_X         false         // §14.3 自檢後若上下顛倒:兩個 mirror 同時取反
#define LCD_MIRROR_Y         true
#define LCD_GAP_X            40            // 裁定 R7:直向 (52,40)【實測】→ 橫向換軸
#define LCD_GAP_Y            52

// ---- 按鍵(輸入上拉低有效;左側鍵是 M5PM1 電源鍵,韌體不可讀!)----
#define BTN_OK_PIN           GPIO_NUM_11   // KEY1 → KEY_OK(產品 §2.1 映射表)
#define BTN_NEXT_PIN         GPIO_NUM_12   // KEY2 → KEY_NEXT

// ---- 音訊 ES8311 + AW8737 ----
#define AUDIO_SAMPLE_RATE    16000
#define I2S_PIN_MCLK         GPIO_NUM_18
#define I2S_PIN_BCLK         GPIO_NUM_17
#define I2S_PIN_LRCK         GPIO_NUM_15
#define I2S_PIN_DOUT         GPIO_NUM_14   // MCU→ES8311 DSDIN(播放)★ v1.4 修:原推斷 G16 反了,
                                           //   以 78/xiaozhi-esp32 m5stack-stick-s3(真機出聲)為準
#define I2S_PIN_DIN          GPIO_NUM_16   // ES8311 ASDOUT→MCU(錄音/未用)
#define AUDIO_VOL_DEFAULT    PK_DEFAULT_VOLUME_PCT
#define AUDIO_VOL_BATT_MAX   75            // 電池供電硬鉗(sticks3-reference §9.3)
#define AUDIO_PATH_LATENCY_MS 62           // 初始估算值:DMA 60ms + codec ~2ms;§15.5 實測後回填

// ---- 使用者體感預設(v1.4:每板獨立調校;目前全板暫定 亮度40%/音量60%)----
#define PK_DEFAULT_VOLUME_PCT      40
#define PK_DEFAULT_BRIGHTNESS_PCT  40
