idf_component_register(
    SRCS
        "app_main.c" "board_i2c.c"
        "tca8418.c" "es8311_cardputer.c"
        "hal_display.c" "hal_input.c" "hal_audio.c"
        "hal_battery.c" "hal_power.c" "hal_misc.c"
    INCLUDE_DIRS "."
    REQUIRES
        poker_core
        esp_lcd esp_lvgl_port lvgl
        esp_driver_spi esp_driver_i2c esp_driver_i2s
        esp_driver_gpio esp_driver_ledc
        esp_adc
        esp_timer esp_partition spi_flash
        esp_wifi nvs_flash
    WHOLE_ARCHIVE      # S3 §21.2-1:HAL 實作在 main,被 poker_core 反向引用;
                       # 不加則 hal_* / pk_board_device_class 全部 undefined reference
)

# 鍵位校準建置(§6.4):idf.py build -DPK_KEYMAP_DUMP=1
if(PK_KEYMAP_DUMP)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PK_KEYMAP_DUMP=1)
endif()
