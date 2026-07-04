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
| `test.bat` | Build and run all unit tests |
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

### OTA releases

`release.ps1` (repo root) bumps `FIRMWARE_VERSION` in `src/config.h`, builds,
commits + tags + pushes, and publishes a GitHub Release with `firmware.bin`
attached, so devices running `OtaUpdater`'s "Check for update" can find it:

```powershell
.\release.ps1 1.3.0
```

Needs a GitHub token (fine-grained, "Contents: Read and write" on this repo)
in `$env:GITHUB_TOKEN`, or it prompts for one. The repo must stay **public**
for this to work — the device's OTA check is deliberately unauthenticated
(no token embedded in firmware), so a private repo's releases are invisible
to it.

Required libraries are declared in `platformio.ini` and fetched automatically by PlatformIO:
- `lvgl/lvgl @ ^8.3.11` — GUI framework
- `bblanchon/ArduinoJson @ ^6.21.5` — JSON parsing

The board target is `esp32-c6-devkitc-1` at 160 MHz. If upload fails, try lowering `upload_speed` in `platformio.ini`.

---

## Architecture

### Entry point: `src/main.cpp`

Creates eight global singletons (incl. `StatusLed` and `Button`), wires them together, and calls `lv_timer_handler()` every loop iteration (required by LVGL). The loop also polls the BOOT button and forwards events to `AppStateManager::onButtonEvent()`.

### Module responsibilities

| File | Responsibility |
|---|---|
| `src/appstate.cpp/h` | **Central state machine** — owns all runtime state, timing, and park/ride data. The only place that calls into other modules. |
| `src/display.cpp/h` | **LVGL-based renderer** — owns two persistent LVGL screens (`_scrMain`, `_scrStatus`). Stateless from AppStateManager's perspective. |
| `src/lcd_st7789.cpp/h` | Raw SPI driver for the ST7789 panel. Landscape mode (MADCTL=0x60, 320×172). Provides `LCD_Init`, `LCD_SetWindow`, `LCD_WritePixels`. |
| `src/lvgl_driver.cpp/h` | Registers the ST7789 as LVGL's display driver. Drives `lv_tick_inc()` via `esp_timer`. |
| `src/configmanager.cpp/h` | Persists `RuntimeConfig` (enabled parks, ride filters, timing values) to NVS via `Preferences`. |
| `src/queueapi.cpp/h` | HTTP GET → JSON parse from queue-times.com. Caches park timezone + country (up to 20 entries, shared cache). |
| `src/wifimgr.cpp/h` | WiFi credential storage (NVS) and captive-portal provisioning. |
| `src/cfgserver.cpp/h` | `WebServer`-based config UI on port 80: park/ride picker, timing sliders, factory reset (`POST /api/factory-reset` → NVS wipe + `ESP.restart()`). |
| `src/tzhelper.cpp/h` | IANA → POSIX timezone mapping, NTP sync via `configTzTime()`, `getLocalMinutesOfDay()` for quiet hours. |
| `src/trendstore.cpp/h` | RAM-only wait-time history keyed by ride id (250 entries) — powers the ↑/↓ trend arrow. 5-min threshold; closed observations neither update nor report. |
| `src/ridefilter.cpp/h` | Pure `applyDisplayOptions()`: skip-closed / min-wait filtering (reverts if it would empty the list) + favorites-first / wait-desc stable sort. |
| `src/statusled.cpp/h` | Onboard WS2812 (GPIO8, `neopixelWrite`) mirrors the current ride's wait-level colour; brightness tracks the backlight. Can be disabled entirely via the web UI (`ledEnabled`). |
| `src/button.cpp/h` | BOOT button (GPIO9, active-low): debounced short press = next ride, 700 ms long press = next park, 10 s hold = factory-reset warning screen, 20 s hold = factory reset + restart (release during the warning cancels). `update()` is a pure, tested state machine. |
| `src/waitlevel.h` | Shared `pickWaitLevel()` enum — single source of the wait-level bucketing for display themes AND the LED (thresholds default 15/30/45, user-configurable). |
| `src/quiethours.h` | Pure `inQuietWindow()` (handles overnight wrap; start==end = never quiet). |
| `src/config.h` | Hardware pin definitions (incl. `RGB_LED_PIN` 8, `BOOT_BTN_PIN` 9) and compile-time constants. |
| `lv_conf.h` | LVGL 8.3 configuration (project root, picked up via `-DLV_CONF_INCLUDE_SIMPLE`). |

### State machine (`SystemState` enum in `appstate.h`)

```
BOOT
 └─ no creds ──> WIFI_CONFIG_PORTAL (captive portal — first boot / after factory reset only)
 └─ has creds -> WIFI_CONNECTING
                  └─ connected ──> STARTUP_INFO (10 s splash)
                                    └─ parks set ──> WAIT_TIME_CYCLE ◄──────────┐
                                    └─ no parks ──> NO_PARKS_CONFIGURED          │
WAIT_TIME_CYCLE                                                                   │
 └─ WiFi lost ──> RECONNECTING ── reconnected ─────────────────────────────────── ┘
```

**Connect failures never fall back into the portal.** On boot timeout
(`WIFI_CONNECT_TIMEOUT_MS`) or 3 consecutive reconnect failures, the device
shows `NoDataReason::WIFI_TROUBLE` ("WiFi Not Found — still trying to
connect...", with the BOOT-20 s recovery hint) and keeps retrying with a
fresh attempt every `WIFI_RECONNECT_INTERVAL` (`_wifiTrouble` flag in
`AppStateManager`). The BOOT button's 20 s factory reset is the intended way
to reconfigure WiFi.

**WiFi credentials survive connect failures.** The portal only overwrites
them on a *new* save (`WiFiManager::runCaptivePortal()` blocks on
`_portalSaved`, not `isConfigured()`); a factory reset wipes them.

`AppStateManager::update()` is called every `loop()`. Config-web-server saves are detected there before the state switch and trigger `restartCycle()`.

### LVGL display design

`DisplayController` owns three pre-built LVGL screens created at `begin()`.
The UI "chrome" colours come from the user-selectable palette (`PALETTES[]`
in display.cpp, default 0 = "Magic Night": deep indigo/purple, warm gold).
`applyPalette()` restyles all three screens live (no reboot); every colour
is read through the `PAL` pointer via the `C_*` macros. Keep `PALETTES[]`,
`COLOR_PALETTE_COUNT` (configmanager.h) and the `PALETTE_DEFS` swatches
(cfgserver.cpp JS) in sync.

Wait themes are NOT palette-dependent: the five level colours (and the
15/30/45 thresholds) are user-configurable via the web UI's "Wait colours"
section (`RuntimeConfig::waitTh1..3` / `waitColors[5]`, indexed by
`(int)WaitLevel`). `AppStateManager::applyWaitConfig()` pushes them to
`DisplayController::setWaitConfig()` (mutable `WAIT_THEMES[]`; the `T_*`
names are macros into it; panel backgrounds are derived from each accent at
~7 %/12 % brightness) and `StatusLed::setColors()` — screen and LED always
agree.

- **`_scrMain`** — normal ride display:
  - Header panel: park name in gold (Montserrat 16, circular scroll); right side stacks the park's country (Montserrat 12, muted — from `QueueApi::getParkCountry`) above the local time in periwinkle. No WiFi glyph — the screen only shows while connected.
  - 1 px gold separator that dims as data ages (see `setDataFreshness`)
  - 3 px progress bar filled in the wait theme's accent colour (animated on ride change)
  - Ride panel (48 px, two rows): 4 px accent stripe + ride name (Montserrat 20, circular scroll) + "3/12" index (gold `* 3/12` when the ride is a favorite); row 2 shows the themed-land name (Montserrat 12, muted — empty for parks without lands)
  - Wait panel: theme-tinted background, 2 px accent top border, big wait number (Montserrat 48), trend arrow + delta top-right (`LV_SYMBOL_UP` red rising / `LV_SYMBOL_DOWN` green falling, hidden when flat/closed) and a letter-spaced caps sub-label ("MINUTE WAIT", "CLOSED" states)
  - Wait themes (`pickTheme`, mapped from `pickWaitLevel` in waitlevel.h): defaults green ≤ 15 min, amber ≤ 30, orange ≤ 45, red above, teal when closed — thresholds and colours user-configurable (see above)

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

- **Landscape mode**: MADCTL byte `0x60` (MX=1, MV=1, RGB order — the BGR bit `0x08` swaps red/blue on this panel; these ST7789 modules ship in both orders). X goes 0–319, Y goes 0–171. `LCD_SetRotation(true)` switches to `0xA0` (MY|MV) for the user's 180° flip option — the 34-row offset is symmetric so windowing is unchanged; `AppStateManager::applyScreenFlip()` applies it at boot and on config save (with a full `lv_obj_invalidate`).
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
- `"queuewatch"` — `ConfigManager` (parks, ride filters/favorites, timings, display + ride-display options) and `WiFiManager` (credentials). `ConfigManager::factoryReset()` clears the whole namespace, so it wipes the WiFi credentials too — by design.
- Keys: `api_int`/`rot_int`/`closed_int`/`time_int` (timings), `enabled_pks`, `ride_flt`, `ride_fav` (per-park JSON, each capped at 1900 chars — `cfgserver` REJECTS oversized saves rather than truncating), `brt`/`qt_en`/`qt_sta`/`qt_end`/`qt_brt`/`led_en`/`flip_scr`/`dev_tz`/`pal` (brightness + quiet hours + quiet-hours timezone + status-LED on/off + 180° screen flip + UI colour palette), `wt1`/`wt2`/`wt3`/`wc0`..`wc4` (wait-level thresholds + 0xRRGGBB level colours), `sort_mode`/`fav_first`/`skip_closed`/`min_wait` (ride display options), `ota_pending` (bool — set just before an OTA reboot, cleared once `AppStateManager` sees enough post-update successful fetches; a factory reset correctly wipes a stale flag along with everything else). New scalars use `putInt`/`putBool` only — the sim/tests Preferences stubs don't implement `putUChar`/`putUShort`.

### Data flow
1. `QueueApi::fetchRideData()` populates a fixed `RideInfo[MAX_RIDES]` array (no heap allocation for ride data). Each ride carries its `land` name (empty for lands-less parks like Tokyo).
2. `AppStateManager::applyRideFilter()` in-place compacts the array to only enabled rides.
3. `AppStateManager::annotateRides()` stamps trend (via `TrendStore`) and favorite flags, then `applyRideDisplayOptions()` filters/sorts per the user's global options.
4. `AppStateManager::refreshRideData()` detects whether only wait-times changed vs. rides added/removed/renamed and repaints only the wait panel or the whole screen accordingly. (Wait-desc sorting can reorder on refresh — detected as a structure change → full repaint.)

### Brightness / quiet hours / LED / button
- `AppStateManager::applyBrightness()` runs every ~10 s in ALL states: quiet-hours window (`inQuietWindow`) → `quietBrightness`, else `brightness`; calls `LCD_SetBacklight()` only on change. Fails bright before NTP sync.
- Quiet hours use the user's `deviceTimezone` (NVS `dev_tz`, IANA name picked from a dropdown in the web UI — the JS `TZ_LIST` must stay in sync with `TZ_TABLE` in tzhelper.cpp) via `getMinutesOfDayInTz()`, which swaps the TZ env var around a `localtime_r` call without disturbing the displayed park clock. Empty `deviceTimezone` = follow the displayed park (legacy behaviour).
- The WS2812 mirrors `pickWaitLevel()` of the currently shown ride (teal when the closed-park screen shows); off outside `WAIT_TIME_CYCLE` and when quiet-hours brightness is 0.
- BOOT button Short/Long only act in `WAIT_TIME_CYCLE`: Short → `advanceRide()`, Long → `advanceToNextPark()`. Sim: N / P keys.
- **Factory reset by button**: holding BOOT 10 s fires `HoldWarning` → warning screen ("Are you sure...?"), red LED, backlight forced ≥30%, and `AppStateManager::update()` freezes (`_resetWarningActive`). Releasing fires `HoldCancel` → screen restored per state. Holding to 20 s fires `HoldReset` → `showFactoryResetting()` + `ConfigManager::factoryReset()` + `ESP.restart()`. Works in any state except the blocking captive portal (which never polls the button). Sim: W = warning, X = cancel.

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
| `Arduino.h` | ESP32 Arduino core | `String`, `millis()`, `delay()`, `Serial`, GPIO no-ops (`pinMode`, `digitalRead`→HIGH, `neopixelWrite`). NOTE: deliberately does NOT define `INPUT`/`OUTPUT` macros — `INPUT` is a winuser.h struct type. |
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

### What is tested (84 test cases, 327 assertions)

**`test_configmanager.cpp`** — ConfigManager  
- `parseEnabledParks`: valid JSON / empty / malformed / non-array / invalid IDs  
- `isRideEnabled`: no filter / park missing / in filter / not in filter / malformed JSON / cache invalidation  
- `saveEnabledParks` round-trip / `hasEnabledParks` / `saveTimings`  
- `saveDisplaySettings` (incl. `ledEnabled`), `saveRideOptions`, `savePalette`, `saveWaitConfig` round-trips  
- `isRideFavorite`: empty / listed / missing park / malformed JSON / cache invalidation  
- `load()`: every saved setting survives a Preferences reload  
- `factoryReset`: everything (incl. new settings) back to defaults, survives reload  

**`test_queueapi_json.cpp`** — QueueApi  
- `fetchRideData`: valid parse / land names / lands + top-level rides mixed / non-ASCII transliteration / missing-field defaults / HTTP error / parkId ≤ 0 / MAX_RIDES limit / malformed JSON  
- `fetchAvailableParks`: valid / HTTP error  
- `getParkTimezone`: correct TZ / unknown park / cache hit  
- `getParkCountry`: ASCII folding / missing field / shared cache with the timezone  

**`test_waitlevel.cpp`** — `pickWaitLevel`: default 15/30/45 boundaries, custom thresholds, closed precedence  

**`test_quiethours.cpp`** — `inQuietWindow`: same-day window, overnight wrap, empty window, one-minute windows, boundaries  

**`test_trendstore.cpp`** — TrendStore: first sighting, ± threshold boundaries, sub-threshold drift, unchanged wait, closed rides, park interleaving, capacity eviction  

**`test_ridefilter.cpp`** — `applyDisplayOptions`: skip-closed, min-wait (boundary inclusive), revert-if-empty, empty/single lists, closed favorite dropped, wait-desc sort (stable, closed last), favorites-first, combinations  

**`test_button.cpp`** — Button state machine: debounce (press and release bounces), short press, long press (fires once while held), 10 s HoldWarning, HoldCancel on release, 20 s HoldReset (release swallowed), skip-past-20 s polling, back-to-back presses  

### Sub-scripts in `tests/`

| Script | Action |
|---|---|
| `tests/build_tests.bat` | Configure (first run) + incremental build |
| `tests/run_tests.bat` | Run with console reporter |
| `tests/clean_tests.bat` | Delete `tests/build/` |
