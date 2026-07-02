# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**QueueWatch** — an ESP32-C6 Arduino firmware that displays theme-park ride wait times on a small TFT LCD. It fetches live data from [queue-times.com](https://queue-times.com), lets users configure parks and rides via a captive-portal + web UI, and cycles through configured parks/rides on-device.

Target hardware: **Waveshare ESP32-C6-LCD-1.47** (172×320 ST7789, operated in landscape = 320×172).

---

## Quick Start (Desktop)

All scripts are at the **repo root** — double-click or run from any terminal:

| Script | What it does |
|---|---|
| `build_sim.bat` | Build the desktop simulator (first run configures CMake) |
| `run_sim.bat` | Launch the simulator window |
| `test.bat` | Build and run all 22 unit tests |
| `clean_all.bat` | Delete `sim/build/` and `tests/build/` |

> Simulator config UI: **http://localhost:8080**  
> Press **ESC** to quit the simulator window.

---

## Building & Flashing (Firmware)

This is a **PlatformIO** project — firmware sources live in `src/`.

```bash
# Build
pio run

# Flash
pio run --target upload

# Serial monitor
pio device monitor
```

Required libraries are declared in `platformio.ini` and fetched automatically by PlatformIO:
- `lvgl/lvgl @ ^8.3.11` — GUI framework
- `bblanchon/ArduinoJson @ ^6.21.5` — JSON parsing

The board target is `esp32-c6-devkitc-1` at 160 MHz. If upload fails, try lowering `upload_speed` in `platformio.ini`.

---

## Architecture

### Entry point: `src/main.cpp`

Creates six global singletons, wires them together, and calls `lv_timer_handler()` every loop iteration (required by LVGL).

### Module responsibilities

| File | Responsibility |
|---|---|
| `src/appstate.cpp/h` | **Central state machine** — owns all runtime state, timing, and park/ride data. The only place that calls into other modules. |
| `src/display.cpp/h` | **LVGL-based renderer** — owns two persistent LVGL screens (`_scrMain`, `_scrStatus`). Stateless from AppStateManager's perspective. |
| `src/lcd_st7789.cpp/h` | Raw SPI driver for the ST7789 panel. Landscape mode (MADCTL=0x60, 320×172). Provides `LCD_Init`, `LCD_SetWindow`, `LCD_WritePixels`. |
| `src/lvgl_driver.cpp/h` | Registers the ST7789 as LVGL's display driver. Drives `lv_tick_inc()` via `esp_timer`. |
| `src/configmanager.cpp/h` | Persists `RuntimeConfig` (enabled parks, ride filters, timing values) to NVS via `Preferences`. |
| `src/queueapi.cpp/h` | HTTP GET → JSON parse from queue-times.com. Caches park timezones (up to 20 entries). |
| `src/wifimgr.cpp/h` | WiFi credential storage (NVS) and captive-portal provisioning. |
| `src/cfgserver.cpp/h` | `WebServer`-based config UI on port 80: park/ride picker, timing sliders, factory reset (`POST /api/factory-reset` → NVS wipe + `ESP.restart()`). |
| `src/tzhelper.cpp/h` | IANA → POSIX timezone mapping, NTP sync via `configTzTime()`. |
| `src/config.h` | Hardware pin definitions and compile-time constants. |
| `lv_conf.h` | LVGL 8.3 configuration (project root, picked up via `-DLV_CONF_INCLUDE_SIMPLE`). |

### State machine (`SystemState` enum in `appstate.h`)

```
BOOT
 └─ no creds ──> WIFI_CONFIG_PORTAL (captive portal)
 └─ has creds -> WIFI_CONNECTING
                  └─ connected ──> STARTUP_INFO (10 s splash)
                                    └─ parks set ──> WAIT_TIME_CYCLE ◄──────────┐
                                    └─ no parks ──> NO_PARKS_CONFIGURED          │
WAIT_TIME_CYCLE                                                                   │
 └─ WiFi lost ──> RECONNECTING ──> (3 failures) ──> WIFI_CONFIG_PORTAL           │
                   └─ reconnected ─────────────────────────────────────────────── ┘
```

**WiFi credentials survive connect failures.** Entering the portal does NOT
clear the stored SSID/password — the device may simply be away from its home
network, and a power cycle back in range must reconnect without reconfiguring.
`WiFiManager::runCaptivePortal()` therefore blocks until a *new* save
(`_portalSaved`), not until `isConfigured()`. Credentials are only overwritten
by a portal save or wiped by a factory reset.

`AppStateManager::update()` is called every `loop()`. Config-web-server saves are detected there before the state switch and trigger `restartCycle()`.

### LVGL display design

`DisplayController` owns three pre-built LVGL screens created at `begin()`
("Magic Night" palette: deep indigo/purple backgrounds, warm gold accents):

- **`_scrMain`** — normal ride display:
  - Indigo header panel: park name in gold (Montserrat 16, circular scroll) + WiFi glyph and local time in periwinkle (right-aligned)
  - 1 px gold separator that dims as data ages (see `setDataFreshness`)
  - 3 px progress bar filled in the wait theme's accent colour (animated on ride change)
  - Ride panel: 4 px accent stripe + ride name (Montserrat 20, circular scroll) + "3/12" index
  - Wait panel: theme-tinted background, 2 px accent top border, big wait number (Montserrat 48) and a letter-spaced caps sub-label ("MINUTE WAIT", "CLOSED" states)
  - Wait themes (`pickTheme`): green ≤ 15 min, amber ≤ 30, orange ≤ 45, red above, teal when closed

- **`_scrStatus`** — generic info/error/splash:
  - Title with LVGL symbol glyph (Montserrat 20), gold underline
  - Sub-title and body text (Montserrat 14)
  - Bottom gold-trimmed panel for IP address / URL (Montserrat 16)
  - Error/info variants are selected via the `NoDataReason` enum in `display.h` — no string matching

- **`_scrPortal`** — WiFi captive-portal setup:
  - Instructions on the left, scannable `WIFI:` QR code on a white quiet-zone panel on the right
  - SSID/password fallback in a bottom bar; loaded with `lv_scr_load()` + `lv_refr_now()` because the portal loop blocks LVGL's timer

Normal screen switches use `lv_scr_load_anim()` with a 220 ms fade. LVGL handles all dirty-rect tracking automatically.

### LCD driver notes

- **Landscape mode**: MADCTL byte `0x60` (MX=1, MV=1). X goes 0–319, Y goes 0–171.
- **Y offset**: `LCD_SetWindow` adds a fixed offset of 34 to all Y addresses (ST7789 controller has 240 rows, panel is 172 px).
- **Colour byte order**: `LV_COLOR_16_SWAP 1` in `lv_conf.h` — LVGL swaps bytes before SPI DMA flush.
- **LVGL tick**: driven by `esp_timer` calling `lv_tick_inc(5)` every 5 ms from `Lvgl_Init()`. In the sim, called manually in the SDL loop.

### Key timing constants

| Constant | Default | Location |
|---|---|---|
| API refresh | 15 min | `configmanager.h` `DEFAULT_API_REFRESH_INTERVAL` |
| Ride rotation | 10 s | `configmanager.h` `DEFAULT_ROTATE_INTERVAL` |
| Closed-park display | 20 s | `configmanager.h` `DEFAULT_CLOSED_PARK_DISPLAY_TIME` |
| WiFi reconnect interval | 30 s | `config.h` `WIFI_RECONNECT_INTERVAL` |

### NVS namespaces
- `"queuewatch"` — `ConfigManager` (parks, ride filters, timings) and `WiFiManager` (credentials). `ConfigManager::factoryReset()` clears the whole namespace, so it wipes the WiFi credentials too — by design.

### Data flow
1. `QueueApi::fetchRideData()` populates a fixed `RideInfo[MAX_RIDES]` array (no heap allocation for ride data).
2. `AppStateManager::applyRideFilter()` in-place compacts the array to only enabled rides.
3. `AppStateManager::refreshRideData()` detects whether only wait-times changed vs. rides added/removed/renamed and repaints only the wait panel or the whole screen accordingly.

---

## Desktop Simulator (`sim/`)

The simulator compiles the **real** `src/` files unchanged. Stubs in `sim/` shadow the Arduino/ESP32 headers so the same code runs natively on Windows with no duplication.

### How it works

`sim/main_sim.cpp` mirrors `src/main.cpp` exactly — same singletons, same init order, same loop structure. The only difference is SDL replaces the hardware display and `lv_tick_inc(5)` is called manually each iteration (the device uses `esp_timer` for this).

### Simulated state flow

1. **WIFI_CONNECTING** — animated dots for ~1.5 s (`WiFiClass.status()` returns `WL_IDLE_STATUS` for 1.5 s, then `WL_CONNECTED`)
2. **STARTUP_INFO** — 10 s splash; `WiFi.localIP().toString()` returns `"localhost:8080"` so the display shows the correct URL
3. **NO_PARKS_CONFIGURED** — waits on startup screen; open http://localhost:8080 to add parks
4. **WAIT_TIME_CYCLE** — real HTTPS calls to queue-times.com, real JSON parsing, real ride cycling

When parks are saved via the browser, `cfgserver` sets `isConfigUpdated()` and the real `AppStateManager` picks it up on the next `update()` call — identical to device behaviour.

### Stub files in `sim/`

| Stub | Replaces | Notes |
|---|---|---|
| `Arduino.h` | ESP32 Arduino core | `String`, `millis()`, `delay()`, `Serial` |
| `WiFi.h` | `<WiFi.h>` | 1.5 s connection delay; `localIP()` returns `"localhost:8080"` |
| `wifi_sim.cpp` | — | `WiFiClass WiFi` singleton definition |
| `wifimgr_sim.cpp` | `src/wifimgr.cpp` | Credentials always "configured"; `connect()` calls `WiFi.begin()` |
| `Preferences.h` | `<Preferences.h>` | File-backed (`sim_prefs_<ns>.txt`) for persistent config between sim runs |
| `HTTPClient.h` | `<HTTPClient.h>` | WinHTTP — real HTTPS, thread-safe (required because QueueApi runs from httplib worker threads) |
| `WebServer.h` | `<WebServer.h>` | cpp-httplib on port 8080 in a background thread (port 80 requires admin on Windows) |
| `DNSServer.h` | `<DNSServer.h>` | No-op |
| `WiFiClientSecure.h` | `<WiFiClientSecure.h>` | No-op |
| `ArduinoJson.h` | — | Redirects to the real PlatformIO library |
| `lvgl_driver_sim.cpp` | `src/lvgl_driver.cpp` | SDL2 display + input driver for LVGL |
| `lcd_st7789_sim.cpp` | `src/lcd_st7789.cpp` | No-op (LVGL talks to SDL directly) |
| `tzhelper_sim.cpp` | `src/tzhelper.cpp` | No-op timezone stubs |

### Build prerequisites for the simulator
- Visual Studio 2019 (MSVC compiler + bundled CMake)
- SDL2 VC development package extracted to `C:\SDL2`
- Run `pio run` once in the project root to download LVGL and ArduinoJson into `.pio/libdeps/`

### Sub-scripts in `sim/`

| Script | Action |
|---|---|
| `sim/build.bat` | Configure (first run) + incremental build |
| `sim/run.bat` | Launch exe |
| `sim/rebuild.bat` | Clean + full rebuild |
| `sim/clean.bat` | Delete `sim/build/` |

---

## Unit Tests (`tests/`)

Tests use **doctest** (header-only, `tests/doctest.h`). No hardware, no LVGL, no SDL. HTTP responses are preset via `tests/HTTPClient.h` (MockHTTP map).

### What is tested (23 test cases, 78 assertions)

**`test_configmanager.cpp`** — ConfigManager  
- `parseEnabledParks`: valid JSON / empty / malformed / invalid IDs  
- `isRideEnabled`: no filter / park missing / ride in filter / ride not in filter / cache invalidation  
- `saveEnabledParks` round-trip  
- `hasEnabledParks`  
- `saveTimings`  
- `factoryReset`: everything back to defaults, survives reload  

**`test_queueapi_json.cpp`** — QueueApi  
- `fetchRideData`: valid parse / HTTP error / parkId ≤ 0 / MAX_RIDES limit / malformed JSON  
- `fetchAvailableParks`: valid / HTTP error  
- `getParkTimezone`: correct TZ / unknown park / cache hit  

### Sub-scripts in `tests/`

| Script | Action |
|---|---|
| `tests/build_tests.bat` | Configure (first run) + incremental build |
| `tests/run_tests.bat` | Run with console reporter |
| `tests/clean_tests.bat` | Delete `tests/build/` |
