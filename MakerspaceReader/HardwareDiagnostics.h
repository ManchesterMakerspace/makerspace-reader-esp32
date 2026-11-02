#pragma once
#include <cstdint>
#ifdef ARDUINO
#include <Arduino.h>
void hardwareProbe(bool busReady);
void hardwareResponding(uint8_t address, bool responding);
void hardwareInitialized(uint8_t address, bool ready, bool selected=true);
String hardwareReport();
#endif
