// Sim stub for wifi_mgr — the device build uses the real one in
// src/net/wifi_mgr.cpp (depends on Arduino-ESP32 WiFi.h). The UI only reads
// connection state, so a fixed "STA connected" keeps the status bar green.

#include "Arduino.h"
#include "../src/net/wifi_mgr.h"

namespace wifi_mgr {

void begin(SettingsStore&) {}
void retrySta(SettingsStore&) {}

bool   connected() { return true; }
bool   apActive()  { return false; }
String ip()        { return String("192.168.1.50"); }
String apSsid()    { return String("CYD-Setup-AB12"); }
String apPass()    { return String(); }

}  // namespace wifi_mgr
