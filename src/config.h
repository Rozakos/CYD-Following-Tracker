#pragma once

#include <cstdint>

namespace cfg {

// Self-hosted social-stats proxy API. The ESP32 only ever talks to this one
// normalized JSON API; the messy per-platform auth (YouTube Data API key,
// Twitch OAuth, scraping, etc.) lives entirely on the server. Bearer-token
// auth; a non-empty User-Agent is always sent (see API_USER_AGENT).
//
// Served by server/ (FastAPI), deployed next to stock-api on rozakos.eu under
// the same /<name>/api/v1 scheme (see server/README.md). See AGENTS.md
// "API contract".
inline constexpr const char* API_BASE       = "https://rozakos.eu/social/api/v1";
inline constexpr const char* API_USER_AGENT = "CYD-Follower-Tracker/1.0 (ESP32)";
// Reported to the Rozakos Home app (GET /api/device-info, mDNS TXT "fw").
inline constexpr const char* FW_VERSION     = "1.1.0";
// Eastern European time with DST: UTC+2 winter, UTC+3 summer.
inline constexpr const char* TIME_TZ        = "EET-2EEST,M3.5.0/3,M10.5.0/4";

// Default tracked accounts as `platform|handle` pairs, comma-separated.
// Platforms are NOT hardcoded anywhere in the firmware — these are just the
// seed entries written to /settings.json on first boot. Launch platforms are
// youtube + twitch; the device discovers the full platform list (names,
// colors, icons) at runtime from GET /platforms.
inline constexpr const char* DEFAULT_ACCOUNTS =
    "youtube|@mkbhd,twitch|pokimane,youtube|@veritasium";

inline constexpr uint32_t DEFAULT_REFRESH_SECONDS = 600;  // server caches ~10 min
inline constexpr uint32_t MIN_REFRESH_SECONDS     = 15;

// Default window for the list row's absolute delta ("+1,240 this week").
// One of the range tokens below. User-overridable via the web admin.
inline constexpr const char* DEFAULT_LIST_RANGE = "1w";

inline constexpr const char* SETTINGS_PATH = "/settings.json";

inline constexpr uint16_t SCREEN_W = 320;
inline constexpr uint16_t SCREEN_H = 240;

inline constexpr uint16_t HISTORY_POINTS   = 30;
inline constexpr uint16_t SPARKLINE_POINTS = 10;

}  // namespace cfg
