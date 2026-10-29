#include "Reader.h"
#include "secrets.h"
#include <ETH.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <NetworkClientSecure.h>
#include <RTClib.h>
#include <esp_sntp.h>
#include <esp_timer.h>
#include <mbedtls/sha256.h>
#include <mqtt_client.h>
extern RTC_PCF8523 rtc;
extern bool rtcPresent;
static esp_mqtt_client_handle_t mqtt = nullptr;
static QueueHandle_t mqttAcks;
static String mqttFragment, mqttTopic;
static uint32_t lastRefresh = 0;
static std::unordered_map<int, String> inFlight;
static std::unordered_map<int, uint32_t> inFlightAt;
static std::atomic<bool> mqttStarted(false);
bool api(const String &path, JsonDocument &response, const String &body) {
  Lock l(apiMutex);
  if (!String(API_URL).startsWith("https://") || !ETH.hasIP() || epochMs() < 1700000000000LL) {
    log_e("MongoDB API %s unavailable: HTTPS=%d, Ethernet IP=%d, TLS clock ready=%d",
          path.c_str(), String(API_URL).startsWith("https://"), ETH.hasIP(),
          epochMs() >= 1700000000000LL);
    apiOK = false;
    return false;
  }
  NetworkClientSecure tls;
  tls.setCACert(SERVER_CA);
  tls.setCertificate(CLIENT_CERT);
  tls.setPrivateKey(CLIENT_KEY);
  HTTPClient http;
  http.setConnectTimeout(2000);
  http.setTimeout(3000);
  if (!http.begin(tls, String(API_URL) + path)) {
    log_e("MongoDB API HTTPS initialization failed: %s", path.c_str());
    apiOK = false;
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  int code = body.length() ? http.POST(body) : http.GET();
  bool ok = false;
  if (code == 200 && http.getSize() >= 0 && http.getSize() <= 524288) {
    String raw = http.getString();
    if (raw.length() <= 524288)
      ok = !deserializeJson(response, raw);
  }
  if (ok && (path == "/reader/v1/snapshot" || path == "/reader/v1/config")) {
    String id = path == "/reader/v1/snapshot" ? String(response["config"]["id"] | "")
                                              : String(response["id"] | "");
    if (id != DEVICE_ID) {
      log_e("MongoDB API returned a different reader identity: %s", path.c_str());
      ok = false;
      deviceBlocked = true;
      checkNvsWrite("blocked", prefs.putBool("blocked", true), 1);
    }
  }
  static uint32_t responseSequence = 0;
  if (ok)
    response["_sequence"] = ++responseSequence;
  http.end();
  apiOK = ok;
  if (code == 401 || code == 403 || code == 409) {
    // A rejected event is retained; rejected reader authorization disables cached access.
    if (path == "/reader/v1/snapshot" || path == "/reader/v1/cards/lookup") {
      deviceBlocked = true;
      checkNvsWrite("blocked", prefs.putBool("blocked", true), 1);
      Lock s(stateMutex);
      stopRelay();
    }
  }
  if (!ok) {
    log_e("MongoDB API %s failed: HTTP/transport code=%d (200 means invalid response)", path.c_str(), code);
    Lock s(stateMutex);
    lastError = "API " + String(code) + " " + path;
  }
  return ok;
}
static void confirmTime(int64_t t) {
  if (t < 1700000000000LL)
    return;
  if (clockTrusted && t + 5000 < epochMs()) {
    log_e("API time moved backwards; authorization clock no longer trusted");
    clockTrusted = false;
    return;
  }
  trustTime(t);
  esp_sntp_stop();
  bool resolved = false;
  {
    Lock s(stateMutex);
    for (auto &pair : cards) {
      auto &c = pair.second;
      if (c.pending && c.observedUs > 0) {
        c.first = t - (esp_timer_get_time() - c.observedUs) / 1000;
        c.pending = false;
        c.observedUs = 0;
        resolved = true;
      }
    }
  }
  if (resolved)
    saveState();
  if (t > int64_t(prefs.getULong64("lastTime", 0)) + 3600000) {
    checkNvsWrite("lastTime", prefs.putULong64("lastTime", t), 8);
    if (rtcPresent) {
      Lock i(i2cMutex);
      rtc.adjust(DateTime(uint32_t(t / 1000)));
    }
  }
}
static void refresh() {
  lastRefresh = millis();
  JsonDocument result;
  if (!api("/reader/v1/snapshot", result)) {
    refreshNeeded = true;
    return;
  }
  if (deviceBlocked) {
    deviceBlocked = false;
    checkNvsWrite("blocked", prefs.putBool("blocked", false), 1);
  }
  if (result["cards"].size() > 300 || result["grants"].size() > 150) {
    apiOK = false;
    log_e("MongoDB API snapshot exceeds cache capacity");
    return;
  }
  confirmTime(result["serverTime"] | int64_t(0));
  bool changed = false;
  String old, newState;
  {
    Lock l(stateMutex);
    uint32_t sequence = result["_sequence"];
    if (sequence < configurationSequence) {
      refreshNeeded = true;
      return;
    }
    configurationSequence = sequence;
    serializeJson(configuration, old);
    String oldTool = configuration["toolId"] | "", oldRole = configuration["role"] | "";
    configuration.set(result["config"]);
    configuration.remove("serverTime");
    configuration["toolAvailable"] = result["tool"]["available"] | false;
    configuration["toolName"] = result["tool"]["name"] | "";
    if (prefs.getBool("enrollOff", false))
      configuration["enrollmentEnabled"] = false;
    serializeJson(configuration, newState);
    changed = old != newState;
    std::unordered_map<std::string, Card> next;
    for (JsonObject o : result["cards"].as<JsonArray>()) {
      Card c;
      c.refreshedAt = millis() + 1;
      c.sourceSequence = sequence;
      c.uid = o["uid"] | "";
      c.id = o["_id"] | "";
      c.member = o["memberId"] | "";
      c.holder = o["holder"] | "";
      c.status = o["validity"] | "unknown";
      c.expiry = o["expiry"] | int64_t(0);
      c.key = o["credential"]["keyA"] | "";
      c.digest = o["credential"]["digest"] | "";
      auto previous = cards.find(c.uid.c_str());
      if (previous != cards.end()) {
        if (previous->second.sourceSequence > sequence)
          c = previous->second;
        c.first = previous->second.first;
        c.pending = previous->second.pending;
        c.observedUs = previous->second.observedUs;
        auto &p = previous->second;
        changed |= p.id != c.id || p.holder != c.holder || p.status != c.status ||
                   p.expiry != c.expiry || p.key != c.key || p.digest != c.digest ||
                   p.member != c.member;
      } else
        changed = true;
      next[c.uid.c_str()] = c;
    }
    // Keep only station observation tombstones when cards disappear.
    for (auto &pair : cards)
      if (!next.count(pair.first) && next.size() < 300 &&
          (pair.second.first || pair.second.pending)) {
        Card c;
        if (pair.second.sourceSequence > sequence)
          c = pair.second;
        c.uid = pair.second.uid;
        c.first = pair.second.first;
        c.pending = pair.second.pending;
        c.observedUs = pair.second.observedUs;
        changed |= !pair.second.id.isEmpty();
        next[pair.first] = c;
      }
    changed |= next.size() != cards.size();
    cards.swap(next);
    std::unordered_map<std::string, Grant> ng;
    for (JsonObject o : result["grants"].as<JsonArray>()) {
      Grant g;
      g.member = o["memberId"] | "";
      g.id = o["checkoutId"] | "";
      g.until = o["until"] | int64_t(0);
      auto p = grants.find(g.member.c_str());
      changed |= p == grants.end() || p->second.id != g.id || p->second.until != g.until;
      ng[g.member.c_str()] = g;
    }
    changed |= ng.size() != grants.size();
    grants.swap(ng);
    if (relayOn && toolDeadline > 0 &&
        (oldTool != String(configuration["toolId"] | "") ||
         oldRole != String(configuration["role"] | "")))
      stopRelay();
    recheckActiveTool();
  }
  if (changed)
    saveState();
  lastRefresh = millis();
}
static std::vector<String> eventFiles() {
  std::vector<String> files;
  Lock f(fsMutex);
  File dir = LittleFS.open("/events");
  if (!dir) {
    log_e("Cannot open event spool directory");
    return files;
  }
  File file = dir.openNextFile();
  while (file) {
    String p = file.path();
    if (!p.endsWith(".tmp"))
      files.push_back(p);
    file = dir.openNextFile();
  }
  std::sort(files.begin(), files.end());
  return files;
}
// Enforced before append, including while Ethernet/API/MQTT are unavailable.
// LittleFS allocates blocks, so also reserve physical free space for both snapshots.
static void reserveEventSpace(size_t incoming) {
  auto files = eventFiles();
  size_t total = 0;
  {
    Lock f(fsMutex);
    for (auto &p : files) {
      File file = LittleFS.open(p);
      if (!file) {
        log_e("Cannot read spool file size: %s", p.c_str());
        continue;
      }
      total += ((file.size() + 4095) / 4096) * 4096;
    }
  }
  for (auto &p : files) {
    Lock f(fsMutex);
    if (total + incoming <= 3 * 1024 * 1024 &&
        LittleFS.usedBytes() + incoming + 640 * 1024 < LittleFS.totalBytes())
      break;
    File file = LittleFS.open(p);
    if (!file) {
      log_e("Cannot open spool file for eviction: %s", p.c_str());
      continue;
    }
    size_t size = ((file.size() + 4095) / 4096) * 4096;
    file.close();
    if (LittleFS.remove(p)) {
      total -= size;
      dropped++;
      checkNvsWrite("dropped", prefs.putUInt("dropped", dropped), 4);
      log_e("Event spool capacity exhausted; oldest audit event discarded");
    } else {
      log_e("Cannot remove spool file to reclaim space: %s", p.c_str());
    }
  }
}
void enqueueEvent(const Card &card, bool allow, const String &reason) {
  JsonDocument e;
  String seed;
  uint64_t seq;
  {
    Lock f(fsMutex);
    seq = prefs.getULong64("eventSeq", 0) + 1;
    if (!seq || !checkNvsWrite("eventSeq", prefs.putULong64("eventSeq", seq), 8)) {
      log_e("Cannot reserve NVS event sequence; audit event dropped");
      dropped++;
      storageOK = false;
      return;
    }
    seed = String(DEVICE_ID) + ":" + String((unsigned long long)seq);
  }
  uint8_t hash[32];
  mbedtls_sha256((const uint8_t *)seed.c_str(), seed.length(), hash, 0);
  String id = hexBytes(hash, 12);
  id.toLowerCase();
  e["collection"] = allow ? "checkins" : "rejections";
  auto d = e["document"].to<JsonObject>();
  d["_id"] = id;
  d["uid"] = card.uid;
  if (card.holder.length())
    d["holder"] = card.holder;
  else
    d["holder"] = nullptr;
  d["validity"] = allow ? "activeMember" : reason;
  d["reason"] = reason;
  {
    Lock l(stateMutex);
    d["where"] = configuration["where"] | "doorboto32";
    d["reader"] = configuration["name"] | DEVICE_ID;
  }
  d["delay"] = 0;
  d["time"] = clockTrusted ? epochMs() : 0;
  e["dbAck"] = false;
  e["mqttAck"] = !allow;
  String payload;
  serializeJson(e, payload);
  reserveEventSpace(8192);
  char prefix[22];
  snprintf(prefix, sizeof(prefix), "%020llu-", static_cast<unsigned long long>(seq));
  if (!sealWrite("/events/" + String(prefix) + id, payload)) {
    dropped++;
    checkNvsWrite("dropped", prefs.putUInt("dropped", dropped), 4);
    storageOK = false;
  }
}
static void mqttEvent(void *, esp_event_base_t, int32_t id, void *data) {
  auto *e = (esp_mqtt_event_handle_t)data;
  if (id == MQTT_EVENT_CONNECTED) {
    mqttOK = true;
    log_i("MQTT connected");
    if (esp_mqtt_client_subscribe(mqtt, "cards/#", 1) < 0)
      log_e("MQTT subscribe failed: cards/#");
    if (esp_mqtt_client_subscribe(mqtt, "readers/all/invalidate", 1) < 0)
      log_e("MQTT subscribe failed: readers/all/invalidate");
    refreshNeeded = true;
  }
  if (id == MQTT_EVENT_DISCONNECTED) {
    mqttOK = false;
    log_e("MQTT disconnected; unacknowledged publications will be retried");
    int reset = -1;
    if (xQueueSend(mqttAcks, &reset, 0) != pdTRUE)
      log_e("MQTT acknowledgment queue full during disconnect");
  }
  if (id == MQTT_EVENT_PUBLISHED) {
    int msg = e->msg_id;
    if (xQueueSend(mqttAcks, &msg, 0) != pdTRUE)
      log_e("MQTT acknowledgment queue full; message %d will be retried", msg);
  }
  if (id == MQTT_EVENT_ERROR) {
    if (e->error_handle)
      log_e("MQTT error: type=%d, TLS=%d, TLS stack=%d, socket errno=%d, broker code=%d",
            int(e->error_handle->error_type), int(e->error_handle->esp_tls_last_esp_err),
            e->error_handle->esp_tls_stack_err, e->error_handle->esp_transport_sock_errno,
            int(e->error_handle->connect_return_code));
    else
      log_e("MQTT error without transport details");
  }
  if (id == MQTT_EVENT_DATA) {
    if (e->total_data_len > 8192)
      return;
    if (e->current_data_offset == 0) {
      mqttFragment = "";
      mqttTopic = String(e->topic, e->topic_len);
    }
    if (size_t(e->current_data_offset) != mqttFragment.length())
      return;
    mqttFragment += String(e->data, e->data_len);
    if (mqttFragment.length() != size_t(e->total_data_len))
      return;
    if (mqttTopic == "readers/all/invalidate") {
      refreshNeeded = true;
      Lock s(stateMutex);
      if (relayOn && toolDeadline > 0)
        stopRelay();
      return;
    }
    if (mqttTopic != "cards/update" && mqttTopic != "cards/insert" && mqttTopic != "cards/delete")
      return;
    int start = mqttFragment.indexOf('{');
    if (start < 0)
      return;
    JsonDocument d;
    if (!deserializeJson(d, mqttFragment.substring(start)) && d["document"].is<JsonObject>()) {
      refreshNeeded = true;
      Lock s(stateMutex);
      // MQTT is an invalidation hint, never authority to install credentials.
      // Missing UID (e.g. delete by ID) conservatively invalidates any active tool.
      String uid = d["document"]["uid"] | "";
      if (relayOn && toolDeadline > 0 && (uid.isEmpty() || uid == activeUid))
        stopRelay();
    }
  }
}
static void spool() {
  int msg;
  while (xQueueReceive(mqttAcks, &msg, 0)) {
    if (msg == -1) {
      inFlight.clear();
      inFlightAt.clear();
      continue;
    }
    auto it = inFlight.find(msg);
    if (it == inFlight.end())
      continue;
    String raw;
    JsonDocument e;
    if (sealRead(it->second, raw) && !deserializeJson(e, raw)) {
      e["mqttAck"] = true;
      raw = "";
      serializeJson(e, raw);
      sealWrite(it->second, raw);
    }
    inFlightAt.erase(it->first);
    inFlight.erase(it);
  }
  for (auto it = inFlightAt.begin(); it != inFlightAt.end();) {
    if (millis() - it->second > 60000) {
      log_e("MQTT publish acknowledgment timed out: message %d", it->first);
      inFlight.erase(it->first);
      it = inFlightAt.erase(it);
    } else
      ++it;
  }
  static String cursor;
  auto files = eventFiles();
  std::rotate(files.begin(), std::upper_bound(files.begin(), files.end(), cursor), files.end());
  unsigned dbAttempts = 0, mqttAttempts = 0;
  for (auto &path : files) {
    String raw;
    JsonDocument e;
    if (!sealRead(path, raw) || deserializeJson(e, raw)) {
      log_e("Corrupt/unreadable audit spool record: %s", path.c_str());
      Lock f(fsMutex);
      if (LittleFS.remove(path)) {
        dropped++;
        checkNvsWrite("dropped", prefs.putUInt("dropped", dropped), 4);
      } else {
        log_e("Cannot remove corrupt spool file: %s", path.c_str());
      }
      continue;
    }
    if (!e["mqttAck"].as<bool>() && mqttOK && inFlight.size() < 8 && mqttAttempts < 5) {
      bool pending = false;
      for (auto &i : inFlight)
        pending |= i.second == path;
      if (!pending) {
        JsonDocument d;
        d.set(e["document"]);
        String oid = d["_id"].as<String>();
        d.remove("_id");
        d["_id"]["$oid"] = oid;
        int64_t t = d["time"];
        d.remove("time");
        d["time"]["$numberDouble"] = String((long long)t);
        d["delay"].clear();
        d["delay"]["$numberInt"] = "0";
        if (t)
          d["timeOf"]["$date"]["$numberLong"] = String((long long)t);
        else
          d["timeOf"] = nullptr;
        d["timeQuality"] = t ? "trusted" : "unavailable";
        d["deviceId"]["$oid"] = DEVICE_ID;
        String payload;
        serializeJson(d, payload);
        int id = esp_mqtt_client_enqueue(mqtt, "activity/checkins", payload.c_str(),
                                         payload.length(), 1, 0, true);
        if (id >= 0) {
          inFlight[id] = path;
          inFlightAt[id] = millis();
          mqttAttempts++;
        } else {
          mqttAttempts++;
          log_e("MQTT publish enqueue failed: activity/checkins (code=%d)", id);
        }
      }
    }
    if (!e["dbAck"].as<bool>() && dbAttempts < 2) {
      dbAttempts++;
      cursor = path;
      JsonDocument request, response;
      request["collection"] = e["collection"];
      request["document"].set(e["document"]);
      String body;
      serializeJson(request, body);
      if (api("/reader/v1/events", response, body) && response["acknowledged"].as<bool>()) {
        e["dbAck"] = true;
        raw = "";
        serializeJson(e, raw);
        sealWrite(path, raw);
      } else {
        apiOK = false;
        log_e("MongoDB API did not acknowledge event; retained for retry");
      }
    }
    if (e["dbAck"].as<bool>() && e["mqttAck"].as<bool>()) {
      Lock f(fsMutex);
      if (!LittleFS.remove(path))
        log_e("Cannot remove acknowledged spool file: %s", path.c_str());
    }
  }
}
void networkTask(void *) {
  mqttAcks = xQueueCreate(32, sizeof(int));
  if (!mqttAcks) {
    log_e("Cannot allocate MQTT acknowledgment queue; network task stopped");
    vTaskDelete(nullptr);
    return;
  }
  {
    Lock f(fsMutex);
    if (!LittleFS.exists("/events") && !LittleFS.mkdir("/events"))
      log_e("Cannot create event spool directory");
  }
  esp_mqtt_client_config_t cfg = {};
  cfg.broker.address.uri = MQTT_URL;
  cfg.broker.verification.certificate = SERVER_CA;
  cfg.credentials.client_id = DEVICE_ID;
  cfg.credentials.authentication.certificate = CLIENT_CERT;
  cfg.credentials.authentication.key = CLIENT_KEY;
  if (String(MQTT_URL).startsWith("mqtts://"))
    mqtt = esp_mqtt_client_init(&cfg);
  if (!mqtt) {
    log_e("MQTT client initialization failed; check mqtts:// configuration and memory");
  } else {
    esp_err_t rc = esp_mqtt_client_register_event(mqtt, MQTT_EVENT_ANY, mqttEvent, nullptr);
    if (rc != ESP_OK) {
      log_e("MQTT event registration failed: %s", esp_err_to_name(rc));
      esp_mqtt_client_destroy(mqtt);
      mqtt = nullptr;
    }
  }
  uint32_t next = 0, backoff = 1000, lastMqttStart = 0;
  for (;;) {
    if (ETH.hasIP() && epochMs() > 1700000000000LL) {
      if (mqtt && !mqttStarted && (!lastMqttStart || millis() - lastMqttStart >= 5000)) {
        lastMqttStart = millis();
        esp_err_t rc = esp_mqtt_client_start(mqtt);
        mqttStarted = rc == ESP_OK;
        if (rc != ESP_OK)
          log_e("MQTT client start failed: %s", esp_err_to_name(rc));
      }
      if (!maintenance && (lastRefresh == 0 || millis() - lastRefresh >= 300000 ||
                           (refreshNeeded && millis() - lastRefresh >= 5000))) {
        refreshNeeded = false;
        refresh();
      }
      if (int32_t(millis() - next) >= 0) {
        spool();
        backoff = apiOK ? 1000 : std::min(backoff * 2, uint32_t(60000));
        next = millis() + backoff;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}
