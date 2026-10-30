#pragma once
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <string>
#include <unordered_map>
using String = std::string;
constexpr int RELAY_PIN = 21, LOW = 0, HIGH = 1;
extern bool stateHeld;
extern int stateMutex, gpio;
struct Lock {
  explicit Lock(int) { assert(!stateHeld); stateHeld = true; }
  ~Lock() { stateHeld = false; }
};
struct Card { bool valid = true, credential = true; int64_t expiry = 100000; };
extern std::unordered_map<std::string, Card> cards;
extern std::atomic<bool> relayOn, maintenance, deviceBlocked, clockTrusted;
extern std::atomic<int64_t> relayDeadline, toolDeadline, relayStartedUs;
extern String activeUid;
int64_t epochMs();
bool authorize(const Card &, String &, int64_t &);
inline void digitalWrite(int, int level) { assert(stateHeld); gpio = level; }
void stopRelay();
void startRelay(int64_t, int64_t, const String &);
void recheckActiveTool();
void serviceRelay();

inline void endToolSession(const char * = "automatic") {}
