# AGENT.md — POKER-NOW 開發記憶與能力移交

> 這份文件是給**接手本專案的 AI(或人)** 的完整記憶轉移。讀完它 + 各 `dev-guide.md`,你就擁有本專案至今所有的設計脈絡、踩過的坑、真機測試方法與續作方向。**動手前務必讀完本文件。**

---

## 0. 專案是什麼

跑在多種 ESP32 手持裝置上的**本地多人德州撲克遊戲機**。3–10 人同室,各持一台裝置,經 **ESP-NOW**(免路由器)直接對局;全程英文語音;任何裝置皆可即時接任莊家以容錯。四種裝置共用同一套遊戲核心,只有 HAL per-device。

四種裝置:StickS3(2 鍵/ES8311)、zuowei-c3-lcd-pwr(3 鍵/VB6824/C3 無 PSRAM)、xingzhi-cube-1.54tft-wifi(3 鍵/原生 I2S)、Cardputer-Adv(全鍵盤/ES8311/無 PSRAM)。

**原始需求(遊戲流程,權威細節見 `common/PROTOCOL.md` 與 `common/PRODUCT-SPEC.md`)**:同室 3–10 台裝置開機後,10 秒窗口內加入同一桌 → 齊播「確認莊家」、第一個按下者為首局莊家 → 由莊家左手第一人起依序認領座位 → 各自設定籌碼(最低 1、預設 15,本機語音播報)→ 莊家設定小盲/大盲/下注上限(預設 1/2/10)→ 發牌(每台只見自己 2 張底牌,5 張公共牌先蓋著;所有牌由莊家裝置分配並廣播全桌,以便莊家掉線時他人接任)→ 齊播「發底牌」「翻牌前下注」→ 由莊家左手起依序 過牌/下注/加注/跟注/棄牌/全下 → 翻牌(Flop 3 張)、轉牌(Turn)、河牌(River)、攤牌(Showdown)各輪 → 全棄則剩者播「我贏了」;攤牌則各台播自己最大牌型、贏家播「我贏了」、各台播最新籌碼 → 莊家左移開下一局,循環;任意時刻可退出,局間可由莊家批准新裝置加入(重排座、老玩家保留籌碼)。

## 1. 目錄地圖

- `common/` — 共用核心 `components/poker_core`(協定/狀態機/決策引擎/手牌/側池/UI/語音)、`voice/`(voice.bin)、設計權威文件 `PROTOCOL.md` + `PRODUCT-SPEC.md`。
- `devices/<裝置>/` — 各裝置 ESP-IDF 專案(只實作 HAL)+ `hardware-reference.md` / `dev-guide.md` / `README.md`。
- `tools/` — 語音產生(gen_voice.sh/wav2adpcm.py)、host 端手牌/側池單元測試(test_hand_eval.c)。

**程式碼註解章節簡寫**:「協定 §」→ `common/PROTOCOL.md`;「產品 §」→ `common/PRODUCT-SPEC.md`;「指南 §」→ 該裝置 `dev-guide.md`。各 `dev-guide.md` 的 **§21(StickS3/zuowei/xingzhi)/§17(Cardputer)「實作經驗回寫」是踩坑精華**。

## 2. 開發歷程與方法(原專案計畫)

- 開發順序(為提高複用而採順序開發,非並行):**StickS3 → zuowei → xingzhi → Cardputer**。第一台誕生共用核心 `poker_core`,之後裝置**只寫 HAL、不改核心**。
- 設計文件(`PROTOCOL.md`、`PRODUCT-SPEC.md`)在開發初期經**雙模型(Fable+Opus)對抗式審查**兩輪後定稿:協定引入 epoch 任期、三階段接管、桌面時鐘、`bet_hand`(退注/側池地基);產品文件確立 HAL 介面、2 鍵互動、畫面全矩陣、英文語音系統。
- 統一工具鏈 **ESP-IDF v6.0.1**。四機皆用 esp_lcd + LVGL(不用 M5GFX —— 它在 IDF v6 無法編譯)。
- **重要工作限制**:開發後期觸發了 org API 月度限額,**subagent 不可用**,一切由主 agent 直接執行。若你也遇到,直接做即可,別依賴 subagent。

## 3. 架構要點

- **深模組**:`pbus`(協定層)對上層只暴露「可靠有序事件流 + 成員/角色」;ESP-NOW 不可靠廣播、重傳、去重、時鐘同步、容錯接任全部封裝。上層是純確定性狀態機。
- **全狀態複製**:每筆牌局資訊(含所有底牌)廣播全桌 → 任一台可接任莊家。不做保密(線下娛樂,goal 明示取捨)。
- **單一序列器 + epoch**:同時只有一台 Master 編序事件,事件全序 = `(epoch, seq)`,高 epoch 無條件勝(消解雙 Master/腦裂)。
- **命令冪等**:所有 CMD 為 set / to-amount 語義,交接窗口重送天然無害。
- **抽象輸入意圖(v2.4 重構的核心)**:HAL 只回報 5 種與硬體無關意圖 `UI_OK / UI_UP / UI_DOWN / UI_BACK / UI_MENU`;物理按鍵如何組合由各裝置 `hal_input.c` 自決。**方向鍵一律空間語義**:數值畫面 UP=+1 / DOWN=−1;清單/滾輪畫面 UP=高亮上移一項 / DOWN=下移一項。這是為了避免「數值 + / 清單方向」相反的錯亂(見 §6 教訓)。
- **UI 渲染下沉共用**:`ui_lvgl.c` 是唯一渲染器,四台像素級一致;裝置 `hal_display.c` 只做面板 bring-up + 亮度 + `ui_lvgl_attach()`。兩檔佈局:240×240 與 240×135。

## 4. 建置/燒錄(共通)

```bash
. ~/.espressif/v6.0.1/esp-idf/export.sh
# 工具鏈 PATH(export.sh 常未自動加):
#   S3 三台:$HOME/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin
#   C3:     $HOME/.espressif/tools/riscv32-esp-elf/esp-15.2.0_20251204/riscv32-esp-elf/bin
cd devices/<裝置>
idf.py set-target <esp32s3|esp32c3>   # 僅首次
idf.py build
python -m esptool --chip <chip> -p <PORT> --before default-reset --after hard-reset \
   write-flash 0x10000 build/poker_<裝置>.bin
# 語音分區(四台共用):write-flash 0x3F0000 ../../common/voice/voice.bin
```

每台裝置以各自的 USB 串列埠連線,埠名依主機與插入順序而定(每台不同、每次可能改變),命令一律用 `-p <PORT>` 佔位,實際燒錄前以 `ls /dev/tty*`(Linux)或 `ls /dev/cu.*`(macOS)確認當台的埠。
**注意**:zsh 迴圈不做字串分詞,逐台寫明確指令,別用 `for spec in "a b c"` 展開(會把整串當一個參數)。

## 5. 串口自動化測試通道(非常有用)

韌體常駐 `pk_testio.c`:USB 序列埠收單字元即注入抽象意圖 —— `o`=OK `u`=UP `d`=DOWN `b`=BACK `m`=MENU;每次畫面渲染輸出一行 `ui: [畫面] t=.. g=.. big=.. pot=.. my=.. hand=.. board=..`(+ 模態行 `MODAL ...`)。可**全自動驅動多機牌局**,不需人工按鍵。

主機端有雙向 daemon(開發時放 scratchpad):`serial_io.py <port> <logfile> <cmdfile>` —— 持續讀串口寫 log,並輪詢 cmdfile 把內容寫進串口。用 `printf o > <dev>.cmd` 即可注入。用它可重現/驗證成桌、儀式、整局、換莊、接管等。「重置裝置」= DTR/RTS 舞步(`s.dtr=False; s.rts=True; sleep; s.rts=False`)。

## 6. 所有踩過的坑與修復(按類別;避免重蹈)

### 協定/容錯(真機聯調批次,程式碼搜「v1.1 真機修訂」「B6」)
- **無 epoch → 雙 Master 同 seq 分歧**:已加 epoch,`(epoch,seq)` 全序仲裁。
- **接管未選最完整日誌**:接管改「宣告 last_seq → 收集 → 最高者當選 + 先補齊再編號」,錯峰宣告(依座位距離),同桌雙 Master 靠 epoch/MAC 消解。
- **`TABLE_DISSOLVE` 未過濾 table_id** → 別桌的解散廣播炸掉本桌(真機踩中三人桌被殺):只認指名本桌者。
- **on_hello 的 PENDING/REJECT 分支漏 `pn_add_peer`** → ESP-NOW 對非 peer 單播靜默失敗,候選者收不到 ACK 無限循環:任何單播回覆前必先 add_peer。
- **成桌後 Master 停發 ANNOUNCE** → 晚開機/重啟裝置找不到已成之桌:ROLE_MASTER 續發 1Hz ANNOUNCE。
- **JOINING/LOBBY/PENDING 無超時或不回掃描** → 目標桌消失即卡死:各加超時回掃描。
- **建桌衝突合併未實作** → 多台同開多桌鎖步不相遇:MAC 小者讓桌 + 帶導引的 dissolve。
- **未入桌(table_id==0)吃到別桌 EVT** → 被 epoch 仲裁降級成幽靈成員:未入桌一律丟棄 EVT。
- **`screen_from_state` 未處理未入桌** → phase=0 恰為 LOBBY,被解散後顯示「LOBBY 0 players」如死機:`my_player_id==0xFF` 一律 SCANNING。

### 兩個致命輸入 bug(早期真機)
- **註冊順序**:`game_init` 先註冊 cmd_handler 再 `pbus_start`,而 `pbus_start` 開頭 `memset(g_pb)` 把註冊抹掉 → Master 拒絕所有命令、引擎 tick 不跑。**必須在 pbus_start 之後註冊。**
- **Master 自環**:Master 本人的命令走「單播給自己」,ESP-NOW 無自環 → Master 台按鍵全無效。drain_tx 中 Master 本人命令直接同步進決策引擎。

### 撲克規則正確性(程式碼搜對應註解)
- 側池:逐層切池,未跟注超額退還,奇數籌碼歸按鈕左手最近贏家;host 測試 37 項全過。
- 首行動位:preflop = 大盲左手(UTG)、postflop = 按鈕左手;heads-up 特例(按鈕兼小盲、preflop 按鈕先動)。
- BB 選擇權保留;短盲不足額全下;不足額全下不重開加注權(已行動者只能跟/棄)。
- 修過:begin_hand 暫停條件 `<2 && <1` 廢掉了 `<2`(只剩 1 人不暫停);do_abort 退注表空槽哨兵未設 0xFF → 誤清 seat 0 籌碼(B3,重大)。

### 顯示(Cardputer 三層,dev-guide §17 / R15)
- **黑屏根因 = 背光**:G38 兼 RGB LED 電源使能,LEDC 照抄 5kHz 令電源軌起不來;M5GFX 官方用 **256Hz** → 改 256Hz 後可見。**教訓:背光腳兼電源使能的板子勿照抄頻率,先查 M5GFX 同板 Light_PWM。**
- 方向 180° + gap:mirror 取反、gap 實測定案(±1 只能實測)。
- 通用:白屏可見→LVGL 後變黑,先分離「GRAM 資料」與「背光」兩個自由度。

### 音訊(dev-guide §21.11 / R17,兩板皆 ES8311)
- **StickS3 無聲根因 = I2S 播放腳接反**:硬體參考文件 G14/G16 方向是「慣例推斷」且推反了。以 78/xiaozhi-esp32 `m5stack-stick-s3`(真機出聲)為準:**DSDIN(播放)=G14**。**教訓:引腳「方向」必須用會出聲的參考實作驗證,寄存器讀回自證救不了接反的線。**
- **Cardputer 無聲三重封印**:① I2S MONO slot 令 BCLK 錯一倍(ES8311 SCLK 模式分頻按 512kHz)→ 改 STEREO;② REG01=0xB0 低半位元組=0 → DAC/ADC 時鐘全關 → 0xBF;③ 序列缺 0x0E/0x12/0x13/0x1C/0x31(類比上電/DAC 上電/輸出級/解靜音)→ 補齊對齊 StickS3。
- **音量模型**:ES8311 兩板 codec 固定 0dB(0x32 用 0xBF 級),音量單走軟體增益 —— 修「codec + 軟體雙重衰減 ≈ 近無聲」。**教訓:同一顆 codec 的第二份驅動,複製已真機驗證的序列再改差異點(時鐘源),別重新推導。**
- **AW8737 功放**:使能腳在 M5PM1 GPIO3,PM1 上電預設開漏、板上無上拉 → 須先配推挽再拉高(讀回 GPIO_IN.3=1 自證)。
- VB6824(zuowei):只需 PCM 16k 下行播放,不需 Opus/麥克風;常開靜音填充在 UART 流上實作。

### 電池
- StickS3 插 USB 時 VBAT 讀**非零殘值**(數百 mV),繞過 `==0` 判斷顯示 0%:守門改 `vbat < 3000mV → 0xFF(未知,顯示 USB)`。
- xingzhi ADC2 與 Wi-Fi 衝突 → 開機先讀基準再啟 Wi-Fi,之後快取。

### 工具鏈
- 各裝置 `main` 的 CMakeLists 需 `WHOLE_ARCHIVE`(HAL 被 poker_core 反向引用,否則 `hal_*` 連結期未解析)—— 這是 R13 之外最通用的建置教訓(StickS3 §21.2）。
- riscv32 GCC 15 比 xtensa 嚴:曾抓到凍結核心 `pbus_session.c on_cmd()` 的真實 2-byte 堆疊溢位(`body[8]` 應為 `sizeof(pn_cmd_ack_t)+6`,R13,已修)。
- `ESP_RETURN_ON_ERROR` 等巨集需顯式 `#include "esp_check.h"`。

## 7. UI 版本演進(理解目前狀態)

- v1.2:電量常駐標題列、亮度/音量系統選單 + NVS 持久化。
- v1.3:仿真紙牌(花色圖形、紅黑、牌背)、遊戲中五區版式(狀態+電量/公共牌/POT/我的籌碼/手牌)、開機 splash 招牌、結算排行榜、渲染下沉 `ui_lvgl.c`。
- v1.6:MY_TURN 行動「滾輪」(金邊閃爍晶片 + 上下鄰項)。
- v1.8:模態對話框(Fold/All-in/離桌/RAISE 金額);修 AMOUNT_PICK 送出後卡住(它排除於畫面重整,送出須主動離開)。
- v2.1:**輸入架構重構** —— 抽象意圖下放裝置端(見 §3)。
- v2.2/2.3:模態提示改「垂直堆疊置中多行」(消除重疊);數值輸入畫面(SET CHIPS/BLINDS/BUY IN)全部改模態版式(消除「金額與文案重疊」)。
- v2.4:方向鍵改純空間 `UI_UP/UI_DOWN` 語義(修「Cardputer 動作滾輪上下顛倒」)。
- 每個裝置的按鍵標籤由 `hal_input_hint()` 自報,提示文字用它組合(鍵盤顯示 ok/ESC/方向,2 鍵顯示 OK/NEXT/hold NEXT,3 鍵顯示 MID/UP/DOWN 等)。

## 8. 已知未完項(續作方向)

1. **實機精修**:各 `dev-guide.md` 的【待實測】—— `AUDIO_PATH_LATENCY_MS` 校準、跨機齊播 <30ms、任務棧高水位、SRAM 高水位。功能已通,屬調校。
2. Cardputer 數字鍵直輸籌碼、StickS3 IMU 傾斜輔助 = 休眠加分項(目前 UP/DOWN 已覆蓋需求)。
3. 若擴充非冪等命令,須維護命令去重表跨接管移交(目前靠冪等性天然安全)。

## 9. 續作工作準則(來自本次開發的實務)

- **改遊戲/協定/UI → 只動 `common/`;新裝置 → 只在 `devices/` 實作 HAL。** 這條界線是複用率與跨裝置一致性的根本。
- **凍結核心的精神**:`poker_core` 已四機驗證;非必要不改,改則四機一起重建驗證。
- **每次改動編譯要零 warning**(自訂碼);packed 協定結構加 `_Static_assert` 鎖 sizeof。
- **真機測試優先用串口通道自動跑**,人工只驗聽感/目視(齊播回聲、畫面觀感)。
- **改任何引腳/音訊/時鐘,先查該裝置 `hardware-reference.md` 與線上參考實作**;寄存器讀回自證不能保證「線接對、方向對」。
- 修 bug 後把「硬體無關的教訓」回寫到 `dev-guide.md` 的經驗回寫章節,供後續裝置直接複用 —— 這是本專案知識累積的機制。
