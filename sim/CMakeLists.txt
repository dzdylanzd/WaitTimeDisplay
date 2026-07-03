cmake_minimum_required(VERSION 3.16)
project(QueueWatchSim CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD   11)

# ---------------------------------------------------------------------------
# SDL2 (hardcoded to C:\SDL2 from the downloaded VC package)
# ---------------------------------------------------------------------------
set(SDL2_ROOT "C:/SDL2" CACHE PATH "Root of SDL2 VC development package")

add_library(SDL2::SDL2 SHARED IMPORTED)
set_target_properties(SDL2::SDL2 PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${SDL2_ROOT}/include"
    IMPORTED_IMPLIB               "${SDL2_ROOT}/lib/x64/SDL2.lib"
    IMPORTED_LOCATION             "${SDL2_ROOT}/lib/x64/SDL2.dll"
)
add_library(SDL2::SDL2main STATIC IMPORTED)
set_target_properties(SDL2::SDL2main PROPERTIES
    IMPORTED_LOCATION "${SDL2_ROOT}/lib/x64/SDL2main.lib"
)

# ---------------------------------------------------------------------------
# LVGL
# ---------------------------------------------------------------------------
set(LVGL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.pio/libdeps/esp32-c6-devkitc-1/lvgl")
if(NOT EXISTS "${LVGL_DIR}/src")
    message(FATAL_ERROR "LVGL not found. Run 'pio run' in the project root first.")
endif()
file(GLOB_RECURSE LVGL_SOURCES "${LVGL_DIR}/src/*.c")

# ---------------------------------------------------------------------------
# Simulator sources
# ---------------------------------------------------------------------------
set(SIM_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")

set(SIM_SOURCES
    ${SIM_DIR}/main_sim.cpp
    ${SIM_DIR}/lvgl_driver_sim.cpp
    ${SIM_DIR}/lcd_st7789_sim.cpp
    ${SIM_DIR}/tzhelper_sim.cpp
    ${SIM_DIR}/wifi_sim.cpp
    ${SIM_DIR}/wifimgr_sim.cpp
    # otaupdater_sim.cpp stands in for src/otaupdater.cpp: real OTA needs
    # Update.h/esp_ota_ops.h (ESP-IDF only) with no real flash to write to
    # in the sim, so it's a no-op stub instead — never add both.
    ${SIM_DIR}/otaupdater_sim.cpp
    ${SRC_DIR}/appstate.cpp
    ${SRC_DIR}/display.cpp
    ${SRC_DIR}/configmanager.cpp
    ${SRC_DIR}/queueapi.cpp
    ${SRC_DIR}/httpjson.cpp
    ${SRC_DIR}/cfgserver.cpp
    ${SRC_DIR}/trendstore.cpp
    ${SRC_DIR}/ridefilter.cpp
    ${SRC_DIR}/statusled.cpp
    ${SRC_DIR}/button.cpp
    ${LVGL_SOURCES}
)

add_executable(queuewatch_sim ${SIM_SOURCES})

# ---------------------------------------------------------------------------
# Include paths — order matters:
#   1. sim/  — shadows Arduino.h, SPI.h, ArduinoJson.h (→ real lib), WiFi.h,
#              Preferences.h, WebServer.h, HTTPClient.h, DNSServer.h, lv_conf.h
#   2. src/  — display.h, queueapi.h, configmanager.h, cfgserver.h, config.h
#   3. LVGL  — lvgl.h
#   4. SDL2  — SDL.h
# ---------------------------------------------------------------------------
set(ARDUINOJSON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.pio/libdeps/esp32-c6-devkitc-1/ArduinoJson")

target_include_directories(queuewatch_sim PRIVATE
    ${SIM_DIR}
    ${SRC_DIR}
    ${LVGL_DIR}
    "${SDL2_ROOT}/include"
    "${ARDUINOJSON_DIR}"
    "${ARDUINOJSON_DIR}/src"
)

target_compile_definitions(queuewatch_sim PRIVATE
    LV_CONF_INCLUDE_SIMPLE
    _CRT_SECURE_NO_WARNINGS
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    ARDUINOJSON_ENABLE_ARDUINO_STRING=1
    SIMULATION=1               # skip WiFi-status guard in httpGetJson() so /api/parks always works
)

if(MSVC)
    target_compile_options(queuewatch_sim PRIVATE /W3 /wd4100 /wd4267 /wd4244)
else()
    target_compile_options(queuewatch_sim PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

target_link_libraries(queuewatch_sim PRIVATE
    SDL2::SDL2
    SDL2::SDL2main
    winhttp      # WinHTTP for real HTTPS in HTTPClient.h (thread-safe)
    ws2_32       # WinSock2 for httplib WebServer
)

add_custom_target(run
    COMMAND queuewatch_sim
    DEPENDS queuewatch_sim
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running QueueWatch simulator"
)
