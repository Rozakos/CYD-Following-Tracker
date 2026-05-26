#include "fake_data.h"

#include <ctime>
#include <initializer_list>
#include <vector>

#include "../src/net/account_store.h"
#include "../src/net/platform_store.h"

namespace fake_data {

namespace {

Account make(const char* platform, const char* handle, const char* name,
             long long followers, std::initializer_list<long long> spark) {
  Account a;
  a.platform    = platform;
  a.handle      = handle;
  a.displayName = name;
  a.followers   = followers;
  a.fresh       = true;
  a.sparkline.reserve(spark.size());
  for (long long v : spark) a.sparkline.push_back((float)v);
  if (a.sparkline.size() >= 2)
    a.delta = (long long)(a.sparkline.back() - a.sparkline.front() + 0.5f);
  return a;
}

void set_history(AccountStore& store, const char* platform, const char* handle,
                 const char* interval, const char* range,
                 std::initializer_list<long long> vals, time_t step_s) {
  History h;
  h.key      = String(platform) + "|" + handle;
  h.range    = range;
  h.interval = interval;
  h.values.reserve(vals.size());
  for (long long v : vals) h.values.push_back((float)v);
  time_t now = std::time(nullptr);
  int n = (int)h.values.size();
  h.timestamps.reserve(n);
  for (int i = 0; i < n; ++i)
    h.timestamps.push_back(now - (time_t)(n - 1 - i) * step_s);
  store.setHistory(std::move(h));
  store.setHistoryError(false);
}

}  // namespace

void seed_platforms(PlatformStore& platforms) {
  std::vector<PlatformMeta> p = {
    {"youtube",   "YouTube",   "#FF0000", "/icon/youtube",   "channel ID or @handle"},
    {"twitch",    "Twitch",    "#9146FF", "/icon/twitch",    "channel login"},
    {"instagram", "Instagram", "#E1306C", "/icon/instagram", "@username"},
    {"tiktok",    "TikTok",    "#25F4EE", "/icon/tiktok",    "@username"},
    {"x",         "X",         "#1DA1F2", "/icon/x",         "@handle"},
  };
  platforms.setPlatforms(std::move(p));
}

void seed(AccountStore& store) {
  std::vector<Account> as{
    make("youtube", "@mkbhd", "MKBHD", 19200000,
         {19010000, 19035000, 19060000, 19088000, 19110000,
          19140000, 19165000, 19182000, 19195000, 19200000}),
    make("twitch", "pokimane", "pokimane", 9320000,
         {9290000, 9296000, 9301000, 9305000, 9309000,
          9312000, 9315000, 9317000, 9319000, 9320000}),
    make("youtube", "@veritasium", "Veritasium", 16100000,
         {15980000, 16000000, 16018000, 16035000, 16052000,
          16068000, 16080000, 16090000, 16096000, 16100000}),
    make("instagram", "natgeo", "National Geographic", 281000000,
         {280400000, 280500000, 280600000, 280680000, 280760000,
          280840000, 280900000, 280950000, 280980000, 281000000}),
    make("x", "@nasa", "NASA", 79200000,
         {79120000, 79135000, 79150000, 79162000, 79173000,
          79182000, 79189000, 79194000, 79198000, 79200000}),
  };
  store.setAccounts(std::move(as), std::time(nullptr));

  // Pre-populate detail history for MKBHD so the detail screen renders before
  // the UI requests a range.
  set_history(store, "youtube", "@mkbhd", "daily", "1mo",
    {18900000, 18930000, 18955000, 18980000, 19005000, 19030000,
     19055000, 19078000, 19098000, 19118000, 19135000, 19150000,
     19162000, 19173000, 19182000, 19200000}, 86400 * 2);
}

void seed_range_history(AccountStore& store, const char* platform,
                        const char* handle, const char* range) {
  String r(range ? range : "");
  if (r == "1w") {
    set_history(store, platform, handle, "daily", "1w",
      {19150000, 19162000, 19173000, 19181000, 19190000, 19196000, 19200000},
      86400);
  } else if (r == "1mo") {
    set_history(store, platform, handle, "daily", "1mo",
      {18950000, 18985000, 19015000, 19045000, 19075000, 19100000,
       19125000, 19150000, 19175000, 19200000}, 86400 * 3);
  } else if (r == "3mo") {
    set_history(store, platform, handle, "daily", "3mo",
      {18300000, 18420000, 18540000, 18650000, 18760000, 18860000,
       18950000, 19030000, 19100000, 19160000, 19200000}, 86400 * 9);
  } else if (r == "1y") {
    set_history(store, platform, handle, "daily", "1y",
      {15800000, 16200000, 16650000, 17050000, 17450000, 17800000,
       18150000, 18450000, 18750000, 19000000, 19200000}, 86400 * 33);
  } else if (r == "max") {
    set_history(store, platform, handle, "daily", "max",
      {2100000, 4200000, 6500000, 8800000, 11000000, 13000000,
       14800000, 16300000, 17600000, 18600000, 19200000}, 86400 * 120);
  }
}

}  // namespace fake_data
