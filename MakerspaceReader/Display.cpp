#include "Display.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace display {
static bool space(unsigned char c) { return c == ' ' || (c >= 9 && c <= 13); }
static bool safe(unsigned char c) { return c >= 32 && c <= 126 && c != '|'; }
void firstName(const char *holder, char (&out)[17]) {
  std::memset(out, 0, sizeof(out));
  if (!holder) return;
  while (*holder && space(static_cast<unsigned char>(*holder))) ++holder;
  unsigned n = 0;
  while (*holder && !space(static_cast<unsigned char>(*holder))) {
    unsigned char c = static_cast<unsigned char>(*holder++);
    if (safe(c) && n < 16) out[n++] = char(c);
  }
}
Reason reasonFrom(const char *reason) {
  if (!reason) return Reason::other;
  if (!std::strcmp(reason, "unknown")) return Reason::unknown;
  if (!std::strcmp(reason, "revoked")) return Reason::revoked;
  if (!std::strcmp(reason, "expired")) return Reason::expired;
  if (!std::strcmp(reason, "checkoutExpired")) return Reason::checkoutExpired;
  return Reason::other;
}
int warningDays(int64_t expiry, int64_t now, int hours) {
  if (expiry <= 0 || expiry <= now) return 0;
  int64_t remaining = expiry - now;
  return remaining <= int64_t(hours) * 3600000
             ? int((remaining + 86399999) / 86400000) : 0;
}
// Gregorian days since 1970-01-01, used only for New York's post-2007 DST rule.
// No setenv/TZ/localtime calls: authorization and RTC remain in UTC.
static int64_t days(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  int era = (y >= 0 ? y : y - 399) / 400;
  unsigned yo = unsigned(y - era * 400);
  unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  return int64_t(era) * 146097 + yo * 365 + yo / 4 - yo / 100 + doy - 719468;
}
void newYorkClock(int64_t utcMs, char (&out)[6]) {
  time_t seconds = time_t(utcMs / 1000);
  tm utc = {};
#ifdef _WIN32
  gmtime_s(&utc, &seconds);
#else
  gmtime_r(&seconds, &utc);
#endif
  int year = utc.tm_year + 1900;
  int marchSunday = 1 + (7 - int((days(year, 3, 1) + 4) % 7)) % 7;
  int novSunday = 1 + (7 - int((days(year, 11, 1) + 4) % 7)) % 7;
  int64_t spring = (days(year, 3, unsigned(marchSunday + 7)) * 24 + 7) * 3600;
  int64_t fall = (days(year, 11, unsigned(novSunday)) * 24 + 6) * 3600;
  int offset = seconds >= spring && seconds < fall ? -4 : -5;
  int64_t minute = (int64_t(seconds) / 60 + offset * 60) % 1440;
  if (minute < 0) minute += 1440;
  std::snprintf(out, sizeof(out), "%02d:%02d", int(minute / 60), int(minute % 60));
}
static void pad(char *out, unsigned width, const char *text) {
  std::memset(out, ' ', width);
  unsigned n = 0;
  while (text && *text && n < width) {
    unsigned char c = static_cast<unsigned char>(*text++);
    if (safe(c)) out[n++] = char(c);
  }
  out[width] = 0;
}
void Presenter::render(Backend &backend, const Frame &frame) {
  if (havePrevious && previous.color == frame.color &&
      !std::strcmp(previous.row1, frame.row1) && !std::strcmp(previous.row2, frame.row2))
    return;
  if (backend.draw(frame)) { previous = frame; havePrevious = true; }
  else havePrevious = false; // A partial frame may have changed the physical display.
}
void Model::append(const char *left, const char *bottom, uint32_t color, int duration) {
  if (count == 4) return;
  auto &m = messages[count++];
  pad(m.left, 11, left);
  pad(m.bottom, 16, bottom);
  m.color = color;
  m.duration = duration;
}
void Model::accept(const Presentation &p, int64_t now) {
  if (!p.id || p.id < id) return;
  if (p.id != id) {
    id = p.id;
    count = index = 0;
    enrollment = Enrollment::none;
    scanQueued = false;
    deadline = now;
    startingUntil = 0;
  }
  while (index < count && now >= deadline) {
    ++index;
    if (index < count) deadline += messages[index].duration;
  }
  if (p.enrollment != enrollment &&
      (enrollment == Enrollment::none || enrollment == Enrollment::running)) {
    enrollment = p.enrollment;
    count = index = 0;
    if (enrollment == Enrollment::succeeded)
      append("Enrollment", "Succeeded", green, 2000);
    if (enrollment == Enrollment::failed)
      append("Enrollment", "Failed", red, 3000);
    deadline = now + (count ? messages[0].duration : 0);
  }
  if (!p.finished || scanQueued) return;
  scanQueued = true;
  if (enrollment == Enrollment::failed) return; // Do not replace failure with generic denial.
  unsigned before = count;
  const auto &s = p.scan;
  if (s.allowed) {
    append("Welcome", s.firstName, green, 2000);
    char text[17];
    int membership = warningDays(s.membershipExpiry, s.decidedAt, 72);
    int checkout = warningDays(s.checkoutExpiry, s.decidedAt, 336);
    if (membership) {
      std::snprintf(text, sizeof(text), "Expires in %dd", membership);
      append("Membership", text, amber, 3000);
    }
    if (checkout) {
      std::snprintf(text, sizeof(text), "Expires in %dd", checkout);
      append("Checkout", text, amber, 3000);
    }
  } else if (s.reason == Reason::expired) append("Membership", "Expired", red, 3000);
  else if (s.reason == Reason::checkoutExpired) append("Checkout", "Expired", red, 3000);
  else if (s.reason == Reason::unknown || s.reason == Reason::revoked)
    append("Unknown Fob", "", red, 3000);
  else append("Access", "Denied", red, 3000);
  // A scan arriving after the enrollment result has elapsed still gets its full duration.
  if (index >= before) {
    index = before;
    deadline = now + messages[index].duration;
  }
}
Frame Model::frame(const Status &s, int64_t now) {
  while (index < count && now >= deadline) {
    ++index;
    if (index < count) deadline += messages[index].duration;
  }
  Frame f;
  const char *left = "", *bottom = "Hold fob to read";
  if (enrollment == Enrollment::running) {
    left = "Enrolling"; bottom = "Keep holding"; f.color = amber;
  } else if (index < count) {
    left = messages[index].left; bottom = messages[index].bottom; f.color = messages[index].color;
  } else if (now < startingUntil) {
    left = "Starting"; bottom = "Reader"; f.color = blue;
  } else if (!s.net || !s.api || !s.mqtt || !s.trusted) {
    bottom = !s.net ? "Error: NET" : !s.api ? "Error: DB" : !s.mqtt ? "Error: MQ" : "Error: NTP";
    f.color = pink;
  } else if (s.relay && !s.tool && s.relayDeadlineMs > now) {
    left = "Access on"; f.color = green;
  }
  pad(f.row1, 16, "");
  // Copy only eleven characters; do not leave a NUL in the reserved clock field.
  char label[12];
  pad(label, 11, left);
  std::memcpy(f.row1, label, 11);
  pad(f.row2, 16, bottom);
  char clock[6] = "     ";
  if (s.relay && s.tool) {
    int64_t minutes = std::max<int64_t>(0, now - s.relayStartedMs) / 60000;
    minutes = std::min<int64_t>(minutes, 5999); // Saturate at 99:59, never wrap.
    std::snprintf(clock, sizeof(clock), "%02d:%02d", int(minutes / 60), int(minutes % 60));
  } else if (s.relay && s.relayDeadlineMs > now) {
    int seconds = int(std::min<int64_t>(9999, (s.relayDeadlineMs - now + 999) / 1000));
    std::snprintf(clock, sizeof(clock), "%4ds", seconds);
  } else if (s.trusted) newYorkClock(s.utcMs, clock);
  std::memcpy(f.row1 + 11, clock, 5);
  return f;
}
} // namespace display
