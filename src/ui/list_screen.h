#pragma once

#include <lvgl.h>

class AccountStore;
class SettingsStore;
class PlatformStore;

namespace list_screen {

// `settings` is borrowed for the lifetime of the screen (favourites/pin +
// the WiFi-icon tap-to-reset handler). `platforms` supplies per-row brand
// color, display name, and icon metadata learned at runtime.
void build(AccountStore* store, SettingsStore* settings, PlatformStore* platforms);
lv_obj_t* screen();
void tick();  // call from UI timer to refresh from store

}  // namespace list_screen
