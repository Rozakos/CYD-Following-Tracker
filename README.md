# CYD Social Follower Tracker

Firmware for the ESP32-2432S028R dual-USB "Cheap Yellow Display" showing social follower counts from a self-hosted normalized API. The firmware is platform-agnostic: tracked accounts are `(platform, handle)` pairs, and platform display metadata comes from `GET /platforms`.

## Current State

- PlatformIO Arduino-ESP32 firmware target: `cyd`
- LovyanGFX ST7789 display + XPT2046 touch configuration for the dual-USB CYD
- LVGL 9 UI with list, detail chart, hero account, settings info, and WiFi setup screens
- Two pinned FreeRTOS tasks: UI on core 1, network/web/WiFi on core 0
- Runtime settings in LittleFS: API token, refresh interval, tracked accounts, primary account, list range, WiFi credentials
- LAN web admin at `http://<device-ip>/settings`
- Captive WiFi setup AP with DNS hijack and OS captive-probe routes
- Desktop simulator under `sim/` with interactive, headless PNG, and web settings HTML modes
- Native unit test coverage for monotone interpolation

## API Contract

All device requests use:

- `Authorization: Bearer <token>` when configured
- Non-empty `User-Agent`
- HTTP/1.0
- `Accept-Encoding: identity`

Expected companion API endpoints:

```text
GET /platforms
GET /social/{platform}/{handle}
GET /history/{platform}/{handle}?range=1w|1mo|3mo|1y|max
GET /icon/{platform}?size=48
```

`platform` is an opaque string throughout the firmware. Do not add platform enums, platform-name switches, or baked-in platform color tables. YouTube and Twitch are the first real server-side fetchers; other platforms can be added server-side behind the same contract.

## Build

```powershell
pio run -e cyd
pio test -e native
```

If `pio` is not on PATH, use the PlatformIO Core environment where it is installed.

## Simulator

The simulator expects MSYS2/MinGW, SDL2, CMake, and the PlatformIO library cache populated for the `cyd` environment.

```powershell
cmake -S sim -B sim/build -G Ninja
cmake --build sim/build

.\sim\build\cyd_sim.exe --headless --screen=list --out=sim\shots\list.png
.\sim\build\cyd_sim.exe --headless --screen=detail --out=sim\shots\detail.png
.\sim\build\cyd_sim.exe --headless --screen=hero --out=sim\shots\hero.png
.\sim\build\cyd_sim.exe --web-settings=sim\shots\settings.html
```

Use `--window` for interactive SDL mode.

## Provisioning

Copy `src/secrets_example.h` to `src/secrets.h` and fill optional first-boot defaults. `API_TOKEN_SEED` and fallback WiFi credentials are only used when `/settings.json` does not exist; after first boot, the web admin and captive portal own runtime settings.

## Notes

- `LV_USE_OS` must remain `LV_OS_NONE`.
- ST7789 `cfg.invert` must remain `false`; the flush callback swaps RGB565 bytes with `writePixels(..., true)`.
- LittleFS is small. Runtime icons are cached opportunistically and skipped when space is low.
- The API base in `src/config.h` is a placeholder until the real companion service is deployed.
