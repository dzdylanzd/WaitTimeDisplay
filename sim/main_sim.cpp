// QueueWatch desktop simulator — mirrors src/main.cpp exactly.
// Stubs in sim/ replace Arduino/hardware APIs transparently.
// Config UI: http://localhost:8080   ESC to quit.

#include <SDL.h>
#include <lvgl.h>
#include <Arduino.h>
#include <WiFi.h>
#include "sim_events.h"

#include "../src/config.h"
#include "../src/wifimgr.h"
#include "../src/queueapi.h"
#include "../src/display.h"
#include "../src/tzhelper.h"
#include "../src/configmanager.h"
#include "../src/cfgserver.h"
#include "../src/statusled.h"
#include "../src/button.h"
#include "../src/appstate.h"

extern SDL_Window* sim_window;

// Same singletons as main.cpp — wired identically
static WiFiManager      wifiMgr;
static QueueApi         queueApi;
static ConfigManager    configMgr;
static ConfigWebServer  configWebServer(configMgr, queueApi);
static DisplayController display;
static StatusLed        statusLed;
static AppStateManager  appState(wifiMgr, queueApi, configMgr, display, configWebServer,
                                 statusLed);

int main(int /*argc*/, char** /*argv*/) {
    setvbuf(stdout, nullptr, _IONBF, 0);   // unbuffered — logs usable when redirected
    Serial.begin(115200);

    // Load config and start the web server BEFORE SDL init so the HTTP
    // endpoints are available immediately — SDL window creation can take ~1s.
    configMgr.load();
    configWebServer.begin();   // starts httplib on :8080

    display.begin();           // init LVGL + SDL window + build screens
    wifiMgr.loadCredentials();

    applyTimeZone("UTC");      // NTP with UTC until park TZ is known

    appState.begin();

    puts("QueueWatch Simulator  |  Config: http://localhost:8080");
    puts("Keys: C = connect WiFi (portal save / bring network back)  |  D = drop WiFi  |  ESC = quit");
    puts("      N = next ride (BOOT short press)  |  P = next park (BOOT long press)");
    puts("      W = factory-reset warning (BOOT held 10s)  |  X = cancel the warning");

    bool running = true;
    while (running) {
        running = sim_pump_events();

        // D switches the simulated router off: Offline screen, retries fail
        // until C switches it back on (never pressing C eventually reaches
        // the WiFi portal after repeated failures, like a dead router).
        int key = sim_take_key();
        if (key == 'd') {
            puts("[sim] WiFi dropped - press C to bring the network back");
            WiFi.simSetNetwork(false);
        } else if (key == 'c') {
            puts("[sim] Network restored - device will reconnect");
            WiFi.simSetNetwork(true);
        } else if (key == 'n') {
            appState.onButtonEvent(ButtonEvent::Short);       // BOOT short press
        } else if (key == 'p') {
            appState.onButtonEvent(ButtonEvent::Long);        // BOOT long press
        } else if (key == 'w') {
            appState.onButtonEvent(ButtonEvent::HoldWarning); // BOOT held 10 s
        } else if (key == 'x') {
            appState.onButtonEvent(ButtonEvent::HoldCancel);  // released again
        }

        lv_tick_inc(5);            // drive LVGL time (esp_timer does this on device)
        lv_timer_handler();
        configWebServer.handleClient();  // no-op — matches main.cpp loop structure
        appState.update();

        SDL_Delay(5);
    }

    configWebServer.stop();
    SDL_Quit();
    return 0;
}
