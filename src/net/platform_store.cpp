#include "platform_store.h"

void PlatformStore::begin() {
  _mu = xSemaphoreCreateMutex();
}

void PlatformStore::setPlatforms(std::vector<PlatformMeta> p) {
  xSemaphoreTake(_mu, portMAX_DELAY);
  _platforms = std::move(p);
  xSemaphoreGive(_mu);
}

std::vector<PlatformMeta> PlatformStore::snapshot() const {
  xSemaphoreTake(_mu, portMAX_DELAY);
  std::vector<PlatformMeta> copy = _platforms;
  xSemaphoreGive(_mu);
  return copy;
}

bool PlatformStore::empty() const {
  xSemaphoreTake(_mu, portMAX_DELAY);
  bool e = _platforms.empty();
  xSemaphoreGive(_mu);
  return e;
}

bool PlatformStore::find(const String& id, PlatformMeta& out) const {
  xSemaphoreTake(_mu, portMAX_DELAY);
  bool found = false;
  for (const auto& p : _platforms) {
    if (p.id == id) { out = p; found = true; break; }
  }
  xSemaphoreGive(_mu);
  return found;
}

String PlatformStore::name(const String& id) const {
  PlatformMeta m;
  if (find(id, m) && m.name.length()) return m.name;
  return id;
}

String PlatformStore::color(const String& id) const {
  PlatformMeta m;
  if (find(id, m)) return m.color;
  return String();
}
