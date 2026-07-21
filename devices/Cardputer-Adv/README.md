# Cardputer-Adv

M5Stack Cardputer-Adv(ESP32-S3FN8,8MB Flash,**無 PSRAM**)上的 POKER-NOW 韌體。板級工作量最大(鍵盤與音訊驅動需自寫)。

- **輸入方案**:全鍵盤(TCA8418,I2C 0x34)。Enter(鍵帽標示 `ok`)=OK;方向鍵免 Fn(絲印 `;`↑ `,`← `.`↓ `/`→):↑/→=UP、↓/←=DOWN;`` ` ``(ESC 位)=返回;Tab=選單;G0(BtnA)並聯 OK。
- **顯示**:ST7789V2 240×135。**背光 G38 兼 RGB LED 電源,LEDC 頻率須用 256Hz**(照抄 5kHz 會令電源軌起不來、全黑);無 PSRAM,LVGL 雙緩衝。
- **音訊**:ES8311 **無 MCLK 模式**(SCLK 作時鐘源,REG01=0xBF);I2S 用 STEREO slot 才能對上分頻(MONO 會令 DAC 時鐘錯一倍而無聲)。
- **無 PSRAM**:512KB SRAM,禁用 CONFIG_SPIRAM。

細節見 [`hardware-reference.md`](hardware-reference.md) 與 [`dev-guide.md`](dev-guide.md)(§17 為踩坑精華:背光/鍵盤矩陣/ES8311)。

## 建置

```bash
. ~/.espressif/v6.0.1/esp-idf/export.sh
export PATH="$HOME/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin:$PATH"
cd devices/Cardputer-Adv
idf.py set-target esp32s3     # 僅首次
idf.py -p <PORT> flash monitor
# 語音分區:python -m esptool --chip esp32s3 -p <PORT> write-flash 0x3F0000 ../../common/voice/voice.bin
# 進下載模式(USB CDC 不可用時):電源開關 OFF → 按住 G0 → 插 USB
```
