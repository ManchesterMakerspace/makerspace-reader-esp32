#pragma once
#include <cstdint>

namespace display {
constexpr uint32_t green = 0x00ff00, red = 0xff0000, pink = 0x804040;
constexpr uint32_t amber = 0xff8000, blue = 0x0000ff, white = 0x101010;
struct Frame {
  char row1[17] = {}, row2[17] = {};
  uint32_t color = white;
};
struct Status {
  bool net = false, api = false, mqtt = false, trusted = false;
  bool relay = false, tool = false;
  int64_t utcMs = 0, relayDeadlineMs = 0, relayStartedMs = 0;
};
enum class Reason { other, unknown, revoked, expired, checkoutExpired };
struct Scan {
  bool allowed = false;
  char firstName[17] = {};
  Reason reason = Reason::other;
  int64_t decidedAt = 0, membershipExpiry = 0, checkoutExpiry = 0;
};
enum class Enrollment { none, running, succeeded, failed };
// One bounded mailbox retains the terminal enrollment result AND final scan.
// It cannot lose enrollment failure if both updates arrive before rendering.
struct Presentation {
  uint64_t id = 0;
  Enrollment enrollment = Enrollment::none;
  bool finished = false;
  Scan scan;
};
Reason reasonFrom(const char *reason);
void firstName(const char *holder, char (&out)[17]);
int warningDays(int64_t expiry, int64_t now, int hours);
void newYorkClock(int64_t utcMs, char (&out)[6]);

class Backend {
public:
  virtual ~Backend() = default;
  // Two padded, sanitized 16-column rows. False means busy/partial: retry later.
  // Implementations must not wait for the bus or acquire reader state locks.
  virtual bool draw(const Frame &) = 0;
};
class NullBackend : public Backend {
public:
  bool draw(const Frame &) override { return true; }
};
class Presenter {
  Frame previous;
  bool havePrevious = false;
public:
  void render(Backend &, const Frame &);
};
class Model {
  struct Message {
    char left[12] = {}, bottom[17] = {};
    uint32_t color = white;
    int duration = 0;
  };
  Message messages[4];
  unsigned count = 0, index = 0;
  uint64_t id = 0;
  Enrollment enrollment = Enrollment::none;
  bool scanQueued = false;
  int64_t deadline = 0, startingUntil;
  void append(const char *, const char *, uint32_t, int);
public:
  explicit Model(int64_t now = 0) : startingUntil(now + 2000) {}
  void accept(const Presentation &, int64_t now);
  Frame frame(const Status &, int64_t now);
};

// Firmware adapters. Calls copy bounded data only; no LCD I/O or message waits.
void setup();
uint64_t beginPresentation();
void enrollmentProgress(uint64_t);
void enrollmentResult(uint64_t, bool);
void scanResult(uint64_t, const Scan &);
// The operation contains all API/RF early returns. Every returned result is
// published exactly once, independently of the phase that failed.
template <typename Operation>
bool enrollWithFeedback(uint64_t id, Operation operation) {
  enrollmentProgress(id);
  bool success = operation();
  enrollmentResult(id, success);
  return success;
}
// Backend factory: nullptr = bus busy, retry; NullBackend = absent.
Backend *detectBackend();
} // namespace display
