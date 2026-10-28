#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <atomic>
#include <deque>
#include <unordered_map>
#include <vector>
constexpr uint8_t RELAY_PIN = 21;
struct Card {
  String uid, id, member, holder, status, key, digest;
  int64_t expiry = 0, first = 0;
  bool pending = false;
  int64_t observedUs = 0;
  uint32_t refreshedAt = 0, sourceSequence = 0;
};
struct Grant {
  String member, id;
  int64_t until = 0;
};
extern SemaphoreHandle_t stateMutex, fsMutex, i2cMutex, apiMutex, snapshotMutex;
struct Lock {
  SemaphoreHandle_t m;
  Lock(SemaphoreHandle_t x) : m(x) {
    xSemaphoreTake(m, portMAX_DELAY);
  }
  ~Lock() {
    xSemaphoreGive(m);
  }
};
extern std::unordered_map<std::string, Card> cards;
extern std::unordered_map<std::string, Grant> grants;
extern JsonDocument configuration;
extern uint32_t configurationSequence;
extern std::atomic<bool> maintenance, relayOn, clockTrusted, refreshNeeded, storageOK, apiOK,
    mqttOK, nfcOK, deviceBlocked;
extern std::atomic<int64_t> relayDeadline, toolDeadline;
extern std::atomic<uint32_t> accepted, rejected, dropped;
extern String activeUid, lastError, csrf;
extern Preferences prefs;
int64_t epochMs();
void trustTime(int64_t t);
extern bool productionReady;
bool api(const String &path, JsonDocument &response, const String &body = "");
extern bool cacheKeyReady; // Set once before workers; never exposes key material.
bool initializeStorage();
bool saveState();
void loadState();
void recoverStorage();
bool sealWrite(const String &path, const String &data);
bool sealRead(const String &path, String &data);
void enqueueEvent(const Card &card, bool allow, const String &reason);
void networkTask(void *);
void nfcTask(void *);
const char *nfcControllerName();
void webSetup();
void readerSetup();
void readerLoop();
bool authorize(const Card &, String &reason, int64_t &until);
String hexBytes(const uint8_t *, size_t);
bool decodeHex(const String &, uint8_t *, size_t);
