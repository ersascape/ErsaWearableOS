file(GLOB_RECURSE APP_SOURCES "*.cpp" "*.c")
set(LIB_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.pio/libdeps/ErsaWearable")
if(DEFINED CONFIG_ERSA_DISPLAY_GXEPD2_GDEY0154D67 AND
   NOT CONFIG_ERSA_DISPLAY_GXEPD2_GDEY0154D67)
    message(FATAL_ERROR "The selected Terra BSP requires CONFIG_ERSA_DISPLAY_GXEPD2_GDEY0154D67")
endif()
list(FILTER APP_SOURCES EXCLUDE REGEX "/drivers/display/gxepd2/gxepd2_display\\.cpp$")
set(GxEPD2_SOURCES)
if(NOT DEFINED CONFIG_ERSA_DISPLAY_GXEPD2_GDEY0154D67 OR
   CONFIG_ERSA_DISPLAY_GXEPD2_GDEY0154D67)
    list(APPEND APP_SOURCES "${CMAKE_CURRENT_LIST_DIR}/drivers/display/gxepd2/gxepd2_display.cpp")
    set(GxEPD2_SOURCES
        "${LIB_ROOT}/GxEPD2/src/GxEPD2_EPD.cpp"
        "${LIB_ROOT}/GxEPD2/src/gdey/GxEPD2_154_GDEY0154D67.cpp"
    )
endif()
file(GLOB GFX_SOURCES "${LIB_ROOT}/Adafruit GFX Library/*.cpp" "${LIB_ROOT}/Adafruit GFX Library/*.c")
file(GLOB BUSIO_SOURCES "${LIB_ROOT}/Adafruit BusIO/*.cpp")
file(GLOB_RECURSE RTCLIB_SOURCES "${LIB_ROOT}/RTClib/src/*.cpp")
file(GLOB_RECURSE ONEBUTTON_SOURCES "${LIB_ROOT}/OneButton/src/*.cpp")

idf_component_register(
    SRCS ${APP_SOURCES} ${GxEPD2_SOURCES} ${GFX_SOURCES} ${BUSIO_SOURCES} ${RTCLIB_SOURCES} ${ONEBUTTON_SOURCES}
    INCLUDE_DIRS "." "../include"
        "${LIB_ROOT}/GxEPD2/src"
        "${LIB_ROOT}/Adafruit GFX Library"
        "${LIB_ROOT}/Adafruit BusIO"
        "${LIB_ROOT}/RTClib/src"
        "${LIB_ROOT}/OneButton/src"
    REQUIRES framework-arduinoespressif32 mbedtls app_update esp_http_client
)
