idf_component_register(SRCS "ble_controller.c" "main.c"
                       PRIV_REQUIRES spi_flash
                       INCLUDE_DIRS "."
                       REQUIRES keyboard button_module rgb_module usb_module config_module ble_module status_module event_bus split comm_module battery_module power_mgr)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wvla -Werror=implicit-fallthrough -Werror=uninitialized)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)