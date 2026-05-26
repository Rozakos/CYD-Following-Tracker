#pragma once

#include <Arduino.h>
#include <lvgl.h>

namespace styles {

extern lv_style_t row;
extern lv_style_t row_pressed;
extern lv_style_t name;        // display name / handle (list row, medium)
extern lv_style_t name_big;    // detail / hero header
extern lv_style_t count;       // follower count (list row)
extern lv_style_t count_big;   // follower count (detail / hero)
extern lv_style_t delta_up;    // positive growth (green)
extern lv_style_t delta_dn;    // negative growth (red)
extern lv_style_t muted;
extern lv_style_t status_bar;
extern lv_style_t status_text;
extern lv_style_t card;
extern lv_style_t badge;
extern lv_style_t badge_text;

void init();

lv_color_t up_color();
lv_color_t dn_color();
lv_color_t bg_color();
lv_color_t card_color();
lv_color_t muted_color();

// Brand color for a platform. Prefers the server-provided hex (e.g. the
// "#FF0000" from GET /platforms metadata); when that's empty or malformed,
// derives a stable color from a FNV-1a hash of the platform id so a
// brand-new platform still looks coherent the moment it's added — no
// firmware change, no hardcoded per-platform table.
lv_color_t platform_color(const String& platform_id, const String& hex_hint);

}  // namespace styles
