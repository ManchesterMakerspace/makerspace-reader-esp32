#pragma once
#include <cstdint>
#ifndef READER_PROFILE_MB
#define READER_PROFILE_MB 16
#endif
#ifndef READER_CONFIGURED_FLASH_BYTES
#define READER_CONFIGURED_FLASH_BYTES (READER_PROFILE_MB * 1024ULL * 1024ULL)
#endif
static_assert(READER_PROFILE_MB == 4 || READER_PROFILE_MB == 8 || READER_PROFILE_MB == 16, "Unsupported reader flash profile");
namespace readerProfile {
constexpr bool compact = READER_PROFILE_MB == 4;
constexpr uint32_t flashBytes = READER_PROFILE_MB * 1024UL * 1024UL;
constexpr uint32_t slotBytes = READER_PROFILE_MB == 16 ? 0x400000 : READER_PROFILE_MB == 8 ? 0x200000 : 0x1a0000;
constexpr uint32_t cacheOffset = 0x10000 + slotBytes * 2;
constexpr uint32_t keysOffset = READER_PROFILE_MB == 16 ? 0xc10000 : flashBytes - 0x1000;
constexpr uint32_t cacheBytes = READER_PROFILE_MB == 16 ? 0x400000 : keysOffset - cacheOffset;
constexpr uint32_t eventBytes = compact ? 64 * 1024 : READER_PROFILE_MB == 8 ? 2 * 1024 * 1024 : 3 * 1024 * 1024;
constexpr uint32_t snapshotBytes = compact ? 128 * 1024 : 320 * 1024;
constexpr unsigned cards = compact ? 128 : 300, grants = compact ? 128 : 150;
}
