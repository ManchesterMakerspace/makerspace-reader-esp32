#include "../MakerspaceReader/Policy.h"
#include <cassert>
#include <iostream>
int main() {
  using namespace policy;
  assert(pulseSeconds("door") == 10 && pulseSeconds("garagedoor") == 30);
  assert(pulseSeconds("test") == 9 && pulseSeconds("latch") == 5 && pulseSeconds("tool") == 0);
  const int64_t now = 1793143879042LL;
  for (const char *role : {"door", "garagedoor", "test", "latch", "tool"}) {
    assert(evaluate(role, true, "activeMember", now + 100, true, true, true, now + 200, now).allow);
    assert(
        !evaluate(role, false, "activeMember", now + 100, true, true, true, now + 200, now).allow);
    assert(!evaluate(role, true, "activeMember", now, true, true, true, now + 200, now).allow);
    assert(
        !evaluate(role, true, "activeMember", now + 100, false, true, true, now + 200, now).allow);
    for (const char *status :
         {"lost", "stolen", "revoked", "suspended", "expired", "nonMember", "pending"})
      assert(!evaluate(role, true, status, now + 100, true, true, true, now + 200, now).allow);
  }
  assert(
      !evaluate("tool", true, "activeMember", now + 100, true, false, true, now + 200, now).allow);
  assert(
      !evaluate("tool", true, "activeMember", now + 100, true, true, false, now + 200, now).allow);
  assert(!evaluate("tool", true, "activeMember", now + 100, true, true, true, now, now).allow);
  assert(evaluate("tool", true, "activeMember", now + 100, true, true, true, now + 50, now).until ==
         now + 50);
  assert(
      evaluate("tool", true, "activeMember", now + 100, true, true, true, INT64_MAX, now).until ==
      now + 100);
  assert(!evaluate("unknown", true, "activeMember", now + 100, true, true, true, now + 200, now)
              .allow);
  uint8_t frame[] = {0, 0, 2, 0x40, 0};
  assert(completeDataFrame(frame, 5));
  for (uint32_t n = 0; n < 5; n++)
    assert(!completeDataFrame(frame, n));
  assert(!completeDataFrame(frame, 6));
  frame[0] = 0x60;
  assert(!completeDataFrame(frame, 5));
  std::cout << "Policy and NCI framing checks passed\n";
}
