#include "../MakerspaceReader/Display.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
using namespace display;
static Status healthy() {
  Status s;
  s.net = s.api = s.mqtt = s.trusted = true;
  s.utcMs = 1767225600000LL; // New York 2025-12-31 19:00.
  return s;
}
static void screen(Model &m, const Status &s, int64_t now, const char *left,
                   const char *bottom, uint32_t color, const char *clock = "19:00") {
  Frame f = m.frame(s, now);
  assert(std::strlen(f.row1) == 16 && std::strlen(f.row2) == 16);
  std::string l(left), b(bottom);
  l.resize(11, ' '); b.resize(16, ' ');
  assert(std::string(f.row1) == l + clock);
  assert(std::string(f.row2) == b);
  assert(f.color == color);
}
static Presentation scan(uint64_t id, bool allowed, Reason reason = Reason::other) {
  Presentation p;
  p.id = id; p.finished = true; p.scan.allowed = allowed; p.scan.reason = reason;
  firstName("Ada Lovelace", p.scan.firstName);
  p.scan.decidedAt = 1767225600000LL;
  return p;
}
struct FakeBackend : Backend {
  unsigned attempts = 0, writes = 0;
  bool busy = false;
  Frame last;
  bool draw(const Frame &f) override {
    ++attempts;
    if (busy) return false;
    ++writes; last = f; return true;
  }
};
static unsigned progressCalls = 0, terminalCalls = 0;
static bool terminalSuccess = false;
namespace display {
void enrollmentProgress(uint64_t id) { assert(id == 42); ++progressCalls; }
void enrollmentResult(uint64_t id, bool success) {
  assert(id == 42); ++terminalCalls; terminalSuccess = success;
}
}
int main() {
  // Run the same production wrapper with failing API-begin, RF-write,
  // RF-verification, and API-commit operations, plus complete success.
  for (unsigned failingPhase = 0; failingPhase <= 4; ++failingPhase) {
    progressCalls = terminalCalls = 0;
    unsigned phasesRun = 0;
    bool result = enrollWithFeedback(42, [&]() {
      for (unsigned phase = 0; phase < 4; ++phase) {
        ++phasesRun;
        if (phase == failingPhase) return false;
      }
      return true;
    });
    assert(progressCalls == 1 && terminalCalls == 1);
    assert(result == (failingPhase == 4) && terminalSuccess == result);
    assert(phasesRun == (failingPhase == 4 ? 4 : failingPhase + 1));
  }
  Status s = healthy();
  Model start;
  screen(start, s, 0, "Starting", "Reader", blue);
  screen(start, s, 1999, "Starting", "Reader", blue);
  screen(start, s, 2000, "", "Hold fob to read", white);
  s.net = s.api = s.mqtt = s.trusted = false;
  screen(start, s, 2001, "", "Error: NET", pink, "     ");
  s.net = true; screen(start, s, 2002, "", "Error: DB", pink, "     ");
  s.api = true; screen(start, s, 2003, "", "Error: MQ", pink, "     ");
  s.mqtt = true; screen(start, s, 2004, "", "Error: NTP", pink, "     ");
  s.trusted = true; screen(start, s, 2005, "", "Hold fob to read", white);

  const int64_t now = 1767225600000LL, day = 86400000;
  assert(warningDays(now + 3 * day, now, 72) == 3);
  assert(warningDays(now + 3 * day + 1, now, 72) == 0);
  assert(warningDays(now + 14 * day, now, 336) == 14);
  assert(warningDays(now + 14 * day + 1, now, 336) == 0);
  assert(warningDays(now + 1, now, 72) == 1);
  assert(warningDays(now + day + 1, now, 72) == 2);
  for (int64_t expiry : {int64_t(0), int64_t(-1), now - 1, now,
                        std::numeric_limits<int64_t>::max()})
    assert(warningDays(expiry, now, 336) == 0);

  Model m;
  auto p = scan(1, true);
  p.scan.membershipExpiry = now + day;
  p.scan.checkoutExpiry = now + 14 * day;
  m.accept(p, 100);
  screen(m, s, 100, "Welcome", "Ada", green);
  screen(m, s, 2099, "Welcome", "Ada", green);
  screen(m, s, 2100, "Membership", "Expires in 1d", amber);
  screen(m, s, 5099, "Membership", "Expires in 1d", amber);
  screen(m, s, 5100, "Checkout", "Expires in 14d", amber);
  screen(m, s, 8099, "Checkout", "Expires in 14d", amber);
  screen(m, s, 8100, "", "Hold fob to read", white);

  // Each denial mapping, exactly three seconds, with stale row removal.
  const char *reasons[] = {"expired", "checkoutExpired", "unknown", "revoked", "unenrolled"};
  const char *left[] = {"Membership", "Checkout", "Unknown Fob", "Unknown Fob", "Access"};
  const char *bottom[] = {"Expired", "Expired", "", "", "Denied"};
  for (unsigned i = 0; i < 5; ++i) {
    Model denied;
    auto d = scan(i + 1, false, reasonFrom(reasons[i]));
    denied.accept(d, 100);
    screen(denied, s, 100, left[i], bottom[i], red);
    screen(denied, s, 3099, left[i], bottom[i], red);
    screen(denied, s, 3100, "", "Hold fob to read", white);
  }
  assert(reasonFrom(nullptr) == Reason::other);

  char name[17];
  firstName("  \tAlexandertheGreatest Last", name);
  // Actual whitespace, unsafe LCD command bytes, unsupported UTF-8 and control bytes.
  firstName(" 	AlexandertheGreatest Last", name);
  assert(std::string(name) == "AlexandertheGrea");
  firstName(nullptr, name); assert(name[0] == 0);
  firstName("   ", name); assert(name[0] == 0);
  firstName("A|B\xfe\x01\xc3\xa9" "C Last", name); assert(std::string(name) == "ABC");
  Model names;
  auto empty = scan(1, true); firstName("", empty.scan.firstName);
  names.accept(empty, 0); screen(names, s, 0, "Welcome", "", green);

  // Failure from any enrollment phase has the same terminal event and must win
  // over the following generic denial, even when coalesced in one mailbox read.
  for (unsigned failurePhase = 0; failurePhase < 3; ++failurePhase) {
    Model enrollment;
    Presentation e; e.id = 1; e.enrollment = Enrollment::running;
    enrollment.accept(e, 0);
    screen(enrollment, s, 100000, "Enrolling", "Keep holding", amber);
    e.enrollment = Enrollment::failed;
    e.finished = true; e.scan = scan(1, false).scan;
    enrollment.accept(e, 100001);
    screen(enrollment, s, 100001, "Enrollment", "Failed", red);
    screen(enrollment, s, 103000, "Enrollment", "Failed", red);
    screen(enrollment, s, 103001, "", "Hold fob to read", white);
    enrollment.accept(e, 110000); // Repeated mailbox reads must not restart timing.
    screen(enrollment, s, 110000, "", "Hold fob to read", white);
  }
  Model success;
  p.id = 1; p.enrollment = Enrollment::succeeded;
  success.accept(p, 0); // Fast enrollment: progress and terminal result coalesced.
  screen(success, s, 0, "Enrollment", "Succeeded", green);
  screen(success, s, 1999, "Enrollment", "Succeeded", green);
  screen(success, s, 2000, "Welcome", "Ada", green);
  screen(success, s, 4000, "Membership", "Expires in 1d", amber);
  screen(success, s, 7000, "Checkout", "Expires in 14d", amber);
  screen(success, s, 10000, "", "Hold fob to read", white);
  Model late;
  Presentation e; e.id = 1; e.enrollment = Enrollment::succeeded;
  late.accept(e, 0);
  e.finished = true; e.scan = scan(1, true).scan;
  late.accept(e, 5000); // Even without an intervening render.
  screen(late, s, 6999, "Welcome", "Ada", green);
  screen(late, s, 7000, "", "Hold fob to read", white);
  auto newer = scan(2, false, Reason::revoked);
  success.accept(newer, 11000);
  success.accept(p, 11001); // Older success cannot overwrite newer denial.
  screen(success, s, 11001, "Unknown Fob", "", red);
  // New presentation interrupts warnings as well as an in-progress enrollment.
  Model interrupt;
  e.finished = false; e.enrollment = Enrollment::running;
  interrupt.accept(e, 0); interrupt.accept(newer, 1);
  screen(interrupt, s, 1, "Unknown Fob", "", red);

  Model interruptedWarning;
  p.enrollment = Enrollment::none;
  interruptedWarning.accept(p, 0);
  screen(interruptedWarning, s, 2500, "Membership", "Expires in 1d", amber);
  interruptedWarning.accept(newer, 2501);
  interruptedWarning.accept(p, 2502);
  screen(interruptedWarning, s, 2502, "Unknown Fob", "", red);
  // Pulse countdown rounds up, disappears on OFF, never mutates deadlines.
  Model pulse;
  s.relay = true; s.relayDeadlineMs = 12000;
  screen(pulse, s, 2000, "Access on", "Hold fob to read", green, "  10s");
  screen(pulse, s, 11999, "Access on", "Hold fob to read", green, "   1s");
  s.relay = false;
  screen(pulse, s, 12000, "", "Hold fob to read", white);
  assert(s.relayDeadlineMs == 12000);
  s.relay = true; s.tool = true; s.relayStartedMs = 2000;
  std::strcpy(s.userName,"Ada");
  auto active=pulse.frame(s,2000);
  assert(std::string(active.row1)=="User is Ada     ");
  assert(std::string(active.row2)=="Scan to log off ");
  s.kickPending=true; assert(pulse.frame(s,2000).color==blue);
  s.outOfService=true; auto outage=pulse.frame(s,3000);
  assert(outage.color==red && std::string(outage.row2)=="OUT OF SERVICE  ");
  s.kickPending=false; s.outOfService=false;
  const auto savedUtc=s.utcMs;
  s.relay = false; s.haveLast=true; s.lastEnded=1782907200000LL;
  s.utcMs=s.lastEnded+86399999; std::strcpy(s.lastName,"Ada L");
  auto recent=pulse.frame(s,24000); assert(std::string(recent.row2)=="Ada L           ");
  s.utcMs=s.lastEnded+86400000;
  auto dated=pulse.frame(s,24000); assert(std::string(dated.row1)!=std::string(recent.row1));
  s.lastEnded=0; assert(std::string(pulse.frame(s,24000).row1)=="Last used: ?    ");
  s.haveLast=false; s.tool = false; s.utcMs=savedUtc;
  // Warning messages outlive the pulse but the countdown must disappear.
  Model warnings;
  p.enrollment = Enrollment::none; warnings.accept(p, 0);
  screen(warnings, s, 2500, "Membership", "Expires in 1d", amber);

  char clock[6];
  struct TimeCase { int64_t utc; const char *expected; };
  const TimeCase times[] = {
    {1767225600000LL, "19:00"}, // winter/day rollover
    {1782907200000LL, "08:00"}, // July 1, 2026 12:00 UTC
    {1772953199000LL, "01:59"}, // Mar 8, 2026 06:59:59 UTC
    {1772953200000LL, "03:00"}, // spring forward at 07:00 UTC
    {1793512799000LL, "01:59"}, // Nov 1, 2026 05:59:59 UTC
    {1793512800000LL, "01:00"}  // fall back at 06:00 UTC
  };
  for (auto t : times) { newYorkClock(t.utc, clock); assert(std::string(clock) == t.expected); }

  // Every clock value must fit the five-character field, including midnight.
  for (int minute = 0; minute < 1440; ++minute) {
    newYorkClock(1782878400000LL + int64_t(minute) * 60000, clock);
    auto hourText = std::to_string(minute / 60);
    auto minuteText = std::to_string(minute % 60);
    if (hourText.size() == 1) hourText = "0" + hourText;
    if (minuteText.size() == 1) minuteText = "0" + minuteText;
    assert(std::string(clock) == hourText + ":" + minuteText);
  }
  // Enrollment leaves the tool timer visible: verify hour rollover and saturation.
  Model timer;
  Presentation enrolling; enrolling.id = 1; enrolling.enrollment = Enrollment::running;
  timer.accept(enrolling, 0);
  auto timed = healthy(); timed.tool = timed.relay = true; timed.relayStartedMs = 120000;
  const TimeCase elapsed[] = {{-1, "00:00"}, {0, "00:00"}, {1, "00:01"},
                              {59, "00:59"}, {60, "01:00"}, {5999, "99:59"}, {6000, "99:59"}};
  for (auto t : elapsed)
    screen(timer, timed, timed.relayStartedMs + t.utc * 60000, "Enrolling", "Keep holding", amber, t.expected);

  FakeBackend backend;
  Presenter presenter;
  Frame f = start.frame(s, 20000);
  backend.busy = true; presenter.render(backend, f); assert(backend.writes == 0);
  backend.busy = false; presenter.render(backend, f); assert(backend.writes == 1);
  presenter.render(backend, f); assert(backend.writes == 1);
  f.color = red; presenter.render(backend, f); assert(backend.writes == 2);
  backend.busy = true;
  Frame partial = f; partial.color = blue;
  presenter.render(backend, partial);
  backend.busy = false;
  presenter.render(backend, f); // Restore a prior frame after a partially applied new one.
  assert(backend.writes == 3);
  NullBackend absent; presenter.render(absent, f);
  std::cout << "Display layout, messages, expiry, enrollment, DST, timing and backend checks passed\n";
}
