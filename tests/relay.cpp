#include "relay-fakes/Reader.h"
#include "relay-fakes/Ticker.h"
#include <iostream>
bool stateHeld = false;
int stateMutex = 0, gpio = LOW;
int64_t monotonicUs = 1000000, epochOffset = 0, grantUntil = 100000;
std::atomic<bool> relayOn(false), maintenance(false), deviceBlocked(false), clockTrusted(true);
std::atomic<int64_t> relayDeadline(0), toolDeadline(0);
String activeUid;
std::unordered_map<std::string, Card> cards;
uint64_t timerMs = 0;
std::function<void()> timerCallback;
bool timerArmed = false;
int64_t epochMs() { return monotonicUs / 1000 + epochOffset; }
bool authorize(const Card &c, String &, int64_t &until) {
  assert(stateHeld);
  until = std::min(c.expiry, grantUntil);
  return c.valid && c.credential && until > epochMs();
}
static void start(int64_t duration, int64_t until = 0) {
  Lock s(stateMutex);
  startRelay(duration, until, until ? "A" : "");
}
static void recheck() { Lock s(stateMutex); recheckActiveTool(); }
static void off() {
  assert(!relayOn && gpio == LOW && !timerArmed);
  assert(relayDeadline == 0 && toolDeadline == 0 && activeUid.empty());
}
int main() {
  start(30000);
  assert(timerArmed && timerMs == 30000 && gpio == HIGH);
  auto oldCallback = timerCallback;
  monotonicUs += 29000000;
  start(30000);
  auto renewed = relayDeadline.load();
  oldCallback();
  serviceRelay();
  assert(relayOn && relayDeadline == renewed);
  monotonicUs += 30000000;
  timerCallback(); serviceRelay(); off();
  start(30000);
  assert(relayOn && relayDeadline > monotonicUs && timerArmed);
  monotonicUs += 30000000;
  serviceRelay(); off(); // Deadline fallback if callback delivery fails.
  cards["A"] = Card{};
  cards["A"].expiry = epochMs() + 100000;
  grantUntil = cards["A"].expiry;
  start(100000, grantUntil);
  cards["A"].valid = false; recheck(); off();
  cards["A"].valid = true;
  start(100000, grantUntil);
  cards["A"].credential = false; recheck(); off();
  cards["A"].credential = true;
  start(100000, grantUntil);
  cards["A"].expiry = epochMs() + 5000; recheck();
  assert(timerMs == 5000 && toolDeadline == cards["A"].expiry);
  monotonicUs += 5000000; timerCallback(); serviceRelay(); off();
  cards["A"].expiry = epochMs() + 100000;
  grantUntil = cards["A"].expiry;
  start(100000, grantUntil);
  grantUntil = epochMs() + 2000; recheck();
  assert(timerMs == 2000);
  grantUntil = 0; recheck(); off();
  grantUntil = cards["A"].expiry;
  start(100000, grantUntil);
  cards.erase("A"); recheck(); off();
  start(30000); maintenance = true; serviceRelay(); off(); maintenance = false;
  start(30000); deviceBlocked = true; serviceRelay(); off(); deviceBlocked = false;
  start(30000); clockTrusted = false; serviceRelay(); off(); clockTrusted = true;
  start(0); off();
  start(5000000000LL); assert(timerMs == 5000000000ULL);
  { Lock s(stateMutex); stopRelay(); }
  oldCallback(); serviceRelay(); off();
  std::cout << "Relay timer, revocation, reset and locking checks passed\n";
}
