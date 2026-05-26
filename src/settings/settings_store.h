#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <vector>

// A tracked account as persisted in settings: an opaque platform id plus a
// platform-specific handle. The pair's canonical key is "platform|handle".
struct AccountRef {
  String platform;
  String handle;
  String key() const { return platform + "|" + handle; }
};

class SettingsStore {
 public:
  // seedWifiSsid/seedWifiPass are written to LittleFS only on first boot (or
  // when /settings.json is absent). After that the captive portal / web admin
  // owns these.
  void begin(const char* seedKey,
             const char* seedWifiSsid = nullptr,
             const char* seedWifiPass = nullptr);

  String   apiKey() const;
  uint32_t refreshSeconds() const;
  String   wifiSsid() const;
  String   wifiPass() const;

  // Tracked accounts as (platform, handle) pairs, in display order.
  std::vector<AccountRef> accounts() const;

  // The window used for the list row's absolute delta and as the detail
  // screen's default range. One of: 1w / 1mo / 3mo / 1y / max.
  String listRange() const;

  // Primary account for the hero/single-account view — a "platform|handle"
  // key, or "" when none is chosen (list stays the default screen).
  String primaryAccount() const;
  void   setPrimaryAccount(const String& key);

  // Favourites — accounts the user pinned to the top of the list via a
  // long-press. Stored as a CSV of "platform|handle" keys; order preserved.
  std::vector<String> favourites() const;
  bool isFavourite(const String& key) const;
  // Adds the key to favourites if absent, removes it if present. Returns the
  // new state (true = is now favourite). Persists to LittleFS.
  bool toggleFavourite(const String& key);

  // Atomic update from the web form. `accountsCsv` is comma-separated
  // "platform|handle" tokens. Persists to LittleFS.
  void update(const String& apiKey,
              uint32_t refreshSeconds,
              const String& accountsCsv,
              const String& primaryKey,
              const String& listRange);

  // Captive portal saves the user's home WiFi here.
  void setWifi(const String& ssid, const String& pass);

 private:
  void load(const char* seedKey, const char* seedWifiSsid, const char* seedWifiPass);
  void save() const;

  mutable SemaphoreHandle_t _mu = nullptr;
  String   _apiKey;
  uint32_t _refresh = 600;
  String   _accountsCsv;
  String   _favouritesCsv;
  String   _primaryKey;
  String   _listRange;
  String   _wifiSsid;
  String   _wifiPass;
};
