#include "hero_screen.h"

#include <lvgl.h>

#include "../config.h"
#include "../net/account_store.h"
#include "../net/platform_store.h"
#include "../util/format.h"
#include "icons.h"
#include "list_screen.h"
#include "styles.h"

namespace {

AccountStore*  g_store     = nullptr;
PlatformStore* g_platforms = nullptr;
String         g_key;

lv_obj_t* g_scr      = nullptr;
lv_obj_t* g_icon_box = nullptr;
lv_obj_t* g_name     = nullptr;
lv_obj_t* g_count    = nullptr;
lv_obj_t* g_gain     = nullptr;
lv_obj_t* g_ms_bar   = nullptr;

long long next_milestone(long long n, long long& prev_out) {
  if (n < 10) { prev_out = 0; return 10; }
  long long scale = 1, v = n;
  while (v >= 10) { v /= 10; scale *= 10; }
  long long prev = (n / scale) * scale;
  prev_out = prev;
  return prev + scale;
}

void on_back(lv_event_t*) { lv_screen_load(list_screen::screen()); }

void build() {
  g_scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(g_scr, styles::bg_color(), 0);
  lv_obj_set_style_bg_opa(g_scr, LV_OPA_COVER, 0);
  lv_obj_set_flex_flow(g_scr, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(g_scr, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(g_scr, 6, 0);
  lv_obj_clear_flag(g_scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(g_scr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(g_scr, on_back, LV_EVENT_CLICKED, nullptr);

  g_icon_box = lv_obj_create(g_scr);
  lv_obj_remove_style_all(g_icon_box);
  lv_obj_set_size(g_icon_box, 56, 56);
  lv_obj_add_flag(g_icon_box, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_clear_flag(g_icon_box, LV_OBJ_FLAG_SCROLLABLE);

  g_name = lv_label_create(g_scr);
  lv_obj_add_style(g_name, &styles::name_big, 0);

  g_count = lv_label_create(g_scr);
  lv_obj_add_style(g_count, &styles::count_big, 0);

  g_gain = lv_label_create(g_scr);
  lv_obj_add_style(g_gain, &styles::delta_up, 0);

  g_ms_bar = lv_bar_create(g_scr);
  lv_obj_set_size(g_ms_bar, cfg::SCREEN_W - 60, 8);
  lv_obj_set_style_bg_color(g_ms_bar, styles::card_color(), LV_PART_MAIN);
  lv_obj_set_style_bg_color(g_ms_bar, styles::up_color(), LV_PART_INDICATOR);
}

void render() {
  Account found;
  bool ok = false;
  if (g_store) {
    for (const auto& a : g_store->snapshot()) {
      if (a.key() == g_key) { found = a; ok = true; break; }
    }
  }
  String color, pName = found.platform;
  if (g_platforms) {
    PlatformMeta m;
    if (g_platforms->find(found.platform, m)) {
      color = m.color;
      if (m.name.length()) pName = m.name;
    }
  }
  lv_obj_clean(g_icon_box);
  lv_obj_t* icon = icons::make(g_icon_box, found.platform, pName, color, 56);
  lv_obj_center(icon);
  lv_obj_add_flag(icon, LV_OBJ_FLAG_EVENT_BUBBLE);

  lv_label_set_text(g_name, ok ? (found.displayName.length()
                                      ? found.displayName : found.handle).c_str()
                               : "—");
  lv_label_set_text(g_count, ok ? fmt::compact(found.followers).c_str() : "—");

  bool up = found.delta >= 0;
  lv_label_set_text(g_gain,
      (String(up ? LV_SYMBOL_UP : LV_SYMBOL_DOWN) + " " + fmt::delta(found.delta)).c_str());
  lv_obj_remove_style(g_gain, &styles::delta_up, 0);
  lv_obj_remove_style(g_gain, &styles::delta_dn, 0);
  lv_obj_add_style(g_gain, up ? &styles::delta_up : &styles::delta_dn, 0);

  int pct = 0;
  if (ok && found.followers > 0) {
    long long prev = 0, ms = next_milestone(found.followers, prev);
    if (ms > prev) pct = (int)(((found.followers - prev) * 100) / (ms - prev));
  }
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  lv_bar_set_value(g_ms_bar, pct, LV_ANIM_OFF);
}

}  // namespace

namespace hero_screen {

void show(AccountStore* store, PlatformStore* platforms, const String& key) {
  g_store     = store;
  g_platforms = platforms;
  g_key       = key;
  if (!g_scr) build();
  render();
  lv_screen_load(g_scr);
}

void tick() {
  if (!active()) return;
  render();
}

bool active() { return g_scr && lv_screen_active() == g_scr; }

}  // namespace hero_screen
