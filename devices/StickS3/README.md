# StickS3

M5Stack StickS3(ESP32-S3-PICO,8MB Flash + 8MB PSRAM)上的 POKER-NOW 韌體。

- **輸入方案**:2 鍵。KEY1(G11)=OK、KEY2(G12)=NEXT;以點擊/雙擊/長按組合出全部意圖:
  OK 點擊=確認、OK 長按=系統選單;NEXT 點擊=UP(+1/上移)、NEXT 雙擊=DOWN(−1/下移)、NEXT 長按=返回。
  (OK 無雙擊手勢,放開即觸發,反應零延遲。)
- **顯示**:ST7789P3 135×240,軟體旋轉為 240×135 橫向。
- **音訊**:ES8311 codec + AW8737 功放(功放使能經 M5PM1)。**注意 I2S 播放腳 DSDIN=G14**(硬體參考文件方向標反,以此為準)。
- **電源**:M5PM1 PMIC —— 啟動須使能 L3B 電源域否則 LCD 全黑;插 USB 時 VBAT 讀數不可靠。

細節見 [`hardware-reference.md`](hardware-reference.md)(硬體事實)與 [`dev-guide.md`](dev-guide.md)(設計理由 + 踩坑,§21 為精華)。

## 建置

```bash
. ~/.espressif/v6.0.1/esp-idf/export.sh
export PATH="$HOME/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin:$PATH"
cd devices/StickS3
idf.py set-target esp32s3     # 僅首次
idf.py -p <PORT> flash monitor
# 語音分區:python -m esptool --chip esp32s3 -p <PORT> write-flash 0x3F0000 ../../common/voice/voice.bin
```
