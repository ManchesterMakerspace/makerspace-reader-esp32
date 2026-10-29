#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace nfcProtocol {
enum class Controller : uint8_t { none, pn7160, pn532 };
inline Controller select(bool have7160, bool have532) {
  return have7160 ? Controller::pn7160 : have532 ? Controller::pn532 : Controller::none;
}
inline bool ready(Controller controller, bool high) {
  return controller == Controller::pn7160 ? high : controller == Controller::pn532 && !high;
}
// Normal PN532 frame, without the I2C RDY byte. Larger/extended frames are not
// needed for Classic sector access and are rejected, as are stale command replies.
inline bool response(const uint8_t *frame, size_t received, uint8_t command, uint8_t *payload,
                     size_t capacity, size_t &length) {
  length = 0;
  if (received < 9 || frame[0] != 0 || frame[1] != 0 || frame[2] != 0xFF)
    return false;
  size_t n = frame[3];
  if (n < 2 || n + 7 > received || uint8_t(frame[3] + frame[4]) != 0 || frame[5] != 0xD5 ||
      frame[6] != uint8_t(command + 1) || n - 2 > capacity || frame[n + 6] != 0)
    return false;
  uint8_t sum = 0;
  for (size_t i = 5; i <= n + 5; ++i)
    sum += frame[i];
  if (sum != 0)
    return false;
  length = n - 2;
  memcpy(payload, frame + 7, length);
  return true;
}
inline bool target(const uint8_t *payload, size_t length, uint8_t *uid, uint8_t &uidLength,
                   bool &classic) {
  uidLength = 0;
  classic = false;
  if (length == 1 && payload[0] == 0)
    return true; // No target.
  if (length < 6 || payload[0] != 1 || payload[1] != 1)
    return false;
  uint8_t n = payload[5];
  if ((n != 4 && n != 7 && n != 10) || length < size_t(6 + n))
    return false;
  // Classic 1K/4K SAK. Other Type A tags are reported but never authenticated/enrolled.
  classic = (n == 4 || n == 7) && (payload[4] == 0x08 || payload[4] == 0x18);
  uidLength = n;
  memcpy(uid, payload + 6, n);
  return true;
}
} // namespace nfcProtocol
