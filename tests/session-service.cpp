#include "session-fakes/Reader.h"
#include "session-fakes/LittleFS.h"
#include <iostream>
#include <algorithm>
#include <vector>
#include "../MakerspaceReader/EventOrder.h"
std::unordered_map<std::string,Grant> grants;
JsonDocument configuration;
bool maintenance=false,storageOK=true,clockTrusted=true,refreshNeeded=false;
int stateMutex=1,fsMutex=2;
int64_t mono=1000000;
Preferences prefs;
std::map<std::string,std::string> flash;
FakeFS LittleFS;
int writes=0, failWrite=0; bool powered=false;
bool sealWrite(const String &p,const String &v){if(++writes==failWrite)return false;flash[p]=v;return true;}
bool sealRead(const String &p,String &v){if(!flash.count(p))return false;v=flash[p];return true;}
size_t eventCapacity=1000;
std::vector<std::string> queuedEvents() {
  std::vector<std::string> paths;
  for (const auto &entry:flash) if(entry.first.find("/events/")==0) paths.push_back(entry.first);
  std::sort(paths.begin(),paths.end(),[](const std::string &a,const std::string &b){return audit::eventPathLess(a.c_str(),b.c_str());});
  return paths;
}
void reserveEventSpace(size_t){auto paths=queuedEvents();while(paths.size()>=eventCapacity){flash.erase(paths.front());paths.erase(paths.begin());}}
void esp_fill_random(void *p,size_t n){static uint8_t seq=1;memset(p,seq++,n);}
String hexBytes(const uint8_t *p,size_t n){String s;char b[3];for(size_t i=0;i<n;i++){snprintf(b,3,"%02x",p[i]);s+=b;}return s;}
int64_t epochMs(){return 1800000000000LL+mono/1000;}
void startRelay(int64_t,int64_t,const String &){powered=true;}
void stopRelay(const char *reason,const Card *actor){powered=false;endToolSession(reason,actor);}
#include "../.test-build/EventSpool.cpp"
#include "../.test-build/ToolSessionService.cpp"
void boot(){session=toolsession::State{};journal.clear();pendingEnd=false;haveLast=false;activeName="";powered=false;maintenance=false;clockTrusted=false;statusDurable=true;toolStatusRevision=0;restoreToolStatus();recoverToolSession();clockTrusted=true;}
void setup(){eventCapacity=1000;flash.clear();prefs.values.clear();prefs.fail=false;writes=failWrite=0;configuration.clear();configuration["role"]="tool";configuration["toolId"]="bbbbbbbbbbbbbbbbbbbbbbbb";configuration["shopId"]="cccccccccccccccccccccccc";configuration["toolName"]="Saw";configuration["shopName"]="Wood";configuration["toolAvailable"]=true;configuration["toolOutOfService"]=false;configuration["shopOutOfService"]=false;boot();statusDurable=true;toolStatusRevision=0;}
Card owner(){Card c;c.uid="00112233";c.member="dddddddddddddddddddddddd";c.holder="Ada L";c.firstName="Ada";c.lastInitial="L";grants[c.member].until=epochMs()+100000;return c;}
JsonDocument status(bool tool,bool shop){JsonDocument d;d["enabled"]=true;d["name"]="Saw";d["shopId"]="cccccccccccccccccccccccc";d["shopName"]="Wood";d["toolOutOfService"]=tool;d["shopOutOfService"]=shop;return d;}
int main(){
  // Shared production allocator interleaves rejection and session records.
  setup();auto orderingOwner=owner();String orderingReason;
  assert(toolScan(orderingOwner,epochMs()+100000,orderingReason));
  uint64_t startOrder=journal["startSequence"];
  String startPath=eventSpoolPath(startOrder,journal["start"]["_id"] | "");
  auto rejection=reserveEventSequence();
  String rejectionPath=eventSpoolPath(rejection,"000000000000000000000000");
  flash[rejectionPath]="rejection";
  assert(toolScan(orderingOwner,epochMs()+100000,orderingReason));
  uint64_t endOrder=journal["endSequence"];
  assert(startOrder<rejection && rejection<endOrder);
  String endPath=eventSpoolPath(endOrder,journal["endId"] | "");
  eventCapacity=2;serviceToolSession();
  assert(!flash.count(startPath) && flash.count(rejectionPath) && flash.count(endPath));
  assert(queuedEvents()==std::vector<std::string>({rejectionPath,endPath}));
  // A failed journal commit retries the original in-memory ordering key.
  setup();orderingOwner=owner();assert(toolScan(orderingOwner,epochMs()+100000,orderingReason));
  assert(toolScan(orderingOwner,epochMs()+100000,orderingReason));
  endOrder=journal["endSequence"];failWrite=writes+1;serviceToolSession();
  reserveEventSequence();failWrite=0;serviceToolSession();assert(journal["endSequence"].as<uint64_t>()==endOrder);
  auto committedSequence=prefs.getULong64("eventSeq",0);boot();assert(prefs.getULong64("eventSeq",0)==committedSequence);
  assert(journal["endSequence"].as<uint64_t>()==endOrder);
  // Reserve failures/overflow cannot create sequence-zero records or energize.
  setup();prefs.fail=true;assert(!toolScan(owner(),epochMs()+100000,orderingReason));assert(!powered && flash.empty());
  prefs.fail=false;prefs.values["eventSeq"]=std::to_string(UINT64_MAX);
  assert(reserveEventSequence()==0 && prefs.getULong64("eventSeq",0)==UINT64_MAX);
  setup();orderingOwner=owner();assert(toolScan(orderingOwner,epochMs()+100000,orderingReason));
  prefs.fail=true;assert(toolScan(orderingOwner,epochMs()+100000,orderingReason));
  assert(!powered && pendingEnd && journal["endSequence"].as<uint64_t>()==0);
  serviceToolSession();assert(queuedEvents().size()==1); // No sequence-zero end file.
  String retryEndId=journal["endId"] | "";
  prefs.fail=false;serviceToolSession();assert(!pendingEnd);
  assert(String(journal["end"]["_id"] | "")==retryEndId && journal["endSequence"].as<uint64_t>()>journal["startSequence"].as<uint64_t>());
  // Padded paths sort numerically across digit boundaries, independent of IDs.
  assert(eventSpoolPath(9,"ffffffffffffffffffffffff")<eventSpoolPath(10,"000000000000000000000000"));
  assert(eventSpoolPath(99,"ffffffffffffffffffffffff")<eventSpoolPath(100,"000000000000000000000000"));
  // Legacy records are kept (including ACKs), but cannot indefinitely displace
  // newer sequenced events. Cursor ordering must use the same comparator.
  setup();orderingOwner=owner();assert(toolScan(orderingOwner,epochMs()+100000,orderingReason));
  startPath=eventSpoolPath(journal["startSequence"],journal["start"]["_id"] | "");
  String legacyPath="/events/session-"+String(journal["start"]["_id"] | "");
  JsonDocument acknowledged;deserializeJson(acknowledged,flash[startPath]);acknowledged["dbAck"]=true;
  String legacyRaw;serializeJson(acknowledged,legacyRaw);flash[legacyPath]=legacyRaw;flash.erase(startPath);
  JsonDocument oldJournal;deserializeJson(oldJournal,flash["/tool-session"]);oldJournal.remove("startSequence");
  String oldRaw;serializeJson(oldJournal,oldRaw);flash["/tool-session"]=oldRaw;
  boot();serviceToolSession();
  assert(!maintenance && flash.count(legacyPath) && flash[legacyPath]==legacyRaw);
  assert(journal["startSequence"].as<uint64_t>()<journal["endSequence"].as<uint64_t>());
  auto legacyQueue=queuedEvents();assert(legacyQueue.front()==legacyPath);
  assert(std::upper_bound(legacyQueue.begin(),legacyQueue.end(),std::string(),
    [](const std::string &a,const std::string &b){return audit::eventPathLess(a.c_str(),b.c_str());})==legacyQueue.begin());
  auto afterLegacy=std::upper_bound(legacyQueue.begin(),legacyQueue.end(),std::string(legacyPath),
    [](const std::string &a,const std::string &b){return audit::eventPathLess(a.c_str(),b.c_str());});
  assert(afterLegacy!=legacyQueue.end() && *afterLegacy==legacyQueue.back());
  eventCapacity=legacyQueue.size();reserveEventSpace(8192);assert(!flash.count(legacyPath));
  // Power cuts before each end-write boundary must replay the same logical ID.
  for(int cut=1;cut<=4;cut++){
    setup();auto c=owner();String reason;assert(toolScan(c,epochMs()+100000,reason));
    String endId=journal["endId"]|"";
    assert(toolScan(c,epochMs()+100000,reason));assert(!powered);
    failWrite=writes+cut;serviceToolSession();failWrite=0;boot();serviceToolSession();
    assert(!maintenance && !powered);
    JsonDocument e;assert(!deserializeJson(e,flash[eventSpoolPath(journal["endSequence"],endId)]));
    assert(e["collection"]=="checkend");assert(String(e["document"]["_id"]|"")==endId);
    auto durable=flash["/tool-session"];boot();serviceToolSession();assert(flash["/tool-session"]==durable);
  }
  setup();auto c=owner();String reason;assert(toolScan(c,epochMs()+100000,reason));
  boot();assert(!powered && pendingEnd);assert(journal["end"]["time"]==0);assert(journal["end"]["reason"]=="reboot");
  serviceToolSession();
  flash["/tool-session"]="{}";boot();assert(maintenance);
  setup();c=owner();assert(toolScan(c,epochMs()+100000,reason));
  assert(toolScan(c,epochMs()+100000,reason));
  String originalEnd=journal["endId"]|"";
  auto originalTime=journal["end"]["time"].as<int64_t>();
  endToolSession("anotherStop"); assert(journal["end"]["reason"]=="logoff");
  failWrite=writes+3; serviceToolSession(); // Closed-marker write fails after both spool files exist.
  String eventPath=eventSpoolPath(journal["endSequence"],originalEnd);
  auto delivered=flash[eventPath]; flash.erase(eventPath); // Both consumers acknowledged before reboot.
  failWrite=0;boot();serviceToolSession();
  assert(flash[eventPath]==delivered); // At-least-once delivery reuses identical ID AND payload.
  assert(journal["end"]["time"].as<int64_t>()==originalTime);
  setup();c=owner();assert(toolScan(c,epochMs()+100000,reason));
  auto runningOutage=status(true,false); updateToolStatus(runningOutage,0);
  assert(powered && toolOutOfService()); // Status changes never switch off current activity.
  assert(toolScan(c,epochMs()+100000,reason)); assert(!powered);
  serviceToolSession(); assert(!toolScan(c,epochMs()+100000,reason)); // New start is blocked.

  setup();configuration["shopOutOfService"]=true;auto clear=status(false,false);updateToolStatus(clear,0);
  configuration["shopOutOfService"]=true;restoreToolStatus();assert(!toolOutOfService()); // Old snapshot cannot resurrect outage.
  auto oldRevision=toolStatusRevision.load();latchToolOutage(true,"cccccccccccccccccccccccc");
  configuration.clear(); configuration["toolId"]="bbbbbbbbbbbbbbbbbbbbbbbb";
  updateToolStatus(clear,oldRevision);assert(toolOutOfService()); // In-flight pre-hint HTTP cannot clear it.
  configuration["shopId"]="stale";configuration["shopOutOfService"]=false;restoreToolStatus();assert(toolOutOfService());
  assert(String(configuration["shopId"]|"")=="cccccccccccccccccccccccc");
  updateToolStatus(clear,toolStatusRevision);assert(!toolOutOfService());
  auto both=status(true,true);updateToolStatus(both,toolStatusRevision);
  auto shopOnly=status(false,true);updateToolStatus(shopOnly,toolStatusRevision);assert(toolOutOfService());
  prefs.fail=true;updateToolStatus(clear,toolStatusRevision);assert(toolOutOfService()); // Failed commit cannot admit.
  boot(); assert(toolOutOfService() && !powered);
  setup();flash["/tool-session"]="{\"closed\":true}";boot();assert(maintenance);
  setup();prefs.values["toolStatus"]="broken";boot();assert(toolOutOfService());
  std::cout<<"Production session recovery and outage persistence checks passed\n";
}
