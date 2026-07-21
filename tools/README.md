# tools — 開發輔助工具

開發 POKER-NOW 過程中會用到的離線工具(在開發機上執行,不進韌體)。

| 檔案 | 用途 |
|---|---|
| `gen_voice.sh` | 產生英文語音資產 `common/voice/voice.bin`。用 macOS 內建 `say` 合成語音 → 16kHz mono WAV → 呼叫 `wav2adpcm.py` 打包。詞表在 [`../common/voice/voice_list.tsv`](../common/voice/voice_list.tsv),改詞後重跑本腳本即可;產物 `voice.bin` 四台共用,燒到分區偏移 `0x3F0000`。 |
| `wav2adpcm.py` | 把 WAV 轉成 IMA-ADPCM(4-bit,~8KB/s)並打包成 `voice.bin` + 生成 `voice_ids.h`(語音 ID 列舉)。由 `gen_voice.sh` 呼叫,也可獨立使用。 |
| `test_hand_eval.c` | 手牌評估器與側池結算的 **host 端單元測試**(用桌機 `cc` 直接編譯,不需 ESP-IDF)。改動 `common/.../hand_eval.c` 或 `side_pot.c` 後務必跑一次。 |

## 用法

產生語音資產:

```bash
cd tools
./gen_voice.sh          # 讀 ../common/voice/voice_list.tsv → 寫 ../common/voice/{voice.bin,voice_ids.h}
```

跑手牌/側池單元測試(改過牌型或側池邏輯後):

```bash
cc -I ../common/components/poker_core/include \
   -o /tmp/htest test_hand_eval.c \
   ../common/components/poker_core/src/hand_eval.c \
   ../common/components/poker_core/src/side_pot.c
/tmp/htest        # 應印 "N passed, 0 failed"
```

> 另有一個**串口測試通道**內建在韌體裡(`common/.../pk_testio.c`):裝置上電後可經 USB 序列埠注入抽象意圖字元(`o`=OK `u`=UP `d`=DOWN `b`=BACK `m`=MENU)並讀取每幀的 UI 語義 trace,用來全自動驅動多機牌局測試,無需人工按鍵。詳見 `AGENT.md`。
