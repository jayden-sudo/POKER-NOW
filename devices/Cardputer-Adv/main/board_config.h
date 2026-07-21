// Cardputer-Adv 板級配置 —— 唯一允許出現 GPIO 編號的檔案(權威:cardputer-adv-reference.md §4)
#pragma once
#include "driver/gpio.h"

// ---- 共享 I2C(內部匯流排:TCA8418 / ES8311 / BMI270,參考 §5.1)----
#define BOARD_I2C_SDA        GPIO_NUM_8
#define BOARD_I2C_SCL        GPIO_NUM_9
#define I2C_ADDR_TCA8418     0x34
#define I2C_ADDR_ES8311      0x18        // CE 接地(參考 §3.3)
#define I2C_ADDR_BMI270      0x69        // 本板 0x69(StickS3 是 0x68!);本專案不初始化(§13)

// ---- 鍵盤(TCA8418RTWR,7 行 × 8 列電氣矩陣)----
#define KBD_INT_PIN          GPIO_NUM_11  // 低有效,晶片側 3.3k 上拉(參考 §3.2)
#define KBD_ROWS             7
#define KBD_COLS             8

// ---- G0 = BOOT / BtnA(參考 §4:運行時可作用戶按鍵)----
#define BTN_G0_PIN           GPIO_NUM_0   // §7.4:並聯 KEY_OK

// ---- LCD (ST7789V2 1.14", 實體 135×240 → 邏輯 240×135 橫向;參考 §3.7) ----
#define LCD_SPI_HOST         SPI3_HOST
#define LCD_PIN_MOSI         GPIO_NUM_35  // DAT
#define LCD_PIN_SCLK         GPIO_NUM_36
#define LCD_PIN_CS           GPIO_NUM_37
#define LCD_PIN_DC           GPIO_NUM_34  // RS
#define LCD_PIN_RST          GPIO_NUM_33
#define LCD_PIN_BL           GPIO_NUM_38  // ★ 兼 Stamp-S3A RGB LED 電源使能(PWR_EN);
                                          //   PWM 佔空下限 10%(產品 §2.2)
#define LCD_H_RES            240          // 邏輯橫向(與 StickS3 同一組 240×135 佈局)
#define LCD_V_RES            135
#define LCD_PIXEL_CLOCK_HZ   (40 * 1000 * 1000)
#define LCD_SPI_MODE         0
// §9.4 自檢定案(R15,2026-07-21 真機,使用者目視三輪):StickS3 初值
// (mirror false/true, gap 40,53) 在本板方向 180° 顛倒 → 兩 mirror 同時取反。
// gap_y 曾按「240-135-52=53」推算
#define LCD_INVERT_COLOR     true
#define LCD_SWAP_XY          true
#define LCD_MIRROR_X         true
#define LCD_MIRROR_Y         false
#define LCD_GAP_X            40
#define LCD_GAP_Y            53

// ---- 音訊(ES8311 + NS4150B;★ MCLK 未接主控,SCLK 作時鐘源,參考 §3.3)----
#define AUDIO_SAMPLE_RATE    16000
#define I2S_PIN_BCLK         GPIO_NUM_41  // ES8311 SCLK
#define I2S_PIN_LRCK         GPIO_NUM_43  // ES8311 LRCK(= 預設 U0TXD,勿啟 UART0!)
#define I2S_PIN_DOUT         GPIO_NUM_42  // ES8311 DSDIN(播放,MCU→codec)
// G46 = ASDOUT(錄音,codec→MCU):本專案不用麥克風,不接(§13)
#define AUDIO_VOL_DEFAULT    PK_DEFAULT_VOLUME_PCT
#define AUDIO_PATH_LATENCY_MS 62          // 初值:DMA 4×240=60ms + ES8311 群延遲 ~2ms
                                          // (與 StickS3 同構同值);§8.5 實測後回填

// ---- 電池(G10 ADC,100k/100k 分壓 → Vbat = 讀數 ×2,參考 §3.1)----
#define BAT_ADC_UNIT         ADC_UNIT_1
#define BAT_ADC_CHANNEL      ADC_CHANNEL_9   // ESP32-S3:GPIO10 = ADC1_CH9(無 Wi-Fi 衝突)
#define BAT_ADC_ATTEN        ADC_ATTEN_DB_12
#define BAT_DIVIDER_NUM      2               // ×2(整數,zuowei §16.1-4 慣例)
#define BAT_FULL_MV          4200
#define BAT_EMPTY_MV         3300
// 無充電狀態腳(TP4057 STAT 未引出):hal_battery_charging() 恆 false(§10)

// ---- 不使用但需「衛生處理」的共享 SPI 片選(§13,參考 §13.6 Launcher 經驗)----
#define UNUSED_CS_LORA       GPIO_NUM_5    // Cap-Bus / SX1262 NSS → 開機拉高
#define UNUSED_CS_SDCARD     GPIO_NUM_12   // microSD CS → 開機拉高

// ---- 使用者體感預設(v1.4:每板獨立調校;目前全板暫定 亮度40%/音量60%)----
#define PK_DEFAULT_VOLUME_PCT      60
#define PK_DEFAULT_BRIGHTNESS_PCT  40
