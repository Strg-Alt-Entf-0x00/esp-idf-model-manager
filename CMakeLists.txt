idf_component_register(
    SRCS
        "src/model_manager.c"
        "src/esp_model_format.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        nvs_flash
        esp_timer
        esp_common
        freertos
        vfs
        fatfs
)

