idf_component_register(
    SRCS "event_bus.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_event
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wvla -Werror=implicit-fallthrough -Werror=uninitialized)
