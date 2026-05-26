# Handoff

## Architecture Invariants

- UI work happens only on the LVGL task pinned to core 1.
- WiFi, HTTPS fetches, captive portal, and web admin run on the network task pinned to core 0.
- Shared settings/account/platform stores are mutex-protected.
- LVGL remains single-threaded behind `g_lvglMu`.
- `LV_USE_OS = LV_OS_NONE`; do not enable LVGL's FreeRTOS port.
- ST7789 color is correct with `cfg.invert = false` and `writePixels(..., true)` in the LVGL flush callback.
- HTTPS fetches intentionally use `WiFiClientSecure::setInsecure()`, `http.useHTTP10(true)`, and `Accept-Encoding: identity`.
- Any header using Arduino `String` must include `<Arduino.h>` itself.

## Companion API Contract

The server is the source of truth for platforms.

```json
[
  {
    "id": "youtube",
    "name": "YouTube",
    "color": "#FF0000",
    "icon_url": "/icon/youtube",
    "handle_hint": "channel ID or @handle"
  }
]
```

Endpoints:

- `GET /platforms`
- `GET /social/{platform}/{handle}` returns `{ "platform": "...", "handle": "...", "display_name": "...", "followers": 12345, "delta_24h": 87, "history": [12300, 12345] }`
- `GET /history/{platform}/{handle}?range=<token>` where token is `1w`, `1mo`, `3mo`, `1y`, or `max`
- `GET /icon/{platform}?size=48`

Firmware treats platform ids as opaque strings. Adding Instagram, TikTok, X, Telegram, or any later platform should be server-only work.

## Current Implementation

- `src/main.cpp`: task startup, LittleFS, LVGL/display init, WiFi/API/web orchestration
- `src/net/social_fetcher.*`: normalized API client with streaming ArduinoJson filters
- `src/net/platform_store.*`: runtime platform metadata
- `src/net/account_store.*`: current account data and async history request handoff
- `src/settings/settings_store.*`: LittleFS-backed runtime settings
- `src/net/web_admin.*` and `src/net/web_admin_page.h`: LAN settings UI
- `src/net/wifi_mgr.*` and `src/net/captive_portal.*`: STA/AP onboarding
- `src/ui/*`: list, detail, hero, settings, WiFi setup, styles, runtime icons
- `sim/*`: SDL/LVGL host simulator and fake data
- `test/test_native/*`: interpolation tests

## Completed So Far

- PlatformIO firmware scaffold is in place for `esp32dev` with Arduino-ESP32, LovyanGFX, LVGL 9, ArduinoJson, ESPAsyncWebServer, AsyncTCP, and LittleFS.
- `min_spiffs.csv` exists and gives LittleFS a 192 KB partition.
- `include/lv_conf.h` is the single LVGL config source and wraps `LV_MEM_SIZE` so the simulator can override it.
- CYD dual-USB ST7789 display and XPT2046 touch are configured in `src/display/lgfx_cyd.hpp`.
- LVGL bridge uses two 16-line DMA flush buffers and byte-swapped `writePixels(..., true)` with ST7789 inversion disabled.
- LittleFS access is guarded by `src/display/fs_littlefs.*`.
- Firmware starts two pinned tasks: `uiTask` on core 1 and `netTask` on core 0.
- Network task disables the core 0 WDT for blocking HTTPS calls, handles WiFi, platform discovery, account refreshes, history fetches, captive portal, and web admin startup.
- AP/captive portal flow is implemented with `CYD-Setup-<MAC>`, DNS hijack, network picker, `/save`, and common Android/Apple/Windows captive-probe routes.
- Runtime settings persist to `/settings.json`: API token, refresh interval, account list, favourite pins, primary account, list range, WiFi SSID, and WiFi password.
- First-boot seeding is implemented through gitignored `src/secrets.h`; `src/secrets_example.h` documents the expected values.
- Platform discovery is implemented through `GET /platforms`; platform ids are opaque strings in firmware.
- Account fetch is implemented through `GET /social/{platform}/{handle}` with streaming ArduinoJson filters.
- History fetch is implemented through `GET /history/{platform}/{handle}?range=...`, downsampling while preserving first and last points.
- Runtime platform icons are fetched through `GET /icon/{platform}?size=48` and cached under `/icons/<platform>.png` when LittleFS has enough space.
- YouTube and Twitch also have tiny firmware built-in icons, so the launch platforms do not depend on icon downloads. Runtime PNGs still take precedence when present.
- Web admin at `/settings` supports refresh interval, API token, list delta range, primary account, tracked account add/delete, and a platform dropdown populated from runtime platform metadata.
- List UI is implemented with status bar, WiFi/RSSI state, settings gear, follower rows, runtime icon/badge fallback, compact follower counts, absolute delta, sparkline, auto-scroll, tap for detail, and long-press pinning.
- Detail UI is implemented with larger platform icon, account name, follower count, absolute range delta, area-filled smoothed history chart, `1W / 1M / 3M / 1Y / All` button matrix, and milestone progress block.
- Hero UI is implemented as a separate single-account screen with large count, gain, icon, and milestone progress.
- Read-only settings info screen and WiFi setup screen are implemented.
- Simulator is implemented under `sim/` with SDL/LVGL host rendering, fake data, `--window`, `--headless --out=...`, screen selection, click/long-press injection, and `--web-settings=...`.
- Fake simulator platform data includes YouTube/Twitch launch platforms plus later-platform examples to exercise the data-driven platform picker.
- Monotone cubic interpolation/PCHIP-style smoothing is implemented and covered by native unit tests.
- `README.md` documents build, simulator, provisioning, API contract, and important hardware/LVGL notes.
- `.gitignore` excludes `.pio`, simulator build output, build logs, local simulator data, and `src/secrets.h`.
- Captive portal cleanup after STA reconnection is handled in `src/main.cpp`.

## Verified

- Native interpolation tests pass: 5 test cases succeeded.
- Firmware build passes for `cyd` using PlatformIO's virtualenv executable:
  `%USERPROFILE%\.platformio\penv\Scripts\platformio.exe run -e cyd`
- Simulator build passes with `cmake --build sim/build`.
- Simulator artifacts were generated:
  - `sim/shots/list.png`
  - `sim/shots/detail.png`
  - `sim/shots/hero.png`
  - `sim/shots/settings.html`

## Not Done Yet

- The real companion API is not implemented in this repo. `cfg::API_BASE` is still a placeholder for the server-side service.
- Real YouTube and Twitch upstream fetchers are not in firmware and are still expected to live server-side behind the normalized API.
- No hardware flash/run has been verified yet, so touch calibration and real ST7789 orientation should still be checked on the device.
- Runtime icon cache has no eviction policy yet; it only skips writes when free LittleFS space is low.

## Next Work

- Deploy or stub the real companion API at the configured `cfg::API_BASE`.
- Implement real server-side YouTube and Twitch fetchers first.
- Run on hardware and tune XPT2046 calibration if touch coordinates are off.
- Add more simulator screenshots for list/detail/hero/settings after UI changes.
- Consider cache eviction for `/icons/*.png` if many platforms are added.
