#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <vector>

// An account is identified by (platform, handle) where `platform` is an
// opaque string the firmware never interprets. `key()` joins them as
// "platform|handle" — the stable id used for favourites, history requests,
// and the primary-account selection.
struct Account {
  String              platform;       // opaque platform id, e.g. "youtube"
  String              handle;         // e.g. "@mkbhd" or a channel id
  String              displayName;    // from API; falls back to handle
  long long           followers = -1;
  // Absolute change over the configured list window (sparkline first->last).
  long long           delta     = 0;
  bool                fresh     = false;
  std::vector<float>  sparkline;       // follower history, oldest -> newest

  String key() const { return platform + "|" + handle; }
};

// "platform|handle" — the canonical key. Free function so callers that only
// have the two strings (settings, fetcher) don't need an Account instance.
inline String account_key(const String& platform, const String& handle) {
  return platform + "|" + handle;
}

struct History {
  String              key;       // platform|handle this history belongs to
  String              range;     // API range token: 1w / 1mo / 3mo / 1y / max
  // "daily" or "hourly" — drives the X-axis label formatter on the detail
  // screen. Left empty for fallback paths.
  String              interval;
  std::vector<float>  values;       // followers, oldest -> newest
  // Per-point epoch (seconds). Same length as `values` when populated.
  std::vector<time_t> timestamps;
};

struct HistoryRequest {
  String   platform;
  String   handle;
  String   range;   // 1w / 1mo / 3mo / 1y / max
  uint32_t gen = 0;
  String   key() const { return platform + "|" + handle; }
};

class AccountStore {
 public:
  void begin();

  void setAccounts(std::vector<Account> a, time_t epoch);

  // Snapshot copy under lock.
  std::vector<Account> snapshot() const;
  time_t               lastUpdate() const;

  // Detail history (single slot, last fetched).
  void    setHistory(History h);
  History history() const;

  // Detail request from UI -> net task. Returns the generation ID this
  // request was assigned; the network task captures it before fetching and
  // the result is stored only if `historyGenCurrent(gen)` is still true at
  // the end (so a later requestHistory call effectively cancels the prior).
  uint32_t requestHistory(const String& platform, const String& handle,
                          const String& range);

  // Net task: returns true if a request is pending and copies it out
  // (clearing the pending state). Otherwise returns false.
  bool     takePendingHistory(HistoryRequest& out);

  // True if `gen` is still the most recent request issued.
  bool     historyGenCurrent(uint32_t gen) const;

  // Sticky error flag for the most recent fetch — set on HTTP / parse
  // failure, cleared on success. UI polls this to show "no data".
  void setHistoryError(bool err);
  bool historyError() const;

 private:
  mutable SemaphoreHandle_t _mu = nullptr;
  std::vector<Account> _accounts;
  time_t               _lastUpdate = 0;
  History              _history;
  HistoryRequest       _pendingHistory;
  bool                 _historyPending = false;
  uint32_t             _historyGen     = 0;
  bool                 _historyError   = false;
};
