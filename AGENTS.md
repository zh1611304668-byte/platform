# Repository Guidelines

## Project Structure & Module Organization
This repository is primarily ESP32 firmware built with PlatformIO.

- `src/`: application logic (`*Manager`, `TrainingMode`, `main.cpp`).
- `include/`: shared headers.
- `lib/`: bundled third-party and generated UI libraries (LVGL, NimBLE, TinyGSM, etc.); avoid editing vendor code unless required.
- `utils/ble_scanner_pio` and `utils/hr_ble_simulator_pio`: small helper firmware projects with their own `platformio.ini`.
- `111/stroke_sim_qt`: Qt/LVGL desktop simulator and analysis assets.
- `.pio/`, `111/stroke_sim_qt/build/`: build outputs; do not commit generated artifacts.

## Build, Test, and Development Commands
- `pio run`: build main firmware (`env:esp32-s3-devkitc-1`).
- `pio run -t upload`: flash firmware to the board.
- `pio device monitor -b 115200`: open serial monitor for runtime logs.
- `pio run -d utils/ble_scanner_pio`: build BLE scanner utility firmware.
- `pio run -d utils/hr_ble_simulator_pio`: build HR BLE simulator firmware.
- `cmake -S 111/stroke_sim_qt -B 111/stroke_sim_qt/build && cmake --build 111/stroke_sim_qt/build`: build Qt simulator.

## Coding Style & Naming Conventions
- Language baseline: C++17 (`111/stroke_sim_qt` and firmware modules).
- Follow existing style: 2-space indentation, braces on the same line, concise functions.
- Naming:
  - Types/classes: `PascalCase` (`GNSSProcessor`, `DataFlowManager`).
  - Functions/variables: `lowerCamelCase`.
  - Macros/constants: `UPPER_SNAKE_CASE`.
- Keep hardware pin mappings and board-specific values centralized in config or top-level definitions.

## Testing Guidelines
- No top-level automated test suite is currently enforced.
- For new firmware tests, add PlatformIO Unity tests under `test/` and run `pio test`.
- Validate changes on hardware (connectivity, GNSS/IMU flow, UI updates, power behavior) before PR.
- No formal coverage gate exists; include manual verification notes in PRs.

## Commit & Pull Request Guidelines
- Prefer short, focused commits in imperative mood.
- Use optional conventional format when helpful, e.g. `fix(time): unify monotonic timestamp base`.
- Keep one logical change per commit; include Chinese or English summaries consistently.
- PRs should include:
  - problem and fix summary,
  - impacted modules/boards,
  - test evidence (logs or command output),
  - UI screenshots/videos when screens or BLE UX changes.
