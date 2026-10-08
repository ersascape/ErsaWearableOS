file(GLOB_RECURSE APP_SOURCES "*.cpp" "*.c")
if(CONFIG_IDF_TARGET_ESP32C6)
    set(LIB_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.pio/libdeps/ErsaWearableC6")
else()
    set(LIB_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.pio/libdeps/ErsaWearable")
endif()
if(DEFINED CONFIG_ERSA_DISPLAY_SSD1681_GDEY0154D67 AND
   NOT CONFIG_ERSA_DISPLAY_SSD1681_GDEY0154D67)
    message(FATAL_ERROR "The selected Terra BSP requires CONFIG_ERSA_DISPLAY_SSD1681_GDEY0154D67")
endif()
idf_component_register(
    SRCS ${APP_SOURCES}
    INCLUDE_DIRS "." "../include"
        "${LIB_ROOT}/Adafruit GFX Library"
        "${LIB_ROOT}/Adafruit BusIO"
        "${LIB_ROOT}/OneButton/src"
    REQUIRES framework-arduinoespressif32 mbedtls app_update esp_http_client
        esp_driver_gpio esp_driver_spi
)
