idf_component_register(
    SRCS
        "app_main.c"
        "hal_display.c" "hal_input.c" "hal_audio.c"
        "hal_battery.c" "hal_power.c" "hal_misc.c"
    INCLUDE_DIRS "."
    REQUIRES
        poker_core
        esp_lcd esp_lvgl_port lvgl
        esp_driver_spi esp_driver_i2s
        esp_driver_gpio esp_driver_ledc
        esp_adc
        esp_timer esp_partition spi_flash
        esp_wifi nvs_flash
    WHOLE_ARCHIVE      # S3 §21.2-1:HAL 實作在 main,被 poker_core 反向引用;
                       # 不加則 hal_battery_pct/hal_rand/hal_settings_*/pk_board_device_class
                       # 全部 undefined reference(四機共通,一律照加)
)
