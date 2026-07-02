// Redirect to the sim's WiFiClientSecure so queueapi.cpp and the sim's
// HTTPClient.h see the same type (tests/WiFiClientSecure.h would clash).
#pragma once
#include "../../sim/WiFiClientSecure.h"
