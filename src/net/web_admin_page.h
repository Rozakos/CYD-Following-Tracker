#pragma once

#include <Arduino.h>

#include <vector>

#include "../config.h"
#include "../settings/settings_store.h"
#include "platform_store.h"

#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef F
#define F(x) x
#endif

namespace web_admin_page {

inline const char PAGE_HEAD_P[] PROGMEM =
  "<!DOCTYPE html><html><head>"
  "<meta name=viewport content='width=device-width,initial-scale=1'>"
  "<title>CYD Follower Tracker</title><style>"
  ":root{--bg:#0d1117;--card:#161b22;--border:#30363d;--text:#e6edf3;"
  "--muted:#8b949e;--accent:#2f81f7;--action:#1f6feb}"
  "*{box-sizing:border-box}"
  "body{font-family:-apple-system,BlinkMacSystemFont,system-ui,sans-serif;"
  "max-width:520px;margin:0 auto;padding:0;background:var(--bg);"
  "color:var(--text);min-height:100vh}"
  "main{padding:16px}"
  "h1{font-size:20px;margin:8px 0 4px}"
  "h2{font-size:11px;margin:22px 0 8px;font-weight:600;color:var(--muted);"
  "text-transform:uppercase;letter-spacing:1.5px}"
  "table{width:100%;border-collapse:separate;border-spacing:0;"
  "background:var(--card);border:1px solid var(--border);border-radius:8px;"
  "overflow:hidden}"
  "th{padding:9px 12px;background:#1c2128;font-size:10px;"
  "text-transform:uppercase;letter-spacing:1.2px;color:var(--muted);"
  "text-align:left;font-weight:600}"
  "td{padding:10px 12px;border-top:1px solid var(--border)}"
  "input,button,select{padding:9px 11px;font-size:15px;border-radius:6px;"
  "border:1px solid var(--border);background:#0d1117;color:var(--text);"
  "font-family:inherit}"
  "input:focus,select:focus{outline:none;border-color:var(--accent)}"
  "button{background:var(--action);border-color:var(--action);"
  "cursor:pointer;font-weight:600;color:white}"
  "button.del{background:#c0392b;border-color:#c0392b;padding:6px 10px}"
  "form.inline{margin:0}"
  "form.add{display:flex;gap:8px;margin-top:8px;flex-wrap:wrap}"
  "form.add input,form.add select{min-width:0}"
  "form.add button{flex-shrink:0}"
  ".empty{opacity:.55;font-style:italic;text-align:center}"
  ".banner{background:#2d0e0e;border:1px solid #8c1a1a;color:#ffb3b3;"
  "padding:10px 14px;border-radius:6px;margin-top:14px;font-size:14px}"
  "label{display:block;margin:18px 0 6px;font-size:11px;color:var(--muted);"
  "text-transform:uppercase;letter-spacing:1.2px;font-weight:600}"
  ".set input,.set select{width:100%}"
  ".set button{width:100%;margin-top:18px;padding:11px}"
  ".hint{font-size:12px;color:var(--muted);margin-top:6px;line-height:1.5}"
  ".hint code{background:var(--card);padding:1px 5px;border-radius:3px;"
  "font-size:11px;color:var(--accent)}"
  "</style></head><body><main>";

inline const char PAGE_FOOT_P[] PROGMEM = "</main></body></html>";

inline String html_escape(const String& s) {
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    switch (c) {
      case '&': out += F("&amp;"); break;
      case '<': out += F("&lt;"); break;
      case '>': out += F("&gt;"); break;
      case '"': out += F("&quot;"); break;
      case '\'': out += F("&#39;"); break;
      default: out += c; break;
    }
  }
  return out;
}

inline String accounts_csv(const std::vector<AccountRef>& accts) {
  String out;
  for (size_t i = 0; i < accts.size(); ++i) {
    if (i) out += ',';
    out += accts[i].platform + "|" + accts[i].handle;
  }
  return out;
}
inline String accounts_csv(const SettingsStore& s) {
  return accounts_csv(s.accounts());
}

inline const char* const kRangeTokens[] = {"1w", "1mo", "3mo", "1y", "max"};
inline const char* const kRangeLabels[] = {"1 week", "1 month", "3 months",
                                           "1 year", "All time"};

template <typename Writer>
void write_settings_page(Writer& response, SettingsStore& settings,
                         PlatformStore& platforms) {
  auto plats = platforms.snapshot();
  auto accts = settings.accounts();
  String primary = settings.primaryAccount();
  String range = settings.listRange();

  response.print(PAGE_HEAD_P);
  response.print(F("<h1>Follower Tracker</h1>"));

  if (settings.apiKey().length() == 0) {
    response.print(F("<div class=banner>No API token set.</div>"));
  }

  // --- General settings -----------------------------------------------------
  response.print(F("<form class=set method='POST' action='/settings'>"));
  response.print(F("<label>Refresh interval</label><input name=refresh_s type=number min='"));
  response.print(String(cfg::MIN_REFRESH_SECONDS));
  response.print(F("' value='"));
  response.print(String(settings.refreshSeconds()));
  response.print(F("' required><div class=hint>Seconds between refreshes "
                   "(server caches ~10 min).</div>"));

  response.print(F("<label>API token</label><input name=api_key value='"));
  response.print(html_escape(settings.apiKey()));
  response.print(F("' placeholder='paste your bearer token'>"));

  response.print(F("<label>List window (delta shown per row)</label><select name=list_range>"));
  for (int i = 0; i < 5; ++i) {
    response.print(F("<option value='"));
    response.print(kRangeTokens[i]);
    response.print(F("'"));
    if (range == kRangeTokens[i]) response.print(F(" selected"));
    response.print(F(">"));
    response.print(kRangeLabels[i]);
    response.print(F("</option>"));
  }
  response.print(F("</select>"));

  response.print(F("<label>Primary account (hero view)</label><select name=primary>"));
  response.print(F("<option value=''"));
  if (!primary.length()) response.print(F(" selected"));
  response.print(F(">None</option>"));
  for (const auto& a : accts) {
    String key = a.platform + "|" + a.handle;
    response.print(F("<option value='"));
    response.print(html_escape(key));
    response.print(F("'"));
    if (primary == key) response.print(F(" selected"));
    response.print(F(">"));
    response.print(html_escape(key));
    response.print(F("</option>"));
  }
  response.print(F("</select>"));
  response.print(F("<button type=submit>Save settings</button></form>"));

  // --- Tracked accounts -----------------------------------------------------
  response.print(F("<h2>Tracked accounts</h2><div style='overflow-x:auto'>"
                   "<table><tr><th>Platform</th><th>Handle</th><th></th></tr>"));
  if (accts.empty()) {
    response.print(F("<tr><td colspan=3 class=empty>No accounts yet &mdash; add one below.</td></tr>"));
  } else {
    for (size_t i = 0; i < accts.size(); ++i) {
      response.print(F("<tr><td>"));
      response.print(html_escape(platforms.name(accts[i].platform)));
      response.print(F("</td><td>"));
      response.print(html_escape(accts[i].handle));
      response.print(F("</td><td><form class=inline method='POST' action='/delete'>"
                       "<input type=hidden name=i value='"));
      response.print(String((unsigned)i));
      response.print(F("'><button class=del type=submit>&times;</button></form></td></tr>"));
    }
  }
  response.print(F("</table></div>"));

  // --- Add account: platform dropdown populated from GET /platforms ---------
  response.print(F("<h2>Add account</h2>"
                   "<form class=add method='POST' action='/add'>"
                   "<select name=platform required>"));
  if (plats.empty()) {
    response.print(F("<option value=''>(no platforms discovered yet)</option>"));
  } else {
    for (const auto& p : plats) {
      response.print(F("<option value='"));
      response.print(html_escape(p.id));
      response.print(F("'>"));
      response.print(html_escape(p.name.length() ? p.name : p.id));
      response.print(F("</option>"));
    }
  }
  response.print(F("</select>"
                   "<input name=handle placeholder='@handle or id' maxlength=64 required>"
                   "<button type=submit>Add</button></form>"));
  if (!plats.empty()) {
    response.print(F("<div class=hint>Platforms come from the server's "
                     "<code>/platforms</code> endpoint &mdash; new ones appear "
                     "here automatically, no reflash.</div>"));
  }

  response.print(PAGE_FOOT_P);
}

}  // namespace web_admin_page
