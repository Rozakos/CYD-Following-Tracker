#pragma once

class AccountStore;
class PlatformStore;

namespace fake_data {

// Seed the platform set the device would otherwise discover from
// GET /platforms (youtube/twitch first, plus a few "later" platforms).
void seed_platforms(PlatformStore& platforms);

// Seed a handful of tracked accounts with follower counts + sparklines.
void seed(AccountStore& store);

// Swap in a history series shaped for the given range token
// (1w / 1mo / 3mo / 1y / max). Mirrors what GET /history would return.
void seed_range_history(AccountStore& store, const char* platform,
                        const char* handle, const char* range);

}  // namespace fake_data
