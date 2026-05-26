#include "settings_screen.h"

#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>

#include "../config.h"
#include "../net/wifi_mgr.h"
#include "../settings/settings_store.h"
#include "list_screen.h"
#include "styles.h"

namespace {

SettingsStore* g_settings = nullptr;
lv_obj_t*      g_scr      = nullptr;
lv_obj_t*      g_body     = nullptr;

void on_back(lv_event_t*) {
  lv_screen_load(list_screen::screen());
}

lv_obj_t* add_line(const char* k, const String& v) {
  lv_obj_t* row = lv_obj_create(g_body);
  lv_obj_remove_style_all(row);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_ver(row, 2, 0);

  lv_obj_t* key = lv_label_create(row);
  lv_obj_add_style(key, &styles::muted, 0);
  lv_label_set_text(key, k);

  lv_obj_t* val = lv_label_create(row);
  lv_obj_add_style(val, &styles::status_text, 0);
  lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);
  lv_obj_set_width(val, LV_PCT(60));
  lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_text(val, v.c_str());
  return val;
}

void build() {
  g_scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(g_scr, styles::bg_color(), 0);
  lv_obj_set_style_bg_opa(g_scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(g_scr, 0, 0);
  lv_obj_clear_flag(g_scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* bar = lv_obj_create(g_scr);
  lv_obj_remove_style_all(bar);
  lv_obj_add_style(bar, &styles::status_bar, 0);
  lv_obj_set_size(bar, cfg::SCREEN_W, 22);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_add_flag(bar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(bar, on_back, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* back = lv_label_create(bar);
  lv_obj_add_style(back, &styles::status_text, 0);
  lv_label_set_text(back, LV_SYMBOL_LEFT " Settings");

  g_body = lv_obj_create(g_scr);
  lv_obj_remove_style_all(g_body);
  lv_obj_set_size(g_body, cfg::SCREEN_W, cfg::SCREEN_H - 22);
  lv_obj_align(g_body, LV_ALIGN_TOP_MID, 0, 22);
  lv_obj_set_flex_flow(g_body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(g_body, 10, 0);
  lv_obj_set_style_pad_row(g_body, 2, 0);
  lv_obj_set_scrollbar_mode(g_body, LV_SCROLLBAR_MODE_AUTO);
}

void populate() {
  lv_obj_clean(g_body);
  bool sta = wifi_mgr::connected();
  add_line("Network", sta ? (LV_SYMBOL_WIFI " " + WiFi.SSID()) : String("offline"));
  add_line("IP", wifi_mgr::ip());
  if (g_settings) {
    add_line("Refresh", String((long)g_settings->refreshSeconds()) + " s");
    add_line("Accounts", String((long)g_settings->accounts().size()));
    add_line("API token", g_settings->apiKey().length() ? "set" : "missing");
    String range = g_settings->listRange();
    add_line("List window", range);
    String primary = g_settings->primaryAccount();
    add_line("Primary", primary.length() ? primary : String("none"));
  }
  add_line("Web admin", String("http://") + wifi_mgr::ip() + "/settings");
}

}  // namespace

namespace settings_screen {

void init(SettingsStore* settings) { g_settings = settings; }

void show() {
  if (!g_scr) build();
  populate();
  lv_screen_load(g_scr);
}

void tick() {}

bool active() { return g_scr && lv_screen_active() == g_scr; }

}  // namespace settings_screen
