#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>
class TwoWire {
public:
  std::deque<std::vector<uint8_t>> incoming;
  std::vector<std::vector<uint8_t>> sent;
  std::vector<uint8_t> current, transmitting;
  size_t cursor = 0, reads = 0;
  uint8_t writeStatus = 0;
  void beginTransmission(uint8_t address) {
    assert(address == 0x24);
    transmitting.clear();
  }
  size_t write(const uint8_t *data, size_t length) {
    transmitting.assign(data, data + length);
    return length;
  }
  uint8_t endTransmission() {
    sent.push_back(transmitting);
    return writeStatus;
  }
  size_t requestFrom(uint8_t address, uint8_t count) {
    assert(address == 0x24);
    ++reads;
    current.clear();
    cursor = 0;
    if (!incoming.empty()) {
      current = incoming.front();
      incoming.pop_front();
    }
    assert(current.size() <= count);
    return current.size();
  }
  int available() {
    return int(current.size() - cursor);
  }
  int read() {
    return current[cursor++];
  }
};
