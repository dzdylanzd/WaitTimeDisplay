#pragma once
// No-op mDNS stub for the desktop simulator; the sim never advertises
// queuewatch.local since there is no real network interface to bind to.
class MDNSClass {
public:
    bool begin(const char*) { return true; }
    void addService(const char*, const char*, uint16_t) {}
};
inline MDNSClass MDNS;
