// Redirect to real ArduinoJson — root header pulls in src/ via relative path.
// Both the ArduinoJson root AND src/ dirs are in the CMake include path,
// so internal #includes like "ArduinoJson/Array/Foo.hpp" resolve correctly.
#pragma once
#include "../.pio/libdeps/esp32-c6-devkitc-1/ArduinoJson/src/ArduinoJson.h"
