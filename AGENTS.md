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
- `GET /social/{platform}/{handle}?range=<token>` returns `{ "platform": "...", "handle": "...", "display_name": "...", "followers": 12345, "delta_24h": 87, "history": [12300, 12345] }`; `history` spans the range (the device sends its List window), so the list delta = first->last
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
- `server/`: the companion API (FastAPI, YouTube + Twitch + demo platforms, SQLite history, poller) with pytest tests; see `server/README.md`

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

- `server/` is implemented and tested (2026-10-11) but NOT deployed yet: it needs `server/.env` on rozakos.eu (API_SECRET, YOUTUBE_API_KEY, TWITCH_CLIENT_ID/SECRET), the systemd unit and the nginx snippet. `cfg::API_BASE` = `https://rozakos.eu/social/api/v1` (same scheme as stock-api).
- History only exists from when the server starts tracking an account (YouTube/Twitch expose no past counts).
- No hardware flash/run has been verified yet, so touch calibration and real ST7789 orientation should still be checked on the device.
- Runtime icon cache has no eviction policy yet; it only skips writes when free LittleFS space is low.

## Next Work

- Deploy `server/` on rozakos.eu (see server/README.md) and set the token on the device.
- Run on hardware and tune XPT2046 calibration if touch coordinates are off.
- Add more simulator screenshots for list/detail/hero/settings after UI changes.
- Consider cache eviction for `/icons/*.png` if many platforms are added.

## Session log

- **2026-10-11 — Claude (Opus 5.5, Claude Code):** Added `server/` (companion API: YouTube Data API v3, Twitch Helix app token, demo platform with synthetic history, 10-min cache with stale fallback, SQLite history + hourly poller, drawn PNG icons; 10 pytest tests with mocked upstreams). Firmware: `/social` now sends `?range=<list window>` (the List window setting was saved but never sent, so it had no effect); settings revision counter makes the net task refetch immediately after web-admin edits instead of waiting up to the refresh interval; `API_BASE` set to `https://rozakos.eu/social/api/v1`. Verified: `pio test -e native` 5/5, `pio run -e cyd` builds, server tests 10/10, server run live in demo mode.

## CI (self-hosted Jenkins)

`https://jenkins.rozakos.eu` — job **`CYD-Following-Tracker-MB`**, a *multibranch* pipeline defined
by the `Jenkinsfile` in this repo. Every push to `main` builds, runs the Unity unit tests (`pio test -e native`) and reports the image size (note: `min_spiffs`, ~1.9 MB app slot — not comparable with CYD-Stock-Ticker's `huge_app`). Triggered by
a GitHub webhook through a Cloudflare Tunnel, with a 5-minute rescan as a fallback.

### Releasing

Push a `v*` tag and CI publishes a GitHub Release with `firmware.bin` attached:

```bash
git tag -a v1.2.0 -m "v1.2.0"
git push origin v1.2.0          # tags do NOT go with a plain `git push`
```

`scripts/publish_release.py` does the upload. It is idempotent — rebuilding an
already-released tag reuses the release and replaces the asset rather than
failing.

### Things that will trip you up

- **`src/secrets.h` is gitignored, so a clean clone cannot compile.** CI copies
  ``src/secrets_example.h`` into place, exactly as you would by hand. That means CI proves the
  code *compiles*; the published artifact carries placeholder credentials and is
  not a drop-in image for a real network.
- **A tag is a snapshot.** Rebuilding an old tag runs the `Jenkinsfile` *from that
  commit*, not the current one. Tags predating a pipeline fix will keep failing,
  and that is correct.
- **Multibranch is required, not a preference.** Jenkins polls with
  `git ls-remote -h`, which lists heads only and cannot see tags at all — a plain
  pipeline-from-SCM job never notices a pushed tag whatever its branch specs say.
- **Build strategies are a whitelist.** If you edit them in the Jenkins UI, keep
  *both* the tag and branch strategies; configuring only tags silently stops
  `main` building.
