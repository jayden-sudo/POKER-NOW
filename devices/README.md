# devices — 各 ESP32 裝置實作

每個子目錄是一個獨立、可直接 `idf.py build` 的 **ESP-IDF 韌體專案**,只負責該裝置的**硬體抽象層(HAL)**;遊戲邏輯、協定、UI 全部來自 [`../common/`](../common/)(以 `EXTRA_COMPONENT_DIRS = ../../common/components` 引用)。

| 裝置 | 方案 | 目標晶片 | 一句話 |
|---|---|---|---|
| [`StickS3/`](StickS3/) | 2 鍵 | esp32s3 | M5Stack StickS3,ES8311 音訊,I2S 播放腳需注意方向 |
| [`zuowei-c3-lcd-pwr/`](zuowei-c3-lcd-pwr/) | 3 鍵 | esp32c3 | DOIT 小智板,VB6824 UART 語音晶片,GPIO13 電源自鎖 |
| [`xingzhi-cube-1.54tft-wifi/`](xingzhi-cube-1.54tft-wifi/) | 3 鍵 | esp32s3 | 星智 Cube,原生 I2S 功放,ADC2 電池與 Wi-Fi 衝突 |
| [`Cardputer-Adv/`](Cardputer-Adv/) | 全鍵盤 | esp32s3 | M5 Cardputer,TCA8418 鍵盤,ES8311 無 MCLK 模式,無 PSRAM |

每個裝置目錄含:ESP-IDF 原始碼(`CMakeLists.txt`、`main/`、`sdkconfig.defaults`、`partitions.csv`、`dependencies.lock`,zuowei 另有 `components/vb6824`)+ 三份文件:

- **`hardware-reference.md`** — 該裝置的硬體事實:晶片/Flash/PSRAM、螢幕驅動與引腳、按鍵(含實體空間位置)、音訊路徑、電池、分區表、線上資料位置。動任何引腳/音訊/電源前必讀。
- **`dev-guide.md`** — 開發指南:為什麼這麼設計、踩坑記錄(§21「實作經驗回寫」是精華)、如何二次開發。
- **`README.md`** — 該裝置的一頁速覽。

## 接一個新裝置

1. 在此新增 `devices/<新裝置>/`,放一個標準 ESP-IDF 專案。
2. 頂層 `CMakeLists.txt` 設 `EXTRA_COMPONENT_DIRS = ${CMAKE_CURRENT_LIST_DIR}/../../common/components`。
3. 在 `main/` 實作 HAL(介面見 `common/components/poker_core/include/hal/`):
   - `hal_display`:面板 bring-up + `ui_lvgl_attach(hres,vres)`(渲染在共用層,裝置只做面板初始化與亮度)。
   - `hal_input`:把物理按鍵映射成 5 個抽象意圖 `UI_OK/UI_UP/UI_DOWN/UI_BACK/UI_MENU`,並提供 `hal_input_hint()` 回報各意圖在本裝置的觸發方式標籤。
   - `hal_audio`:把 16kHz mono PCM 送進喇叭;實作 `hal_audio_play_at()`(常開靜音填充 + 樣本級切入)。
   - `hal_battery` / `hal_power` / `hal_misc`。
4. `main/` 提供 `pk_board_device_class()`、`pk_board_default_volume()`、`pk_board_default_brightness()` 的強符號覆寫。
5. `main/CMakeLists.txt` 需 `WHOLE_ARCHIVE`(HAL 被 poker_core 反向引用,否則連結期符號未解析)。
6. 參考最接近的既有裝置照抄骨架(2 鍵抄 StickS3、3 鍵抄 xingzhi、鍵盤抄 Cardputer)。

> 首次建置會依 `dependencies.lock` 自動抓取 `managed_components`(LVGL 等),需網路。
