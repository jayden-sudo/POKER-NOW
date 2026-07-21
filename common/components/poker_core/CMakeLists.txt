idf_component_register(
    SRCS
        "src/pbus.c" "src/pbus_transport.c" "src/pbus_reliab.c"
        "src/pbus_session.c" "src/pbus_clock.c"
        "src/game_state.c" "src/master_engine.c" "src/side_pot.c"
        "src/hand_eval.c"
        "src/voice_adpcm.c" "src/number_speech.c"
        "src/app_flow.c" "src/app_screens.c"
        "src/ui_lvgl.c" "src/pk_testio.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_event esp_timer nvs_flash
    PRIV_REQUIRES lvgl esp_lvgl_port esp_driver_usb_serial_jtag
)
# 單機冒煙模式:idf.py build -DPK_DEBUG_SOLO=1(§20.3)
if(PK_DEBUG_SOLO)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC PK_DEBUG_SOLO=1)
endif()
