# zuowei-c3-lcd-pwr

DOIT「小智」開發板(ESP32-C3,單核 RISC-V,8MB Flash,**無 PSRAM**)上的 POKER-NOW 韌體。

- **輸入方案**:3 鍵。中 G9=OK、左 G8=UP(+1/上移)、右 G7=DOWN(−1/下移);返回=右鍵長按、選單=左鍵長按。**中鍵長按保留給關機**(不分配 UI 意圖)。
- **顯示**:ST7789 240×240,SPI mode 3,80MHz。**LCD CS 必須為 GPIO12**(DIO flash 模式下空閒的 SPIHD 腳)。
- **音訊**:無直連麥克風/喇叭,全走 **VB6824** 語音晶片(UART1 @2Mbps),只用 PCM 16k 下行播放。
- **電源**:**GPIO13 整機電源自鎖** —— `app_main` 第一行必須拉高,否則電池供電開不了機;中鍵長按=軟關機(先觸發 Master 交接)。
- **Flash 為 DIO**,嚴禁改成 QIO。

細節見 [`hardware-reference.md`](hardware-reference.md) 與 [`dev-guide.md`](dev-guide.md)。

## 建置

```bash
. ~/.espressif/v6.0.1/esp-idf/export.sh
export PATH="$HOME/.espressif/tools/riscv32-esp-elf/esp-15.2.0_20251204/riscv32-esp-elf/bin:$PATH"
cd devices/zuowei-c3-lcd-pwr
idf.py set-target esp32c3     # 僅首次
idf.py -p <PORT> flash monitor
# 語音分區:python -m esptool --chip esp32c3 -p <PORT> write-flash 0x3F0000 ../../common/voice/voice.bin
```

> `main/` 旁的 `components/vb6824` 是本板專屬的閉源音訊驅動(隨專案自帶)。
