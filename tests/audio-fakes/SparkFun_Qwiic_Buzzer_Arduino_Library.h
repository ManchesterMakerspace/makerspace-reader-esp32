#pragma once
#include "Reader.h"
#include "Wire.h"
constexpr int ksfTkErrOk = 0;
extern bool present;
extern int configureResult, onResult, offResult;
extern unsigned probes, configures, starts, stops;
extern uint16_t frequency, duration;
extern uint8_t loudness;
class QwiicBuzzer {
public:
  bool begin(uint8_t address, TwoWire &) {
    assert(busHeld && address == 0x34); ++probes; return present;
  }
  int configureBuzzer(uint16_t hz, uint16_t ms, uint8_t volume) {
    assert(busHeld && ms > 0 && volume <= 4);
    ++configures; frequency = hz; duration = ms; loudness = volume; return configureResult;
  }
  int on() { assert(busHeld); ++starts; return onResult; }
  int off() { assert(busHeld); ++stops; return offResult; }
};
