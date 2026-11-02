#pragma once
#include "ReservationConfig.h"
#if READER_ENABLE_RESERVATIONS
#include <cstdint>
#include <string>
#include <vector>

namespace reservation {
constexpr int64_t hour = 3600000, grace = 900000;
constexpr size_t capacity = 64;
struct Record {
  std::string id, member, name, associatedSession;
  int64_t start = 0, end = 0, arrived = 0;
  uint32_t revision = 0;
  bool monitored = false, released = false;
};
struct Session {
  std::string id, member; int64_t started;
  Session(std::string i="", std::string m="", int64_t t=0):id(i),member(m),started(t) {}
};
struct Alarm {
  std::string id;
  int64_t deadline;
  bool own;
};
struct Decision {
  std::string claim;
  int64_t observedAt = 0;
  bool flash = false, newReservationCue = false, jeopardy = false;
  std::vector<Alarm> cues, warnings;
};
struct View {
  bool present = false, countdown = false, own = false, due = false;
  std::string name;
  int64_t start = 0, end = 0, deadline = 0;
};
class Model {
  struct Seen { std::string id; int64_t end; };
  struct Warning { std::string key; unsigned mask; };
  std::vector<Seen> seen;
  std::vector<Warning> warnings;
  std::string windowKey;
  bool baseline = false, firstSessionTick = true;
  int64_t deferredClaimAt = 0;
  std::vector<Alarm> newCues;
  bool warn(const Record &, bool own, int64_t now);
public:
  std::vector<Record> records;
  Session session;
  bool complete = false;
  void reset();
  void setSession(const Session &);
  void replace(std::vector<Record> next, bool all, int64_t now);
  Decision tick(int64_t now);
  void retryClaim(int64_t observedAt) { deferredClaimAt=observedAt; }
  View view(int64_t now) const;
  Record *find(const std::string &id);
};
} // namespace reservation
#endif
