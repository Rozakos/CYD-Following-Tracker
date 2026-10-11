#pragma once

class SettingsStore;
class PlatformStore;
class AccountStore;

namespace web_admin {

// `accounts` feeds GET /api/state (live counts for the Rozakos Home app).
void begin(SettingsStore* settings, PlatformStore* platforms,
           AccountStore* accounts = nullptr);

}  // namespace web_admin
