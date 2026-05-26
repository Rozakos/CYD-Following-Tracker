#pragma once

#include <Arduino.h>

class SettingsStore;
class AccountStore;
class PlatformStore;

namespace fetcher {

// GET /platforms -> [ {id,name,color,icon_url,handle_hint}, ... ].
// This is what makes platforms add-able without a firmware change: the
// device discovers the full platform set here. Returns true on success.
bool fetchPlatforms(SettingsStore& settings, PlatformStore& platforms);

// For each configured account: GET /social/{platform}/{handle}, fill the
// AccountStore, and ensure the platform icon PNG is cached for the UI.
bool fetchAccounts(SettingsStore& settings, AccountStore& store,
                   PlatformStore& platforms);

// GET /history/{platform}/{handle}?range=<token>. `gen` is the request
// generation from AccountStore::requestHistory; a newer request supersedes
// this one (the result is dropped if it's no longer current).
bool fetchHistory(SettingsStore& settings, AccountStore& store,
                  const String& platform, const String& handle,
                  const String& range, uint32_t gen);

}  // namespace fetcher
