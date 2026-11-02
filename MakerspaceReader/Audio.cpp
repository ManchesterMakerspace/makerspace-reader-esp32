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
#if READER_ENABLE_RESERVATIONS
// Donkey Kong (arcade), Game Start: single-voice reduction, raised two octaves.
// Reference: https://themushroomkingdom.net/sounds/mid/dk_gamestart.mid
// MARIOMaSTeR's sequence identifies the opening phrase and closing B/C trill.
// Round attacks to 100 ms for the buzzer, omitting the MIDI's initial silence.
static constexpr Note donkeyKongStart[] = {
  {262,880,20}, {294,280,20}, {311,630,20}, {262,530,20},
  {494,80,20}, {523,80,20}, {494,80,20}, {523,80,20},
  {494,80,20}, {523,80,20}, {494,80,20}, {523,80,20},
  {494,80,20}, {523,80,20}, {494,80,20}, {523,80,20},
  {494,80,20}, {523,1480,20}
};
// Single-voice Think! arrangement. Notes include bounded articulation gaps.
// Melody reference: https://gist.github.com/scallopedllama/326c72cd3cd389d1ac323a5506d5efe6
#define Q(f) {f,440,60}
#define E(f) {f,220,30}
#define H(f) {f,940,60}
#define D(f) {f,690,60}
static constexpr Note jeopardy[] = {
  Q(523),Q(698),Q(523),Q(349),Q(523),Q(698),H(523),
  Q(523),Q(698),Q(523),Q(698),D(880),E(784),E(698),E(659),E(587),E(554),
  Q(523),Q(698),Q(523),Q(349),Q(523),Q(698),H(523),
  D(698),E(587),Q(523),Q(466),Q(440),Q(392),H(349),
  Q(587),Q(784),Q(587),Q(392),Q(587),Q(784),H(587),
  Q(587),Q(784),Q(587),Q(784),D(988),E(880),E(784),E(740),E(659),E(622),
  Q(587),Q(784),Q(587),Q(392),Q(587),Q(784),H(587),
  D(784),E(659),Q(587),Q(523),Q(494),Q(440),H(392)
};
#undef Q
#undef E
#undef H
#undef D
#endif
Melody melody(MelodyId id) {
  switch (id) {
  case MelodyId::Coin: return {coin, sizeof(coin) / sizeof(*coin)};
  case MelodyId::MarioDied: return {marioDied, sizeof(marioDied) / sizeof(*marioDied)};
  case MelodyId::PowerUp: return {powerUp, sizeof(powerUp) / sizeof(*powerUp)};
#if READER_ENABLE_RESERVATIONS
  case MelodyId::DonkeyKongStart: return {donkeyKongStart, sizeof(donkeyKongStart)/sizeof(*donkeyKongStart)};
  case MelodyId::Jeopardy: return {jeopardy, sizeof(jeopardy)/sizeof(*jeopardy)};
#endif
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
#if READER_ENABLE_RESERVATIONS
  paused=false;
#endif
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
#if READER_ENABLE_RESERVATIONS
  noteUntil=device.nowMs()+n.durationMs;
#endif
  ++index;
  waiting = true;
  return uint32_t(n.durationMs) + n.gapMs;
}
#if READER_ENABLE_RESERVATIONS
Result Sequencer::suspend(Device &device) {
  if(paused || !active()) return Result::Ok;
  auto result=device.off();
  if(result==Result::Failed) { abort(); return result; }
  if(result==Result::Busy) return result;
  paused=true; pausedAt=device.nowMs(); return Result::Ok;
}
Result Sequencer::resume(Device &device) {
  if(!paused) return Result::Ok;
  if(waiting && index && noteUntil>pausedAt) {
    Note remaining=score.notes[index-1]; remaining.durationMs=uint16_t(noteUntil-pausedAt); remaining.gapMs=0;
    auto result=remaining.frequencyHz ? device.note(remaining,volume) : device.off();
    if(result==Result::Busy) return result;
    if(result==Result::Failed) { abort(); return result; }
  }
  int64_t elapsed=device.nowMs()-pausedAt; nextAt+=elapsed; noteUntil+=elapsed; paused=false;
  return Result::Ok;
}
#endif
} // namespace audio
