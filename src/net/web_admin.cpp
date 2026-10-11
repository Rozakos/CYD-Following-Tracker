#include "web_admin.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include "../config.h"
#include "../settings/settings_store.h"
#include "account_store.h"
#include "device_identity.h"
#include "platform_store.h"
#include "web_admin_page.h"

namespace {

AsyncWebServer g_server(80);
SettingsStore* g_settings  = nullptr;
PlatformStore* g_platforms = nullptr;
AccountStore*  g_accounts  = nullptr;

void send_json(AsyncWebServerRequest* req, JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  req->send(200, "application/json", out);
}

void redirect_to_settings(AsyncWebServerRequest* req) {
  AsyncWebServerResponse* response = req->beginResponse(303, "text/plain", "");
  response->addHeader("Location", "/settings");
  req->send(response);
}

void send_settings_page(AsyncWebServerRequest* req) {
  AsyncResponseStream* response = req->beginResponseStream("text/html");
  web_admin_page::write_settings_page(*response, *g_settings, *g_platforms);
  req->send(response);
}

}  // namespace

namespace web_admin {

void begin(SettingsStore* settings, PlatformStore* platforms,
           AccountStore* accounts) {
  g_settings  = settings;
  g_platforms = platforms;
  g_accounts  = accounts;

  // Rozakos local device protocol (identity + live state) for the Rozakos
  // Home app; same contract as CYD-Stock-Ticker. No auth, LAN only.
  g_server.on("/api/device-info", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument d;
    d["id"]        = device_identity::deviceId();
    d["name"]      = "CYD Follower Tracker";
    d["type"]      = "cyd_follower_tracker";
    d["fw"]        = cfg::FW_VERSION;
    d["mac"]       = device_identity::macString();
    d["webUiPath"] = "/settings";
    JsonArray caps = d["capabilities"].to<JsonArray>();
    caps.add("web_ui");
    caps.add("settings");
    if (g_accounts) caps.add("state");
    send_json(req, d);
  });

  g_server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument d;
    JsonArray arr = d["accounts"].to<JsonArray>();
    if (g_accounts) {
      for (const Account& a : g_accounts->snapshot()) {
        JsonObject o = arr.add<JsonObject>();
        o["platform"]     = a.platform;
        o["handle"]       = a.handle;
        o["display_name"] = a.displayName.length() ? a.displayName : a.handle;
        if (a.fresh && a.followers >= 0) {
          o["followers"] = a.followers;
          o["delta"]     = a.delta;
        } else {
          o["followers"] = nullptr;
        }
      }
      d["updated"] = (long long)g_accounts->lastUpdate();
    }
    d["range"] = g_settings->listRange();
    send_json(req, d);
  });

  g_server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    redirect_to_settings(req);
  });

  g_server.on("/settings", HTTP_GET, [](AsyncWebServerRequest* req) {
    send_settings_page(req);
  });

  g_server.on("/settings", HTTP_POST, [](AsyncWebServerRequest* req) {
    String api = req->hasParam("api_key", true)
                     ? req->getParam("api_key", true)->value() : "";
    String ref = req->hasParam("refresh_s", true)
                     ? req->getParam("refresh_s", true)->value()
                     : String(cfg::DEFAULT_REFRESH_SECONDS);
    String range = req->hasParam("list_range", true)
                       ? req->getParam("list_range", true)->value() : "";
    String primary = req->hasParam("primary", true)
                         ? req->getParam("primary", true)->value() : "";
    api.trim();
    g_settings->update(api, ref.toInt(),
                       web_admin_page::accounts_csv(*g_settings),
                       primary, range);
    redirect_to_settings(req);
  });

  g_server.on("/add", HTTP_POST, [](AsyncWebServerRequest* req) {
    String platform = req->hasParam("platform", true)
                          ? req->getParam("platform", true)->value() : "";
    String handle = req->hasParam("handle", true)
                        ? req->getParam("handle", true)->value() : "";
    platform.trim();
    platform.toLowerCase();
    handle.trim();
    if (platform.length() && handle.length()) {
      String csv = web_admin_page::accounts_csv(*g_settings);
      String entry = platform + "|" + handle;
      // Avoid duplicate (platform|handle) entries.
      bool exists = false;
      for (const auto& a : g_settings->accounts()) {
        if (a.platform == platform && a.handle == handle) { exists = true; break; }
      }
      if (!exists) {
        if (csv.length()) csv += ',';
        csv += entry;
        g_settings->update(g_settings->apiKey(), g_settings->refreshSeconds(),
                           csv, g_settings->primaryAccount(),
                           g_settings->listRange());
      }
    }
    redirect_to_settings(req);
  });

  g_server.on("/delete", HTTP_POST, [](AsyncWebServerRequest* req) {
    int idx = req->hasParam("i", true)
                  ? req->getParam("i", true)->value().toInt() : -1;
    auto accts = g_settings->accounts();
    if (idx >= 0 && (size_t)idx < accts.size()) {
      accts.erase(accts.begin() + idx);
      g_settings->update(g_settings->apiKey(), g_settings->refreshSeconds(),
                         web_admin_page::accounts_csv(accts),
                         g_settings->primaryAccount(),
                         g_settings->listRange());
    }
    redirect_to_settings(req);
  });

  g_server.onNotFound([](AsyncWebServerRequest* req) {
    req->send(404, "text/plain", "not found");
  });

  g_server.begin();
}

}  // namespace web_admin
