idf_component_register(
    SRCS "main.c" "rush_font_16.c" "rush_cover.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_hw_support nvs_flash
)
