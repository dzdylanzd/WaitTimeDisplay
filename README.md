# QueueWatch

A tiny desk display for theme-park nerds: an **ESP32-C6** with a 1.47" LCD that
cycles through live ride wait times for the parks you care about, powered by
[queue-times.com](https://queue-times.com).

```
┌──────────────────────────────────────────┐
│  Magic Kingdom                 ᯤ 14:05   │  ← park name + local park time
│ ─────────────────────────────────────────│  ← gold line (dims as data ages)
│ ████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░ │  ← progress through ride list
│ ▌ Space Mountain                  * 3/12 │  ← ride name + position (★ favorite)
│ ▌ Tomorrowland                           │  ← themed land
│                    45              ▲ 10  │  ← big wait number + trend arrow
│               MINUTE WAIT                │     green → yellow → orange → red
└──────────────────────────────────────────┘
```

## Features

- **Live wait times** from queue-times.com, refreshed every 15 minutes (configurable)
- **Multiple parks** — cycles through every park you enable, ride by ride
- **Colour-coded waits** — green ≤ 15 min, yellow ≤ 30, orange ≤ 45, red above; teal for closed rides
- **Themed-land names** — each ride shows its land ("Frontierland", "Tomorrowland", ...)
- **Trend arrows** — a red ▲ or green ▼ with the change in minutes when a ride's wait moved since the last refresh
- **Favorites & sorting** — star rides in the web UI to show them first, sort by longest wait, skip closed rides, or hide short waits
- **Brightness & quiet hours** — dim or switch off the screen on a nightly schedule, in *your* timezone (or the park's, if you prefer)
- **Flippable screen** — rotate the display 180° in the web UI when the device is mounted upside-down
- **Status LED** — the onboard RGB LED glows in the current wait colour (can be turned off in the web UI)
- **BOOT-button controls** — tap for next ride, hold for next park, hold 20 s for an on-device factory reset (with an "are you sure?" warning at 10 s)
- **Local park time** — automatic IANA → POSIX timezone mapping + NTP sync per park
- **Zero-typing WiFi setup** — captive portal with a scannable QR code
- **Forgiving WiFi handling** — stored credentials survive failed connects; power the device on away from home and it won't lose your network
- **Web configuration UI** — pick parks, filter rides, tune timings, back-up/restore, factory-reset from your browser
- **Data freshness indicator** — the gold header line dims when data gets stale
- **Desktop simulator** — the exact firmware sources running on Windows under SDL2
- **Unit tests** — 75 doctest cases covering config persistence, API parsing, filtering/sorting, quiet hours, trends, and the button state machine

## Hardware

| Part | Detail |
|---|---|
| Board | [Waveshare ESP32-C6-LCD-1.47](https://www.waveshare.com/wiki/ESP32-C6-LCD-1.47) (integrated — no wiring needed) |
| Display | 172×320 ST7789, used in landscape (320×172), PWM-dimmable backlight |
| MCU | ESP32-C6, 160 MHz, WiFi 2.4 GHz |
| Extras | Onboard WS2812 RGB LED (GPIO8) + BOOT button (GPIO9) — both used by the firmware |

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
- hide individual rides per park and star your favorites,
- choose the ride order (park order or longest wait first, favorites first),
  skip closed rides, or hide rides waiting under N minutes,
- set the screen brightness, quiet hours (nightly dim/off schedule), flip
  the screen 180° for upside-down mounting, and turn the status LED on or off,
- adjust the API refresh, ride rotation, and closed-park display intervals,
- export/import the whole configuration as a JSON file,
- factory-reset the device (wipes WiFi credentials and all settings, then
  restarts into WiFi setup mode).

Saved settings persist in flash (NVS) across power cycles.

### 4. On-device controls (BOOT button)

| Action | Result |
|---|---|
| Tap | Next ride |
| Hold ~1 s | Next park |
| Hold 10 s | Factory-reset warning appears — release to cancel |
| Keep holding to 20 s | Factory reset: erases WiFi + all settings and restarts into setup |

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

Simulator keys: `C` connect WiFi / `D` drop WiFi / `N` next ride / `P` next
park / `W` show the factory-reset warning / `X` cancel it / `ESC` quit.

## Tests

```bat
test.bat        # build + run all doctest unit tests
```

75 test cases / 266 assertions, no hardware needed:

- `ConfigManager` — park/filter/favorites JSON parsing, NVS round-trips,
  cache invalidation, factory reset
- `QueueApi` — JSON parsing (incl. land names and non-ASCII transliteration),
  HTTP failure handling, timezone cache (mocked HTTP responses)
- `applyDisplayOptions` — skip-closed / min-wait filtering, favorites-first
  and wait-descending sorting, revert-when-empty safety
- `TrendStore` — trend thresholds, closed-ride handling, capacity eviction
- `inQuietWindow` — overnight wrap, boundaries, empty window
- `Button` — debounce, short/long press, the 10 s/20 s hold-to-reset sequence

A separate `queuewatch_live_tests` binary exercises the parser against every
real park on queue-times.com (needs internet; run from `tests/build/`).

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
