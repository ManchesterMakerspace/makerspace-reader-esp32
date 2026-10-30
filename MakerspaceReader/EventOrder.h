#pragma once
#include <cstring>
namespace audit {
// Old firmware did not save an ordering key for these records. Drain/evict
// that legacy backlog before sequenced records; its original order is unknown.
inline bool legacyEventPath(const char *path) {
  return std::strncmp(path, "/events/session-", 16) == 0;
}
inline bool eventPathLess(const char *a, const char *b) {
  // The empty replay cursor precedes every real filename.
  if (!*a || !*b) return std::strcmp(a,b)<0;
  bool oldA=legacyEventPath(a), oldB=legacyEventPath(b);
  return oldA != oldB ? oldA : std::strcmp(a,b)<0;
}
}
