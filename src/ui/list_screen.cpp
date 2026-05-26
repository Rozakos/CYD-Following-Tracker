#include "list_screen.h"

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "../config.h"
#include "../net/account_store.h"
#include "../net/platform_store.h"
#include "../settings/settings_store.h"
#include "../util/area_fill.h"
#include "../util/format.h"
#include "../util/interpolate.h"
#include "detail_screen.h"
#include "icons.h"
#include "settings_screen.h"
#include "styles.h"

namespace {

constexpr uint16_t STATUS_H     = 22;
constexpr uint16_t ROW_H        = 50;
constexpr uint16_t ROW_GAP      = 4;
constexpr uint16_t ICON_SIZE    = 38;
constexpr uint16_t SPARK_W      = 70;
constexpr uint16_t SPARK_H      = 28;
constexpr uint16_t VISIBLE_ROWS = (cfg::SCREEN_H - STATUS_H) / (ROW_H + ROW_GAP);
constexpr int      SPARK_FACTOR  = 3;
constexpr int      SPARK_MAX_OUT = (cfg::SPARKLINE_POINTS - 1) * SPARK_FACTOR + 1;

AccountStore*  g_store     = nullptr;
SettingsStore* g_settings  = nullptr;
PlatformStore* g_platforms = nullptr;

lv_obj_t*   g_scr       = nullptr;
lv_obj_t*   g_list      = nullptr;
lv_obj_t*   g_wifi_icon = nullptr;
lv_obj_t*   g_updated   = nullptr;
lv_obj_t*   g_gear      = nullptr;
lv_timer_t* g_rot_timer = nullptr;

struct Row {
  lv_obj_t* obj;
  lv_obj_t* icon;
  lv_obj_t* name;
  lv_obj_t* sub;
  lv_obj_t* count;
  lv_obj_t* delta;
  lv_obj_t* spark;
  lv_point_precise_t spark_pts[SPARK_MAX_OUT];
  int32_t   spark_xs[SPARK_MAX_OUT];
  int32_t   spark_ys[SPARK_MAX_OUT];
  int       spark_n   = 0;
  lv_color_t accent   = lv_color_hex(0x8a98ad);
  uint32_t  icon_sig  = 0;
  String    platform;
  String    handle;
  String    key;
};
std::vector<Row> g_rows;

void spark_fill_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN_BEGIN) return;
  lv_obj_t* spark = (lv_obj_t*)lv_event_get_target(e);
  if (!spark) return;
  lv_layer_t* layer = lv_event_get_layer(e);
  if (!layer) return;

  size_t idx = (size_t)(uintptr_t)lv_obj_get_user_data(spark);
  if (idx >= g_rows.size()) return;
  const Row& r = g_rows[idx];
  if (r.spark_n < 2) return;

  lv_area_t coords;
  lv_obj_get_coords(spark, &coords);
  util::draw_polyline_fill(layer, r.spark_xs, r.spark_ys, r.spark_n,
                           coords.x1, coords.y1,
                           SPARK_H - 1, r.accent, LV_OPA_50);
}

void open_detail_async(void* user_data) {
  char* key = static_cast<char*>(user_data);
  if (!key) return;
  String k(key);
  int bar = k.indexOf('|');
  if (bar <= 0) return;
  detail_screen::show(g_store, g_platforms,
                      k.substring(0, bar), k.substring(bar + 1));
}

// LVGL fires LONG_PRESSED then CLICKED for one long-press gesture; swallow
// the click that follows a pin toggle.
bool g_swallow_next_click = false;
bool g_favourites_dirty   = false;

void on_row_click(lv_event_t* e) {
  if (g_swallow_next_click) {
    g_swallow_next_click = false;
    return;
  }
  char* key = static_cast<char*>(lv_event_get_user_data(e));
  if (!key) return;
  lv_async_call(open_detail_async, (void*)key);
}

void on_row_long_press(lv_event_t* e) {
  char* key = static_cast<char*>(lv_event_get_user_data(e));
  if (!key || !g_settings) return;
  g_settings->toggleFavourite(String(key));
  g_swallow_next_click = true;
  g_favourites_dirty   = true;
}

Row make_row(lv_obj_t* parent, const Account& a) {
  Row r{};
  r.platform = a.platform;
  r.handle   = a.handle;
  r.key      = a.key();

  String pColor, pName = a.platform;
  if (g_platforms) {
    PlatformMeta m;
    if (g_platforms->find(a.platform, m)) {
      pColor = m.color;
      if (m.name.length()) pName = m.name;
    }
  }

  r.obj = lv_obj_create(parent);
  lv_obj_remove_style_all(r.obj);
  lv_obj_add_style(r.obj, &styles::row, LV_STATE_DEFAULT);
  lv_obj_add_style(r.obj, &styles::row_pressed, LV_STATE_PRESSED);
  lv_obj_set_size(r.obj, cfg::SCREEN_W - 12, ROW_H);
  lv_obj_set_flex_flow(r.obj, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r.obj, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(r.obj, 6, 0);
  lv_obj_set_scrollbar_mode(r.obj, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(r.obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(r.obj, LV_OBJ_FLAG_CLICKABLE);

  r.icon     = icons::make(r.obj, a.platform, pName, pColor, ICON_SIZE);
  r.icon_sig = icons::signature(a.platform);
  lv_obj_add_flag(r.icon, LV_OBJ_FLAG_EVENT_BUBBLE);

  // Name column: display name (or handle) over the @handle / platform line.
  lv_obj_t* name_col = lv_obj_create(r.obj);
  lv_obj_remove_style_all(name_col);
  lv_obj_set_height(name_col, ROW_H - 8);
  lv_obj_set_flex_grow(name_col, 1);
  lv_obj_set_flex_flow(name_col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(name_col, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_clear_flag(name_col, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(name_col, LV_OBJ_FLAG_EVENT_BUBBLE);

  r.name = lv_label_create(name_col);
  lv_obj_add_style(r.name, &styles::name, 0);
  lv_label_set_long_mode(r.name, LV_LABEL_LONG_DOT);
  lv_obj_set_width(r.name, LV_PCT(100));
  lv_obj_set_height(r.name, 22);   // one line: dot-truncate, never wrap
  bool fav = g_settings && g_settings->isFavourite(a.key());
  String label = a.displayName.length() ? a.displayName : a.handle;
  lv_label_set_text(r.name, (fav ? String("* ") + label : label).c_str());

  r.sub = lv_label_create(name_col);
  lv_obj_add_style(r.sub, &styles::muted, 0);
  lv_obj_set_width(r.sub, LV_PCT(100));
  lv_obj_set_height(r.sub, 14);
  lv_label_set_long_mode(r.sub, LV_LABEL_LONG_DOT);
  lv_label_set_text(r.sub, a.handle.c_str());

  r.spark = lv_line_create(r.obj);
  lv_obj_set_size(r.spark, SPARK_W, SPARK_H);
  lv_obj_set_style_line_width(r.spark, 2, 0);
  lv_obj_set_style_line_rounded(r.spark, true, 0);
  lv_obj_set_style_line_color(r.spark, styles::muted_color(), 0);
  lv_obj_add_flag(r.spark, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_event_cb(r.spark, spark_fill_cb, LV_EVENT_DRAW_MAIN_BEGIN, nullptr);

  // Right column: follower count over the absolute window delta.
  lv_obj_t* right_col = lv_obj_create(r.obj);
  lv_obj_remove_style_all(right_col);
  lv_obj_set_size(right_col, 78, ROW_H - 8);
  lv_obj_set_flex_flow(right_col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(right_col, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_clear_flag(right_col, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(right_col, LV_OBJ_FLAG_EVENT_BUBBLE);

  r.count = lv_label_create(right_col);
  lv_obj_add_style(r.count, &styles::count, 0);
  lv_label_set_text(r.count, "—");

  r.delta = lv_label_create(right_col);
  lv_obj_add_style(r.delta, &styles::muted, 0);
  lv_label_set_text(r.delta, "—");

  char* heapKey = strdup(r.key.c_str());
  lv_obj_add_event_cb(r.obj, on_row_click,      LV_EVENT_CLICKED,      heapKey);
  lv_obj_add_event_cb(r.obj, on_row_long_press, LV_EVENT_LONG_PRESSED, heapKey);
  return r;
}

void rebuild_icon(Row& r) {
  uint32_t want = icons::signature(r.platform);
  if (r.icon && want == r.icon_sig) return;
  String pColor, pName = r.platform;
  if (g_platforms) {
    PlatformMeta m;
    if (g_platforms->find(r.platform, m)) {
      pColor = m.color;
      if (m.name.length()) pName = m.name;
    }
  }
  if (r.icon) lv_obj_delete(r.icon);
  r.icon     = icons::make(r.obj, r.platform, pName, pColor, ICON_SIZE);
  r.icon_sig = want;
  lv_obj_add_flag(r.icon, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_move_to_index(r.icon, 0);
}

void update_spark(Row& r, const std::vector<float>& vals, bool up) {
  if (vals.size() < 2) {
    r.spark_n = 0;
    lv_line_set_points(r.spark, r.spark_pts, 0);
    lv_obj_invalidate(r.spark);
    return;
  }
  size_t n_in = vals.size();
  if (n_in > cfg::SPARKLINE_POINTS) n_in = cfg::SPARKLINE_POINTS;
  float lo = vals.back(), hi = vals.back();
  for (size_t i = vals.size() - n_in; i < vals.size(); ++i) {
    if (vals[i] < lo) lo = vals[i];
    if (vals[i] > hi) hi = vals[i];
  }
  float span = hi - lo;
  if (span < 0.0001f) span = 1.0f;

  int out_n = (int)(n_in - 1) * SPARK_FACTOR + 1;
  if (out_n > SPARK_MAX_OUT) out_n = SPARK_MAX_OUT;
  static float smooth[SPARK_MAX_OUT];
  util::monotone_cubic_interpolate(&vals[vals.size() - n_in], n_in,
                                   smooth, (size_t)out_n, SPARK_FACTOR);

  const lv_coord_t W = SPARK_W - 2;
  const lv_coord_t H = SPARK_H - 4;
  for (int i = 0; i < out_n; ++i) {
    float v = smooth[i];
    float t = (out_n == 1) ? 0.5f : (float)i / (float)(out_n - 1);
    float x = 1.0f + t * (float)W;
    float y = 2.0f + (1.0f - (v - lo) / span) * (float)H;
    r.spark_pts[i].x = (lv_value_precise_t)x;
    r.spark_pts[i].y = (lv_value_precise_t)y;
    r.spark_xs[i] = (int32_t)(x + 0.5f);
    r.spark_ys[i] = (int32_t)(y + 0.5f);
  }
  r.spark_n = out_n;
  r.accent  = up ? styles::up_color() : styles::dn_color();
  lv_line_set_points(r.spark, r.spark_pts, out_n);
  lv_obj_set_style_line_color(r.spark, r.accent, 0);
  lv_obj_invalidate(r.spark);
}

void rebuild_rows(const std::vector<Account>& in) {
  // Stable-partition favourites (pinned) to the top.
  std::vector<Account> accts;
  accts.reserve(in.size());
  if (g_settings) {
    for (const auto& a : in)
      if (g_settings->isFavourite(a.key())) accts.push_back(a);
    for (const auto& a : in)
      if (!g_settings->isFavourite(a.key())) accts.push_back(a);
  } else {
    accts = in;
  }

  bool same = accts.size() == g_rows.size();
  for (size_t i = 0; same && i < accts.size(); ++i) {
    if (accts[i].key() != g_rows[i].key) same = false;
  }
  if (!same) {
    lv_obj_clean(g_list);
    g_rows.clear();
    icons::clearRuntimeCache();
    g_rows.reserve(accts.size());
    for (const auto& a : accts) g_rows.push_back(make_row(g_list, a));
    for (size_t i = 0; i < g_rows.size(); ++i) {
      lv_obj_set_user_data(g_rows[i].spark, (void*)(uintptr_t)i);
    }
  } else {
    for (auto& r : g_rows) rebuild_icon(r);
  }

  for (size_t i = 0; i < accts.size(); ++i) {
    const auto& a = accts[i];
    Row& r = g_rows[i];
    if (!a.fresh) {
      lv_label_set_text(r.count, "—");
      lv_label_set_text(r.delta, "—");
      lv_obj_remove_style(r.delta, &styles::delta_up, 0);
      lv_obj_remove_style(r.delta, &styles::delta_dn, 0);
      lv_obj_add_style(r.delta, &styles::muted, 0);
      lv_line_set_points(r.spark, r.spark_pts, 0);
      continue;
    }
    lv_label_set_text(r.count, fmt::compact(a.followers).c_str());

    bool up = a.delta >= 0;
    String d = String(up ? LV_SYMBOL_UP : LV_SYMBOL_DOWN) + " " + fmt::delta(a.delta);
    lv_label_set_text(r.delta, d.c_str());
    lv_obj_remove_style(r.delta, &styles::muted, 0);
    lv_obj_remove_style(r.delta, &styles::delta_up, 0);
    lv_obj_remove_style(r.delta, &styles::delta_dn, 0);
    lv_obj_add_style(r.delta, up ? &styles::delta_up : &styles::delta_dn, 0);
    update_spark(r, a.sparkline, up);
  }
}

void refresh_wifi() {
  if (WiFi.status() == WL_CONNECTED) {
    char buf[24];
    snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI " %ld", (long)WiFi.RSSI());
    lv_label_set_text(g_wifi_icon, buf);
    lv_obj_set_style_text_color(g_wifi_icon, styles::up_color(), 0);
  } else {
    lv_label_set_text(g_wifi_icon, LV_SYMBOL_CLOSE " no link");
    lv_obj_set_style_text_color(g_wifi_icon, styles::dn_color(), 0);
  }
}

void update_status(time_t lastUpdate) {
  refresh_wifi();
  if (lastUpdate == 0) {
    lv_label_set_text(g_updated, "—");
  } else {
    struct tm t;
    localtime_r(&lastUpdate, &t);
    char buf[24];
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    lv_label_set_text(g_updated, buf);
  }
}

void on_gear_click(lv_event_t*) { settings_screen::show(); }

void on_wifi_reset_confirm(lv_event_t* e) {
  SettingsStore* s = static_cast<SettingsStore*>(lv_event_get_user_data(e));
  if (s) s->setWifi("", "");
  delay(150);
  ESP.restart();
}

void on_wifi_reset_cancel(lv_event_t* e) {
  auto* btn  = static_cast<lv_obj_t*>(lv_event_get_target(e));
  auto* mbox = lv_obj_get_parent(lv_obj_get_parent(btn));
  lv_msgbox_close(mbox);
}

void on_wifi_click(lv_event_t* e) {
  SettingsStore* s = static_cast<SettingsStore*>(lv_event_get_user_data(e));
  lv_obj_t* mbox = lv_msgbox_create(nullptr);
  lv_msgbox_add_title(mbox, "Reset WiFi");
  lv_msgbox_add_text(mbox,
      "Forget the saved network and reboot into setup mode (QR code)?");
  lv_obj_t* cancel = lv_msgbox_add_footer_button(mbox, "Cancel");
  lv_obj_add_event_cb(cancel, on_wifi_reset_cancel, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* reset = lv_msgbox_add_footer_button(mbox, "Reset");
  lv_obj_add_event_cb(reset, on_wifi_reset_confirm, LV_EVENT_CLICKED, s);
}

void rotate_cb(lv_timer_t*) {
  if (g_rows.size() <= VISIBLE_ROWS) return;
  lv_coord_t y = lv_obj_get_scroll_y(g_list);
  lv_coord_t max = lv_obj_get_scroll_bottom(g_list) + y;
  lv_coord_t next = y + (ROW_H + ROW_GAP);
  if (next >= max) next = 0;
  lv_obj_scroll_to_y(g_list, next, LV_ANIM_ON);
}

}  // namespace

namespace list_screen {

void build(AccountStore* store, SettingsStore* settings, PlatformStore* platforms) {
  g_store     = store;
  g_settings  = settings;
  g_platforms = platforms;
  g_scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(g_scr, styles::bg_color(), 0);
  lv_obj_set_style_bg_opa(g_scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(g_scr, 0, 0);
  lv_obj_clear_flag(g_scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* bar = lv_obj_create(g_scr);
  lv_obj_remove_style_all(bar);
  lv_obj_add_style(bar, &styles::status_bar, 0);
  lv_obj_set_size(bar, cfg::SCREEN_W, STATUS_H);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  g_wifi_icon = lv_label_create(bar);
  lv_obj_add_style(g_wifi_icon, &styles::status_text, 0);
  lv_label_set_text(g_wifi_icon, LV_SYMBOL_CLOSE " no link");
  lv_obj_set_style_text_color(g_wifi_icon, styles::dn_color(), 0);
  lv_obj_set_style_pad_hor(g_wifi_icon, 6, 0);
  lv_obj_set_style_pad_ver(g_wifi_icon, 2, 0);
  lv_obj_add_flag(g_wifi_icon, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(g_wifi_icon, 14);
  lv_obj_add_event_cb(g_wifi_icon, on_wifi_click, LV_EVENT_CLICKED, g_settings);

  lv_obj_t* title = lv_label_create(bar);
  lv_obj_add_style(title, &styles::status_text, 0);
  lv_label_set_text(title, "FOLLOWERS");

  g_updated = lv_label_create(bar);
  lv_obj_add_style(g_updated, &styles::status_text, 0);
  lv_label_set_text(g_updated, "—");

  g_gear = lv_label_create(bar);
  lv_obj_add_style(g_gear, &styles::status_text, 0);
  lv_label_set_text(g_gear, LV_SYMBOL_SETTINGS);
  lv_obj_set_style_pad_hor(g_gear, 6, 0);
  lv_obj_set_style_pad_ver(g_gear, 2, 0);
  lv_obj_add_flag(g_gear, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(g_gear, 14);
  lv_obj_add_event_cb(g_gear, on_gear_click, LV_EVENT_CLICKED, nullptr);

  g_list = lv_obj_create(g_scr);
  lv_obj_remove_style_all(g_list);
  lv_obj_set_size(g_list, cfg::SCREEN_W, cfg::SCREEN_H - STATUS_H);
  lv_obj_align(g_list, LV_ALIGN_TOP_MID, 0, STATUS_H);
  lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(g_list, ROW_GAP, 0);
  lv_obj_set_style_pad_ver(g_list, ROW_GAP, 0);
  lv_obj_set_style_bg_color(g_list, styles::bg_color(), 0);
  lv_obj_set_style_bg_opa(g_list, LV_OPA_COVER, 0);
  lv_obj_set_scroll_dir(g_list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(g_list, LV_SCROLLBAR_MODE_OFF);

  g_rot_timer = lv_timer_create(rotate_cb, 4000, nullptr);
}

lv_obj_t* screen() { return g_scr; }

void tick() {
  if (!g_store) return;
  static time_t lastSeen = 0;
  auto accts = g_store->snapshot();
  time_t lu = g_store->lastUpdate();
  if (g_favourites_dirty) {
    lv_obj_clean(g_list);
    g_rows.clear();
    icons::clearRuntimeCache();
    g_favourites_dirty = false;
    lastSeen = 0;  // force rebuild below
  }
  if (lu != lastSeen || g_rows.size() != accts.size()) {
    rebuild_rows(accts);
    update_status(lu);
    lastSeen = lu;
  } else {
    refresh_wifi();
  }
}

}  // namespace list_screen
