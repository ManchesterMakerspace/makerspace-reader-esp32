#pragma once
#include <cstdint>

// Override with compiler -D; shared by snapshots and per-card freshness checks.
#ifndef READER_REFRESH_INTERVAL_MS
#define READER_REFRESH_INTERVAL_MS 900000UL
#endif
static_assert(READER_REFRESH_INTERVAL_MS > 0 && READER_REFRESH_INTERVAL_MS <= 0x7fffffffUL,
              "Reader refresh interval must fit a positive signed millisecond interval");
