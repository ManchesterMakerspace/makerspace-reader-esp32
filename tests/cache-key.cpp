#include "../MakerspaceReader/CacheKey.h"
#include <cassert>
#include <iostream>
#include <vector>
struct Store {
  bool exists = false, failWrite = false, failRead = false, corruptRead = false;
  bool commitButReportFailure = false;
  std::vector<uint8_t> value;
  unsigned writes = 0;
  bool isKey(const char *) {
    return exists;
  }
  size_t getBytesLength(const char *) {
    return value.size();
  }
  size_t getBytes(const char *, void *out, size_t capacity) {
    if (failRead || capacity < value.size())
      return 0;
    std::memcpy(out, value.data(), value.size());
    if (corruptRead && !value.empty())
      static_cast<uint8_t *>(out)[0] ^= 1;
    return value.size();
  }
  size_t putBytes(const char *, const void *data, size_t length) {
    ++writes;
    if (failWrite)
      return 0;
    const auto *bytes = static_cast<const uint8_t *>(data);
    value.assign(bytes, bytes + length);
    exists = true;
    return commitButReportFailure ? 0 : length;
  }
};
static bool zero(const uint8_t *bytes) {
  for (size_t i = 0; i < cacheKey::bytes; ++i)
    if (bytes[i])
      return false;
  return true;
}
int main() {
  using namespace cacheKey;
  unsigned randomCalls = 0;
  auto random = [&](uint8_t *out, size_t count) {
    ++randomCalls;
    for (size_t i = 0; i < count; ++i)
      out[i] = uint8_t(i + randomCalls);
  };
  uint8_t key[bytes], original[bytes];
  const char *configured = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";

  Store first;
  assert(loadOrCreate(first, nullptr, true, true, random, key) == Result::created);
  assert(first.value.size() == bytes && first.writes == 1 && randomCalls == 1);
  std::memcpy(original, key, bytes);
  // Reboot after data is written: identical key, no random generation or NVS rewrite.
  assert(loadOrCreate(first, nullptr, false, false, random, key) == Result::stored);
  assert(std::memcmp(original, key, bytes) == 0 && first.writes == 1 && randomCalls == 1);
  // Interrupted first boot after committing the key but before any filesystem writes.
  assert(loadOrCreate(first, nullptr, false, true, random, key) == Result::stored);
  assert(std::memcmp(original, key, bytes) == 0 && randomCalls == 1);
  Store second;
  assert(loadOrCreate(second, nullptr, true, true, random, key) == Result::created);
  assert(std::memcmp(original, key, bytes) != 0 && randomCalls == 2);

  for (bool emptyNamespace : {false, true}) {
    for (bool emptyFiles : {false, true}) {
      if (emptyNamespace && emptyFiles)
        continue;
      Store used;
      std::memset(key, 0xAA, bytes);
      assert(loadOrCreate(used, nullptr, emptyNamespace, emptyFiles, random, key) ==
             Result::missingUsed);
      assert(used.writes == 0 && randomCalls == 2 && zero(key));
    }
  }
  // Legacy compiled-key devices remain readable; defining an invalid value never falls back.
  Store legacy;
  assert(loadOrCreate(legacy, configured, false, false, random, key) == Result::compiled);
  for (size_t i = 0; i < bytes; ++i)
    assert(key[i] == i);
  assert(legacy.writes == 0 && randomCalls == 2);
  for (const char *invalid : {"", "1234", "REPLACE_WITH_64_HEX_DIGITS",
                              "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1g"}) {
    assert(loadOrCreate(legacy, invalid, true, true, random, key) == Result::invalidConfigured);
    assert(zero(key) && legacy.writes == 0 && randomCalls == 2);
  }
  Store stored;
  stored.exists = true;
  stored.value.resize(bytes);
  for (size_t i = 0; i < bytes; ++i)
    stored.value[i] = uint8_t(i);
  assert(loadOrCreate(stored, configured, false, false, random, key) == Result::stored);
  stored.value[0] ^= 1;
  assert(loadOrCreate(stored, configured, false, false, random, key) == Result::mismatch);
  assert(zero(key) && stored.writes == 0);
  for (size_t size : {size_t(0), size_t(16), size_t(31), size_t(33)}) {
    stored.value.resize(size);
    assert(loadOrCreate(stored, nullptr, false, true, random, key) == Result::invalidStored);
    assert(zero(key) && stored.writes == 0 && randomCalls == 2);
  }
  stored.value.resize(bytes);
  stored.failRead = true;
  assert(loadOrCreate(stored, nullptr, false, false, random, key) == Result::invalidStored);
  assert(zero(key));

  Store noSpace;
  noSpace.failWrite = true;
  assert(loadOrCreate(noSpace, nullptr, true, true, random, key) == Result::persistenceFailed);
  assert(zero(key) && !noSpace.exists);
  Store badReadback;
  badReadback.corruptRead = true;
  assert(loadOrCreate(badReadback, nullptr, true, true, random, key) == Result::persistenceFailed);
  assert(zero(key) && badReadback.writes == 1);
  badReadback.corruptRead = false;
  assert(loadOrCreate(badReadback, nullptr, false, true, random, key) == Result::stored);
  assert(badReadback.writes == 1);
  Store interrupted;
  interrupted.commitButReportFailure = true;
  assert(loadOrCreate(interrupted, nullptr, true, true, random, key) == Result::persistenceFailed);
  assert(zero(key));
  unsigned callsBeforeReboot = randomCalls;
  assert(loadOrCreate(interrupted, nullptr, false, true, random, key) == Result::stored);
  assert(interrupted.writes == 1 && randomCalls == callsBeforeReboot);
  assert(!ready(Result::invalidStored) && !ready(Result::persistenceFailed));
  std::cout << "Cache-key first boot, reboot, distinct devices, legacy keys and "
               "persistence-failure checks passed\n";
}
