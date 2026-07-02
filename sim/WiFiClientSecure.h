#pragma once
class WiFiClientSecure {
public:
    void setInsecure() {}
    bool connect(const char*, uint16_t) { return false; }
};
