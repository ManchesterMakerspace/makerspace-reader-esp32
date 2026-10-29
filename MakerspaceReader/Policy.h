#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
namespace policy {
inline int pulseSeconds(const char *role) {
  if (!std::strcmp(role, "door"))
    return 10;
  if (!std::strcmp(role, "garagedoor"))
    return 30;
  if (!std::strcmp(role, "test"))
    return 9;
  if (!std::strcmp(role, "latch"))
    return 5;
  return 0;
}
struct Decision {
  const char *reason;
  int64_t until;
  bool allow;
};
inline Decision evaluate(const char *role, bool known, const char *status, int64_t expiry,
                         bool enrolled, bool toolAvailable, bool hasGrant, int64_t grantExpiry,
                         int64_t now) {
  if (!known)
    return {"unknown", 0, false};
  if (std::strcmp(status, "activeMember"))
    return {status, 0, false};
  if (expiry <= now)
    return {"expired", 0, false};
  if (!enrolled)
    return {"unenrolled", 0, false};
  if (!std::strcmp(role, "tool")) {
    if (!toolAvailable)
      return {"toolUnavailable", 0, false};
    if (!hasGrant)
      return {"checkoutRequired", 0, false};
    int64_t end = std::min(expiry, grantExpiry);
    if (end <= now)
      return {"checkoutExpired", 0, false};
    return {"activeMember", end, true};
  }
  if (!pulseSeconds(role))
    return {"unconfigured", 0, false};
  return {"activeMember", expiry, true};
}
inline bool completeDataFrame(const uint8_t *frame, uint32_t count) {
  return count >= 3 && count == uint32_t(frame[2]) + 3 && frame[0] == 0 && frame[1] == 0;
}
} // namespace policy
