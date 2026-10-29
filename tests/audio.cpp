#include "../MakerspaceReader/Audio.h"
#include <cassert>
#include <iostream>
#include <vector>
using namespace audio;
struct Call {
  bool off;
  Note note;
  uint8_t volume;
  int64_t at;
};
struct FakeDevice : Device {
  int64_t now = 0, latency = 0;
  bool busy = false, failNote = false, failOff = false, held = false;
  std::vector<Call> calls;
  Result begin() override { return Result::Ok; }
  Result note(const Note &n, uint8_t volume) override {
    assert(!held);
    if (busy) return Result::Busy;
    held = true;
    calls.push_back({false, n, volume, now});
    now += latency;
    held = false;
    return failNote ? Result::Failed : Result::Ok;
  }
  Result off() override {
    assert(!held);
    if (busy) return Result::Busy;
    held = true;
    calls.push_back({true, {0, 0, 0}, 0, now});
    now += latency;
    held = false;
    return failOff ? Result::Failed : Result::Ok;
  }
  int64_t nowMs() const override { assert(!held); return now; }
};
static void checkMelody(MelodyId id, const std::vector<unsigned> &hz,
                        const std::vector<unsigned> &durations, unsigned gap) {
  auto score = melody(id);
  assert(score.count == hz.size());
  Mailbox mailbox;
  assert(mailbox.play(id));
  FakeDevice device;
  Sequencer player;
  assert(player.service(mailbox.snapshot(), device) == 0); // Initial off
  int64_t expectedStart = 0;
  for (size_t i = 0; i < hz.size(); ++i) {
    assert(score.notes[i].frequencyHz == hz[i]);
    assert(score.notes[i].durationMs == durations[i] && score.notes[i].gapMs == gap);
    assert(player.service(mailbox.snapshot(), device) == durations[i] + gap);
    const auto &call = device.calls.back();
    assert(!call.off && call.note.frequencyHz == hz[i] && call.at == expectedStart);
    assert(call.note.durationMs == durations[i] && call.volume == 2);
    size_t count = device.calls.size();
    device.now += durations[i] + gap - 1;
    assert(player.service(mailbox.snapshot(), device) == 1);
    assert(device.calls.size() == count && !device.held);
    ++device.now;
    expectedStart = device.now;
  }
  assert(player.service(mailbox.snapshot(), device) == 0);
  assert(player.service(mailbox.snapshot(), device) == idleWait);
  assert(device.calls.back().off && !player.active());
  size_t count = device.calls.size();
  assert(player.service(mailbox.snapshot(), device) == idleWait && device.calls.size() == count);
}
static Melody rests(MelodyId id) {
  static constexpr Note notes[] = {{500, 20, 5}, {0, 40, 10}, {600, 30, 0}};
  static constexpr Note indefinite[] = {{500, 0, 10}};
  if (id == MelodyId::Coin) return {notes, 3};
  return {indefinite, 1};
}
int main() {
  checkMelody(MelodyId::Coin, {1318,1567,2093}, {100,100,150}, 20);
  checkMelody(MelodyId::MarioDied, {880,830,784,740,698,659,622},
              {135,135,135,135,135,135,270}, 13);
  checkMelody(MelodyId::PowerUp, {660,1320,1568,1760,1976,1568,1760,1976},
              {100,100,100,100,100,100,100,300}, 20);

  // Replacement in a sounding note, a gap, or a deferred bus operation.
  for (int64_t replaceAt : {50, 110, 120}) {
    Mailbox box; FakeDevice d; Sequencer s;
    box.play(MelodyId::Coin);
    s.service(box.snapshot(), d); s.service(box.snapshot(), d);
    d.now = replaceAt;
    d.busy = true;
    box.play(MelodyId::MarioDied);
    auto count = d.calls.size();
    assert(s.service(box.snapshot(), d) == 10 && d.calls.size() == count);
    box.play(MelodyId::PowerUp); // Only the latest survives contention.
    d.busy = false;
    assert(s.service(box.snapshot(), d) == 0 && d.calls.back().off);
    assert(s.service(box.snapshot(), d) == 120 && d.calls.back().note.frequencyHz == 660);
    d.now += 10;
    box.play(MelodyId::PowerUp); // Same sound restarts; generation matters.
    assert(s.service(box.snapshot(), d) == 0);
    assert(s.service(box.snapshot(), d) == 120 && d.calls.back().note.frequencyHz == 660);
    box.stop();
    assert(s.service(box.snapshot(), d) == idleWait && d.calls.back().off);
    assert(!s.active());
  }

  Mailbox box; FakeDevice d; Sequencer s;
  box.play(MelodyId::Coin, 4);
  s.service(box.snapshot(), d);
  d.latency = 7;
  assert(s.service(box.snapshot(), d) == 120 && d.calls.back().volume == 4);
  d.now = 126;
  assert(s.service(box.snapshot(), d) == 1); // Deadline includes command latency.
  d.now = 10000; // Long scheduler stall: one next note, not a catch-up burst.
  size_t count = d.calls.size();
  assert(s.service(box.snapshot(), d) == 120 && d.calls.size() == count + 1);
  assert(d.calls.back().note.frequencyHz == 1567 && d.calls.back().note.durationMs == 100);
  assert(s.service(box.snapshot(), d) == 120 && d.calls.size() == count + 1);

  auto generation = box.snapshot().generation;
  assert(!box.play(MelodyId::Coin, 5));
  assert(!box.play(static_cast<MelodyId>(99)));
  assert(box.snapshot().generation == generation);
  assert(box.play(MelodyId::Coin, 0));
  assert(s.service(box.snapshot(), d) == idleWait && d.calls.back().off);
  for (uint8_t volume = 1; volume <= 4; ++volume) {
    box.play(MelodyId::Coin, volume);
    s.service(box.snapshot(), d); s.service(box.snapshot(), d);
    assert(d.calls.back().volume == volume);
  }
  box.stop(); d.busy = true;
  assert(s.service(box.snapshot(), d) == 10);
  d.busy = false;
  assert(s.service(box.snapshot(), d) == idleWait);
  // Busy next-note acquisition doesn't advance the cursor or truncate duration.
  box.play(MelodyId::Coin); s.service(box.snapshot(), d);
  d.busy = true; count = d.calls.size();
  assert(s.service(box.snapshot(), d) == 10 && d.calls.size() == count);
  d.now += 500; d.busy = false;
  assert(s.service(box.snapshot(), d) == 120 && d.calls.back().note.frequencyHz == 1318);

  for (bool offFailure : {false, true}) {
    Mailbox b; FakeDevice device; Sequencer player;
    b.play(MelodyId::Coin);
    device.failOff = offFailure;
    device.failNote = !offFailure;
    player.service(b.snapshot(), device);
    assert(player.service(b.snapshot(), device) == idleWait);
    assert(!player.active() && !device.held);
    count = device.calls.size();
    assert(player.service(b.snapshot(), device) == idleWait && device.calls.size() == count);
    device.failOff = device.failNote = false;
    b.play(MelodyId::Coin);
    assert(player.service(b.snapshot(), device) == 0);
    assert(player.service(b.snapshot(), device) == 120); // Later requests can recover.
  }

  Mailbox b; FakeDevice restDevice; Sequencer restPlayer(rests);
  b.play(MelodyId::Coin);
  restPlayer.service(b.snapshot(), restDevice);
  assert(restPlayer.service(b.snapshot(), restDevice) == 25);
  restDevice.now += 25;
  assert(restPlayer.service(b.snapshot(), restDevice) == 50 && restDevice.calls.back().off);
  restDevice.now += 50;
  assert(restPlayer.service(b.snapshot(), restDevice) == 30);
  assert(restDevice.calls.back().note.frequencyHz == 600);
  b.play(MelodyId::MarioDied); // Test resolver returns an invalid zero-duration note.
  assert(restPlayer.service(b.snapshot(), restDevice) == idleWait);
  assert(!restPlayer.active() && restDevice.calls.back().off);
  std::cout << "Audio melodies, timing, replacement, volume, rests and failures passed\n";
}
