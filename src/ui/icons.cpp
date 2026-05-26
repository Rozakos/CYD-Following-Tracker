#include "icons.h"

#include <LittleFS.h>

#include <cstring>
#include <cstdlib>
#include <vector>

#include <lgfx/utility/lgfx_pngle.h>

#include "../display/fs_littlefs.h"
#include "styles.h"

namespace {

constexpr size_t   MAX_RUNTIME_PNG_BYTES   = 64 * 1024;
constexpr unsigned MAX_RUNTIME_ICON_SIDE   = 128;
// Matches the size the API serves (see fetcher's ?size=48). Caching at the
// source resolution means the pngle callback writes pixels 1:1 into the slot
// and LVGL bilinearly scales 48->display at draw time.
constexpr uint32_t RUNTIME_ICON_CACHE_SIDE = 48;

pngle_t* g_pngle = nullptr;

struct RuntimeIcon {
  lv_image_dsc_t dsc;
  uint8_t* pixels;
};

struct CachedIcon {
  String       key;
  uint32_t     signature;
  RuntimeIcon* icon;
};

std::vector<CachedIcon> g_runtime_cache;

struct PixelBounds {
  uint32_t x0;
  uint32_t y0;
  uint32_t x1;
  uint32_t y1;
};

enum class PngDecodePass : uint8_t { Bounds, Render };

struct PngDecodeCtx {
  const uint8_t* png;
  size_t pngSize;
  size_t pos;
  PngDecodePass pass;
  uint32_t srcW;
  uint32_t srcH;
  PixelBounds bounds;
  uint8_t* dst;
  uint32_t dstSide;
  uint32_t outW;
  uint32_t outH;
  uint32_t offX;
  uint32_t offY;
  uint16_t* accum;
};

String iconPath(const String& platform) {
  return String("/icons/") + platform + ".png";
}

void destroyRuntimeIcon(RuntimeIcon* icon) {
  if (!icon) return;
  std::free(icon->pixels);
  std::free(icon);
}

lv_obj_t* makeBadge(lv_obj_t* parent, const String& platform,
                    const String& label, const String& color_hex,
                    lv_coord_t size) {
  lv_obj_t* badge = lv_obj_create(parent);
  lv_obj_set_size(badge, size, size);
  lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(badge, styles::platform_color(platform, color_hex), 0);
  lv_obj_set_style_border_width(badge, 0, 0);
  lv_obj_set_style_pad_all(badge, 0, 0);
  lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

  String letters = label.length() ? label : platform;
  letters.toUpperCase();
  if (letters.length() > 2) letters = letters.substring(0, 2);

  lv_obj_t* lbl = lv_label_create(badge);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0xffffff), 0);
  if (size >= 64) {
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
  } else if (size >= 48) {
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
  } else if (size >= 32) {
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
  } else {
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
  }
  lv_label_set_text(lbl, letters.c_str());
  lv_obj_center(lbl);
  return badge;
}

uint32_t pngReadCb(void* userData, uint8_t* buf, uint32_t len) {
  auto* ctx = static_cast<PngDecodeCtx*>(userData);
  if (!ctx || !ctx->png) return 0;
  size_t avail = ctx->pngSize - ctx->pos;
  if (len > avail) len = (uint32_t)avail;
  if (buf && len) memcpy(buf, ctx->png + ctx->pos, len);
  ctx->pos += len;
  return len;
}

void pngDrawCb(void* userData, uint32_t x, uint32_t y,
               uint_fast8_t divX, size_t len, const uint8_t* argb) {
  auto* ctx = static_cast<PngDecodeCtx*>(userData);
  if (!ctx || !argb || !ctx->srcW || !ctx->srcH || y >= ctx->srcH) return;

  for (size_t i = 0; i < len; ++i) {
    uint32_t px = x + (uint32_t)i * divX;
    if (px >= ctx->srcW) continue;

    const uint8_t* in = argb + i * 4;
    if (ctx->pass == PngDecodePass::Bounds) {
      if (in[0] > 8) {
        if (px < ctx->bounds.x0) ctx->bounds.x0 = px;
        if (y < ctx->bounds.y0) ctx->bounds.y0 = y;
        if (px + 1 > ctx->bounds.x1) ctx->bounds.x1 = px + 1;
        if (y + 1 > ctx->bounds.y1) ctx->bounds.y1 = y + 1;
      }
      continue;
    }

    if (!ctx->dst || !ctx->dstSide || !ctx->outW || !ctx->outH ||
        px < ctx->bounds.x0 || px >= ctx->bounds.x1 ||
        y < ctx->bounds.y0 || y >= ctx->bounds.y1) {
      continue;
    }

    uint32_t cropW = ctx->bounds.x1 - ctx->bounds.x0;
    uint32_t cropH = ctx->bounds.y1 - ctx->bounds.y0;
    uint32_t relX = px - ctx->bounds.x0;
    uint32_t relY = y - ctx->bounds.y0;
    if (ctx->accum) {
      uint32_t dx = ctx->offX + (relX * ctx->outW) / cropW;
      uint32_t dy = ctx->offY + (relY * ctx->outH) / cropH;
      if (dx >= ctx->dstSide) dx = ctx->dstSide - 1;
      if (dy >= ctx->dstSide) dy = ctx->dstSide - 1;
      uint16_t* acc = ctx->accum + ((size_t)dy * ctx->dstSide + dx) * 5;
      acc[0] += in[3];
      acc[1] += in[2];
      acc[2] += in[1];
      acc[3] += in[0];
      acc[4] += 1;
    } else {
      uint32_t dx0 = ctx->offX + (relX * ctx->outW) / cropW;
      uint32_t dx1 = ctx->offX + ((relX + 1) * ctx->outW + cropW - 1) / cropW;
      uint32_t dy0 = ctx->offY + (relY * ctx->outH) / cropH;
      uint32_t dy1 = ctx->offY + ((relY + 1) * ctx->outH + cropH - 1) / cropH;
      if (dx1 <= dx0) dx1 = dx0 + 1;
      if (dy1 <= dy0) dy1 = dy0 + 1;
      if (dx1 > ctx->dstSide) dx1 = ctx->dstSide;
      if (dy1 > ctx->dstSide) dy1 = ctx->dstSide;

      for (uint32_t yy = dy0; yy < dy1; ++yy) {
        for (uint32_t xx = dx0; xx < dx1; ++xx) {
          uint8_t* out = ctx->dst + ((size_t)yy * ctx->dstSide + xx) * 4;
          // pngle emits A,R,G,B. LVGL ARGB8888 wants little-endian B,G,R,A.
          out[0] = in[3];
          out[1] = in[2];
          out[2] = in[1];
          out[3] = in[0];
        }
      }
    }
  }
}

bool configureRenderBounds(PngDecodeCtx& ctx, uint32_t dstSide) {
  if (ctx.bounds.x0 >= ctx.bounds.x1 || ctx.bounds.y0 >= ctx.bounds.y1) {
    ctx.bounds = PixelBounds{0, 0, ctx.srcW, ctx.srcH};
  }
  uint32_t cropW = ctx.bounds.x1 - ctx.bounds.x0;
  uint32_t cropH = ctx.bounds.y1 - ctx.bounds.y0;
  if (!cropW || !cropH || !dstSide) return false;

  float scaleX = (float)dstSide / (float)cropW;
  float scaleY = (float)dstSide / (float)cropH;
  float scale = (scaleX < scaleY) ? scaleX : scaleY;
  ctx.outW = (uint32_t)((float)cropW * scale + 0.5f);
  ctx.outH = (uint32_t)((float)cropH * scale + 0.5f);
  if (ctx.outW < 1) ctx.outW = 1;
  if (ctx.outH < 1) ctx.outH = 1;
  if (ctx.outW > dstSide) ctx.outW = dstSide;
  if (ctx.outH > dstSide) ctx.outH = dstSide;
  ctx.offX = (dstSide - ctx.outW) / 2;
  ctx.offY = (dstSide - ctx.outH) / 2;
  ctx.dstSide = dstSide;
  return true;
}

void resolveAccumulatedIcon(PngDecodeCtx& ctx) {
  if (!ctx.accum || !ctx.dst || !ctx.dstSide) return;
  for (uint32_t y = 0; y < ctx.dstSide; ++y) {
    for (uint32_t x = 0; x < ctx.dstSide; ++x) {
      uint16_t* acc = ctx.accum + ((size_t)y * ctx.dstSide + x) * 5;
      uint16_t n = acc[4];
      if (!n) continue;
      uint8_t* out = ctx.dst + ((size_t)y * ctx.dstSide + x) * 4;
      out[0] = (uint8_t)((acc[0] + n / 2) / n);
      out[1] = (uint8_t)((acc[1] + n / 2) / n);
      out[2] = (uint8_t)((acc[2] + n / 2) / n);
      out[3] = (uint8_t)((acc[3] + n / 2) / n);
    }
  }
}

RuntimeIcon* decodeRuntimeIcon(const String& platform, const String& path,
                               lv_coord_t targetSide) {
  uint8_t* png = nullptr;
  size_t bytes = 0;
  {
    fs_littlefs::Guard g;
    File f = LittleFS.open(path, "rb");
    if (!f) return nullptr;
    bytes = f.size();
    if (bytes == 0 || bytes > MAX_RUNTIME_PNG_BYTES) {
      log_w("[icon] %s skip bytes=%u", platform.c_str(), (unsigned)bytes);
      return nullptr;
    }
    png = static_cast<uint8_t*>(std::malloc(bytes));
    if (!png) {
      log_w("[icon] %s png buffer alloc failed", platform.c_str());
      return nullptr;
    }
    size_t got = f.readBytes(reinterpret_cast<char*>(png), bytes);
    f.close();
    if (got != bytes) {
      log_w("[icon] %s short read", platform.c_str());
      std::free(png);
      return nullptr;
    }
  }

  if (!icons::prepareRuntimeDecoder()) {
    std::free(png);
    return nullptr;
  }
  pngle_t* pngle = g_pngle;

  PngDecodeCtx ctx{png, bytes, 0, PngDecodePass::Bounds, 0, 0,
                   PixelBounds{0, 0, 0, 0}, nullptr, 0, 0, 0, 0, 0, nullptr};
  if (lgfx_pngle_prepare(pngle, pngReadCb, &ctx) < 0) {
    std::free(png);
    return nullptr;
  }
  unsigned w = lgfx_pngle_get_width(pngle);
  unsigned h = lgfx_pngle_get_height(pngle);
  if (w == 0 || h == 0 || targetSide <= 0 ||
      w > MAX_RUNTIME_ICON_SIDE || h > MAX_RUNTIME_ICON_SIDE) {
    log_w("[icon] %s bad dims=%ux%u", platform.c_str(), w, h);
    std::free(png);
    return nullptr;
  }

  ctx.srcW = w;
  ctx.srcH = h;
  ctx.bounds = PixelBounds{w, h, 0, 0};
  if (lgfx_pngle_decomp(pngle, pngDrawCb) < 0) {
    std::free(png);
    return nullptr;
  }

  uint32_t outSide = (uint32_t)targetSide;
  size_t pixelBytes = (size_t)outSide * (size_t)outSide * 4;
  auto* icon = static_cast<RuntimeIcon*>(std::calloc(1, sizeof(RuntimeIcon)));
  if (!icon) { std::free(png); return nullptr; }
  icon->pixels = static_cast<uint8_t*>(std::malloc(pixelBytes));
  if (!icon->pixels) {
    std::free(icon);
    std::free(png);
    return nullptr;
  }
  memset(icon->pixels, 0, pixelBytes);

  if (!configureRenderBounds(ctx, outSide)) {
    destroyRuntimeIcon(icon);
    std::free(png);
    return nullptr;
  }
  ctx.pos = 0;
  ctx.pass = PngDecodePass::Render;
  ctx.dst = icon->pixels;
  uint32_t cropW = ctx.bounds.x1 - ctx.bounds.x0;
  uint32_t cropH = ctx.bounds.y1 - ctx.bounds.y0;
  if (cropW > ctx.outW && cropH > ctx.outH) {
    size_t accumBytes = (size_t)outSide * outSide * 5 * sizeof(uint16_t);
    ctx.accum = static_cast<uint16_t*>(std::calloc(1, accumBytes));
  }
  if (lgfx_pngle_prepare(pngle, pngReadCb, &ctx) < 0 ||
      lgfx_pngle_decomp(pngle, pngDrawCb) < 0) {
    std::free(ctx.accum);
    destroyRuntimeIcon(icon);
    std::free(png);
    return nullptr;
  }
  resolveAccumulatedIcon(ctx);
  std::free(ctx.accum);
  std::free(png);

  icon->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  icon->dsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
  icon->dsc.header.flags = 0;
  icon->dsc.header.w = outSide;
  icon->dsc.header.h = outSide;
  icon->dsc.header.stride = outSide * 4;
  icon->dsc.header.reserved_2 = 0;
  icon->dsc.data_size = (uint32_t)pixelBytes;
  icon->dsc.data = icon->pixels;
  icon->dsc.reserved = nullptr;
  icon->dsc.reserved_2 = nullptr;
  return icon;
}

RuntimeIcon* cachedRuntimeIcon(const String& platform, const String& path,
                               size_t bytes) {
  uint32_t sig = 0x02000000u | (uint32_t)(bytes & 0x00FFFFFFu);
  for (const auto& cached : g_runtime_cache) {
    if (cached.key == platform && cached.signature == sig) return cached.icon;
  }
  for (auto it = g_runtime_cache.begin(); it != g_runtime_cache.end();) {
    if (it->key == platform) {
      destroyRuntimeIcon(it->icon);
      it = g_runtime_cache.erase(it);
    } else {
      ++it;
    }
  }
  RuntimeIcon* icon = decodeRuntimeIcon(platform, path, RUNTIME_ICON_CACHE_SIDE);
  if (!icon) return nullptr;
  g_runtime_cache.push_back(CachedIcon{platform, sig, icon});
  return icon;
}

}  // namespace

namespace icons {

bool prepareRuntimeDecoder() {
  if (g_pngle) return true;
  g_pngle = lgfx_pngle_new();
  return g_pngle != nullptr;
}

void releaseRuntimeDecoder() {
  if (!g_pngle) return;
  lgfx_pngle_destroy(g_pngle);
  g_pngle = nullptr;
}

void clearRuntimeCache() {
  if (g_runtime_cache.empty()) return;
  for (auto& cached : g_runtime_cache) destroyRuntimeIcon(cached.icon);
  g_runtime_cache.clear();
}

lv_obj_t* make(lv_obj_t* parent, const String& platform, const String& label,
               const String& color_hex, lv_coord_t size) {
  String path = iconPath(platform);
  bool exists = false;
  size_t bytes = 0;
  {
    fs_littlefs::Guard g;
    exists = LittleFS.exists(path);
    if (exists) {
      File f = LittleFS.open(path, "r");
      if (f) { bytes = f.size(); f.close(); }
    }
  }
  if (exists) {
    bool hadDecoder = (g_pngle != nullptr);
    RuntimeIcon* rt = cachedRuntimeIcon(platform, path, bytes);
    if (!hadDecoder) releaseRuntimeDecoder();
    if (rt) {
      lv_obj_t* img = lv_image_create(parent);
      lv_image_set_src(img, &rt->dsc);
      if (rt->dsc.header.w > 0) {
        int32_t scale = (size * 256) / rt->dsc.header.w;
        if (scale != 256) lv_image_set_scale(img, scale);
      }
      lv_obj_set_size(img, size, size);
      return img;
    }
  }
  return makeBadge(parent, platform, label, color_hex, size);
}

uint32_t signature(const String& platform) {
  String path = iconPath(platform);
  size_t bytes = 0;
  bool exists = false;
  {
    fs_littlefs::Guard g;
    if (LittleFS.exists(path)) {
      exists = true;
      File f = LittleFS.open(path, "r");
      if (f) { bytes = f.size(); f.close(); }
    }
  }
  if (exists) return 0x02000000u | (uint32_t)(bytes & 0x00FFFFFFu);
  return 0x03000000u;  // badge fallback
}

}  // namespace icons
