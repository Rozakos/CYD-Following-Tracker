#include "account_store.h"

void AccountStore::begin() {
  _mu = xSemaphoreCreateMutex();
}

void AccountStore::setAccounts(std::vector<Account> a, time_t epoch) {
  xSemaphoreTake(_mu, portMAX_DELAY);
  _accounts   = std::move(a);
  _lastUpdate = epoch;
  xSemaphoreGive(_mu);
}

std::vector<Account> AccountStore::snapshot() const {
  xSemaphoreTake(_mu, portMAX_DELAY);
  std::vector<Account> copy = _accounts;
  xSemaphoreGive(_mu);
  return copy;
}

time_t AccountStore::lastUpdate() const {
  xSemaphoreTake(_mu, portMAX_DELAY);
  time_t v = _lastUpdate;
  xSemaphoreGive(_mu);
  return v;
}

void AccountStore::setHistory(History h) {
  xSemaphoreTake(_mu, portMAX_DELAY);
  _history = std::move(h);
  xSemaphoreGive(_mu);
}

History AccountStore::history() const {
  xSemaphoreTake(_mu, portMAX_DELAY);
  History copy = _history;
  xSemaphoreGive(_mu);
  return copy;
}

uint32_t AccountStore::requestHistory(const String& platform,
                                      const String& handle,
                                      const String& range) {
  xSemaphoreTake(_mu, portMAX_DELAY);
  ++_historyGen;
  _pendingHistory.platform = platform;
  _pendingHistory.handle   = handle;
  _pendingHistory.range    = range;
  _pendingHistory.gen      = _historyGen;
  _historyPending          = true;
  _historyError            = false;   // new request -> clear previous error
  uint32_t gen             = _historyGen;
  xSemaphoreGive(_mu);
  return gen;
}

bool AccountStore::takePendingHistory(HistoryRequest& out) {
  xSemaphoreTake(_mu, portMAX_DELAY);
  bool had = _historyPending;
  if (had) {
    out             = _pendingHistory;
    _historyPending = false;
  }
  xSemaphoreGive(_mu);
  return had;
}

bool AccountStore::historyGenCurrent(uint32_t gen) const {
  xSemaphoreTake(_mu, portMAX_DELAY);
  bool ok = (gen == _historyGen);
  xSemaphoreGive(_mu);
  return ok;
}

void AccountStore::setHistoryError(bool err) {
  xSemaphoreTake(_mu, portMAX_DELAY);
  _historyError = err;
  xSemaphoreGive(_mu);
}

bool AccountStore::historyError() const {
  xSemaphoreTake(_mu, portMAX_DELAY);
  bool v = _historyError;
  xSemaphoreGive(_mu);
  return v;
}
