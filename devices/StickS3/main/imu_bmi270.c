/* imu_bmi270.c -- BMI270 傾斜輸入(加分項,指南 §16.2)。
 * v2.1 起輸入改為抽象意圖、映射在 hal_input.c;2 鍵方案已用「NEXT 雙擊=UI_PREV」
 * 涵蓋減值需求,IMU 傾斜輔助非必要。本檔保留為空,待未來要做時在 hal_input.c 內
 * 另起 IMU 讀取並直接 emit(UI_PREV/UI_NEXT)。
 * TODO:I2C 0x68(本機非 0x69),100ms 輪詢加速度 Y 軸,傾角>25° 每 300ms 一次。 */
