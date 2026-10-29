#include "Audio.h"
#include <algorithm>

namespace audio {
static constexpr Note coin[] = {{1318, 100, 20}, {1567, 100, 20}, {2093, 150, 20}};
static constexpr Note marioDied[] = {
    {880, 135, 13}, {830, 135, 13}, {784, 135, 13}, {740, 135, 13},
    {698, 135, 13}, {659, 135, 13}, {622, 270, 13}};
static constexpr Note powerUp[] = {
    {660, 100, 20}, {1320, 100, 20}, {1568, 100, 20}, {1760, 100, 20},
    {1976, 100, 20}, {1568, 100, 20}, {1760, 100, 20}, {1976, 300, 20}};
Melody melody(MelodyId id) {
  switch (id) {
  case MelodyId::Coin: return {coin, sizeof(coin) / sizeof(*coin)};
  case MelodyId::MarioDied: return {marioDied, sizeof(marioDied) / sizeof(*marioDied)};
  case MelodyId::PowerUp: return {powerUp, sizeof(powerUp) / sizeof(*powerUp)};
  default: return {nullptr, 0};
  }
}
bool Mailbox::play(MelodyId id, uint8_t volume) {
  if (volume > 4 || !melody(id).count) return false;
  ++latest.generation;
  latest.id = volume ? id : MelodyId::None;
  latest.volume = volume;
  return true;
}
void Mailbox::stop() {
  ++latest.generation;
  latest.id = MelodyId::None;
}
void Sequencer::abort() {
  score = {nullptr, 0};
  stopping = waiting = false;
  index = 0;
}
uint32_t Sequencer::service(const Request &request, Device &device) {
  if (request.generation != generation) {
    generation = request.generation;
    score = request.volume && request.volume <= 4 ? lookup(request.id) : Melody{nullptr, 0};
    volume = request.volume;
    index = 0;
    waiting = false;
    stopping = true; // Silence the prior note before starting the replacement.
    if (!score.notes) score.count = 0;
    for (size_t i = 0; i < score.count; ++i) {
      if (!score.notes[i].durationMs) {
        score = {nullptr, 0}; // Never send indefinite hardware tones.
        break;
      }
    }
  }
  if (stopping) {
    Result result = device.off();
    if (result == Result::Busy) return 10;
    stopping = false;
    if (result == Result::Failed) { abort(); return idleWait; }
    if (!score.count) return idleWait;
    return 0; // Reread the mailbox before any new note.
  }
  if (!score.count) return idleWait;
  if (waiting) {
    int64_t remaining = nextAt - device.nowMs();
    if (remaining > 0) return uint32_t(remaining);
    waiting = false;
    if (index == score.count) {
      score = {nullptr, 0};
      stopping = true;
      return 0; // Final off(), without affecting a newer request.
    }
  }
  const Note &n = score.notes[index];
  Result result = n.frequencyHz ? device.note(n, volume) : device.off();
  if (result == Result::Busy) return 10;
  if (result == Result::Failed) { abort(); return idleWait; }
  // The driver programs a finite duration. A late worker extends only the gap.
  // Starting from completion also accommodates command latency conservatively.
  nextAt = device.nowMs() + int64_t(n.durationMs) + n.gapMs;
  ++index;
  waiting = true;
  return uint32_t(n.durationMs) + n.gapMs;
}
} // namespace audio
