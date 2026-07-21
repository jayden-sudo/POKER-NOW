# common — 全裝置共用元件

這裡是 POKER-NOW 的「大腦」。四種裝置**完全共用同一份**程式碼,規則寫一次、四台一致。共用碼**嚴禁任何板型 `#ifdef`**;所有裝置差異都表達為 HAL 介面的不同實作(在 [`../devices/`](../devices/))。

## 內容

| 路徑 | 說明 |
|---|---|
| `components/poker_core/` | 唯一的共用 ESP-IDF 元件。各裝置專案以 `EXTRA_COMPONENT_DIRS = ../../common/components` 引用。 |
| `components/poker_core/include/` | 公開標頭:協定層 `pbus.h`、狀態機 `game_state.h`、決策引擎 `master_engine.h`、手牌評估 `hand_eval.h`、側池 `side_pot.h`、語音 `voice.h`、畫面流程 `app_flow.h`,以及 **HAL 介面** `hal/hal_*.h`(裝置端實作)。 |
| `components/poker_core/src/` | 實作。分層見下。 |
| `voice/` | 英文語音資產:`voice.bin`(74 段 IMA-ADPCM,四台共用)、`voice_ids.h`、`voice_list.tsv`(文字清單,改詞後用 [`../tools/`](../tools/) 重新產生)。 |
| [`PROTOCOL.md`](PROTOCOL.md) | **ESP-NOW 核心協定(權威)**。程式碼註解中的「協定 §X」即指此文件對應章節。 |
| [`PRODUCT-SPEC.md`](PRODUCT-SPEC.md) | **產品/HAL/UI/語音規格(權威)**。程式碼註解中的「產品 §X」即指此文件。 |

> 程式碼註解常見的章節簡寫對照:**「協定 §」→ [`PROTOCOL.md`](PROTOCOL.md);「產品 §」→ [`PRODUCT-SPEC.md`](PRODUCT-SPEC.md);「指南 §」→ 該裝置的 `devices/<裝置>/dev-guide.md`**。

## 分層(`poker_core`)

```
app_flow / app_screens / ui_lvgl      畫面狀態機、畫面組裝、LVGL 渲染(消費「抽象意圖」)
game_state / master_engine            全員一致狀態機 + Master 端決策引擎(側池結算等)
hand_eval / side_pot / number_speech  純函式:7 選 5 牌型、側池切池、數字轉語音
voice_adpcm                           IMA-ADPCM 拉取式串流解碼
pbus_*(session/reliab/clock/transport)  ESP-NOW 協定層:發現/加入/心跳/補洞/接管/桌面時鐘
pk_testio                             串口測試通道(注入抽象意圖 + UI 語義 trace)
hal/*.h                               對裝置端的窄介面(唯一的 per-device 邊界)
```

## 關鍵設計原則

- **深模組**:`pbus` 對上層只暴露「可靠有序的事件流 + 成員/角色」,把 ESP-NOW 的不可靠廣播、重傳、去重、時鐘同步、容錯接任全部藏起來。
- **全狀態複製**:牌局每一筆資訊(含所有人底牌)廣播給所有裝置,故任一台可無縫接任莊家(協定需求 9)。線下娛樂,不做保密。
- **抽象輸入意圖**:HAL 只回報 5 種與硬體無關的意圖(`UI_OK/UI_UP/UI_DOWN/UI_BACK/UI_MENU`);物理按鍵(2 鍵點/雙/長、3 鍵、全鍵盤)如何組合成意圖,由各裝置 `hal_input.c` 自決。方向鍵一律空間語義:數值畫面 UP=+1/DOWN=−1,清單畫面 UP=上移/DOWN=下移。

改動遊戲行為、協定、UI 只需動這裡;新增裝置只需在 `devices/` 實作 HAL。
