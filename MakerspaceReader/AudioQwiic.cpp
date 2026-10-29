#include "Audio.h"
#include "Reader.h"
#include <SparkFun_Qwiic_Buzzer_Arduino_Library.h>
#include <Wire.h>
#include <esp_timer.h>

namespace audio {
class QwiicDevice : public Device {
  QwiicBuzzer buzzer;
  // Called with the bus held; one bounded best-effort stop, no retry loop.
  Result failed(const char *operation, int error) {
    log_e("Qwiic buzzer %s failed: %d; melody aborted", operation, error);
    auto stopError = buzzer.off();
    if (stopError != ksfTkErrOk)
      log_e("Qwiic buzzer cleanup off failed: %d", int(stopError));
    return Result::Failed;
  }
public:
  Result begin() override {
    if (xSemaphoreTake(i2cMutex, 0) != pdTRUE) return Result::Busy;
    if (!buzzer.begin(0x34, Wire)) {
      xSemaphoreGive(i2cMutex);
      log_i("Optional Qwiic buzzer not detected at 0x34; audio disabled");
      return Result::Failed;
    }
    auto error = buzzer.off();
    if (error != ksfTkErrOk) failed("initial off", int(error));
    xSemaphoreGive(i2cMutex);
    log_i("I2C device found: Qwiic buzzer at 0x34");
    // Detection succeeded. A transient cleanup failure must not permanently
    // disable the device; every new melody attempts off again before any note.
    return Result::Ok;
  }
  Result note(const Note &note, uint8_t volume) override {
    if (xSemaphoreTake(i2cMutex, 0) != pdTRUE) return Result::Busy;
    auto error = buzzer.configureBuzzer(note.frequencyHz, note.durationMs, volume);
    Result result;
    if (error != ksfTkErrOk) result = failed("configure", int(error));
    else {
      error = buzzer.on();
      result = error == ksfTkErrOk ? Result::Ok : failed("on", int(error));
    }
    xSemaphoreGive(i2cMutex);
    return result;
  }
  Result off() override {
    if (xSemaphoreTake(i2cMutex, 0) != pdTRUE) return Result::Busy;
    auto error = buzzer.off();
    Result result = error == ksfTkErrOk ? Result::Ok : failed("off", int(error));
    xSemaphoreGive(i2cMutex);
    return result;
  }
  int64_t nowMs() const override { return esp_timer_get_time() / 1000; }
};
Device &qwiicDevice() {
  static QwiicDevice device;
  return device;
}
} // namespace audio
