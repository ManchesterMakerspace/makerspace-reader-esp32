#pragma once
#include <map>
#include <set>
#include <cstdint>
struct TwoWire {
 std::set<uint8_t> present;
 std::map<uint8_t,unsigned> probes;
 uint8_t target=0;
 void beginTransmission(uint8_t address){target=address; ++probes[address];}
 unsigned endTransmission(){return present.count(target)?0:2;}
};
extern TwoWire Wire;
