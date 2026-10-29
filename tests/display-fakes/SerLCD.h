#pragma once
#include "Wire.h"
#include <cstring>
extern unsigned begins, colors, rows;
extern uint32_t lastColor;
extern char lastRows[2][17];
class SerLCD {
  unsigned row = 0;
public:
  void begin(TwoWire &, int address) { assert(busHeld && address == 0x72); ++begins; }
  void setFastBacklight(unsigned long color) { assert(busHeld); ++colors; lastColor = color; }
  void setCursor(unsigned col, unsigned r) { assert(busHeld && col == 0 && r < 2); row = r; }
  void write(const uint8_t *data, unsigned count) {
    assert(busHeld && count == 16);
    std::memcpy(lastRows[row], data, 16);
    lastRows[row][16] = 0; ++rows;
  }
};
