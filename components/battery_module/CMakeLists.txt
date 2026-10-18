idf_component_register(
    SRCS "battery.c"
    INCLUDE_DIRS "include" "."
    REQUIRES esp_adc esp_timer event_bus ble_module esp_event
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wvla -Werror=implicit-fallthrough -Werror=uninitialized)
