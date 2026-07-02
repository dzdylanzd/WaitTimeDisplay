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
}

// ----------- loop() -----------
void loop() {
    lv_timer_handler();          // let LVGL render dirty regions
    configWebServer.handleClient();
    appState.onButtonEvent(bootBtn.poll(millis()));
    appState.update();
}
