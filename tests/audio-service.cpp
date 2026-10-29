#include "../MakerspaceReader/Audio.h"
#include "audio-fakes/Reader.h"
#include <cstring>
#include <functional>
#include <iostream>
bool busBusy = false, busHeld = false, criticalHeld = false;
static bool creationSucceeds = false;
static unsigned notifications = 0;
static void (*taskEntry)(void *) = nullptr;
static std::function<void(TickType_t)> onWait;
struct End {};
int xTaskCreate(void (*entry)(void *), const char *name, unsigned stack, void *,
                unsigned priority, TaskHandle_t *) {
  assert(!criticalHeld && priority == 1 && stack >= 2048 && !std::strcmp(name, "audio"));
  taskEntry = entry;
  return creationSucceeds ? pdPASS : 0;
}
TaskHandle_t xTaskGetCurrentTaskHandle() { return reinterpret_cast<void *>(1); }
void xTaskNotifyGive(TaskHandle_t handle) {
  assert(handle == xTaskGetCurrentTaskHandle() && !criticalHeld);
  ++notifications;
}
uint32_t ulTaskNotifyTake(int clear, TickType_t ticks) {
  assert(clear == pdTRUE && ticks != 0 && !criticalHeld && !busHeld);
  onWait(ticks);
  return 0;
}
struct FakeDevice : audio::Device {
  audio::Result initialization = audio::Result::Busy;
  unsigned offCount = 0, noteCount = 0;
  uint16_t hz = 0;
  uint8_t volume = 0;
  int64_t now = 0;
  audio::Result begin() override { assert(!criticalHeld); return initialization; }
  audio::Result note(const audio::Note &n, uint8_t v) override {
    assert(!criticalHeld); ++noteCount; hz = n.frequencyHz; volume = v;
    return audio::Result::Ok;
  }
  audio::Result off() override { assert(!criticalHeld); ++offCount; return audio::Result::Ok; }
  int64_t nowMs() const override { return now; }
} device;
namespace audio { Device &qwiicDevice() { return device; } }
int main() {
  using namespace audio;
  assert(!play(MelodyId::Coin)); // Before setup.
  setup(); // Task allocation failure disables audio.
  assert(!play(MelodyId::Coin) && notifications == 0);
  creationSucceeds = true;
  setup();
  assert(play(MelodyId::Coin));
  assert(play(MelodyId::MarioDied, 4)); // Latest request retained before worker starts.
  assert(!play(MelodyId::Coin, 5));
  assert(notifications == 0); // Worker handle is not yet published.
  unsigned waits = 0;
  onWait = [&](TickType_t ticks) {
    switch (++waits) {
    case 1:
      assert(ticks == 1 && device.noteCount == 0); // Busy detection: 10 ms.
      device.initialization = Result::Ok;
      device.now += 10;
      break;
    case 2:
      assert(ticks == 15 && device.hz == 880 && device.volume == 4);
      assert(play(MelodyId::Coin)); // Notification wakes the old note wait early.
      break;
    case 3:
      assert(ticks == 12 && device.hz == 1318 && device.volume == 2);
      stop();
      break;
    case 4:
      assert(ticks == portMAX_DELAY && device.offCount == 3);
      assert(play(MelodyId::PowerUp));
      break;
    case 5:
      assert(ticks == 12 && device.hz == 660 && notifications == 3);
      throw End{};
    default: assert(false);
    }
  };
  try { taskEntry(nullptr); } catch (const End &) {}
  assert(waits == 5);
  device.initialization = Result::Failed;
  setup();
  onWait = [&](TickType_t ticks) {
    assert(ticks == portMAX_DELAY && !play(MelodyId::Coin));
    unsigned before = notifications;
    stop();
    assert(notifications == before);
    throw End{};
  };
  try { taskEntry(nullptr); } catch (const End &) {}
  assert(!play(MelodyId::Coin));
  std::cout << "Audio task creation, mailbox wakeups, timed waits and absent hardware passed\n";
}
