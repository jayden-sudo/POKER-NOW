// xingzhi-cube-1.54tft-wifi 板級配置 —— 唯一允許出現 GPIO 編號的檔案(權威:HARDWARE.md)
#pragma once
#include "driver/gpio.h"
#include "hal/adc_types.h"
#include "driver/i2s_common.h"

// ---- LCD (ST7789, 1.54" 240x240, SPI3) ----【demo 实测原值,一個不改】
#define LCD_SPI_HOST        SPI3_HOST      // ★ SPI3(StickS3 同名巨集但腳位全不同)
#define LCD_PIN_MOSI        GPIO_NUM_10    // SPI3_D   【实测】
#define LCD_PIN_SCLK        GPIO_NUM_9     // SPI3_CLK 【实测】
#define LCD_PIN_CS          GPIO_NUM_14    // SPI3_CS0 【实测】
#define LCD_PIN_DC          GPIO_NUM_8     // 【实测】
#define LCD_PIN_RST         GPIO_NUM_18    // 【实测】
#define LCD_H_RES           240
#define LCD_V_RES           240
#define LCD_PIXEL_CLOCK_HZ  (80 * 1000 * 1000)   // 復刻出廠 80MHz(demo 驗證)
#define LCD_SPI_MODE        3              // ★ mode 3(StickS3 是 0,勿照搬)
#define LCD_INVERT_COLOR    true
#define LCD_MIRROR_X        false
#define LCD_MIRROR_Y        false
#define LCD_SWAP_XY         false          // 240×240 直用,無旋轉
#define LCD_OFFSET_X        0
#define LCD_OFFSET_Y        0

// ---- 背光 (LEDC PWM ch0) ----
#define LCD_PIN_BL          GPIO_NUM_13    // 【实测】非反相
#define BL_OUTPUT_INVERT    false

// ---- 按鍵(輸入上拉低有效;物理由上到下 = GPIO39/40/0)----
// 第 4 顆是專用硬體電源鍵(不接 GPIO,韌體不可見):短按開機、長按≈2s 關機。
#define BTN_OK_PIN          GPIO_NUM_39    // 上鍵 → KEY_OK(產品 §2.1 映射表)
#define BTN_NEXT_PIN        GPIO_NUM_0     // 下鍵 → KEY_NEXT(BOOT 腳:開機勿按住,§9)
#define BTN_INCDEC_PIN      GPIO_NUM_40    // 中鍵 → incdec(-1) 加分捷徑,不進共用層(§9)

// ---- 電池 / 電源 ----
#define BAT_ADC_UNIT        ADC_UNIT_2     // ★ ADC2 與 Wi-Fi 衝突:開機先讀基準(§8)
#define BAT_ADC_CHANNEL     ADC_CHANNEL_6  // = GPIO17【实测】
#define BAT_ADC_ATTEN       ADC_ATTEN_DB_12
#define BAT_DIVIDER         1.90f          // 【实测校准】萬用表實測電池 4.20V
#define BAT_FULL_MV         4200           // 整數(zuowei §16.1-4:走整數百分比運算)
#define BAT_EMPTY_MV        3300
#define CHARGE_DETECT_PIN   GPIO_NUM_38    // 高=充電中【实测】
#define PWR_KEEP_PIN        GPIO_NUM_21    // RTC-GPIO 電源自鎖:開機後拉高維持供電;
                                           // 關機由硬體電源鍵,韌體不做軟關機(HARDWARE.md §2)

// ---- 音訊(純 I2S 功放直推,無 codec 晶片;板上 PDM 麥克風本專案不用)----
#define AUDIO_SPK_I2S_PORT  I2S_NUM_1      // 喇叭走 I2S1【实测】(I2S0 留給 PDM,不初始化)
#define AUDIO_SPK_BCLK      GPIO_NUM_15
#define AUDIO_SPK_WS        GPIO_NUM_16
#define AUDIO_SPK_DOUT      GPIO_NUM_7
#define AUDIO_SAMPLE_RATE   16000          // 全產品統一 16k(§6.1)
#define AUDIO_VOL_DEFAULT    PK_DEFAULT_VOLUME_PCT
#define AUDIO_PATH_LATENCY_MS 60           // 初始估算:DMA 4×240=60ms + 功放≈0(無 codec 群延遲);
                                           // §6.4 實測後回填,並記 §15

// ---- 使用者體感預設(v1.4:每板獨立調校;目前全板暫定 亮度40%/音量60%)----
#define PK_DEFAULT_VOLUME_PCT      30
#define PK_DEFAULT_BRIGHTNESS_PCT  40
