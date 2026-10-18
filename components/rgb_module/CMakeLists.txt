idf_component_register(
    SRCS "rgb.c"
    INCLUDE_DIRS "."
    REQUIRES driver led_strip event_bus split esp_driver_gpio
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wvla -Werror=implicit-fallthrough -Werror=uninitialized)