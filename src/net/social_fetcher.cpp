#include "social_fetcher.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFiClientSecure.h>

#include <time.h>
#include <vector>

#include "../config.h"
#include "../display/fs_littlefs.h"
#include "../settings/settings_store.h"
#include "account_store.h"
#include "platform_store.h"

// NOTE(api): These call the documented companion-API contract (see AGENTS.md).
// The server is currently a mock; the YouTube + Twitch fetchers live
// server-side. On-device behaviour degrades gracefully: a failed call leaves
// the account marked not-fresh and the UI shows "—".

namespace {

bool fetchAndParse(const String& url, const String& token,
                   const JsonDocument& filter, JsonDocument& doc) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  http.setReuse(false);
  if (!http.begin(client, url)) return false;

  http.useHTTP10(true);                           // avoid chunked transfer
  http.addHeader("Accept-Encoding", "identity");  // avoid gzip/deflate
  http.addHeader("User-Agent", cfg::API_USER_AGENT);
  if (token.length()) {
    http.addHeader("Authorization", String("Bearer ") + token);
  }

  int code = http.GET();
  if (code != 200) {
    log_w("HTTP %d %s", code, url.c_str());
    http.end();
    return false;
  }
  DeserializationError err = deserializeJson(
      doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    log_w("json parse: %s", err.c_str());
    return false;
  }
  return true;
}

String iconPath(const String& platform) {
  return String("/icons/") + platform + ".png";
}

// GET /icon/{platform}?size=48 -> small PNG cached at /icons/<platform>.png.
// Only fetched when not already cached. Mirrors the stock-ticker logo path:
// body buffered in RAM, FS guard held only across the quick write + rename.
bool fetchIcon(const String& platform, const String& token) {
  String path = iconPath(platform);
  {
    fs_littlefs::Guard g;
    if (LittleFS.exists(path)) return true;
  }

  String url = String(cfg::API_BASE) + "/icon/" + platform + "?size=48";
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  http.setReuse(false);
  if (!http.begin(client, url)) return false;
  http.useHTTP10(true);
  http.addHeader("Accept-Encoding", "identity");
  http.addHeader("User-Agent", cfg::API_USER_AGENT);
  if (token.length()) http.addHeader("Authorization", String("Bearer ") + token);

  int code = http.GET();
  if (code != 200) { http.end(); return false; }

  std::vector<uint8_t> body;
  body.reserve(8192);
  static constexpr size_t kMax = 65536;
  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[512];
  bool ok = true;
  uint32_t lastData = millis();
  while (http.connected() && millis() - lastData < 10000) {
    int avail = stream->available();
    if (!avail) { vTaskDelay(pdMS_TO_TICKS(2)); continue; }
    size_t want = avail < (int)sizeof(buf) ? (size_t)avail : sizeof(buf);
    int got = stream->readBytes(buf, want);
    if (got <= 0) break;
    lastData = millis();
    if (body.size() + (size_t)got > kMax) { ok = false; break; }
    body.insert(body.end(), buf, buf + got);
  }
  http.end();
  if (!ok || body.size() < 8) return false;
  const uint8_t* s = body.data();
  if (!(s[0] == 0x89 && s[1] == 'P' && s[2] == 'N' && s[3] == 'G')) return false;

  String tmp = path + ".tmp";
  {
    fs_littlefs::Guard g;
    size_t total = LittleFS.totalBytes();
    size_t used  = LittleFS.usedBytes();
    size_t free_ = (total > used) ? (total - used) : 0;
    if (free_ < body.size() + 8192) {
      log_w("[icon] %s skip (fs full)", platform.c_str());
      return false;
    }
    LittleFS.mkdir("/icons");
    LittleFS.remove(tmp);
    File f = LittleFS.open(tmp, "w");
    if (!f) return false;
    size_t wrote = f.write(body.data(), body.size());
    f.close();
    if (wrote != body.size()) { LittleFS.remove(tmp); return false; }
    LittleFS.remove(path);
    ok = LittleFS.rename(tmp, path);
    if (!ok) LittleFS.remove(tmp);
  }
  return ok;
}

}  // namespace

namespace fetcher {

bool fetchPlatforms(SettingsStore& settings, PlatformStore& platforms) {
  String token = settings.apiKey();
  JsonDocument filter;
  JsonObject el = filter.add<JsonObject>();
  el["id"] = true;
  el["name"] = true;
  el["color"] = true;
  el["icon_url"] = true;
  el["handle_hint"] = true;

  String url = String(cfg::API_BASE) + "/platforms";
  JsonDocument doc;
  if (!fetchAndParse(url, token, filter, doc)) return false;

  JsonArrayConst arr = doc.as<JsonArrayConst>();
  if (arr.isNull()) return false;
  std::vector<PlatformMeta> out;
  for (JsonObjectConst o : arr) {
    PlatformMeta m;
    m.id         = (const char*)(o["id"] | "");
    m.name       = (const char*)(o["name"] | "");
    m.color      = (const char*)(o["color"] | "");
    m.iconUrl    = (const char*)(o["icon_url"] | "");
    m.handleHint = (const char*)(o["handle_hint"] | "");
    if (m.id.length()) out.push_back(m);
  }
  if (out.empty()) return false;
  platforms.setPlatforms(std::move(out));
  log_i("[platforms] %u discovered", (unsigned)platforms.snapshot().size());
  return true;
}

bool fetchAccounts(SettingsStore& settings, AccountStore& store,
                   PlatformStore& platforms) {
  auto accts = settings.accounts();
  if (accts.empty()) return false;
  String token = settings.apiKey();
  if (!token.length()) {
    log_w("no api token configured");
    return false;
  }

  JsonDocument filter;
  filter["display_name"] = true;
  filter["followers"]    = true;
  filter["delta_24h"]    = true;
  filter["history"]      = true;

  std::vector<Account> out;
  out.reserve(accts.size());
  for (const auto& ref : accts) {
    Account a;
    a.platform = ref.platform;
    a.handle   = ref.handle;

    String url = String(cfg::API_BASE) + "/social/" + ref.platform + "/" + ref.handle;
    JsonDocument doc;
    if (fetchAndParse(url, token, filter, doc)) {
      a.displayName = (const char*)(doc["display_name"] | ref.handle.c_str());
      a.followers   = doc["followers"] | (long long)-1;
      JsonArrayConst hist = doc["history"].as<JsonArrayConst>();
      if (!hist.isNull()) {
        a.sparkline.reserve(hist.size());
        for (JsonVariantConst v : hist) {
          long long f = v | (long long)-1;
          if (f >= 0) a.sparkline.push_back((float)f);
        }
      }
      // List delta over the window the server returned: first -> last point.
      // Falls back to delta_24h when history is unavailable.
      if (a.sparkline.size() >= 2) {
        a.delta = (long long)(a.sparkline.back() - a.sparkline.front() + 0.5f);
      } else {
        a.delta = doc["delta_24h"] | (long long)0;
      }
      a.fresh = a.followers >= 0;
    }
    out.push_back(a);
    fetchIcon(ref.platform, token);
    vTaskDelay(pdMS_TO_TICKS(150));  // gentle on the API between calls
  }

  time_t now;
  time(&now);
  store.setAccounts(std::move(out), now);
  return true;
}

bool fetchHistory(SettingsStore& settings, AccountStore& store,
                  const String& platform, const String& handle,
                  const String& range, uint32_t gen) {
  if (!platform.length() || !handle.length()) return false;
  String token = settings.apiKey();
  if (!token.length()) return false;

  JsonDocument filter;
  filter["interval"]               = true;
  filter["points"][0]["ts"]        = true;
  filter["points"][0]["followers"] = true;

  String url = String(cfg::API_BASE) + "/history/" + platform + "/" + handle +
               "?range=" + range;
  JsonDocument doc;
  if (!fetchAndParse(url, token, filter, doc)) {
    if (store.historyGenCurrent(gen)) store.setHistoryError(true);
    return false;
  }
  if (!store.historyGenCurrent(gen)) return false;

  String interval = (const char*)(doc["interval"] | "");
  JsonArrayConst pts = doc["points"].as<JsonArrayConst>();
  if (pts.isNull() || pts.size() == 0) {
    store.setHistoryError(true);
    return false;
  }

  std::vector<float>  vals;
  std::vector<time_t> ts;
  vals.reserve(pts.size());
  ts.reserve(pts.size());
  for (JsonVariantConst p : pts) {
    long long f = p["followers"] | (long long)-1;
    if (f < 0) continue;
    vals.push_back((float)f);
    ts.push_back((time_t)(p["ts"] | (long long)0));
  }
  if (vals.empty()) { store.setHistoryError(true); return false; }

  // Downsample (preserving first + last) when the server returns more points
  // than the chart holds, so the displayed window and delta stay correct.
  if (vals.size() > cfg::HISTORY_POINTS) {
    const size_t total = vals.size();
    const size_t kept  = cfg::HISTORY_POINTS;
    std::vector<float>  dv;
    std::vector<time_t> dt;
    dv.reserve(kept);
    dt.reserve(kept);
    for (size_t i = 0; i < kept; ++i) {
      size_t src = (i * (total - 1)) / (kept - 1);
      dv.push_back(vals[src]);
      dt.push_back(ts[src]);
    }
    vals = std::move(dv);
    ts   = std::move(dt);
  }

  History h;
  h.key        = platform + "|" + handle;
  h.range      = range;
  h.interval   = interval;
  h.values     = std::move(vals);
  h.timestamps = std::move(ts);
  if (!store.historyGenCurrent(gen)) return false;
  store.setHistory(std::move(h));
  store.setHistoryError(false);
  return true;
}

}  // namespace fetcher
