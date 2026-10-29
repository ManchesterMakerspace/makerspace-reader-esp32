#pragma once
#include "Reader.h"
extern bool lcdPresent;
struct TwoWire {
  void beginTransmission(int addr) { assert(busHeld && addr == 0x72); }
  int endTransmission() { return lcdPresent ? 0 : 2; }
};
extern TwoWire Wire;
