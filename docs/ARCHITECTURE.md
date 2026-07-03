# QueueWatch Architecture

This document describes how the QueueWatch firmware is put together — for
anyone who wants to understand, extend, or debug it. For build/flash
instructions see the top-level [README](../README.md); for simulator and
test specifics see [sim/README.md](../sim/README.md) and
[tests/README.md](../tests/README.md).

## Overview

QueueWatch is an ESP32-C6 Arduino (PlatformIO) firmware for the
**Waveshare ESP32-C6-LCD-1.47** board: a 172×320 ST7789 LCD driven in
landscape (320×172), an onboard WS2812 RGB LED (GPIO8), and the BOOT button
(GPIO9) as the only input. It fetches live theme-park wait times from
[queue-times.com](https://queue-times.com), and is configured through a web
UI served by the device itself.

The same `src/` sources are compiled three ways:

| Target | Entry point | What replaces the hardware |
|---|---|---|
| Firmware | `src/main.cpp` | nothing — real ESP32-C6 |
| Windows simulator | `sim/main_sim.cpp` | SDL2 window + stub Arduino/ESP32 headers |
| Unit tests | `tests/*.cpp` | mock `HTTPClient`/`Preferences`, no display |

## Modules

Everything hangs off a handful of global singletons created in `main.cpp`:

```
main.cpp
 ├─ WiFiManager      (wifimgr)       credentials in NVS + captive portal
 ├─ QueueApi         (queueapi)      HTTPS → JSON → RideInfo[]; park TZ/country cache
 ├─ ConfigManager    (configmanager) RuntimeConfig persisted to NVS
 ├─ ConfigWebServer  (cfgserver)     web UI + JSON API on port 80
 ├─ DisplayController(display)       LVGL screens (the only render code)
 ├─ StatusLed        (statusled)     WS2812 mirrors the wait colour
 ├─ Button           (button)        debounced BOOT-button state machine
 └─ AppStateManager  (appstate)      THE state machine; owns all runtime state
```

`AppStateManager` is the only module that calls into the others; every other
module is independent. Small pure helpers live in their own headers so the
tests can compile them without hardware: `ridefilter` (filter/sort),
`quiethours` (time-window math), `waitlevel` (wait → colour level),
`trendstore` (wait-time history for the trend arrows).

## System states

```
BOOT
 ├─ no WiFi credentials ──► WIFI_CONFIG_PORTAL   (captive portal + QR code)
 └─ has credentials ──────► WIFI_CONNECTING
                              │ connected
                              ▼
                            STARTUP_INFO (10 s splash with the config URL)
                              ├─ parks configured ──► WAIT_TIME_CYCLE
                              └─ none ─────────────► NO_PARKS_CONFIGURED
WAIT_TIME_CYCLE ◄────────────────────────────┐
 └─ WiFi lost ──► RECONNECTING ── reconnected ┘
```

**There is no automatic fallback into the WiFi portal.** If the device
cannot connect (boot timeout, or three consecutive reconnect failures) it
shows a *"WiFi Not Found — still trying to connect..."* screen that explains
how to recover (hold BOOT 20 s to erase WiFi and reconfigure) and simply
keeps retrying, starting a fresh attempt every 30 s. The stored credentials
are never discarded by a failure — power-cycling the device back in range of
its home network reconnects without any reconfiguration. The portal is only
entered on first boot (no credentials) or after a factory reset.

Config saves from the web UI are detected in `AppStateManager::update()`
(`ConfigWebServer::isConfigUpdated()`), which re-applies display settings
(brightness, flip, colour palette) and restarts the park cycle.

## Data flow

1. `QueueApi::fetchRideData()` fills a fixed `RideInfo[MAX_RIDES]` array
   (no heap for ride data). Each ride carries its themed-land name; all text
   is transliterated to ASCII because the LCD fonts only ship ASCII glyphs.
2. `applyRideFilter()` compacts the array to the rides enabled in the web UI.
3. `annotateRides()` stamps each ride with its trend (via `TrendStore`) and
   favorite flag.
4. `applyRideDisplayOptions()` applies the global options: skip closed,
   minimum wait, favorites first, sort by longest wait.
5. On refresh, `refreshRideData()` diffs against the previous list and
   repaints only the wait panel (values moved) or the whole screen
   (rides added/removed/renamed/reordered).

Park metadata (IANA timezone + country) comes from `parks.json` and is
cached per park. The timezone drives the header clock and quiet hours; the
country is shown in small text above the clock so it's clear whose local
time is displayed.

## Display

`DisplayController` owns three LVGL screens built once at startup:

- **Main screen** — header (park name, country + park-local clock), a 1 px
  separator that dims as data ages, a progress bar through the ride list,
  the ride panel (name, land, index, favorite star), and the wait panel
  (big number, trend arrow, sub-label).
- **Status screen** — one layout reused for every info/error message,
  selected by the `NoDataReason` enum (no parks, no rides, fetch failed,
  WiFi lost, WiFi trouble) plus splash/factory-reset variants.
- **Portal screen** — WiFi setup instructions with a scannable `WIFI:` QR
  code.

### Colour system

Two independent layers:

- **Wait levels** (user-configurable): a wait maps to one of five levels —
  short / medium / long / very long / closed. The thresholds (default
  15/30/45 min) and the colour of each level are set in the web UI's
  "Wait colours" section and stored in NVS. The level logic lives in one
  place — `pickWaitLevel()` in `waitlevel.h` — and the colours are pushed
  to both the display themes (`DisplayController::setWaitConfig()`, which
  derives the dark panel backgrounds from each accent colour) and the RGB
  LED (`StatusLed::setColors()`), so screen and LED can never disagree.
- **UI palettes** (user-selectable): everything else — header, separators,
  text, panel backgrounds — comes from one of the palettes in
  `PALETTES[]` (`display.cpp`): *Magic Night* (default), *Deep Ocean*,
  *Sunset Ember*, *Forest Twilight*, *Carbon Mono*, picked via visual
  swatch cards in the web UI. The choice is saved to NVS and applied live
  by `DisplayController::applyPalette()` — no reboot. To add a palette:
  append a row to `PALETTES[]`, bump `COLOR_PALETTE_COUNT`
  (`configmanager.h`), and add its swatch to `PALETTE_DEFS` in the web UI
  (`cfgserver.cpp`).

### LCD driver notes

- Landscape via MADCTL `0x68`; the 180° flip option uses `0xA8`. The
  ST7789 has 240 rows but the panel 172, so all Y addresses get a fixed
  +34 offset (symmetric, so flipping needs no window change).
- `LV_COLOR_16_SWAP 1`: LVGL swaps RGB565 bytes before the SPI DMA flush.
- LVGL ticks come from an `esp_timer` (`lv_tick_inc(5)` every 5 ms); the
  simulator calls it manually in its SDL loop.

## Configuration & persistence

All settings live in one NVS namespace, `"queuewatch"` (including the WiFi
credentials — so a factory reset wipes everything in one call). Highlights:

- Per-park ride filters and favorites are stored as JSON strings capped at
  1900 chars; oversized saves are **rejected**, never truncated (a truncated
  string would parse as "no filter").
- Timings (API refresh, ride rotation, closed-park display, clock update),
  brightness + quiet hours (with an optional device timezone independent of
  the displayed park), status LED on/off, screen flip, colour palette,
  wait-level thresholds + colours, and the ride display options.
- The web UI can export/import the whole configuration as JSON; unknown or
  missing fields keep their current values so old exports import cleanly.

## Web UI

`cfgserver.cpp` serves a single self-contained HTML page (no external
assets) with a sticky jump-nav to its sections: Timing, Parks, Rides,
Display, Wait colours, Options, Backup, Reset. The JSON API:

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/config` | GET | full current configuration |
| `/api/config` | POST | save configuration (restarts the display cycle) |
| `/api/parks` | GET | all parks from queue-times.com, grouped by resort |
| `/api/rides?parkId=N` | GET | rides + live waits for one park |
| `/api/factory-reset` | POST | NVS wipe + restart |

Note for the simulator build: MSVC caps string literals at 16 KB, so the
page is split into several adjacent raw literals inside `CONFIG_HTML`.

## Button & factory reset

`Button::update()` is a pure state machine (fully unit-tested): debounced
short press (next ride), 700 ms long press (next park), 10 s hold shows a
red warning screen and freezes the app, releasing cancels, 20 s completes
the factory reset and restarts. It works in every state except the blocking
captive portal.
