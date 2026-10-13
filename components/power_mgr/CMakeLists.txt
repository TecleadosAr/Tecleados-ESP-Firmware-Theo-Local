idf_component_register(
    SRCS "power_mgr.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_pm esp_hw_support esp_timer keyboard driver
)
