#include "Display.h"
#include "Reader.h"
#include <ETH.h>
#include <esp_timer.h>

namespace display {
static portMUX_TYPE mailboxMutex = portMUX_INITIALIZER_UNLOCKED;
static Presentation mailbox;
uint64_t beginPresentation() {
  portENTER_CRITICAL(&mailboxMutex);
  uint64_t next = mailbox.id + 1;
  mailbox = Presentation{};
  mailbox.id = next;
  portEXIT_CRITICAL(&mailboxMutex);
  return next;
}
void enrollmentProgress(uint64_t id) {
  portENTER_CRITICAL(&mailboxMutex);
  if (mailbox.id == id && mailbox.enrollment == Enrollment::none)
    mailbox.enrollment = Enrollment::running;
  portEXIT_CRITICAL(&mailboxMutex);
}
void enrollmentResult(uint64_t id, bool success) {
  portENTER_CRITICAL(&mailboxMutex);
  if (mailbox.id == id && mailbox.enrollment == Enrollment::running)
    mailbox.enrollment = success ? Enrollment::succeeded : Enrollment::failed;
  portEXIT_CRITICAL(&mailboxMutex);
}
void scanResult(uint64_t id, const Scan &scan) {
  portENTER_CRITICAL(&mailboxMutex);
  if (mailbox.id == id && !mailbox.finished) {
    mailbox.scan = scan;
    mailbox.finished = true;
  }
  portEXIT_CRITICAL(&mailboxMutex);
}
static void task(void *) {
  Backend *backend = nullptr;
  while (!backend) {
    backend = detectBackend();
    if (!backend) vTaskDelay(pdMS_TO_TICKS(50));
  }
  Model model(esp_timer_get_time() / 1000);
  Presenter presenter;
  for (;;) {
    Presentation p;
    portENTER_CRITICAL(&mailboxMutex);
    p = mailbox;
    portEXIT_CRITICAL(&mailboxMutex);
    int64_t now = esp_timer_get_time() / 1000;
    model.accept(p, now);
    // Skip this frame rather than competing with a scan or relay transition.
    if (xSemaphoreTake(stateMutex, 0) == pdTRUE) {
      Status s;
      s.api = apiOK;
      s.mqtt = mqttOK;
      s.trusted = clockTrusted;
      s.utcMs = s.trusted ? epochMs() : 0;
      s.relay = relayOn;
      s.tool = toolDeadline > 0;
      s.relayDeadlineMs = relayDeadline / 1000;
      s.relayStartedMs = relayStartedUs / 1000;
      xSemaphoreGive(stateMutex);
      s.net = ETH.linkUp() && ETH.hasIP();
      // No state or mailbox lock is held during backend I/O.
      presenter.render(*backend, model.frame(s, now));
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
void setup() {
  if (xTaskCreate(task, "display", 4096, nullptr, tskIDLE_PRIORITY, nullptr) != pdPASS)
    log_e("Optional display task unavailable; reader continues without LCD");
}
} // namespace display
