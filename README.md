# POKER-NOW

一套跑在多種 **ESP32 手持裝置**上的**本地多人德州撲克遊戲機**。3–10 名玩家各持一台裝置,同處一室,透過 **ESP-NOW**(免路由器)直接連線對局;全程英文語音提示,任何裝置皆可即時接任莊家以容錯。

目前支援四種裝置,共用同一套遊戲核心與協定,只有硬體抽象層(HAL)per-device:

| 裝置 | 主控 | 螢幕 | 輸入 | 音訊 |
|---|---|---|---|---|
| [StickS3](devices/StickS3/) | ESP32-S3 (8MB PSRAM) | 135×240 | 2 鍵 | ES8311 codec |
| [zuowei-c3-lcd-pwr](devices/zuowei-c3-lcd-pwr/) | ESP32-C3 (無 PSRAM) | 240×240 | 3 鍵 | VB6824 (UART) |
| [xingzhi-cube-1.54tft-wifi](devices/xingzhi-cube-1.54tft-wifi/) | ESP32-S3 (8MB PSRAM) | 240×240 | 3 鍵 | 原生 I2S 功放 |
| [Cardputer-Adv](devices/Cardputer-Adv/) | ESP32-S3FN8 (無 PSRAM) | 240×135 | 全鍵盤 | ES8311 codec |

## 目錄結構

| 路徑 | 用途 |
|---|---|
| **[`common/`](common/)** | 全裝置共用的元件:遊戲核心(協定/牌局狀態機/決策引擎/手牌評估/側池)、UI 渲染、語音資產。四台裝置**完全共用同一份**,不含任何板型 `#ifdef`。另含兩份設計權威文件 [`PROTOCOL.md`](common/PROTOCOL.md)(ESP-NOW 核心協定)與 [`PRODUCT-SPEC.md`](common/PRODUCT-SPEC.md)(產品/UI/HAL 規格)。 |
| **[`devices/`](devices/)** | 四種裝置的 ESP-IDF 專案,每個子目錄直接是一個可建置的韌體,只實作該裝置的 HAL(顯示/輸入/音訊/電池/電源)。 |
| **[`tools/`](tools/)** | 開發輔助工具:語音資產產生(`gen_voice.sh` + `wav2adpcm.py`)、手牌/側池 host 端單元測試(`test_hand_eval.c`)。 |
| **[`AGENT.md`](AGENT.md)** | 專案的完整「開發記憶」——設計決策、所有踩坑與修復、真機測試方法、續作指引。**接手本專案(人或 AI)請先讀它。** |

## 快速開始(建置與燒錄)

需要 **ESP-IDF v6.0.1**。以 StickS3 為例:

```bash
. ~/.espressif/v6.0.1/esp-idf/export.sh
# 首次建置會依 dependencies.lock 自動抓取 managed_components(LVGL 等)
cd devices/StickS3
idf.py set-target esp32s3        # 僅首次;zuowei 為 esp32c3
idf.py build
idf.py -p <PORT> flash monitor
```

語音分區另外燒錄(全部設備共用同一份 `common/voice/voice.bin`,分區偏移 `0x3F0000`):

```bash
python -m esptool --chip esp32s3 -p <PORT> write-flash 0x3F0000 ../../common/voice/voice.bin
```

各裝置的實際晶片、燒錄埠、工具鏈 PATH 細節見各自的 [`devices/<裝置>/README.md`](devices/) 與 `dev-guide.md`。

## 如何二次開發

- **改遊戲規則 / 協定 / UI 邏輯** → 只動 [`common/`](common/)。改一次,全部設備一起生效。設計權威見 `common/PROTOCOL.md`、`common/PRODUCT-SPEC.md`。
- **接新裝置** → 在 [`devices/`](devices/) 新增一個目錄,實作 HAL 介面(`common/components/poker_core/include/hal/*.h`:`hal_display` / `hal_input` / `hal_audio` / `hal_battery` / `hal_power` / `hal_misc`),`EXTRA_COMPONENT_DIRS` 指向 `../../common/components`。輸入層只需把物理按鍵映射成 5 個抽象意圖(`UI_OK/UI_UP/UI_DOWN/UI_BACK/UI_MENU`)。詳見 [`devices/README.md`](devices/README.md)。
- **改語音** → 編輯 `common/voice/voice_list.tsv`,用 [`tools/`](tools/) 重新產生 `voice.bin`。
- **理解為什麼這麼設計 / 避免重蹈覆轍** → 讀 [`AGENT.md`](AGENT.md) 與各裝置的 `dev-guide.md`。

## 狀態

全部設備皆為**建置級交付並經真機聯調**(成桌、儀式、完整牌局、換莊、Master 掉線接任、錯峰開機、音訊、UI 皆真機驗證)。仍待逐台精修的實測項(音訊延遲校準、齊播精度)列於各 `dev-guide.md` 的【待實測】清單。

## 後記:一個不該存在的專案

本專案由 **Opus 4.8 + Fable 5** 開發。

按舊世界的帳本,它本不該存在:受眾寥寥,分文不入,開發成本高昂,亦無網路效應可言——在 AI 時代之前,沒有任何一條理性的路徑通向這裡。過去只會停留在「要是有空就好了」的嘆息裡。

如今它就在你眼前。AI 撥動了世界時間線:無數可能的未來並排流淌,而我們所處的這一條,已被輕輕撥向了另一支流。未來正離人類昔日的預言分叉越來越遠;那些預言裡不會有的小東西,將一件一件地,先於預言抵達。
