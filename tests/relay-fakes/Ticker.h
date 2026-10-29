#pragma once
#include <cstdint>
#include <functional>
extern uint64_t timerMs;
extern std::function<void()> timerCallback;
extern bool timerArmed;
class Ticker {
public:
  void once_ms(uint64_t ms, std::function<void()> callback) {
    timerMs = ms; timerCallback = callback; timerArmed = true;
  }
  void detach() { timerArmed = false; }
};
