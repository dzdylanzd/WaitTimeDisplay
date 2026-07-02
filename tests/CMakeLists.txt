cmake_minimum_required(VERSION 3.16)
project(QueueWatchTests CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD   11)

set(TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(SIM_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/../sim")
set(SRC_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/../src")
set(JSON_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/../.pio/libdeps/esp32-c6-devkitc-1/ArduinoJson")

if(NOT EXISTS "${JSON_DIR}/ArduinoJson.h")
    message(FATAL_ERROR "ArduinoJson not found. Run 'pio run' in the project root first.")
endif()

add_executable(queuewatch_tests
    ${TESTS_DIR}/test_main.cpp
    ${TESTS_DIR}/test_configmanager.cpp
    ${TESTS_DIR}/test_queueapi_json.cpp
    ${SRC_DIR}/configmanager.cpp
    ${SRC_DIR}/queueapi.cpp
)

# Include order matters:
#   1. tests/  — mock Preferences, WiFi, HTTPClient (shadow real hardware headers)
#   2. sim/    — Arduino.h, SPI.h (and ArduinoJson.h which now redirects to real lib)
#   3. src/    — configmanager.h, queueapi.h, config.h
#   4. JSON    — real ArduinoJson (found via redirect in sim/ArduinoJson.h)
target_include_directories(queuewatch_tests PRIVATE
    ${TESTS_DIR}
    ${SIM_DIR}
    ${SRC_DIR}
    ${JSON_DIR}
    "${JSON_DIR}/src"
)

target_compile_definitions(queuewatch_tests PRIVATE
    _CRT_SECURE_NO_WARNINGS
    ARDUINOJSON_ENABLE_ARDUINO_STRING=1
)

if(MSVC)
    target_compile_options(queuewatch_tests PRIVATE /W3 /wd4100 /wd4267)
else()
    target_compile_options(queuewatch_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

add_custom_target(run_tests
    COMMAND queuewatch_tests
    DEPENDS queuewatch_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running unit tests"
)
