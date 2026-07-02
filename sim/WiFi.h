#pragma once
#include "Arduino.h"

#define WL_CONNECTED      3
#define WL_DISCONNECTED   6
#define WL_IDLE_STATUS    0
#define WL_CONNECT_FAILED 4
#define WL_NO_SSID_AVAIL  1
#define WIFI_STA   1
#define WIFI_AP    2
#define WIFI_AP_STA 3

// IPAddress supports a custom display string so localIP() can show "localhost:8080"
class IPAddress {
    std::string _str;
public:
    IPAddress()                                   : _str("0.0.0.0") {}
    IPAddress(uint8_t a,uint8_t b,uint8_t c,uint8_t d) {
        char b2[16]; snprintf(b2,16,"%d.%d.%d.%d",a,b,c,d); _str=b2;
    }
    IPAddress(const char* s) : _str(s) {}
    String toString() const { return String(_str.c_str()); }
    operator String() const { return toString(); }
};

// WiFiClass simulates a 1.5-second connection delay then stays connected.
// The "router" can be switched off/on from the keyboard (D / C in main_sim):
// while it's off, status() reports WL_DISCONNECTED no matter how often the
// state machine retries — same as a real network that vanished.
class WiFiClass {
    bool     _begun        = false;
    bool     _networkUp    = true;
    uint32_t _beginTime    = 0;
public:
    int status() {
        if (!_begun)      return WL_IDLE_STATUS;
        // A vanished router reports "SSID not available" on the ESP32 — this
        // is the status that counts as a terminal failure in RECONNECTING,
        // so the sim reaches the WiFi portal after repeated failures too.
        if (!_networkUp)  return WL_NO_SSID_AVAIL;
        return (millis() - _beginTime >= 1500) ? WL_CONNECTED : WL_IDLE_STATUS;
    }

    // Sim-only controls (not part of the Arduino API)
    void simSetNetwork(bool up) { _networkUp = up; }
    bool simNetworkUp() const   { return _networkUp; }
    bool begin(const char* /*ssid*/, const char* /*pass*/ = nullptr) {
        if (!_begun) { _begun = true; _beginTime = millis(); }
        return true;
    }
    bool disconnect()    { _begun = false; return true; }
    bool reconnect()     { _beginTime = millis(); return true; }
    void mode(int)       {}
    void setAutoReconnect(bool) {}
    bool softAP(const char*, const char* = nullptr) { return true; }
    IPAddress softAPIP() { return IPAddress(192,168,4,1); }
    // Show the config URL as the "IP" so the startup screen is actually useful
    IPAddress localIP()  { return IPAddress("localhost:8080"); }
    String SSID()        { return String("SimNetwork"); }
};
extern WiFiClass WiFi;
