#include "Audio.h"
#include "Reader.h"

namespace audio {
static portMUX_TYPE mailboxMutex = portMUX_INITIALIZER_UNLOCKED;
static Mailbox mailbox;
static TaskHandle_t worker = nullptr;
static bool accepting = false;
bool play(MelodyId id, uint8_t volume) {
  portENTER_CRITICAL(&mailboxMutex);
  bool accepted = accepting && mailbox.play(id, volume);
  TaskHandle_t target = accepted ? worker : nullptr;
  portEXIT_CRITICAL(&mailboxMutex);
  if (target) xTaskNotifyGive(target);
  return accepted;
}
void stop() {
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
  for (;;) {
    portENTER_CRITICAL(&mailboxMutex);
    Request request = mailbox.snapshot();
    portEXIT_CRITICAL(&mailboxMutex);
    // Backend operations release I2C before service returns and before waiting.
    wait(sequencer.service(request, device));
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
