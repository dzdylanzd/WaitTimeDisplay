# QueueWatch

A tiny desk display for theme-park nerds: an **ESP32-C6** with a 1.47" LCD that
cycles through live ride wait times for the parks you care about, powered by
[queue-times.com](https://queue-times.com).

```
┌──────────────────────────────────────────┐
│  Magic Kingdom                 ᯤ 14:05   │  ← park name + local park time
│ ─────────────────────────────────────────│  ← gold line (dims as data ages)
│ ████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░ │  ← progress through ride list
│ ▌ Space Mountain                    3/12 │  ← ride name + position
│                                          │
│                    45                    │  ← big wait number, colour-coded
│               MINUTE WAIT                │     green → yellow → orange → red
└──────────────────────────────────────────┘
```

## Features

- **Live wait times** from queue-times.com, refreshed every 15 minutes (configurable)
- **Multiple parks** — cycles through every park you enable, ride by ride
- **Colour-coded waits** — green ≤ 15 min, yellow ≤ 30, orange ≤ 45, red above; teal for closed rides
- **Local park time** — automatic IANA → POSIX timezone mapping + NTP sync per park
- **Zero-typing WiFi setup** — captive portal with a scannable QR code
- **Forgiving WiFi handling** — stored credentials survive failed connects; power the device on away from home and it won't lose your network
- **Web configuration UI** — pick parks, filter rides, tune timings, factory-reset from your browser
- **Data freshness indicator** — the gold header line dims when data gets stale
- **Desktop simulator** — the exact firmware sources running on Windows under SDL2
- **Unit tests** — 22 doctest cases covering config persistence and API parsing

## Hardware

| Part | Detail |
|---|---|
| Board | [Waveshare ESP32-C6-LCD-1.47](https://www.waveshare.com/wiki/ESP32-C6-LCD-1.47) (integrated — no wiring needed) |
| Display | 172×320 ST7789, used in landscape (320×172) |
| MCU | ESP32-C6, 160 MHz, WiFi 2.4 GHz |

## Getting started

### 1. Flash the firmware

This is a [PlatformIO](https://platformio.org/) project:

```bash
pio run                     # build
pio run --target upload     # flash
pio device monitor          # serial log @ 115200
```

Dependencies (LVGL 8.3, ArduinoJson 6) are fetched automatically per
`platformio.ini`.

### 2. Join it to your WiFi

On first boot the device opens a setup access point and shows a QR code —
scan it with your phone camera to join, then enter your WiFi credentials in
the captive portal (or connect manually to `QueueWatch-Config` and browse to
`192.168.4.1`).

### 3. Pick your parks

Once connected, the display shows the device's address. Open it in a browser
to reach the config UI, where you can:

- enable parks (grouped by resort),
- hide individual rides per park,
- adjust the API refresh, ride rotation, and closed-park display intervals,
- factory-reset the device (wipes WiFi credentials and all settings, then
  restarts into WiFi setup mode).

Saved settings persist in flash (NVS) across power cycles.

### WiFi behaviour when the network isn't found

If the device can't reach its saved network — say you powered it on
somewhere else by accident — it opens the setup portal but **keeps the old
credentials**. Just plug it back in at home and it reconnects on its own.
The stored network is only replaced when you save a new one in the portal
(or wiped by a factory reset).

## Desktop simulator

The simulator compiles the **real** `src/` files against stub Arduino/ESP32
headers and renders with SDL2 — same state machine, same LVGL screens, real
HTTPS calls to queue-times.com.

```bat
build_sim.bat   # configure + build (VS2019 + SDL2 in C:\SDL2 required)
run_sim.bat     # launch; config UI at http://localhost:8080, ESC quits
```

## Tests

```bat
test.bat        # build + run all doctest unit tests
```

Covers `ConfigManager` (park/filter JSON parsing, NVS round-trips, cache
invalidation) and `QueueApi` (JSON parsing, HTTP failure handling, timezone
cache) with mocked HTTP responses — no hardware needed.

## Project layout

```
src/          firmware sources (also compiled by the simulator and tests)
sim/          Windows simulator: SDL2 display + Arduino/ESP32 header stubs
tests/        doctest unit tests with mock HTTP/Preferences
lv_conf.h     LVGL configuration (root = firmware; sim/ has its own copy)
platformio.ini  board, build flags, library versions
```

See [CLAUDE.md](CLAUDE.md) for a deeper architecture tour (state machine,
display design, module responsibilities).

## Data source

Wait times are provided by the excellent free API at
[queue-times.com](https://queue-times.com/). If you build one of these,
consider supporting them.
