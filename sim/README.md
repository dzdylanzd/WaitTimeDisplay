# QueueWatch Desktop Simulator

Runs the **real firmware sources** (`src/`) natively on Windows: SDL2
replaces the LCD, stub headers in this folder replace the Arduino/ESP32
core. Same state machine, same LVGL screens, real HTTPS calls to
queue-times.com — if it works here, only hardware-specific bugs (SPI, flash,
WiFi radio) remain.

## Prerequisites

- Visual Studio 2019 (MSVC + its bundled CMake)
- SDL2 VC development package extracted to `C:\SDL2`
- Run `pio run` once at the repo root so LVGL and ArduinoJson are downloaded
  into `.pio/libdeps/`

## Build & run

From the repo root (recommended):

```bat
build_sim.bat   REM configure (first run) + incremental build
run_sim.bat     REM launch the simulator window
```

Or the scripts in this folder: `build.bat`, `run.bat`, `rebuild.bat`
(clean + full rebuild), `clean.bat` (delete `sim/build/`).

The config web UI is at **http://localhost:8080** (port 80 needs admin on
Windows, so the sim uses 8080).

## Keys

| Key | Action |
|---|---|
| `N` / `P` | next ride / next park (BOOT button short/long press) |
| `C` / `D` | connect / drop WiFi |
| `W` / `X` | show / cancel the factory-reset warning screen |
| `ESC` | quit |

## What happens on first run

1. ~1.5 s fake "connecting" animation
2. 10 s startup splash showing `localhost:8080`
3. "No Parks Set" until you enable parks in the browser
4. Then the real wait-time cycle with live queue-times.com data

Saved settings persist between runs in `sim_prefs_*.txt` files (the
file-backed `Preferences` stub) inside `sim/build/`.

## How the stubbing works

`sim/main_sim.cpp` mirrors `src/main.cpp` line for line. The stub headers
shadow the ESP32 ones via include-path order:

| Stub | Replaces | Behaviour |
|---|---|---|
| `Arduino.h` | ESP32 core | `String`, `millis()`, `Serial`, GPIO no-ops |
| `WiFi.h` + `wifi_sim.cpp` | `<WiFi.h>` | connects after 1.5 s; `localIP()` → `localhost:8080` |
| `wifimgr_sim.cpp` | `src/wifimgr.cpp` | credentials always "configured" |
| `Preferences.h` | NVS | file-backed, persistent between runs |
| `HTTPClient.h` | HTTPS | WinHTTP — real TLS, thread-safe |
| `WebServer.h` | web server | cpp-httplib on port 8080, background thread |
| `lvgl_driver_sim.cpp` | LVGL driver | SDL2 window + mouse/keyboard input |
| `lcd_st7789_sim.cpp` | LCD SPI driver | no-op (LVGL draws straight to SDL) |
| `DNSServer.h`, `WiFiClientSecure.h`, `tzhelper_sim.cpp` | — | no-ops |

Two things to keep in mind when changing code:

- There are **two `lv_conf.h` files** — repo root (firmware) and `sim/`
  (simulator). Toggle LVGL features in **both**.
- The simulator cannot catch hardware-only bugs: anything in
  `lcd_st7789.cpp`, SPI timing, or real NVS behaviour must be verified on
  the device.
