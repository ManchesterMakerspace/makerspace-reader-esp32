#include "../MakerspaceReader/ToolSession.h"
#include "../MakerspaceReader/RefreshConfig.h"
#include <cassert>
#include <iostream>
using namespace toolsession;
int main() {
  State s;
  Card owner, other;
  owner.uid="A"; owner.member="1"; owner.valid=true; owner.checkout=true;
  other.uid="B"; other.member="2"; other.valid=true; other.checkout=true;
  assert(s.scan(owner,0,true,false)==Action::start);
  assert(s.scan(owner,0,true,true)==Action::denied);
  assert(s.scan(owner,0,false,false)==Action::denied);
  s.begin("session1",owner);
  assert(s.scan(owner,0,false,true)==Action::logoff);
  assert(s.scan(other,1,false,true)==Action::confirmKick);
  assert(s.active());
  assert(s.scan(other,15000,false,true)==Action::kick);
  assert(s.active()); // Adapter owns the committed transition.
  assert(s.scan(other,20000,true,false)==Action::confirmKick);
  assert(s.scan(other,35000,true,false)==Action::confirmKick); // Exact timeout.
  Card invalid;
  assert(s.scan(invalid,35001,true,false)==Action::denied);
  assert(s.scan(other,35002,true,false)==Action::confirmKick);
  other.checkout=false;
  assert(s.scan(other,35003,true,false)==Action::denied); // Permission recheck.
  other.managesShop=true;
  assert(s.scan(other,35004,true,false)==Action::confirmKick);
  assert(s.scan(other,35005,true,false)==Action::kick);
  other.managesShop=false; other.boardOrAdmin=true;
  assert(s.scan(other,35006,true,false)==Action::confirmKick);
  s.end(); s.begin("session2",owner);
  assert(s.scan(other,35007,true,false)==Action::confirmKick); // No stale confirmation.
  other.valid=false;
  assert(s.scan(other,35008,true,false)==Action::denied);
  s.end(); other.valid=true;
  assert(s.scan(other,35009,true,false)==Action::denied); // Management is not a checkout.
  std::cout << "Tool session policy tests passed; refresh=" << READER_REFRESH_INTERVAL_MS << "ms\n";
}
