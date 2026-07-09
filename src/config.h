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
constexpr const char* FIRMWARE_VERSION = "1.6.7";

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
// 10 s: connect + headers (TLS handshakes to the API's CDN can be slow).
constexpr uint16_t HTTP_TIMEOUT_MS     = 10000;
// Mid-body stall limit. Everything — web server included — shares the loop
// task, so a stalled fetch freezes the config page for exactly this long;
// retries on a fresh connection recover better than waiting longer would
// (the CDN stalls that motivated this are typically permanent for that
// connection). Keep it well under HTTP_TIMEOUT_MS.
constexpr uint16_t HTTP_STALL_TIMEOUT_MS = 4000;
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
// 80: with SHOW entities included (themeparks.wiki), big parks exceed the
// old 50-attraction cap and the web ride picker would silently truncate.
constexpr int MAX_RIDES = 80;

// ----------- Startup -----------
// Startup splash duration ("Connected!" + IP) is now user-configurable —
// see RuntimeConfig::startupSplashDuration / DEFAULT_STARTUP_SPLASH_DURATION
// in configmanager.h.
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 30000UL;

// ----------- Loop task watchdog -----------
// Everything (LVGL, web server, button, fetches) runs on the Arduino loop
// task; a hard hang in any blocking network call would otherwise freeze the
// device forever — screen stuck, BOOT button dead, config page offline —
// until someone pulls the plug. Generous timeout: the longest legitimate
// uninterrupted block is a pathological TLS handshake (~tens of seconds);
// the intentionally long phases (captive portal, OTA download) feed the
// watchdog inside their loops.
constexpr uint32_t LOOP_WDT_TIMEOUT_MS = 90000;

// ----------- HTTP response limit -----------
// Sanity guard against pathological bodies only: the streaming parse in
// httpGetJson never buffers the body, so memory is bounded by the JSON doc
// capacity, not this. Big parks' /live responses reach ~66 KB raw.
constexpr size_t HTTP_MAX_RESPONSE_SIZE = 262144;

#endif // CONFIG_H
