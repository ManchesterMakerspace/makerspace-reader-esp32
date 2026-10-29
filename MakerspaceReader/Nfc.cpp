#include "NfcProtocol.h"
#include "Pn532.h"
#include "Policy.h"
#include "Reader.h"
#include <MakerspacePN7160.h>
#include <SerLCD.h>
#include <SparkFun_Qwiic_Buzzer_Arduino_Library.h>
#include <Wire.h>
#include <esp_timer.h>
#include <mbedtls/sha256.h>
static constexpr uint8_t NFC_IRQ = 17, NFC_RESET = 18;
static Electroniccats_PN7150 nfc(NFC_IRQ, NFC_RESET, 0x28, PN7160, &Wire);
static std::atomic<nfcProtocol::Controller> controller(nfcProtocol::Controller::none);
static TaskHandle_t nfcTaskHandle = nullptr;
static uint8_t selectedUid[10] = {}, selectedUidLength = 0;
static bool irqReady() {
  return nfcProtocol::ready(controller.load(), digitalRead(NFC_IRQ) == HIGH);
}
static void ARDUINO_ISR_ATTR nfcInterrupt() {
  BaseType_t wake = pdFALSE;
  vTaskNotifyGiveFromISR(nfcTaskHandle, &wake);
  if (wake)
    portYIELD_FROM_ISR();
}
static bool waitForNfcIrq(uint32_t timeout) {
  uint32_t start = millis();
  for (;;) {
    // Level check covers IRQ asserted before registration and missed/stale notifications.
    if (irqReady())
      return true;
    uint32_t elapsed = millis() - start;
    if (elapsed >= timeout)
      return false;
    TickType_t ticks = pdMS_TO_TICKS(timeout - elapsed);
    ulTaskNotifyTake(pdTRUE, ticks ? ticks : 1);
  }
}
static Pn532 pn532(Wire, waitForNfcIrq);
const char *nfcControllerName() {
  switch (controller.load()) {
  case nfcProtocol::Controller::pn7160:
    return "PN7160";
  case nfcProtocol::Controller::pn532:
    return "PN532";
  default:
    return "none";
  }
}
static bool reactivate() {
  if (controller == nfcProtocol::Controller::pn532)
    return pn532.reactivate();
  if (nfc.readerReActivate())
    return false; // NCI SUCCESS is zero.
  return nfc.remoteDevice.getNFCIDLen() == selectedUidLength &&
         memcmp(nfc.remoteDevice.getNFCID(), selectedUid, selectedUidLength) == 0;
}
static SerLCD lcd;
static QwiicBuzzer buzzer;
static bool haveLcd = false, haveBuzzer = false;
// Fresh, bounded NCI exchange. Never inspect a buffer after timeout/short receive.
static bool command(const uint8_t *data, uint8_t length, uint8_t *answer, uint8_t &size) {
  size = 0;
  uint8_t frame[258] = {};
  unsigned drained = 0;
  while (nfc.hasMessage() && drained++ < 16) {
    nfc.readData(frame);
  }
  if (nfc.hasMessage())
    return false;
  frame[0] = 0;
  frame[1] = 0;
  frame[2] = length;
  memcpy(frame + 3, data, length);
  if (nfc.writeData(frame, length + 3) != 0)
    return false;
  uint32_t start = millis();
  while (millis() - start < 1000) {
    if (!waitForNfcIrq(100))
      continue;
    memset(frame, 0, sizeof(frame));
    uint32_t count = nfc.readData(frame);
    if (count >= 3 && count == uint32_t(frame[2]) + 3) {
      if (policy::completeDataFrame(frame, count)) {
        size = frame[2];
        memcpy(answer, frame + 3, size);
        return true;
      }
      if (frame[0] == 0x61 && frame[1] == 0x06)
        return false; // RF deactivation
    }
    delay(1);
  }
  return false;
}
static bool auth(const String &key, bool keyB = false) {
  if (controller == nfcProtocol::Controller::pn532) {
    uint8_t bytes[6];
    if (!decodeHex(key, bytes, sizeof(bytes)))
      return false;
    bool ok = pn532.authenticate(selectedUid, selectedUidLength, bytes, keyB);
    memset(bytes, 0, sizeof(bytes));
    return ok;
  }
  uint8_t cmd[9] = {0x40, 1, uint8_t(keyB ? 0x11 : 0x10)}, answer[255], size = 0;
  if (!decodeHex(key, cmd + 3, 6))
    return false;
  bool ok = command(cmd, 9, answer, size) && size == 2 && answer[0] == 0x40 && answer[1] == 0;
  memset(cmd, 0, sizeof(cmd));
  return ok;
}
static bool readBlock(uint8_t block, uint8_t *target) {
  if (controller == nfcProtocol::Controller::pn532)
    return pn532.readBlock(block, target);
  uint8_t cmd[] = {0x10, 0x30, block}, answer[255], size = 0;
  if (!command(cmd, 3, answer, size) || size != 18 || answer[0] != 0x10 || answer[17] != 0)
    return false;
  memcpy(target, answer + 1, 16);
  return true;
}
static bool writeBlock(uint8_t block, const uint8_t *data) {
  if (controller == nfcProtocol::Controller::pn532)
    return pn532.writeBlock(block, data);
  uint8_t cmd[] = {0x10, 0xA0, block}, answer[255], size = 0;
  if (!command(cmd, 3, answer, size) || size < 1 || answer[size - 1] != 0x14)
    return false;
  uint8_t payload[17] = {0x10};
  memcpy(payload + 1, data, 16);
  bool ok = command(payload, 17, answer, size) && size >= 1 && answer[size - 1] == 0x14;
  memset(payload, 0, 17);
  return ok;
}
static String digest(const String &uid, const uint8_t *contents) {
  uint8_t input[58], out[32];
  size_t n = uid.length() / 2;
  if (!decodeHex(uid, input, n))
    return "";
  memcpy(input + n, contents, 48);
  mbedtls_sha256(input, n + 48, out, 0);
  String s = hexBytes(out, 32);
  s.toLowerCase();
  return s;
}
static bool verify(const Card &card) {
  if (!auth(card.key))
    return false;
  uint8_t contents[48], trailer[16];
  for (int i = 0; i < 3; i++)
    if (!readBlock(4 + i, contents + 16 * i))
      return false;
  if (!readBlock(7, trailer) || trailer[6] != 0x78 || trailer[7] != 0x77 || trailer[8] != 0x88)
    return false;
  return digest(card.uid, contents) == card.digest;
}
static bool enroll(Card &card) {
  JsonDocument request, response;
  request["uid"] = card.uid;
  String text;
  serializeJson(request, text);
  if (!api("/reader/v1/enrollment/begin", response, text))
    return false;
  String ka = response["keyA"] | "", kb = response["keyB"] | "", expected = response["digest"] | "",
         enrollment = response["enrollmentId"] | "";
  uint8_t contents[48], existing[48], trailer[16] = {};
  if (!decodeHex(response["contents"] | "", contents, 48) || !decodeHex(ka, trailer, 6) ||
      !decodeHex(kb, trailer + 10, 6))
    return false;
  trailer[6] = 0x78;
  trailer[7] = 0x77;
  trailer[8] = 0x88;
  {
    Lock i(i2cMutex);
    // New keys first allow recovery after an interrupted trailer write.
    bool provisioned = auth(kb, true);
    if (!provisioned) {
      if (!reactivate())
        return false;
      if (!auth("FFFFFFFFFFFF"))
        return false;
    }
    for (int j = 0; j < 3; j++) {
      if (!readBlock(4 + j, existing + 16 * j))
        return false;
      bool blank = true, same = true;
      for (int k = 0; k < 16; k++) {
        blank &= existing[j * 16 + k] == 0 || existing[j * 16 + k] == 255;
        same &= existing[j * 16 + k] == contents[j * 16 + k];
      }
      if (!blank && !same)
        return false;
    }
    for (int j = 0; j < 3; j++)
      if (!writeBlock(4 + j, contents + 16 * j))
        return false;
    if (!provisioned && !writeBlock(7, trailer))
      return false;
    if (!reactivate())
      return false;
    Card probe = card;
    probe.key = ka;
    probe.digest = expected;
    if (!verify(probe))
      return false;
  }
  request.clear();
  request["uid"] = card.uid;
  request["enrollmentId"] = enrollment;
  request["digest"] = expected;
  text = "";
  serializeJson(request, text);
  memset(contents, 0, 48);
  memset(trailer, 0, 16);
  kb = "";
  response.clear();
  if (!api("/reader/v1/enrollment/commit", response, text))
    return false;
  card.sourceSequence = response["_sequence"];
  card.key = ka;
  card.digest = expected;
  refreshNeeded = true;
  return true;
}
static void feedback(bool success, const String &reason, const String &holder) {
  Lock i(i2cMutex);
  if (haveLcd) {
    lcd.clear();
    lcd.setBacklight(success ? 0x00FF00 : 0xFF0000);
    lcd.print(success ? "Access granted" : reason.substring(0, 16));
    lcd.setCursor(0, 1);
    lcd.print(holder.substring(0, 16));
  }
  if (haveBuzzer) {
    buzzer.configureBuzzer(success ? 2700 : 1500, success ? 100 : 350, 2);
    buzzer.on();
  }
}
static void scan(const String &uid, bool classic) {
  Card card;
  card.uid = uid;
  bool fresh = false;
  {
    Lock s(stateMutex);
    auto it = cards.find(uid.c_str());
    if (it != cards.end())
      card = it->second;
    if (!card.first && !card.pending) {
      card.first = clockTrusted ? epochMs() : 0;
      card.pending = !clockTrusted;
      card.observedUs = esp_timer_get_time() + 1;
      fresh = true;
    }
  }
  if (!card.refreshedAt || millis() - card.refreshedAt >= 300000 || refreshNeeded) {
    JsonDocument request, response;
    request["uid"] = uid;
    String text;
    serializeJson(request, text);
    if (api("/reader/v1/cards/lookup", response, text)) {
      card.refreshedAt = millis() + 1;
      card.sourceSequence = response["_sequence"];
      JsonObject o = response["document"];
      card.id = o["_id"] | "";
      card.holder = o["holder"] | "";
      card.member = o["memberId"] | "";
      card.status = o["validity"] | "unknown";
      card.expiry = o["expiry"] | int64_t(0);
      card.key = o["credential"]["keyA"] | "";
      card.digest = o["credential"]["digest"] | "";
      fresh = true;
    }
  }
  bool enrollment = false;
  {
    Lock s(stateMutex);
    enrollment = configuration["enrollmentEnabled"] | false;
  }
  if (classic && !card.id.isEmpty() && card.key.isEmpty() && enrollment && !maintenance &&
      !deviceBlocked) {
    bool enrolled = enroll(card);
    if (!enrolled)
      log_e("NFC enrollment failed; card was not authorized");
    fresh |= enrolled;
  }
  if (fresh) {
    {
      Lock s(stateMutex);
      auto current = cards.find(uid.c_str());
      if (current != cards.end()) {
        auto first = card.first;
        auto pending = card.pending;
        auto observed = card.observedUs;
        if (current->second.first || current->second.pending) {
          first = current->second.first;
          pending = current->second.pending;
          observed = current->second.observedUs;
        }
        if (current->second.sourceSequence > card.sourceSequence)
          card = current->second;
        card.first = first;
        card.pending = pending;
        card.observedUs = observed;
      }
      if (cards.count(uid.c_str()) || cards.size() < 300)
        cards[uid.c_str()] = card;
      recheckActiveTool();
    }
    saveState();
  }
  String reason;
  int64_t until = 0;
  bool allow = false;
  {
    Lock s(stateMutex);
    allow = authorize(card, reason, until);
  }
  if (!classic) {
    allow = false;
    reason = "unsupportedCard";
  }
  if (allow) {
    Lock i(i2cMutex);
    if (!verify(card)) {
      allow = false;
      reason = "authenticationFailed";
      log_e("NFC credential authentication/read verification failed");
    }
  }
  // Recheck authorization after RF/network work, including concurrent invalidations.
  if (allow) {
    Lock s(stateMutex);
    auto it = cards.find(uid.c_str());
    if (it == cards.end() || it->second.key != card.key || it->second.digest != card.digest) {
      allow = false;
      reason = "credentialChanged";
    } else
      allow = authorize(it->second, reason, until);
  }

  if (allow) {
    Lock s(stateMutex); // Final decision and relay transition share the state lock.
    auto it = cards.find(uid.c_str());
    if (it != cards.end() && it->second.key == card.key && it->second.digest == card.digest &&
        it->second.id == card.id && authorize(it->second, reason, until)) {
      String role = configuration["role"] | "";
      if (role == "tool") {
        if (relayOn)
          stopRelay();
        else
          startRelay(until - epochMs(), until, uid);
      } else {
        startRelay(int64_t(policy::pulseSeconds(role.c_str())) * 1000, 0, "");
      }
      accepted++;
    } else {
      allow = false;
      if (reason == "activeMember")
        reason = "credentialChanged";
      rejected++;
    }
  } else
    rejected++;
  enqueueEvent(card, allow, reason);
  feedback(allow, reason, card.holder);
}
static bool probe(uint8_t address) {
  for (int attempt = 0; attempt < 3; ++attempt) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0)
      return true;
    delay(2); // Also allow a sleeping I2C controller to wake.
  }
  return false;
}
static bool initializeNfc() {
  detachInterrupt(NFC_IRQ);
  controller = nfcProtocol::Controller::none;
  selectedUidLength = 0;
  pinMode(NFC_RESET, OUTPUT);
  digitalWrite(NFC_RESET, LOW);
  delay(2);
  digitalWrite(NFC_RESET, HIGH);
  delay(10);
  bool have7160 = probe(0x28);
  controller = nfcProtocol::select(have7160, !have7160 && probe(Pn532::address));
  bool ok = false;
  if (controller == nfcProtocol::Controller::pn7160) {
    log_i("I2C device found: PN7160 at 0x28");
    pinMode(NFC_IRQ, INPUT_PULLDOWN);
    attachInterrupt(NFC_IRQ, nfcInterrupt, RISING);
    nfc.setI2CPins(1, 2);
    nfc.setVerboseTrace(false);
    nfc.setPn7160FixedVbat3V3(false);
    ok =
        !nfc.connectNCI() && !nfc.configureSettings() && !nfc.configMode() && !nfc.startDiscovery();
  } else if (controller == nfcProtocol::Controller::pn532) {
    log_i("I2C device found: PN532 at 0x24");
    pinMode(NFC_IRQ, INPUT_PULLUP);
    attachInterrupt(NFC_IRQ, nfcInterrupt, FALLING);
    ok = pn532.begin() && pn532.startDetection();
  }
  Wire.setClock(100000);
  Wire.setTimeOut(50);
  if (ok)
    log_i("NFC reader initialized successfully: %s", nfcControllerName());
  else if (controller == nfcProtocol::Controller::none)
    log_e("No NFC reader found: PN7160 at 0x28 and PN532 at 0x24 did not respond; retrying");
  else
    log_e("NFC reader initialization failed: %s; retrying", nfcControllerName());
  // A present but failing PN7160 is retried, never silently bypassed by PN532.
  return ok;
}
void nfcTask(void *) {
  nfcTaskHandle = xTaskGetCurrentTaskHandle();
  {
    Lock i(i2cMutex);
    Wire.beginTransmission(0x72);
    haveLcd = Wire.endTransmission() == 0;
    if (haveLcd) {
      log_i("I2C device found: SerLCD at 0x72");
      lcd.begin(Wire);
      lcd.setBacklight(0x0000FF);
      lcd.print("Starting reader");
    }
    haveBuzzer = buzzer.begin();
    if (haveBuzzer)
      log_i("I2C device found: Qwiic buzzer at 0x34");
    nfcOK = initializeNfc();
  }
  String previous;
  uint32_t lastSeen = 0;
  for (;;) {
    if (maintenance) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    if (!nfcOK) {
      vTaskDelay(pdMS_TO_TICKS(3000));
      Lock i(i2cMutex);
      nfcOK = initializeNfc();
      continue;
    }
    // Timeout handles removal debounce/maintenance only. No reader I2C traffic
    // or tag-detection call occurs while its IRQ is inactive.
    if (!waitForNfcIrq(100)) {
      if (millis() - lastSeen >= 500)
        previous = "";
      continue;
    }
    bool detected = false, classic = false;
    String uid;
    {
      Lock i(i2cMutex);
      if (!irqReady())
        continue; // Recheck after waiting for the shared bus.
      if (controller == nfcProtocol::Controller::pn7160) {
        detected = nfc.isTagDetected(50);
        if (detected) {
          selectedUidLength = nfc.remoteDevice.getNFCIDLen();
          if (selectedUidLength != 4 && selectedUidLength != 7 && selectedUidLength != 10) {
            log_e("PN7160 returned invalid UID length: %u", unsigned(selectedUidLength));
            nfcOK = false;
            continue;
          }
          memcpy(selectedUid, nfc.remoteDevice.getNFCID(), selectedUidLength);
          classic = nfc.remoteDevice.getProtocol() == nfc.protocol.MIFARE;
        }
      } else {
        nfcOK = pn532.readDetection(selectedUid, selectedUidLength, classic);
        detected = nfcOK && selectedUidLength != 0;
        if (nfcOK && !detected)
          nfcOK = pn532.startDetection();
        if (!nfcOK)
          log_e("PN532 detection/rearm failed");
      }
      if (detected)
        uid = hexBytes(selectedUid, selectedUidLength);
    }
    if (detected) {
      if (uid != previous)
        scan(uid, classic);
      previous = uid;
      lastSeen = millis();
      {
        Lock i(i2cMutex);
        nfcOK = controller == nfcProtocol::Controller::pn7160
                    ? nfc.reset()
                    : pn532.release() && pn532.startDetection();
        if (!nfcOK)
          log_e("NFC reader reset/rearm failed: %s", nfcControllerName());
      }
    } else if (millis() - lastSeen >= 500)
      previous = "";
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
