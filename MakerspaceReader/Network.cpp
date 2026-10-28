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
  if (!String(API_URL).startsWith("https://") || !ETH.hasIP() || epochMs() < 1700000000000LL)
    return false;
  NetworkClientSecure tls;
  tls.setCACert(SERVER_CA);
  tls.setCertificate(CLIENT_CERT);
  tls.setPrivateKey(CLIENT_KEY);
  HTTPClient http;
  http.setConnectTimeout(2000);
  http.setTimeout(3000);
  if (!http.begin(tls, String(API_URL) + path))
    return false;
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
      ok = false;
      deviceBlocked = true;
      prefs.putBool("blocked", true);
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
      prefs.putBool("blocked", true);
      digitalWrite(RELAY_PIN, LOW);
      relayOn = false;
    }
  }
  if (!ok) {
    Lock s(stateMutex);
    lastError = "API " + String(code) + " " + path;
  }
  return ok;
}
static void confirmTime(int64_t t) {
  if (t < 1700000000000LL)
    return;
  if (clockTrusted && t + 5000 < epochMs()) {
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
    prefs.putULong64("lastTime", t);
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
    prefs.putBool("blocked", false);
  }
  if (result["cards"].size() > 300 || result["grants"].size() > 150)
    return;
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
    if (relayOn && toolDeadline > 0) {
      auto c = cards.find(activeUid.c_str());
      String reason;
      int64_t until = 0;
      if (oldTool != String(configuration["toolId"] | "") ||
          oldRole != String(configuration["role"] | "") || c == cards.end() ||
          !authorize(c->second, reason, until)) {
        digitalWrite(RELAY_PIN, LOW);
        relayOn = false;
        toolDeadline = 0;
      } else
        toolDeadline = until;
    }
  }
  if (changed)
    saveState();
  lastRefresh = millis();
}
static std::vector<String> eventFiles() {
  std::vector<String> files;
  Lock f(fsMutex);
  File dir = LittleFS.open("/events");
  if (!dir)
    return files;
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
      total += ((file.size() + 4095) / 4096) * 4096;
    }
  }
  for (auto &p : files) {
    Lock f(fsMutex);
    if (total + incoming <= 3 * 1024 * 1024 &&
        LittleFS.usedBytes() + incoming + 640 * 1024 < LittleFS.totalBytes())
      break;
    File file = LittleFS.open(p);
    size_t size = ((file.size() + 4095) / 4096) * 4096;
    file.close();
    if (LittleFS.remove(p)) {
      total -= size;
      dropped++;
      prefs.putUInt("dropped", dropped);
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
    if (!seq || prefs.putULong64("eventSeq", seq) != 8) {
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
    prefs.putUInt("dropped", dropped);
    storageOK = false;
  }
}
static void mqttEvent(void *, esp_event_base_t, int32_t id, void *data) {
  auto *e = (esp_mqtt_event_handle_t)data;
  if (id == MQTT_EVENT_CONNECTED) {
    mqttOK = true;
    esp_mqtt_client_subscribe(mqtt, "cards/#", 1);
    esp_mqtt_client_subscribe(mqtt, "readers/all/invalidate", 1);
    refreshNeeded = true;
  }
  if (id == MQTT_EVENT_DISCONNECTED) {
    mqttOK = false;
    int reset = -1;
    xQueueSend(mqttAcks, &reset, 0);
  }
  if (id == MQTT_EVENT_PUBLISHED) {
    int msg = e->msg_id;
    xQueueSend(mqttAcks, &msg, 0);
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
      return;
    }
    if (mqttTopic != "cards/update" && mqttTopic != "cards/insert" && mqttTopic != "cards/delete")
      return;
    int start = mqttFragment.indexOf('{');
    if (start < 0)
      return;
    JsonDocument d;
    if (!deserializeJson(d, mqttFragment.substring(start)) && d["document"].is<JsonObject>())
      refreshNeeded = true;
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
      Lock f(fsMutex);
      if (LittleFS.remove(path)) {
        dropped++;
        prefs.putUInt("dropped", dropped);
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
      }
    }
    if (e["dbAck"].as<bool>() && e["mqttAck"].as<bool>()) {
      Lock f(fsMutex);
      LittleFS.remove(path);
    }
  }
}
void networkTask(void *) {
  mqttAcks = xQueueCreate(32, sizeof(int));
  {
    Lock f(fsMutex);
    LittleFS.mkdir("/events");
  }
  esp_mqtt_client_config_t cfg = {};
  cfg.broker.address.uri = MQTT_URL;
  cfg.broker.verification.certificate = SERVER_CA;
  cfg.credentials.client_id = DEVICE_ID;
  cfg.credentials.authentication.certificate = CLIENT_CERT;
  cfg.credentials.authentication.key = CLIENT_KEY;
  if (String(MQTT_URL).startsWith("mqtts://"))
    mqtt = esp_mqtt_client_init(&cfg);
  if (mqtt)
    esp_mqtt_client_register_event(mqtt, MQTT_EVENT_ANY, mqttEvent, nullptr);
  uint32_t next = 0, backoff = 1000;
  for (;;) {
    if (ETH.hasIP() && epochMs() > 1700000000000LL) {
      if (mqtt && !mqttStarted) {
        esp_mqtt_client_start(mqtt);
        mqttStarted = true;
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
