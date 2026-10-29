#include "../MakerspaceReader/Audio.h"
#include "audio-fakes/Wire.h"
#include <cassert>
#include <iostream>
bool busBusy = false, busHeld = false, present = false;
int configureResult = 0, onResult = 0, offResult = 0;
unsigned probes = 0, configures = 0, starts = 0, stops = 0;
uint16_t frequency = 0, duration = 0;
uint8_t loudness = 0;
int64_t monotonicUs = 1234000;
TwoWire Wire;
int main() {
  using namespace audio;
  auto &d = qwiicDevice();
  busBusy = true;
  assert(d.begin() == Result::Busy && probes == 0 && !busHeld);
  busBusy = false;
  assert(d.begin() == Result::Failed && stops == 0 && !busHeld);
  present = true;
  assert(d.begin() == Result::Ok && stops == 1 && !busHeld);
  assert(d.nowMs() == 1234);
  Note n = {1318, 100, 20};
  busBusy = true;
  assert(d.note(n, 2) == Result::Busy && configures == 0 && !busHeld);
  assert(d.off() == Result::Busy && stops == 1 && !busHeld);
  busBusy = false;
  assert(d.note(n, 2) == Result::Ok && configures == 1 && starts == 1 && !busHeld);
  assert(frequency == 1318 && duration == 100 && loudness == 2);
  configureResult = -1;
  assert(d.note(n, 2) == Result::Failed && starts == 1 && stops == 2 && !busHeld);
  configureResult = 0; onResult = -2;
  assert(d.note(n, 2) == Result::Failed && starts == 2 && stops == 3 && !busHeld);
  onResult = 0; offResult = -3;
  assert(d.off() == Result::Failed && stops == 5 && !busHeld); // One best-effort retry only.
  assert(d.begin() == Result::Ok && stops == 7 && !busHeld);
  offResult = 0;
  assert(d.note(n, 4) == Result::Ok && starts == 3 && !busHeld);
  assert(d.off() == Result::Ok && !busHeld);
  std::cout << "Audio hardware absence, bus contention, command failures and lock release passed\n";
}
