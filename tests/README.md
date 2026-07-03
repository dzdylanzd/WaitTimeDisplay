# QueueWatch Unit Tests

[doctest](https://github.com/doctest/doctest)-based tests (header-only,
vendored as `doctest.h`) that compile the real `src/` logic modules against
mocks — no hardware, no LVGL, no network for the unit suite.

## Run

From the repo root:

```bat
test.bat            REM build + unit tests + live API tests
```

Or from this folder: `build_tests.bat`, then `run_tests.bat`
(`clean_tests.bat` deletes `tests/build/`). Requires Visual Studio 2019,
same as the simulator.

Two binaries are produced:

- **`queuewatch_tests.exe`** — the unit suite (84 test cases /
  327 assertions), fully offline.
- **`queuewatch_live_tests.exe`** — parses *every* real park on
  queue-times.com through the production parser. Needs internet and takes a
  few minutes; useful before releases to catch API-shape surprises.

## Mocks

| File | Mocks | How |
|---|---|---|
| `HTTPClient.h` | HTTPS | `MockHTTP::set(url, body, code)` presets responses per URL |
| `Preferences.h` | NVS | in-memory map, survives `load()` round-trips within a test |
| `Arduino.h` | ESP32 core | minimal `String`/`millis()` shims |

## Coverage by file

| Test file | Module | What's covered |
|---|---|---|
| `test_configmanager.cpp` | `ConfigManager` | park/filter/favorites JSON parsing (incl. malformed input), NVS round-trips via `load()`, cache invalidation, display/palette/ride-option settings, factory reset back to defaults |
| `test_queueapi_json.cpp` | `QueueApi` | ride parsing (lands, Tokyo top-level format, both mixed), non-ASCII → ASCII transliteration, missing fields, HTTP errors, `MAX_RIDES` cap, park list, timezone + country lookup and their shared cache |
| `test_ridefilter.cpp` | `applyDisplayOptions` | skip-closed, min-wait (inclusive boundary), revert-if-empty safety, stable wait-descending sort (closed last), favorites-first, combinations |
| `test_trendstore.cpp` | `TrendStore` | first sighting, ±threshold boundaries, closed rides ignored, park interleaving, capacity eviction |
| `test_waitlevel.cpp` | `pickWaitLevel` | default 15/30/45 boundaries, custom thresholds, closed precedence |
| `test_quiethours.cpp` | `inQuietWindow` | same-day and overnight windows, boundaries, empty window |
| `test_button.cpp` | `Button` | debounce, short/long press, 10 s warning → cancel, 20 s reset (release swallowed), skipped-poll robustness |

## Conventions

- Tests include mock headers first (`tests/` is first on the include path),
  then the real `src/*.h`.
- New `ConfigManager` scalars must use `putInt`/`putBool` — the
  `Preferences` mock (and the sim stub) implement only those getters/setters.
- When adding a setting, extend **three** tests: its own round-trip case,
  the `load()` persistence case, and the `factoryReset` case.
