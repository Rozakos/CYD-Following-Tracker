#pragma once

#include <Arduino.h>
#include <lvgl.h>

namespace icons {

// Allocates the reusable runtime PNG decoder before UI widgets fragment the
// heap. Platform icons can still fall back to a badge if this fails.
bool prepareRuntimeDecoder();

// Frees the reusable PNG decoder after cached icons have been decoded.
void releaseRuntimeDecoder();

// Frees decoded runtime icons after their owning LVGL widgets are deleted.
void clearRuntimeCache();

// Creates a square platform icon sized `size` x `size`. Resolves
// /icons/<platform>.png on LittleFS (fetched at runtime — a brand-new
// platform's icon arrives the same way); if missing, draws a circular badge
// in the platform brand color showing the first 1-2 letters of `label`.
// `color_hex` is the platform's server-provided "#RRGGBB" (or "" for the
// hash fallback); `label` is the platform display name (or id).
lv_obj_t* make(lv_obj_t* parent, const String& platform, const String& label,
               const String& color_hex, lv_coord_t size);

// Stable "what would make() render?" signature (badge vs runtime PNG, plus
// the PNG file size). Lets the list skip rebuilding an unchanged icon widget.
uint32_t signature(const String& platform);

}  // namespace icons
