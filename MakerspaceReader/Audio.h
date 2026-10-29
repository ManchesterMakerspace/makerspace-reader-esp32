#pragma once
#include <cstddef>
#include <cstdint>

namespace audio {
enum class MelodyId : uint8_t { None, Coin, MarioDied, PowerUp };
struct Note {
  uint16_t frequencyHz;
  uint16_t durationMs;
  uint16_t gapMs;
};
struct Melody {
  const Note *notes;
  size_t count;
};
Melody melody(MelodyId);
struct Request {
  uint64_t generation = 0;
  MelodyId id = MelodyId::None;
  uint8_t volume = 2;
};
// Caller holds the mailbox critical section. No pointers or dynamic buffers.
class Mailbox {
  Request latest;
public:
  bool play(MelodyId id, uint8_t volume = 2);
  void stop();
  Request snapshot() const { return latest; }
};
enum class Result { Ok, Busy, Failed };
class Device {
public:
  virtual ~Device() = default;
  virtual Result begin() = 0;
  virtual Result note(const Note &, uint8_t volume) = 0;
  virtual Result off() = 0;
  virtual int64_t nowMs() const = 0;
};
constexpr uint32_t idleWait = UINT32_MAX;
using MelodyLookup = Melody (*)(MelodyId);
class Sequencer {
  MelodyLookup lookup;
  uint64_t generation = 0;
  Melody score = {nullptr, 0};
  size_t index = 0;
  uint8_t volume = 2;
  bool stopping = false, waiting = false;
  int64_t nextAt = 0;
  void abort();
public:
  explicit Sequencer(MelodyLookup source = melody) : lookup(source) {}
  // Executes at most one bus operation, returning a notification-aware wait.
  // All elapsed time comes from Device's monotonic clock, including I2C latency.
  uint32_t service(const Request &, Device &);
  bool active() const { return stopping || score.count != 0; }
};

void setup();
// True means recorded, not necessarily audible. False: disabled/invalid request.
bool play(MelodyId, uint8_t volume = 2);
void stop();
Device &qwiicDevice();
} // namespace audio
