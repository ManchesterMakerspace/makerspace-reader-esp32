#pragma once
#include <cstdint>
extern int64_t monotonicUs;
inline int64_t esp_timer_get_time() { return monotonicUs; }
