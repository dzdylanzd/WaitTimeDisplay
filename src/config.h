#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

// ----------- Storage -----------
// Shared NVS namespace for ConfigManager (parks/timings/display settings)
// AND WiFiManager (credentials) — a factory reset wipes both by clearing
// this one namespace, so the two must agree on the name.
constexpr const char* NVS_NAMESPACE = "queuewatch";

// ----------- Firmware version -----------
// Bumped manually per release; compared against GitHub Releases' tag_name
// (see OtaUpdater) to decide whether an update is available. Keep in sync
// with the git tag pushed for each release (e.g. "v1.0.0").
constexpr const char* FIRMWARE_VERSION = "1.1.1";

// ----------- TFT pins (Waveshare ESP32-C6-LCD-1.47) -----------
// These match lcd_st7789.h but are kept here for modules that need them.
#define TFT_CS    14
#define TFT_DC    15
#define TFT_RST   21
#define TFT_MOSI   6
#define TFT_SCLK   7
#define TFT_LIGHT 22

// ----------- Other onboard peripherals -----------
#define RGB_LED_PIN   8   // WS2812 (Waveshare ESP32-C6-LCD-1.47 onboard LED)
#define BOOT_BTN_PIN  9   // BOOT button, active-low. Strapping pin — safe to
                          // read with INPUT_PULLUP at runtime (matches its
                          // boot-time default state).

// ----------- Network -----------
constexpr uint8_t  WIFI_RETRY_MAX      = 20;
constexpr uint16_t WIFI_RETRY_DELAY_MS = 300;
constexpr uint16_t HTTP_TIMEOUT_MS     = 5000;
constexpr uint8_t  HTTP_RETRY_MAX      = 2;
#ifdef SIMULATION
// Sim: poll fast so the keyboard-driven drop/reconnect (D / C keys) responds
// within a few seconds instead of the device's 30 s cadence.
constexpr unsigned long WIFI_RECONNECT_INTERVAL = 3000UL;
#else
constexpr unsigned long WIFI_RECONNECT_INTERVAL = 30000UL;
#endif
constexpr unsigned long DATA_RETRY_INTERVAL     = 30000UL;

// ----------- Hardware limits -----------
constexpr int MAX_RIDES = 50;

// ----------- Startup -----------
constexpr unsigned long STARTUP_SPLASH_DURATION = 10000UL;
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 30000UL;

// ----------- HTTP response limit -----------
constexpr size_t HTTP_MAX_RESPONSE_SIZE = 65536;

#endif // CONFIG_H
