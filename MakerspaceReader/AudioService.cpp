#include "Audio.h"
#include "Reader.h"

namespace audio {
static portMUX_TYPE mailboxMutex = portMUX_INITIALIZER_UNLOCKED;
static Mailbox mailbox;
static TaskHandle_t worker = nullptr;
static bool accepting = false;
#if READER_ENABLE_RESERVATIONS
static bool pendingJeopardy=false, pendingDonkeyKongStart=false;
static uint64_t cancelJeopardy=0, cancelDonkeyKongStart=0;
static MelodyId audibleReservation=MelodyId::None;
static bool reservationOutstanding=false;
bool playReservation(MelodyId id) {
  if(id!=MelodyId::Jeopardy && id!=MelodyId::DonkeyKongStart) return false;
  portENTER_CRITICAL(&mailboxMutex);
  bool accepted=accepting && melody(id).count;
  if(accepted) { reservationOutstanding=true; if(id==MelodyId::Jeopardy) pendingJeopardy=true; else pendingDonkeyKongStart=true; }
  TaskHandle_t target=accepted ? worker : nullptr;
  portEXIT_CRITICAL(&mailboxMutex);
  if(target) xTaskNotifyGive(target);
  return accepted;
}
void cancelReservation(MelodyId id) {
  portENTER_CRITICAL(&mailboxMutex);
  if(!reservationOutstanding) { portEXIT_CRITICAL(&mailboxMutex); return; }
  if(id==MelodyId::None || id==MelodyId::Jeopardy) {pendingJeopardy=false; ++cancelJeopardy;}
  if(id==MelodyId::None || id==MelodyId::DonkeyKongStart) {pendingDonkeyKongStart=false; ++cancelDonkeyKongStart;}
  if(id==MelodyId::None) reservationOutstanding=false;
  if(id==MelodyId::None || audibleReservation==id) audibleReservation=MelodyId::None;
  TaskHandle_t target=worker;
  portEXIT_CRITICAL(&mailboxMutex);
  if(target) xTaskNotifyGive(target);
}
MelodyId reservationPlaying() {
  portENTER_CRITICAL(&mailboxMutex); auto result=audibleReservation; portEXIT_CRITICAL(&mailboxMutex); return result;
}
#endif
bool play(MelodyId id, uint8_t volume) {
  portENTER_CRITICAL(&mailboxMutex);
  bool accepted = accepting && mailbox.play(id, volume);
  TaskHandle_t target = accepted ? worker : nullptr;
  portEXIT_CRITICAL(&mailboxMutex);
  if (target) xTaskNotifyGive(target);
  return accepted;
}
void stop() {
#if READER_ENABLE_RESERVATIONS
  cancelReservation();
#endif
  portENTER_CRITICAL(&mailboxMutex);
  if (accepting) mailbox.stop();
  TaskHandle_t target = accepting ? worker : nullptr;
  portEXIT_CRITICAL(&mailboxMutex);
  if (target) xTaskNotifyGive(target);
}
static void wait(uint32_t milliseconds) {
  if (!milliseconds) return; // A bounded state transition; no bus polling here.
  TickType_t ticks = milliseconds == idleWait
      ? portMAX_DELAY
      : TickType_t((milliseconds + portTICK_PERIOD_MS - 1) / portTICK_PERIOD_MS);
  ulTaskNotifyTake(pdTRUE, ticks);
}
static void task(void *) {
  // Publish our handle under the same lock used by callers. This also works if
  // the worker runs before setup() returns from xTaskCreate().
  portENTER_CRITICAL(&mailboxMutex);
  worker = xTaskGetCurrentTaskHandle();
  portEXIT_CRITICAL(&mailboxMutex);
  Device &device = qwiicDevice();
  Result initialized;
  do {
    initialized = device.begin();
    if (initialized == Result::Busy) wait(10);
  } while (initialized == Result::Busy);
  if (initialized != Result::Ok) {
    portENTER_CRITICAL(&mailboxMutex);
    accepting = false;
    portEXIT_CRITICAL(&mailboxMutex);
    // Keep the handle valid for a producer already about to notify us.
    for (;;) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  }
  Sequencer sequencer;
#if READER_ENABLE_RESERVATIONS
  Sequencer background;
  Request backgroundRequest;
  uint64_t seenJeopardy=0, seenDonkeyKongStart=0, foregroundGeneration=0;
  bool silenceNeeded=false;
#endif
  for (;;) {
    portENTER_CRITICAL(&mailboxMutex);
    Request request = mailbox.snapshot();
    portEXIT_CRITICAL(&mailboxMutex);
    // Backend operations release I2C before service returns and before waiting.
#if READER_ENABLE_RESERVATIONS
    portENTER_CRITICAL(&mailboxMutex);
    bool cancelled=(backgroundRequest.id==MelodyId::Jeopardy && seenJeopardy!=cancelJeopardy) ||
      (backgroundRequest.id==MelodyId::DonkeyKongStart && seenDonkeyKongStart!=cancelDonkeyKongStart);
    seenJeopardy=cancelJeopardy; seenDonkeyKongStart=cancelDonkeyKongStart;
    portEXIT_CRITICAL(&mailboxMutex);
    if(cancelled) { background=Sequencer{}; backgroundRequest=Request{}; silenceNeeded=true; }
    bool foreground=sequencer.active() || request.generation!=foregroundGeneration;
    if(foreground) {
      auto paused=background.suspend(device);
      if(paused==Result::Busy) { wait(10); continue; }
      foregroundGeneration=request.generation;
      silenceNeeded=false; // Foreground owns hardware from this point.
      portENTER_CRITICAL(&mailboxMutex); audibleReservation=MelodyId::None; portEXIT_CRITICAL(&mailboxMutex);
      auto delay=sequencer.service(request,device);
      // Completion of a foreground request must wake the suspended sequence.
      portENTER_CRITICAL(&mailboxMutex); bool pending=pendingJeopardy || pendingDonkeyKongStart; portEXIT_CRITICAL(&mailboxMutex);
      wait(delay==idleWait && (background.active() || pending) ? 1 : delay);
      continue;
    }
    if(silenceNeeded) {
      if(device.off()==Result::Busy) { wait(10); continue; }
      silenceNeeded=false;
    }
    if(!background.active()) {
      portENTER_CRITICAL(&mailboxMutex);
      MelodyId pending=pendingJeopardy ? MelodyId::Jeopardy : pendingDonkeyKongStart ? MelodyId::DonkeyKongStart : MelodyId::None;
      if(pending==MelodyId::Jeopardy) pendingJeopardy=false;
      if(pending==MelodyId::DonkeyKongStart) pendingDonkeyKongStart=false;
      portEXIT_CRITICAL(&mailboxMutex);
      if(pending!=MelodyId::None) { ++backgroundRequest.generation; backgroundRequest.id=pending; backgroundRequest.volume=2; }
    }
    auto resumed=background.resume(device);
    if(resumed==Result::Busy) { wait(10); continue; }
    auto delay=background.service(backgroundRequest,device);
    portENTER_CRITICAL(&mailboxMutex);
    audibleReservation=background.playing() ? backgroundRequest.id : MelodyId::None;
    bool more=pendingJeopardy || pendingDonkeyKongStart;
    if(!background.active() && !more) reservationOutstanding=false;
    portEXIT_CRITICAL(&mailboxMutex);
    wait(delay==idleWait && more ? 1 : delay);
#else
    wait(sequencer.service(request, device));
#endif
  }
}
void setup() {
  portENTER_CRITICAL(&mailboxMutex);
  accepting = true;
  portEXIT_CRITICAL(&mailboxMutex);
  if (xTaskCreate(task, "audio", 3072, nullptr, 1, nullptr) != pdPASS) {
    portENTER_CRITICAL(&mailboxMutex);
    accepting = false;
    portEXIT_CRITICAL(&mailboxMutex);
    log_e("Optional audio task unavailable; reader continues without buzzer");
  }
}
} // namespace audio
