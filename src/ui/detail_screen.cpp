#include "detail_screen.h"

#include <Arduino.h>
#include <lvgl.h>
#include <math.h>
#include <time.h>

#include "../config.h"
#include "../net/account_store.h"
#include "../net/platform_store.h"
#include "../util/area_fill.h"
#include "../util/format.h"
#include "../util/interpolate.h"
#include "icons.h"
#include "list_screen.h"
#include "styles.h"

namespace {

constexpr int DETAIL_FACTOR  = 4;
constexpr int DETAIL_MAX_OUT = (cfg::HISTORY_POINTS - 1) * DETAIL_FACTOR + 1;

// Growth-first ranges. No intraday "1D" — a day of follower data is flat.
const char* const kRanges[]      = {"1W", "1M", "3M", "1Y", "All"};
const char* const kRangeTokens[] = {"1w", "1mo", "3mo", "1y", "max"};
constexpr int kRangeCount = 5;

AccountStore*  g_store     = nullptr;
PlatformStore* g_platforms = nullptr;
String         g_platform;
String         g_handle;
int            g_range_idx = 1;   // default 1M
uint32_t       g_gen       = 0;
String         g_shown_range;     // range currently rendered on the chart

lv_obj_t* g_scr      = nullptr;
lv_obj_t* g_icon_box = nullptr;
lv_obj_t* g_name     = nullptr;
lv_obj_t* g_count    = nullptr;
lv_obj_t* g_delta    = nullptr;
lv_obj_t* g_chart    = nullptr;   // lv_line
lv_obj_t* g_ms_label = nullptr;
lv_obj_t* g_ms_bar   = nullptr;
lv_obj_t* g_buttons  = nullptr;   // lv_buttonmatrix

lv_point_precise_t g_pts[DETAIL_MAX_OUT];
int32_t  g_xs[DETAIL_MAX_OUT];
int32_t  g_ys[DETAIL_MAX_OUT];
int      g_n = 0;
lv_color_t g_accent = lv_color_hex(0x4ade80);

long long next_milestone(long long n, long long& prev_out) {
  if (n < 10) { prev_out = 0; return 10; }
  long long scale = 1;
  long long v = n;
  while (v >= 10) { v /= 10; scale *= 10; }   // scale = 10^(digits-1)
  long long prev = (n / scale) * scale;
  prev_out = prev;
  return prev + scale;
}

void chart_fill_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN_BEGIN) return;
  if (g_n < 2) return;
  lv_layer_t* layer = lv_event_get_layer(e);
  if (!layer) return;
  lv_area_t coords;
  lv_obj_get_coords(g_chart, &coords);
  int32_t h = coords.y2 - coords.y1;
  util::draw_polyline_fill(layer, g_xs, g_ys, g_n, coords.x1, coords.y1,
                           h, g_accent, LV_OPA_40);
}

void on_back(lv_event_t*) { lv_screen_load(list_screen::screen()); }

void request_current_range() {
  if (!g_store) return;
  g_gen = g_store->requestHistory(g_platform, g_handle,
                                  kRangeTokens[g_range_idx]);
}

void on_range(lv_event_t* e) {
  lv_obj_t* bm = (lv_obj_t*)lv_event_get_target(e);
  uint32_t id = lv_buttonmatrix_get_selected_button(bm);
  if (id == LV_BUTTONMATRIX_BUTTON_NONE || (int)id >= kRangeCount) return;
  g_range_idx = (int)id;
  request_current_range();
}

void render_header() {
  // Pull the live follower count + name from the account snapshot.
  String name = g_handle, color, pName = g_platform;
  long long followers = -1;
  if (g_platforms) {
    PlatformMeta m;
    if (g_platforms->find(g_platform, m)) {
      color = m.color;
      if (m.name.length()) pName = m.name;
    }
  }
  if (g_store) {
    for (const auto& a : g_store->snapshot()) {
      if (a.platform == g_platform && a.handle == g_handle) {
        followers = a.followers;
        if (a.displayName.length()) name = a.displayName;
        break;
      }
    }
  }

  lv_obj_clean(g_icon_box);
  lv_obj_t* icon = icons::make(g_icon_box, g_platform, pName, color, 48);
  lv_obj_center(icon);

  lv_label_set_text(g_name, name.c_str());
  if (followers >= 0) lv_label_set_text(g_count, fmt::compact(followers).c_str());
  else                lv_label_set_text(g_count, "—");
}

void render_history(const History& h) {
  if (h.values.size() < 2) {
    g_n = 0;
    lv_line_set_points(g_chart, g_pts, 0);
    lv_obj_invalidate(g_chart);
    lv_label_set_text(g_delta, "—");
    return;
  }
  size_t n_in = h.values.size();
  if (n_in > cfg::HISTORY_POINTS) n_in = cfg::HISTORY_POINTS;
  const float* src = &h.values[h.values.size() - n_in];

  float lo = src[0], hi = src[0];
  for (size_t i = 0; i < n_in; ++i) {
    if (src[i] < lo) lo = src[i];
    if (src[i] > hi) hi = src[i];
  }
  float span = hi - lo;
  if (span < 0.0001f) span = 1.0f;

  int out_n = (int)(n_in - 1) * DETAIL_FACTOR + 1;
  if (out_n > DETAIL_MAX_OUT) out_n = DETAIL_MAX_OUT;
  static float smooth[DETAIL_MAX_OUT];
  util::monotone_cubic_interpolate(src, n_in, smooth, (size_t)out_n, DETAIL_FACTOR);

  lv_coord_t W = lv_obj_get_width(g_chart);
  lv_coord_t H = lv_obj_get_height(g_chart);
  if (W <= 0) W = cfg::SCREEN_W - 20;
  if (H <= 0) H = 110;
  for (int i = 0; i < out_n; ++i) {
    float t = (out_n == 1) ? 0.5f : (float)i / (float)(out_n - 1);
    float x = t * (float)(W - 2) + 1.0f;
    float y = 2.0f + (1.0f - (smooth[i] - lo) / span) * (float)(H - 4);
    g_pts[i].x = (lv_value_precise_t)x;
    g_pts[i].y = (lv_value_precise_t)y;
    g_xs[i] = (int32_t)(x + 0.5f);
    g_ys[i] = (int32_t)(y + 0.5f);
  }
  g_n = out_n;

  // Delta over the displayed window: first -> last point.
  long long first = (long long)(src[0] + 0.5f);
  long long last  = (long long)(src[n_in - 1] + 0.5f);
  long long d = last - first;
  bool up = d >= 0;
  g_accent = up ? styles::up_color() : styles::dn_color();
  lv_line_set_points(g_chart, g_pts, out_n);
  lv_obj_set_style_line_color(g_chart, g_accent, 0);
  lv_obj_invalidate(g_chart);

  String dtxt = String(up ? LV_SYMBOL_UP : LV_SYMBOL_DOWN) + " " + fmt::delta(d);
  lv_label_set_text(g_delta, dtxt.c_str());
  lv_obj_remove_style(g_delta, &styles::delta_up, 0);
  lv_obj_remove_style(g_delta, &styles::delta_dn, 0);
  lv_obj_add_style(g_delta, up ? &styles::delta_up : &styles::delta_dn, 0);

  // Milestone block keyed off the latest follower value.
  long long prev = 0;
  long long ms = next_milestone(last, prev);
  String mtxt = fmt::compact(last) + "  " LV_SYMBOL_RIGHT "  " + fmt::compact(ms);
  lv_label_set_text(g_ms_label, mtxt.c_str());
  int pct = 0;
  if (ms > prev) pct = (int)(((last - prev) * 100) / (ms - prev));
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  lv_bar_set_value(g_ms_bar, pct, LV_ANIM_OFF);
}

void build() {
  g_scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(g_scr, styles::bg_color(), 0);
  lv_obj_set_style_bg_opa(g_scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(g_scr, 0, 0);
  lv_obj_clear_flag(g_scr, LV_OBJ_FLAG_SCROLLABLE);

  // Top bar (back).
  lv_obj_t* bar = lv_obj_create(g_scr);
  lv_obj_remove_style_all(bar);
  lv_obj_add_style(bar, &styles::status_bar, 0);
  lv_obj_set_size(bar, cfg::SCREEN_W, 22);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_add_flag(bar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(bar, on_back, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* back = lv_label_create(bar);
  lv_obj_add_style(back, &styles::status_text, 0);
  lv_label_set_text(back, LV_SYMBOL_LEFT " Back");
  lv_obj_center(back);

  // Header: icon + name/count/delta.
  lv_obj_t* head = lv_obj_create(g_scr);
  lv_obj_remove_style_all(head);
  lv_obj_set_size(head, cfg::SCREEN_W, 54);
  lv_obj_align(head, LV_ALIGN_TOP_MID, 0, 22);
  lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_hor(head, 10, 0);
  lv_obj_set_style_pad_column(head, 8, 0);
  lv_obj_clear_flag(head, LV_OBJ_FLAG_SCROLLABLE);

  g_icon_box = lv_obj_create(head);
  lv_obj_remove_style_all(g_icon_box);
  lv_obj_set_size(g_icon_box, 48, 48);
  lv_obj_clear_flag(g_icon_box, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* name_col = lv_obj_create(head);
  lv_obj_remove_style_all(name_col);
  lv_obj_set_height(name_col, 48);
  lv_obj_set_flex_grow(name_col, 1);
  lv_obj_set_flex_flow(name_col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(name_col, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_clear_flag(name_col, LV_OBJ_FLAG_SCROLLABLE);
  g_name = lv_label_create(name_col);
  lv_obj_add_style(g_name, &styles::name, 0);
  lv_label_set_long_mode(g_name, LV_LABEL_LONG_DOT);
  lv_obj_set_width(g_name, LV_PCT(100));
  g_delta = lv_label_create(name_col);
  lv_obj_add_style(g_delta, &styles::muted, 0);
  lv_label_set_text(g_delta, "—");

  g_count = lv_label_create(head);
  lv_obj_add_style(g_count, &styles::count_big, 0);
  lv_label_set_text(g_count, "—");

  // Chart.
  g_chart = lv_line_create(g_scr);
  lv_obj_set_size(g_chart, cfg::SCREEN_W - 20, 104);
  lv_obj_align(g_chart, LV_ALIGN_TOP_MID, 0, 80);
  lv_obj_set_style_line_width(g_chart, 2, 0);
  lv_obj_set_style_line_rounded(g_chart, true, 0);
  lv_obj_set_style_line_color(g_chart, styles::up_color(), 0);
  lv_obj_add_event_cb(g_chart, chart_fill_cb, LV_EVENT_DRAW_MAIN_BEGIN, nullptr);

  // Milestone block.
  g_ms_label = lv_label_create(g_scr);
  lv_obj_add_style(g_ms_label, &styles::muted, 0);
  lv_obj_align(g_ms_label, LV_ALIGN_TOP_MID, 0, 188);
  lv_label_set_text(g_ms_label, "");

  g_ms_bar = lv_bar_create(g_scr);
  lv_obj_set_size(g_ms_bar, cfg::SCREEN_W - 24, 6);
  lv_obj_align(g_ms_bar, LV_ALIGN_TOP_MID, 0, 204);
  lv_obj_set_style_bg_color(g_ms_bar, styles::card_color(), LV_PART_MAIN);
  lv_obj_set_style_bg_color(g_ms_bar, styles::up_color(), LV_PART_INDICATOR);
  lv_bar_set_value(g_ms_bar, 0, LV_ANIM_OFF);

  // Range buttons.
  static const char* map[] = {"1W", "1M", "3M", "1Y", "All", ""};
  g_buttons = lv_buttonmatrix_create(g_scr);
  lv_buttonmatrix_set_map(g_buttons, map);
  lv_buttonmatrix_set_one_checked(g_buttons, true);
  lv_obj_set_size(g_buttons, cfg::SCREEN_W, 32);
  lv_obj_align(g_buttons, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_opa(g_buttons, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(g_buttons, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(g_buttons, 2, LV_PART_MAIN);
  lv_obj_set_style_text_color(g_buttons, styles::muted_color(), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(g_buttons, styles::card_color(), LV_PART_ITEMS);
  lv_obj_set_style_text_color(g_buttons, lv_color_hex(0x0b0f17),
                              LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(g_buttons, styles::up_color(),
                            LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_add_event_cb(g_buttons, on_range, LV_EVENT_VALUE_CHANGED, nullptr);
}

}  // namespace

namespace detail_screen {

void show(AccountStore* store, PlatformStore* platforms,
          const String& platform, const String& handle) {
  g_store     = store;
  g_platforms = platforms;
  g_platform  = platform;
  g_handle    = handle;
  if (!g_scr) build();
  lv_buttonmatrix_set_button_ctrl(g_buttons, g_range_idx, LV_BUTTONMATRIX_CTRL_CHECKED);
  g_shown_range = "";
  render_header();
  lv_label_set_text(g_ms_label, "");
  g_n = 0;
  lv_line_set_points(g_chart, g_pts, 0);
  request_current_range();
  lv_screen_load(g_scr);
}

void tick() {
  if (!active() || !g_store) return;
  History h = g_store->history();
  if (h.key != (g_platform + "|" + g_handle)) return;
  if (h.range == g_shown_range) return;   // already rendered
  if (h.range != kRangeTokens[g_range_idx]) return;  // stale fetch
  g_shown_range = h.range;
  render_history(h);
}

bool active() { return g_scr && lv_screen_active() == g_scr; }

}  // namespace detail_screen
