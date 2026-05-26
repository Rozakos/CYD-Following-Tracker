#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <vector>

// Display metadata for one platform, learned at runtime from GET /platforms.
// The firmware treats `id` as an opaque string and never switches on it — a
// brand-new platform configured server-side simply appears here and renders
// coherently (name in lists, brand color on badges, icon fetched by url).
struct PlatformMeta {
  String id;          // opaque, e.g. "youtube"
  String name;        // display name, e.g. "YouTube"
  String color;       // "#RRGGBB" brand color, or "" (UI hashes a fallback)
  String iconUrl;     // server-relative icon path, e.g. "/icon/youtube"
  String handleHint;  // web-admin hint, e.g. "channel ID or @handle"
};

class PlatformStore {
 public:
  void begin();

  // Replace the known platform set (called by the fetcher after GET
  // /platforms, or by the simulator's fake_data).
  void setPlatforms(std::vector<PlatformMeta> p);

  std::vector<PlatformMeta> snapshot() const;
  bool   empty() const;

  // Look up metadata by id. Returns false when the platform is unknown; the
  // UI then falls back to a hash-derived color and the id as its own name,
  // so unknown platforms degrade gracefully rather than breaking layout.
  bool   find(const String& id, PlatformMeta& out) const;

  String name(const String& id) const;   // falls back to `id`
  String color(const String& id) const;  // "" when unknown

 private:
  mutable SemaphoreHandle_t _mu = nullptr;
  std::vector<PlatformMeta> _platforms;
};
