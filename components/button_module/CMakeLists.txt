idf_component_register(
    SRCS "button.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES driver freertos
    REQUIRES esp_timer esp_driver_gpio
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wvla -Werror=implicit-fallthrough -Werror=uninitialized)