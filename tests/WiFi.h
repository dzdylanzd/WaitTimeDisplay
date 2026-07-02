#pragma once
#define WL_CONNECTED 3
class WiFiClass { public: int status() { return WL_CONNECTED; } };
inline WiFiClass WiFi;
