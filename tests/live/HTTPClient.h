// Live tests use the simulator's WinHTTP client (real HTTPS),
// not the tests/ mock. This shadow header just redirects.
#pragma once
#include "../../sim/HTTPClient.h"
