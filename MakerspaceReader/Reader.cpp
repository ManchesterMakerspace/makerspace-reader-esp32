#include "Reader.h"
#include "Policy.h"
#include "secrets.h"
#include <ETH.h>
#include <ElegantOTA.h>
#include <LittleFS.h>
#include <RTClib.h>
#include <SPI.h>
#include <Wire.h>
#include <bootloader_random.h>
#include <esp_flash_encrypt.h>
#include <esp_ota_ops.h>
#include <esp_secure_boot.h>
#include <esp_timer.h>
static bool pendingBootValidation = false;
SemaphoreHandle_t stateMutex, fsMutex, i2cMutex, apiMutex, snapshotMutex;
std::unordered_map<std::string, Card> cards;
std::unordered_map<std::string, Grant> grants;
JsonDocument configuration;
uint32_t configurationSequence = 0;
std::atomic<bool> maintenance(false), relayOn(false), clockTrusted(false), refreshNeeded(true),
    storageOK(false), apiOK(false), mqttOK(false), nfcOK(false), deviceBlocked(false);
std::atomic<int64_t> relayDeadline(0), toolDeadline(0);
std::atomic<uint32_t> accepted(0), rejected(0), dropped(0);
String activeUid, lastError, csrf;
Preferences prefs;
RTC_PCF8523 rtc;
bool rtcPresent = false, productionReady = true;
static std::atomic<int64_t> epochOffset(0);
int64_t epochMs() {
  if (clockTrusted)
    return epochOffset.load() + esp_timer_get_time() / 1000;
  struct timeval t;
  gettimeofday(&t, nullptr);
  return int64_t(t.tv_sec) * 1000 + t.tv_usec / 1000;
}
void trustTime(int64_t t) {
  epochOffset = t - esp_timer_get_time() / 1000;
  timeval tv{time_t(t / 1000), suseconds_t(t % 1000 * 1000)};
  settimeofday(&tv, nullptr);
  clockTrusted = true;
}
bool authorize(const Card &c, String &reason, int64_t &until) {
  if (!cacheKeyReady) {
    reason = "cacheKeyUnavailable";
    return false;
  }
  if (!productionReady) {
    reason = "unsecuredDevice";
    return false;
  }
  if (deviceBlocked) {
    reason = "readerAuthorizationRejected";
    return false;
  }
  if (maintenance) {
    reason = "maintenance";
    return false;
  }
  if (!clockTrusted) {
    reason = "timeUnavailable";
    return false;
  }
  auto g = grants.find(c.member.c_str());
  String role = configuration["role"] | "";
  auto decision = policy::evaluate(role.c_str(), !c.id.isEmpty(), c.status.c_str(), c.expiry,
                                   c.key.length() == 12 && c.digest.length() == 64,
                                   configuration["toolAvailable"].as<bool>(), g != grants.end(),
                                   g == grants.end() ? 0 : g->second.until, epochMs());
  reason = decision.reason;
  until = decision.until;
  return decision.allow;
}
static void relayTask(void *) {
  for (;;) {
    if (relayOn) {
      bool off = maintenance || deviceBlocked || !clockTrusted ||
                 (relayDeadline > 0 && esp_timer_get_time() >= relayDeadline) ||
                 (toolDeadline > 0 && epochMs() >= toolDeadline);
      if (off) {
        digitalWrite(RELAY_PIN, LOW);
        relayOn = false;
        relayDeadline = 0;
        toolDeadline = 0;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
void readerSetup() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  Serial.begin(115200);
  stateMutex = xSemaphoreCreateMutex();
  fsMutex = xSemaphoreCreateMutex();
  i2cMutex = xSemaphoreCreateMutex();
  apiMutex = xSemaphoreCreateMutex();
  snapshotMutex = xSemaphoreCreateMutex();
  // Ethernet-only firmware: enable the internal entropy source (ADC is not used).
  bootloader_random_enable();
  uint8_t nonce[24];
  esp_fill_random(nonce, sizeof(nonce));
  csrf = hexBytes(nonce, sizeof(nonce));
#if READER_PRODUCTION
  productionReady = false;
#if defined(CONFIG_SECURE_SIGNED_APPS) && CONFIG_SECURE_SIGNED_APPS
  productionReady = esp_flash_encryption_enabled() && esp_secure_boot_enabled();
#endif
#endif
  initializeStorage();
  if (configuration["name"].isNull()) {
    configuration["name"] = DEVICE_ID;
    configuration["role"] = "test";
    configuration["enrollmentEnabled"] = false;
  }
  Wire.begin(1, 2);
  Wire.setClock(100000);
  Wire.setTimeOut(50);
  rtcPresent = rtc.begin(&Wire);
  if (rtcPresent && rtc.initialized() && !rtc.lostPower()) {
    int64_t t = int64_t(rtc.now().unixtime()) * 1000;
    if (t >= int64_t(prefs.getULong64("lastTime", 0)) && t > 1700000000000LL) {
      trustTime(t);
    }
  }
  SPI.begin(13, 12, 11);
  ETH.begin(ETH_PHY_W5500, 1, 14, 10, 9, SPI);
  if (!clockTrusted)
    configTime(0, 0, "pool.ntp.org",
               "time.cloudflare.com"); // bootstrap TLS only; API confirms authorization time
#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
  esp_ota_img_states_t imageState;
  pendingBootValidation =
      esp_ota_get_state_partition(esp_ota_get_running_partition(), &imageState) == ESP_OK &&
      imageState == ESP_OTA_IMG_PENDING_VERIFY;
  if (pendingBootValidation)
    maintenance = true;
#endif
  webSetup();
  xTaskCreate(relayTask, "relay", 3072, nullptr, 4, nullptr);
  if (cacheKeyReady) {
    xTaskCreate(networkTask, "network", 16384, nullptr, 1, nullptr);
    xTaskCreate(nfcTask, "nfc", 12288, nullptr, 2, nullptr);
  }
}
extern void otaWatchdog();
void readerLoop() {
#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
  if (pendingBootValidation && millis() > 30000 && productionReady && storageOK && nfcOK) {
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
      pendingBootValidation = false;
      maintenance = false;
    }
  }
  if (pendingBootValidation && millis() > 120000) {
    digitalWrite(RELAY_PIN, LOW);
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
#endif
  ElegantOTA.loop();
  otaWatchdog();
  delay(10);
}
