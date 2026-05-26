#include "styles.h"

#include <cstring>

namespace styles {

lv_style_t row;
lv_style_t row_pressed;
lv_style_t name;
lv_style_t name_big;
lv_style_t count;
lv_style_t count_big;
lv_style_t delta_up;
lv_style_t delta_dn;
lv_style_t muted;
lv_style_t status_bar;
lv_style_t status_text;
lv_style_t card;
lv_style_t badge;
lv_style_t badge_text;

static lv_color_t c_up    = lv_color_hex(0x4ade80);
static lv_color_t c_dn    = lv_color_hex(0xf87171);
static lv_color_t c_bg    = lv_color_hex(0x0b0f17);
static lv_color_t c_card  = lv_color_hex(0x16202d);
static lv_color_t c_text  = lv_color_hex(0xe7eef7);
static lv_color_t c_muted = lv_color_hex(0x8a98ad);

lv_color_t up_color()    { return c_up; }
lv_color_t dn_color()    { return c_dn; }
lv_color_t bg_color()    { return c_bg; }
lv_color_t card_color()  { return c_card; }
lv_color_t muted_color() { return c_muted; }

// Small palette for unknown platforms, picked by id-character hash.
static const uint32_t kFallback[] = {
    0x60a5fa, 0xa78bfa, 0xf472b6, 0xfb923c, 0x34d399,
    0xfbbf24, 0x22d3ee, 0xf87171, 0xa3e635, 0x818cf8,
};

static bool parse_hex_color(const String& s, uint32_t& out) {
  const char* p = s.c_str();
  if (*p == '#') ++p;
  if (std::strlen(p) != 6) return false;
  uint32_t v = 0;
  for (int i = 0; i < 6; ++i) {
    char c = p[i];
    uint32_t d;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (c >= 'a' && c <= 'f') d = 10 + (c - 'a');
    else if (c >= 'A' && c <= 'F') d = 10 + (c - 'A');
    else return false;
    v = (v << 4) | d;
  }
  out = v;
  return true;
}

lv_color_t platform_color(const String& platform_id, const String& hex_hint) {
  uint32_t rgb;
  if (hex_hint.length() && parse_hex_color(hex_hint, rgb)) {
    return lv_color_hex(rgb);
  }
  uint32_t h = 2166136261u;  // FNV-1a over the platform id
  for (size_t i = 0; i < platform_id.length(); ++i) {
    h ^= (uint8_t)platform_id[i];
    h *= 16777619u;
  }
  return lv_color_hex(kFallback[h % (sizeof(kFallback) / sizeof(kFallback[0]))]);
}

void init() {
  lv_style_init(&row);
  lv_style_set_bg_color(&row, c_card);
  lv_style_set_bg_opa(&row, LV_OPA_COVER);
  lv_style_set_border_width(&row, 0);
  lv_style_set_radius(&row, 8);
  lv_style_set_pad_hor(&row, 8);
  lv_style_set_pad_ver(&row, 4);
  lv_style_set_text_color(&row, c_text);

  lv_style_init(&row_pressed);
  lv_style_set_bg_color(&row_pressed, lv_color_hex(0x223349));

  lv_style_init(&name);
  lv_style_set_text_font(&name, &lv_font_montserrat_18);
  lv_style_set_text_color(&name, c_text);

  lv_style_init(&name_big);
  lv_style_set_text_font(&name_big, &lv_font_montserrat_24);
  lv_style_set_text_color(&name_big, c_text);

  lv_style_init(&count);
  lv_style_set_text_font(&count, &lv_font_montserrat_18);
  lv_style_set_text_color(&count, c_text);

  lv_style_init(&count_big);
  lv_style_set_text_font(&count_big, &lv_font_montserrat_28);
  lv_style_set_text_color(&count_big, c_text);

  lv_style_init(&delta_up);
  lv_style_set_text_font(&delta_up, &lv_font_montserrat_16);
  lv_style_set_text_color(&delta_up, c_up);

  lv_style_init(&delta_dn);
  lv_style_set_text_font(&delta_dn, &lv_font_montserrat_16);
  lv_style_set_text_color(&delta_dn, c_dn);

  lv_style_init(&muted);
  lv_style_set_text_font(&muted, &lv_font_montserrat_12);
  lv_style_set_text_color(&muted, c_muted);

  lv_style_init(&status_bar);
  lv_style_set_bg_color(&status_bar, lv_color_hex(0x07090d));
  lv_style_set_bg_opa(&status_bar, LV_OPA_COVER);
  lv_style_set_border_width(&status_bar, 0);
  lv_style_set_pad_hor(&status_bar, 8);
  lv_style_set_pad_ver(&status_bar, 2);

  lv_style_init(&status_text);
  lv_style_set_text_font(&status_text, &lv_font_montserrat_14);
  lv_style_set_text_color(&status_text, c_muted);

  lv_style_init(&card);
  lv_style_set_bg_color(&card, c_card);
  lv_style_set_bg_opa(&card, LV_OPA_COVER);
  lv_style_set_border_width(&card, 0);
  lv_style_set_radius(&card, 10);
  lv_style_set_pad_all(&card, 8);

  lv_style_init(&badge);
  lv_style_set_radius(&badge, LV_RADIUS_CIRCLE);
  lv_style_set_bg_opa(&badge, LV_OPA_COVER);
  lv_style_set_border_width(&badge, 0);
  lv_style_set_pad_all(&badge, 0);

  lv_style_init(&badge_text);
  lv_style_set_text_font(&badge_text, &lv_font_montserrat_14);
  lv_style_set_text_color(&badge_text, lv_color_hex(0xffffff));
}

}  // namespace styles
