#pragma once
#include "Arduino.h"
inline void bootloader_random_disable() { entropy=false; calls.push_back("entropy-off"); }
inline void bootloader_random_enable() { entropy=true; calls.push_back("entropy-on"); }
