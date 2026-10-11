#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp32-hal.h>      // disableCore0WDT
#include <lvgl.h>
#include <time.h>

#include "config.h"
#include "display/fs_littlefs.h"
#include "display/lgfx_cyd.hpp"
#include "display/lvgl_bridge.h"
#include "net/account_store.h"
#include "net/captive_portal.h"
#include "net/platform_store.h"
#include "net/social_fetcher.h"
#include "net/web_admin.h"
#include "net/wifi_mgr.h"
#include "secrets.h"
#include "settings/settings_store.h"
#include "ui/detail_screen.h"
#include "ui/hero_screen.h"
#include "ui/icons.h"
#include "ui/list_screen.h"
#include "ui/settings_screen.h"
#include "ui/styles.h"
#include "ui/wifi_setup_screen.h"

namespace {

LGFX_CYD          g_gfx;
SettingsStore     g_settings;
AccountStore      g_accounts;
PlatformStore     g_platforms;
SemaphoreHandle_t g_lvglMu = nullptr;

void uiTask(void*) {
  xSemaphoreTake(g_lvglMu, portMAX_DELAY);
  styles::init();
  icons::prepareRuntimeDecoder();
  settings_screen::init(&g_settings);
  list_screen::build(&g_accounts, &g_settings, &g_platforms);
  icons::releaseRuntimeDecoder();
  lv_screen_load(list_screen::screen());
  xSemaphoreGive(g_lvglMu);

  uint32_t lastPoll = 0;
  for (;;) {
    xSemaphoreTake(g_lvglMu, portMAX_DELAY);
    uint32_t wait = lv_timer_handler();
    if (millis() - lastPoll > 250) {
      list_screen::tick();
      detail_screen::tick();
      hero_screen::tick();
      settings_screen::tick();
      wifi_setup_screen::tick();
      lastPoll = millis();
    }
    xSemaphoreGive(g_lvglMu);
    vTaskDelay(pdMS_TO_TICKS(wait > 30 ? 30 : (wait ? wait : 5)));
  }
}

void netTask(void*) {
  // HTTPS handshakes can block core 0 for several seconds and starve IDLE0,
  // tripping the task watchdog. Detaching IDLE0 from the WDT is the canonical
  // fix for tasks making blocking network calls outside loop().
  disableCore0WDT();

  wifi_mgr::begin(g_settings);

  bool ap_mode_was = wifi_mgr::apActive();
  if (ap_mode_was) {
    captive_portal::begin(g_settings);
    xSemaphoreTake(g_lvglMu, portMAX_DELAY);
    wifi_setup_screen::show(wifi_mgr::apSsid(), wifi_mgr::apPass());
    xSemaphoreGive(g_lvglMu);
  }

  bool sta_services_up = false;
  auto bringUpStaServices = [&] {
    if (sta_services_up) return;
    // NTP first — configTime() sets the clock to UTC and resets TZ to UTC
    // internally, so our local TZ must be applied AFTER it. (Doing it before
    // was clobbered, leaving localtime_r on UTC.)
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    setenv("TZ", cfg::TIME_TZ, 1);
    tzset();
    // Discover the platform set once before the first account fetch so list
    // rows render with proper names / brand colors / icons.
    fetcher::fetchPlatforms(g_settings, g_platforms);
    web_admin::begin(&g_settings, &g_platforms);
    xSemaphoreTake(g_lvglMu, portMAX_DELAY);
    lv_screen_load(list_screen::screen());
    xSemaphoreGive(g_lvglMu);
    sta_services_up = true;
  };

  if (wifi_mgr::connected()) bringUpStaServices();

  uint32_t lastFetch = 0;
  uint32_t seenRevision = g_settings.revision();
  for (;;) {
    captive_portal::loop();

    if (wifi_mgr::connected()) {
      if (ap_mode_was) {
        ap_mode_was = false;
        captive_portal::end();
      }
      bringUpStaServices();
      if (g_platforms.empty()) fetcher::fetchPlatforms(g_settings, g_platforms);
      if (g_settings.revision() != seenRevision) {   // web admin changed something
        seenRevision = g_settings.revision();
        lastFetch = 0;
      }
      if (lastFetch == 0 ||
          millis() - lastFetch > g_settings.refreshSeconds() * 1000UL) {
        if (fetcher::fetchAccounts(g_settings, g_accounts, g_platforms)) {
          lastFetch = millis();
        } else {
          lastFetch = millis() - (g_settings.refreshSeconds() * 1000UL) + 5000;
        }
      }
      HistoryRequest hr;
      if (g_accounts.takePendingHistory(hr)) {
        fetcher::fetchHistory(g_settings, g_accounts, hr.platform, hr.handle,
                              hr.range, hr.gen);
      }
    } else if (wifi_mgr::apActive() && !ap_mode_was) {
      ap_mode_was = true;
      captive_portal::begin(g_settings);
      xSemaphoreTake(g_lvglMu, portMAX_DELAY);
      wifi_setup_screen::show(wifi_mgr::apSsid(), wifi_mgr::apPass());
      xSemaphoreGive(g_lvglMu);
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(50);
  log_i("CYD follower tracker starting");

  if (!LittleFS.begin(true)) {
    log_e("LittleFS mount failed");
  }
  g_settings.begin(API_TOKEN_SEED, WIFI_SSID, WIFI_PASS);
  g_accounts.begin();
  g_platforms.begin();

  g_gfx.init();
  g_gfx.setRotation(1);   // landscape, USB-C/header at left
  g_gfx.setBrightness(255);
  g_gfx.fillScreen(0x0000);

  lv_init();
  bridge::init(&g_gfx);
  fs_littlefs::init();

  g_lvglMu = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(uiTask,  "ui",  8192,  nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(netTask, "net", 12288, nullptr, 1, nullptr, 0);
}

void loop() {
  vTaskDelete(nullptr);
}
