#include "settings_store.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

#include "../config.h"
#include "../display/fs_littlefs.h"

namespace {

// Split a CSV of "platform|handle" tokens into (platform, handle) pairs.
// Platform ids are lower-cased (they're opaque server keys, conventionally
// lowercase); handles are preserved verbatim (often case-sensitive).
std::vector<AccountRef> split_accounts(const String& s) {
  std::vector<AccountRef> out;
  int start = 0;
  while (start < (int)s.length()) {
    int comma = s.indexOf(',', start);
    if (comma < 0) comma = s.length();
    String tok = s.substring(start, comma);
    tok.trim();
    if (tok.length()) {
      int bar = tok.indexOf('|');
      if (bar > 0 && bar < (int)tok.length() - 1) {
        AccountRef a;
        a.platform = tok.substring(0, bar);
        a.platform.trim();
        a.platform.toLowerCase();
        a.handle = tok.substring(bar + 1);
        a.handle.trim();
        if (a.platform.length() && a.handle.length()) out.push_back(a);
      }
    }
    start = comma + 1;
  }
  return out;
}

std::vector<String> split_csv(const String& s) {
  std::vector<String> out;
  int start = 0;
  while (start < (int)s.length()) {
    int comma = s.indexOf(',', start);
    if (comma < 0) comma = s.length();
    String tok = s.substring(start, comma);
    tok.trim();
    if (tok.length()) out.push_back(tok);
    start = comma + 1;
  }
  return out;
}

}  // namespace

void SettingsStore::begin(const char* seedKey,
                          const char* seedWifiSsid,
                          const char* seedWifiPass) {
  _mu = xSemaphoreCreateMutex();
  load(seedKey, seedWifiSsid, seedWifiPass);
}

void SettingsStore::load(const char* seedKey,
                         const char* seedWifiSsid,
                         const char* seedWifiPass) {
  bool needSave = false;
  auto defaults = [&] {
    _apiKey      = seedKey ? seedKey : "";
    _refresh     = cfg::DEFAULT_REFRESH_SECONDS;
    _accountsCsv = cfg::DEFAULT_ACCOUNTS;
    _listRange   = cfg::DEFAULT_LIST_RANGE;
    _primaryKey  = "";
    _wifiSsid    = seedWifiSsid ? seedWifiSsid : "";
    _wifiPass    = seedWifiPass ? seedWifiPass : "";
  };

  fs_littlefs::Guard g;
  if (!LittleFS.exists(cfg::SETTINGS_PATH)) {
    defaults();
    needSave = true;
  } else {
    File f = LittleFS.open(cfg::SETTINGS_PATH, "r");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
      defaults();
      needSave = true;
    } else {
      _apiKey        = doc["api_key"]    | (seedKey ? seedKey : "");
      _refresh       = doc["refresh_s"]  | cfg::DEFAULT_REFRESH_SECONDS;
      _accountsCsv   = doc["accounts"]   | cfg::DEFAULT_ACCOUNTS;
      _favouritesCsv = doc["favourites"] | "";
      _primaryKey    = doc["primary"]    | "";
      _listRange     = doc["list_range"] | cfg::DEFAULT_LIST_RANGE;
      _wifiSsid      = doc["wifi_ssid"]  | (seedWifiSsid ? seedWifiSsid : "");
      _wifiPass      = doc["wifi_pass"]  | (seedWifiPass ? seedWifiPass : "");
    }
  }

  if (_refresh < cfg::MIN_REFRESH_SECONDS) _refresh = cfg::MIN_REFRESH_SECONDS;
  if (!_listRange.length()) _listRange = cfg::DEFAULT_LIST_RANGE;
  if (needSave) save();
}

void SettingsStore::save() const {
  JsonDocument doc;
  doc["api_key"]    = _apiKey;
  doc["refresh_s"]  = _refresh;
  doc["accounts"]   = _accountsCsv;
  doc["favourites"] = _favouritesCsv;
  doc["primary"]    = _primaryKey;
  doc["list_range"] = _listRange;
  doc["wifi_ssid"]  = _wifiSsid;
  doc["wifi_pass"]  = _wifiPass;

  fs_littlefs::Guard g;
  File f = LittleFS.open(cfg::SETTINGS_PATH, "w");
  serializeJson(doc, f);
  f.close();
}

String SettingsStore::apiKey() const {
  xSemaphoreTake(_mu, portMAX_DELAY);
  String v = _apiKey;
  xSemaphoreGive(_mu);
  return v;
}

uint32_t SettingsStore::refreshSeconds() const {
  xSemaphoreTake(_mu, portMAX_DELAY);
  uint32_t v = _refresh;
  xSemaphoreGive(_mu);
  return v;
}

std::vector<AccountRef> SettingsStore::accounts() const {
  xSemaphoreTake(_mu, portMAX_DELAY);
  String csv = _accountsCsv;
  xSemaphoreGive(_mu);
  return split_accounts(csv);
}

String SettingsStore::listRange() const {
  xSemaphoreTake(_mu, portMAX_DELAY);
  String v = _listRange;
  xSemaphoreGive(_mu);
  return v;
}

String SettingsStore::primaryAccount() const {
  xSemaphoreTake(_mu, portMAX_DELAY);
  String v = _primaryKey;
  xSemaphoreGive(_mu);
  return v;
}

void SettingsStore::setPrimaryAccount(const String& key) {
  xSemaphoreTake(_mu, portMAX_DELAY);
  _primaryKey = key;
  save();
  xSemaphoreGive(_mu);
}

std::vector<String> SettingsStore::favourites() const {
  xSemaphoreTake(_mu, portMAX_DELAY);
  String csv = _favouritesCsv;
  xSemaphoreGive(_mu);
  return split_csv(csv);
}

bool SettingsStore::isFavourite(const String& key) const {
  String k = key;
  k.trim();
  if (!k.length()) return false;
  for (const auto& f : favourites()) {
    if (f == k) return true;
  }
  return false;
}

bool SettingsStore::toggleFavourite(const String& key) {
  String k = key;
  k.trim();
  if (!k.length()) return false;

  xSemaphoreTake(_mu, portMAX_DELAY);
  std::vector<String> favs = split_csv(_favouritesCsv);
  bool was = false;
  for (auto it = favs.begin(); it != favs.end(); ++it) {
    if (*it == k) { was = true; favs.erase(it); break; }
  }
  if (!was) favs.push_back(k);

  String csv;
  for (size_t i = 0; i < favs.size(); ++i) {
    if (i) csv += ',';
    csv += favs[i];
  }
  _favouritesCsv = csv;
  save();
  xSemaphoreGive(_mu);
  return !was;
}

String SettingsStore::wifiSsid() const {
  xSemaphoreTake(_mu, portMAX_DELAY);
  String v = _wifiSsid;
  xSemaphoreGive(_mu);
  return v;
}

String SettingsStore::wifiPass() const {
  xSemaphoreTake(_mu, portMAX_DELAY);
  String v = _wifiPass;
  xSemaphoreGive(_mu);
  return v;
}

void SettingsStore::setWifi(const String& ssid, const String& pass) {
  xSemaphoreTake(_mu, portMAX_DELAY);
  _wifiSsid = ssid;
  _wifiPass = pass;
  save();
  xSemaphoreGive(_mu);
}

void SettingsStore::update(const String& apiKey,
                           uint32_t refreshSeconds,
                           const String& accountsCsv,
                           const String& primaryKey,
                           const String& listRange) {
  xSemaphoreTake(_mu, portMAX_DELAY);
  _apiKey      = apiKey;
  _refresh     = refreshSeconds < cfg::MIN_REFRESH_SECONDS
                     ? cfg::MIN_REFRESH_SECONDS
                     : refreshSeconds;
  _accountsCsv = accountsCsv;
  _primaryKey  = primaryKey;
  if (listRange.length()) _listRange = listRange;
  save();
  _revision = _revision + 1;
  xSemaphoreGive(_mu);
}
