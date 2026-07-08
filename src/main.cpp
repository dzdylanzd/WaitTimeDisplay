#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>

#include "config.h"
#include "wifimgr.h"
#include "queueapi.h"
#include "display.h"
#include "tzhelper.h"
#include "configmanager.h"
#include "cfgserver.h"
#include "statusled.h"
#include "button.h"
#include "appstate.h"

// ----------- Loop-task watchdog -----------
// The FEED_WDT() calls sprinkled through httpjson/otaupdater/wifimgr only
// work if the loop task is actually subscribed to the task watchdog — which
// the Arduino core does NOT do by itself. Subscribe it here so a wedged
// network call reboots the device instead of freezing it until a power
// cycle (config is all in NVS, so a restart is cheap).
#if __has_include(<esp_task_wdt.h>)
#include <esp_task_wdt.h>
#include <esp_idf_version.h>
static void enableLoopWatchdog() {
#if ESP_IDF_VERSION_MAJOR >= 5
    esp_task_wdt_config_t cfg = {};
    cfg.timeout_ms    = LOOP_WDT_TIMEOUT_MS;
    cfg.idle_core_mask = 0;          // don't watch the idle tasks
    cfg.trigger_panic  = true;       // panic handler reboots
    // The core initialises the TWDT already, so reconfigure is the normal
    // path; init is the fallback (avoids the "already initialized" error log).
    if (esp_task_wdt_reconfigure(&cfg) != ESP_OK) esp_task_wdt_init(&cfg);
#else
    esp_task_wdt_init(LOOP_WDT_TIMEOUT_MS / 1000, true);
#endif
    esp_task_wdt_add(NULL);          // subscribe the loop task
}
static inline void feedLoopWatchdog() { esp_task_wdt_reset(); }
#else   // simulator build
static void enableLoopWatchdog() {}
static inline void feedLoopWatchdog() {}
#endif

// The default 8 KB loopTask stack overflows during TLS handshakes (mbedtls
// runs its SHA-256 state on the caller's stack) with the fetch pipeline's
// frames on top — seen as "Stack protection fault" crash loops on device.
#if defined(ARDUINO_ARCH_ESP32)
SET_LOOP_TASK_STACK_SIZE(16 * 1024);
#endif

// ----------- Global singletons -----------
WiFiManager     wifiMgr;
QueueApi        queueApi;
ConfigManager   configMgr;
ConfigWebServer configWebServer(configMgr, queueApi);
DisplayController display;
StatusLed       statusLed;
Button          bootBtn;
AppStateManager appState(wifiMgr, queueApi, configMgr, display, configWebServer,
                         statusLed);

// ----------- setup() -----------
void setup() {
    Serial.begin(115200);
    delay(150);

    display.begin();        // init LCD + LVGL + build screens
    wifiMgr.loadCredentials();
    configMgr.load();
    statusLed.begin();
    bootBtn.begin(BOOT_BTN_PIN);

    applyTimeZone("UTC");   // NTP with UTC until park TZ is known

    appState.begin();
    configWebServer.begin();
    enableLoopWatchdog();
}

// ----------- loop() -----------
void loop() {
    feedLoopWatchdog();
    lv_timer_handler();          // let LVGL render dirty regions
    configWebServer.handleClient();
    appState.onButtonEvent(bootBtn.poll(millis()));
    appState.update();
}
