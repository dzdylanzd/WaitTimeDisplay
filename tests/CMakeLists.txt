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
    ${TESTS_DIR}/test_quiethours.cpp
    ${TESTS_DIR}/test_trendstore.cpp
    ${TESTS_DIR}/test_ridefilter.cpp
    ${TESTS_DIR}/test_button.cpp
    ${TESTS_DIR}/test_waitlevel.cpp
    ${SRC_DIR}/configmanager.cpp
    ${SRC_DIR}/queueapi.cpp
    ${SRC_DIR}/trendstore.cpp
    ${SRC_DIR}/ridefilter.cpp
    ${SRC_DIR}/button.cpp
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

# ── Live integration tests (real HTTPS to queue-times.com) ──────────────
# Separate executable: uses the sim's WinHTTP HTTPClient instead of the
# mock, so it exercises the parser against every real park. Needs internet.
add_executable(queuewatch_live_tests
    ${TESTS_DIR}/test_live_api.cpp
    ${SRC_DIR}/queueapi.cpp
)

# tests/live/ first so its HTTPClient.h (→ sim WinHTTP) and WiFi.h shadow
# the offline mocks in tests/. doctest.h still comes from tests/.
target_include_directories(queuewatch_live_tests PRIVATE
    ${TESTS_DIR}/live
    ${TESTS_DIR}
    ${SIM_DIR}
    ${SRC_DIR}
    ${JSON_DIR}
    "${JSON_DIR}/src"
)

target_compile_definitions(queuewatch_live_tests PRIVATE
    _CRT_SECURE_NO_WARNINGS
    ARDUINOJSON_ENABLE_ARDUINO_STRING=1
)

target_link_libraries(queuewatch_live_tests PRIVATE winhttp)

if(MSVC)
    target_compile_options(queuewatch_live_tests PRIVATE /W3 /wd4100 /wd4267)
endif()

add_custom_target(run_tests
    COMMAND queuewatch_tests
    DEPENDS queuewatch_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running unit tests"
)
