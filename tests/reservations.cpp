#include "../MakerspaceReader/Reservation.h"
#include <cassert>
#include <iostream>
using namespace reservation;
static Record booking(std::string id="a",std::string member="owner",int64_t start=10*hour) {
  Record r; r.id=id; r.member=member; r.name="Ada L"; r.start=start; r.end=start+hour; return r;
}
int main() {
  const int64_t start=10*hour;
  Model m; m.setSession({"session","owner",start-2*hour});
  m.replace({booking()},true,start-2*hour);
  assert(m.tick(start-hour-1).claim.empty());
  assert(m.tick(start-hour).claim=="a");
  assert(m.tick(start+grace).claim=="a");
  assert(m.tick(start+grace+1).claim.empty());
  assert(!m.view(start+grace+1).present);
  m.records[0].arrived=start;
  assert(m.view(start+grace+1).own);
  assert(m.tick(start+hour-grace).jeopardy);
  assert(!m.tick(start+hour-grace+1).jeopardy);
  assert(m.tick(start+hour-300000).jeopardy);
  assert(!m.tick(start+hour-299999).jeopardy);
  assert(!m.view(start+hour).present);
  m.setSession({"next","owner",start});
  assert(m.tick(start+hour-100000).jeopardy);
  assert(!m.tick(start+hour-99999).jeopardy);
  m.reset(); m.setSession({"s","owner",start-hour});
  m.replace({booking(),booking("b","other",start-600000)},true,start-hour);
  assert(!m.tick(start-hour).newReservationCue); // Initial retained inventory is silent.
  assert(m.tick(start).claim.empty()); // Other active booking blocks association.
  m.records[1].start=start-2*hour; m.records[1].end=start+hour;
  assert(m.tick(start).claim.empty()); // Hidden overdue notices still block until Rails releases them.
  m.replace({booking(),booking("new","other",start+hour)},true,start);
  assert(m.tick(start).newReservationCue);
  m.replace({booking(),booking("new","other",start+hour)},true,start+1);
  assert(!m.tick(start+1).newReservationCue);
  m.records[0].arrived=start;
  assert(m.view(start).deadline==start+hour);
  m.records[1].start=start+hour/2;
  assert(!m.view(start).own && m.view(start).deadline==start+hour/2);
  assert(m.tick(start+hour/2-grace).jeopardy);
  assert(m.view(start+hour/2).due);
  m.replace({booking()},false,start);
  m.records[0].arrived=0;
  assert(m.tick(start).claim.empty()); // Partial inventory never proves no conflict.
  m.reset(); m.replace({booking()},true,start);
  assert(m.view(start).present && !m.view(start).countdown);
  m.records[0].released=true;
  assert(!m.view(start).present);
  m.setSession({"s","owner",start});
  assert(m.tick(0).claim.empty() && !m.view(0).present);
  m.reset(); m.setSession({"s","owner",start});
  m.replace({booking("later","other",start+4*hour)},true,start-1);
  assert(!m.tick(start-1).flash && !m.view(start-1).present);
  assert(m.tick(start).flash && m.view(start).present);
  assert(!m.tick(start+1).flash);
  m.records[0].start=start+300001;
  auto warning=m.tick(start+1);
  assert(warning.jeopardy && warning.warnings.size()==1 && warning.warnings[0].deadline==start+300001);
  assert(!m.tick(start+2).jeopardy); // Delayed tick coalesces crossed thresholds.
  m.reset();m.replace({booking()},true,start);
  m.setSession({"boundary","owner",start+grace});
  auto boundary=m.tick(start+grace+1000);
  assert(boundary.claim=="a" && boundary.observedAt==start+grace);
  // A future booking by another member does not block an eligible owner booking.
  m.reset(); m.setSession({"early","owner",9*hour});
  auto earlier=booking("earlier","other",9*hour+grace);
  earlier.end=9*hour+2*grace;
  m.replace({booking("owner","owner",9*hour+3*grace),earlier},true,9*hour);
  auto early=m.tick(9*hour);
  assert(early.claim=="owner" && early.observedAt==9*hour);
  assert(m.tick(earlier.start).claim.empty()); // Once active, the conflict still blocks.
  assert(m.tick(earlier.end).claim=="owner");
  // Initial fetch and partial inventory must not consume the scan timestamp.
  for (bool partial : {false,true}) {
    m.reset(); m.setSession({"deferred","owner",start+grace});
    if (partial) m.replace({booking()},false,start+grace);
    assert(m.tick(start+grace).claim.empty());
    assert(m.tick(start+grace+500).claim.empty());
    m.replace({booking()},true,start+grace+1000);
    auto deferred=m.tick(start+grace+1000);
    assert(deferred.claim=="a" && deferred.observedAt==start+grace);
  }
  // Deferred evaluation still respects arrival-time conflicts and reservation end.
  m.reset(); m.setSession({"conflict","owner",start+grace});
  assert(m.tick(start+grace).claim.empty());
  auto conflict=booking("conflict","other",start);
  conflict.end=start+grace+500;
  m.replace({booking(),conflict},true,start+grace+1000);
  assert(m.tick(start+grace+1000).claim.empty());
  m.reset(); m.setSession({"expired","owner",start+grace});
  assert(m.tick(start+grace).claim.empty());
  m.replace({booking()},true,start+hour);
  assert(m.tick(start+hour).claim.empty());
  std::cout << "Reservation boundaries, ownership, warnings, deduplication and incomplete inventory passed\n";
}
