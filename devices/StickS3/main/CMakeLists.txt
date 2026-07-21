idf_component_register(
    SRCS
        "app_main.c" "board_i2c.c" "m5pm1.c"
        "hal_display.c" "hal_input.c" "hal_audio.c"
        "hal_battery.c" "hal_power.c" "hal_misc.c"
        "imu_bmi270.c" "es8311_min.c"
    INCLUDE_DIRS "."
    REQUIRES
        poker_core
        esp_lcd esp_lvgl_port lvgl
        esp_driver_spi esp_driver_i2c esp_driver_i2s
        esp_driver_gpio esp_driver_ledc
        esp_timer esp_partition spi_flash
        esp_wifi nvs_flash
    WHOLE_ARCHIVE      # HAL 實作在 main,被 poker_core 反向引用;
                       # 全 archive 連結以消解與 poker_core 的循環相依(否則 hal_* 未解析)
)
