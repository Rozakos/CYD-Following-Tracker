#pragma once

#include <Arduino.h>

class AccountStore;
class PlatformStore;

namespace hero_screen {

// Full-screen single-account view for `key` ("platform|handle"). Big centered
// follower count, this-window gain, milestone progress, slim sparkline.
void show(AccountStore* store, PlatformStore* platforms, const String& key);
void tick();
bool active();

}  // namespace hero_screen
