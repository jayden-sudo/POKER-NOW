# xingzhi-cube-1.54tft-wifi

星智 Cube 1.54"(ESP32-S3,8MB Flash + 8MB Octal PSRAM)上的 POKER-NOW 韌體。

- **輸入方案**:3 鍵。中 G40=OK、上 G39=UP(+1/上移)、下 G0=DOWN(−1/下移);返回=下鍵長按、選單=上鍵長按。開關機由專用硬體電源鍵負責(固件不參與)。
- **顯示**:ST7789 240×240,SPI3,mode 3,80MHz,`invert_color`。
- **音訊**:ESP32-S3 原生 I2S 直推功放(無 codec 晶片),16kHz mono,軟體音量增益;I2S TX 空閒須常開靜音填充以免嘟嘟聲。
- **電池**:ADC2_CH6(GPIO17)**與 Wi-Fi 衝突** —— 開機先讀一次基準值再啟 Wi-Fi,之後回快取值。

細節見 [`hardware-reference.md`](hardware-reference.md) 與 [`dev-guide.md`](dev-guide.md)(音訊/顯示大量沿用 StickS3 範本,差異驅動)。

## 建置

```bash
. ~/.espressif/v6.0.1/esp-idf/export.sh
export PATH="$HOME/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin:$PATH"
cd devices/xingzhi-cube-1.54tft-wifi
idf.py set-target esp32s3     # 僅首次
idf.py -p <PORT> flash monitor
# 語音分區:python -m esptool --chip esp32s3 -p <PORT> write-flash 0x3F0000 ../../common/voice/voice.bin
```
