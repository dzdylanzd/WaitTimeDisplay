#pragma once
#include "WiFi.h"  // for IPAddress
class DNSServer {
public:
    void start(uint8_t, const char*, const IPAddress&) {}
    void processNextRequest() {}
    void stop() {}
};
