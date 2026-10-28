#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace cacheKey {
constexpr size_t bytes = 32;
enum class Result {
  compiled,
  stored,
  created,
  invalidConfigured,
  invalidStored,
  mismatch,
  missingUsed,
  persistenceFailed
};
inline bool ready(Result result) {
  return result == Result::compiled || result == Result::stored || result == Result::created;
}
inline bool parse(const char *hex, uint8_t *out) {
  if (!hex || std::strlen(hex) != bytes * 2)
    return false;
  for (size_t i = 0; i < bytes; ++i) {
    unsigned value = 0;
    for (size_t j = 0; j < 2; ++j) {
      char c = hex[i * 2 + j];
      int digit = c >= '0' && c <= '9'   ? c - '0'
                  : c >= 'a' && c <= 'f' ? c - 'a' + 10
                  : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                         : -1;
      if (digit < 0)
        return false;
      value = value * 16 + unsigned(digit);
    }
    out[i] = uint8_t(value);
  }
  return true;
}
// Store has the Preferences byte-blob interface. Only boot calls this function,
// before any nonce, event, config or cache writes and before worker tasks start.
template <typename Store, typename Random>
Result loadOrCreate(Store &store, const char *configured, bool namespaceEmpty, bool filesystemEmpty,
                    Random random, uint8_t *out) {
  std::memset(out, 0, bytes);
  uint8_t candidate[bytes] = {}, saved[bytes] = {};
  Result result = Result::missingUsed;
  if (configured && !parse(configured, candidate)) {
    result = Result::invalidConfigured; // Explicitly invalid is never an auto-generation request.
  } else if (store.isKey("cacheKey")) {
    if (store.getBytesLength("cacheKey") != bytes ||
        store.getBytes("cacheKey", saved, bytes) != bytes)
      result = Result::invalidStored;
    else if (configured && std::memcmp(candidate, saved, bytes) != 0)
      result = Result::mismatch;
    else {
      std::memcpy(out, saved, bytes);
      result = Result::stored;
    }
  } else if (configured) {
    std::memcpy(out, candidate, bytes);
    result = Result::compiled; // Preserve existing devices without persisting their compiled key.
  } else if (namespaceEmpty && filesystemEmpty) {
    random(candidate, bytes);
    if (store.putBytes("cacheKey", candidate, bytes) == bytes &&
        store.getBytesLength("cacheKey") == bytes &&
        store.getBytes("cacheKey", saved, bytes) == bytes &&
        std::memcmp(candidate, saved, bytes) == 0) {
      std::memcpy(out, saved, bytes); // Never use a key until durable write/readback succeeds.
      result = Result::created;
    } else
      result = Result::persistenceFailed;
  }
  std::memset(candidate, 0, bytes);
  std::memset(saved, 0, bytes);
  return result;
}
} // namespace cacheKey
