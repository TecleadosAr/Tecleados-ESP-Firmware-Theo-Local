idf_component_register(SRCS "statusmod.c"
                    INCLUDE_DIRS "include"
                    REQUIRES config_module usb_module event_bus split comm_module)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wvla -Werror=implicit-fallthrough -Werror=uninitialized)
