#include "Reader.h"

uint64_t reserveEventSequence() {
  Lock f(fsMutex);
  uint64_t sequence=prefs.getULong64("eventSeq",0)+1;
  if (!sequence || !checkNvsWrite("eventSeq",prefs.putULong64("eventSeq",sequence),8)) {
    storageOK=false;
    return 0;
  }
  return sequence;
}
String eventSpoolPath(uint64_t sequence, const String &id) {
  char prefix[22];
  snprintf(prefix,sizeof(prefix),"%020llu-",static_cast<unsigned long long>(sequence));
  return "/events/" + String(prefix) + id;
}
