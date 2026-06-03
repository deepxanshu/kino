set(SOURCES
    app_state.c
    device_mode.c
    bluetooth/bt_hid_mouse.c
    bluetooth/bt_input.c
    joystick/joyc_i2c_bridge.cpp
    joystick/joyc_button.c
    joystick/mouse_controller.c
    joystick/joyc_soft_i2c.c
    joystick/joystick_handle.c
    mic/mic_spectrum.c
    mic/mic_handle.cpp
    ui/ui_running_screen.c
    ui/ui_setup_screen.c
    ui/ui_imu_screen.c
    ui/ui_mic_screen.c
    ui/ui_theme.c
    ui/ui.c
    ui/updown_img.c
    lvgl_port.cpp
    StickC-JoyMic.cpp
)

idf_component_register(SRCS ${SOURCES}
                    INCLUDE_DIRS 
                    "."
                    bluetooth
                    joystick
                    mic
                    ui
                    REQUIRES bt nvs_flash
                    )
