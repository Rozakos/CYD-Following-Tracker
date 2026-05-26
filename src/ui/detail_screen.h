#pragma once

#include <Arduino.h>

class AccountStore;
class PlatformStore;

namespace detail_screen {

// Open the detail view for one account. Requests history for the default
// range from the store; tick() renders it when the net task delivers it.
void show(AccountStore* store, PlatformStore* platforms,
          const String& platform, const String& handle);
void tick();
bool active();

}  // namespace detail_screen
