#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include <cassert>
using String = std::string;
extern uint32_t testNow;
extern bool entropy;
extern std::vector<std::string> calls;
inline uint32_t millis() { return testNow; }
#define log_e(...) ((void)0)
#define log_i(...) ((void)0)
struct IPAddress {
  uint32_t value;
  explicit IPAddress(uint32_t v) : value(v) {}
  operator uint32_t() const { return value; }
  String toString() const { return value ? "192.0.2.1" : "0.0.0.0"; }
};
