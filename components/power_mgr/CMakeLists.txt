idf_component_register(
    SRCS "power_mgr.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_pm esp_hw_support esp_timer keyboard driver battery_module event_bus
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wvla -Werror=implicit-fallthrough -Werror=uninitialized)
