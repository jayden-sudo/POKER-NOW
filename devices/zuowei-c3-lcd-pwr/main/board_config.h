// zuowei-c3-lcd-pwr 板級配置 —— 唯一允許出現 GPIO 編號的檔案(權威:HARDWARE.md)
#pragma once
#include "driver/gpio.h"
#include "hal/adc_types.h"

// ---- LCD (ST7789, 1.54" 240x240, SPI2) ----
#define LCD_SPI_HOST        SPI2_HOST
#define LCD_PIN_MOSI        GPIO_NUM_1     // FSPID  【实测】
#define LCD_PIN_SCLK        GPIO_NUM_3     // FSPICLK【实测】
#define LCD_PIN_DC          GPIO_NUM_0     // 【实测】
#define LCD_PIN_RST         GPIO_NUM_2     // 【实测】
#define LCD_PIN_CS          GPIO_NUM_12    // ★【实测·实机验证】DIO 模式下空閒的 SPIHD 腳
                                           //   被板方複用作片選;設 NC = 整屏全黑(demo 最大坑)
                                           //   spi_common 的 GPIO12 conflict 警告 = 預期,忽略
#define LCD_H_RES           240
#define LCD_V_RES           240
#define LCD_PIXEL_CLOCK_HZ  (80 * 1000 * 1000)   // 復刻出廠 80MHz(demo 驗證)
#define LCD_SPI_MODE        3              // ★ 本板是 mode 3(StickS3 是 0,勿照搬)
#define LCD_INVERT_COLOR    true
#define LCD_MIRROR_X        false
#define LCD_MIRROR_Y        false
#define LCD_SWAP_XY         false          // 240×240 直用,無旋轉
#define LCD_OFFSET_X        0
#define LCD_OFFSET_Y        0

// ---- 背光 (LEDC PWM) ----
#define LCD_PIN_BL          GPIO_NUM_5     // LEDC ch0【实测】
#define BL_OUTPUT_INVERT    false

// ---- 按鍵(輸入上拉低有效;物理左→右 = GPIO8, GPIO9, GPIO7【实机确认】)----
// 命名依物理位置(專案慣例);各鍵→抽象意圖的實際映射見 hal_input.c:
//   左 G8   單擊→UI_UP、長按→UI_MENU
//   中 G9   單擊→UI_OK、長按→關機(BTN_PWR_HOLD_MS;不分配 UI 意圖)
//   右 G7   單擊→UI_DOWN、長按→UI_BACK
#define BTN_LEFT_PIN        GPIO_NUM_8     // 物理最左
#define BTN_RIGHT_PIN       GPIO_NUM_7     // 物理最右
#define BTN_MID_PIN         GPIO_NUM_9     // 物理中間(BOOT):短按=OK、長按=關機(§7)
#define BTN_PWR_HOLD_MS     800            // 中鍵長按關機門檻;關機屬破壞性但可攔截,足夠防誤觸

// ---- 電池 ADC(GPIO4 / ADC1_CH4 —— ADC1 與 Wi-Fi 無衝突,demo 驗證)----
#define BAT_ADC_UNIT        ADC_UNIT_1
#define BAT_ADC_CHANNEL     ADC_CHANNEL_4  // = GPIO4【实测】
#define BAT_ADC_ATTEN       ADC_ATTEN_DB_12
#define BAT_DIVIDER         6.08f          // 【实测校准】pin 687mV ↔ 電池 4.18V
#define BAT_FULL_MV         4200
#define BAT_EMPTY_MV        3300

// ---- 電源管理【实机验证】----
#define PWR_KEEP_PIN        GPIO_NUM_13    // ★ 整機電源自鎖:高=供電,低=立即斷電(USB/電池皆然)
#define USB_DETECT_PIN      GPIO_NUM_21    // VBUS 檢測:高=插入(外部主動驅動;非充電狀態腳)

// ---- 音訊 (VB6824 over UART1 @2Mbps) ----
#define VB_PIN_TX           GPIO_NUM_20    // C3 TX → VB6824 RX【实测】
#define VB_PIN_RX           GPIO_NUM_10    // C3 RX ← VB6824 TX【实测】
#define AUDIO_SAMPLE_RATE   16000
#define AUDIO_PREFILL_FRAMES 3             // §6.3:開機灌 3 幀靜音(60ms 水位)
#define AUDIO_PATH_LATENCY_MS 80           // ★ 初始估算值:水位 60 + UART ~2 + VB6824 內部 ~20
                                           //   【待实测】§6.5 量測後回填,並記入 §16

// ---- 使用者體感預設(v1.4:每板獨立調校;目前全板暫定 亮度40%/音量60%)----
#define PK_DEFAULT_VOLUME_PCT      45
#define PK_DEFAULT_BRIGHTNESS_PCT  40
