#pragma once

class SettingsStore;

namespace settings_screen {

// Borrows the settings store for read-only display.
void init(SettingsStore* settings);
void show();          // build (if needed), refresh, and load the screen
void tick();          // refresh dynamic fields; no-op when not active
bool active();

}  // namespace settings_screen
