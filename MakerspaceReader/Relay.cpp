#include "Reader.h"
#include <Ticker.h>
#include <algorithm>
#include <esp_timer.h>

// All transitions require stateMutex. The timer task only posts an event.
static std::atomic<bool> relayOffEvent(false);
static Ticker relayTicker;
static void armRelayTimer() {
  int64_t remaining = (relayDeadline.load() - esp_timer_get_time() + 999) / 1000;
  relayOffEvent = false;
  relayTicker.once_ms(uint64_t(std::max<int64_t>(remaining, 1)), []() {
    relayOffEvent = true;
  });
}
void stopRelay(const char *reason, const Card *actor) {
  relayTicker.detach();
  relayOffEvent = false;
  digitalWrite(RELAY_PIN, LOW);
  endToolSession(reason, actor);
  relayOn = false;
  relayDeadline = 0;
  toolDeadline = 0;
  relayStartedUs = 0;
  activeUid = "";
}
void startRelay(int64_t durationMs, int64_t until, const String &uid) {
  if (durationMs <= 0) {
    stopRelay();
    return;
  }
  relayStartedUs = esp_timer_get_time();
  relayDeadline = relayStartedUs.load() + durationMs * 1000;
  toolDeadline = until;
  activeUid = uid;
  armRelayTimer(); // once_ms() replaces any previous countdown.
  digitalWrite(RELAY_PIN, HIGH);
  relayOn = true;
}
void recheckActiveTool() {
  if (!relayOn || toolDeadline <= 0)
    return;
  auto c = cards.find(activeUid.c_str());
  String reason;
  int64_t until = 0;
  if (c == cards.end() || !authorize(c->second, reason, until)) {
    stopRelay(c == cards.end() ? "cardRemoved" : reason.c_str());
    return;
  }
  if (until != toolDeadline) {
    toolDeadline = until;
    relayDeadline = esp_timer_get_time() + (until - epochMs()) * 1000;
    if (relayDeadline <= esp_timer_get_time())
      stopRelay();
    else
      armRelayTimer();
  }
}
void serviceRelay() {
  Lock s(stateMutex);
  // A callback from the old countdown may arrive after a scan reset it.
  // Recheck the current deadline rather than letting that event stop a new run.
  bool offEvent = relayOffEvent.exchange(false);
  if (offEvent && relayOn && esp_timer_get_time() >= relayDeadline) {
    stopRelay("expired");
  }
  // Independent deadline guard also handles trusted-clock advances.
  if (relayOn && (maintenance || deviceBlocked || !clockTrusted ||
                  relayDeadline <= 0 || esp_timer_get_time() >= relayDeadline ||
                  (toolDeadline > 0 && epochMs() >= toolDeadline)))
    stopRelay(maintenance ? "maintenance" : deviceBlocked ? "readerBlocked" : !clockTrusted ? "timeUnavailable" : "expired");
}
