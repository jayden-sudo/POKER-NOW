#pragma once
/*
 * hal_input.h -- 抽象 UI 意圖介面(v2.1 重構)。
 *
 * 設計:common 只認識 5 種與硬體無關的「意圖」;各裝置的 main/hal_input.c 自行決定
 * 物理按鍵(數量、點擊/雙擊/長按、鍵盤鍵位)如何組合出這些意圖 —— 按鍵相關邏輯
 * 全部在裝置端,不在 common。common 只註冊回呼、消費意圖、用 hal_input_hint() 顯示
 * 各裝置自己的觸發方式提示。
 */
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_OK   = 0,   /* 確認 / 選定 / 執行 */
    UI_UP,         /* 空間「上」:數值 +1 / 清單高亮上移一項 */
    UI_DOWN,       /* 空間「下」:數值 -1 / 清單高亮下移一項 */
    UI_BACK,       /* 取消 / 退出目前介面回上一層 */
    UI_MENU,       /* 開啟系統選單(音量/亮度/離桌) */
    UI_EVENT_COUNT
} hal_ui_event_t;
/* 設計原則(v2.4):方向鍵一律以「空間」語義出現,各畫面自行解讀 ——
 * 數值畫面:UP=+1、DOWN=-1(上=多);清單/滾輪:DOWN=移到「畫面下方」那項、UP=上方那項。
 * 如此上下鍵在任何畫面都符合人類直覺,不會出現數值與清單方向相反的錯亂。 */

typedef void (*hal_ui_cb_t)(hal_ui_event_t ev);

/* 裝置端實作:初始化物理輸入,合成意圖後呼叫 cb(於普通 task 上下文)。 */
esp_err_t hal_input_init(hal_ui_cb_t cb);

/* 裝置端實作:回傳該意圖在「本裝置」上的觸發方式短標籤,供 UI 提示。
 * 例:2 鍵裝置 UI_BACK→"hold NEXT";鍵盤裝置 UI_BACK→"ESC"。 */
const char *hal_input_hint(hal_ui_event_t ev);

#ifdef __cplusplus
}
#endif
