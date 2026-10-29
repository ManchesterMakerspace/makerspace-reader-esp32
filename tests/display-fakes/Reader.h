#pragma once
#include <cassert>
#include <cstdint>
extern bool busBusy, busHeld;
constexpr int i2cMutex = 1, pdTRUE = 1;
inline int xSemaphoreTake(int, int timeout) {
  assert(timeout == 0);
  if (busBusy) return 0;
  assert(!busHeld); busHeld = true; return 1;
}
inline void xSemaphoreGive(int) { assert(busHeld); busHeld = false; }
#define log_i(...) ((void)0)
