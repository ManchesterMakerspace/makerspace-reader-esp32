#include "session-fakes/Reader.h"
#include "session-fakes/LittleFS.h"
#include <iostream>
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
void reserveEventSpace(size_t){}
void esp_fill_random(void *p,size_t n){static uint8_t seq=1;memset(p,seq++,n);}
String hexBytes(const uint8_t *p,size_t n){String s;char b[3];for(size_t i=0;i<n;i++){snprintf(b,3,"%02x",p[i]);s+=b;}return s;}
int64_t epochMs(){return 1800000000000LL+mono/1000;}
void startRelay(int64_t,int64_t,const String &){powered=true;}
void stopRelay(const char *reason,const Card *actor){powered=false;endToolSession(reason,actor);}
#include "../.test-build/ToolSessionService.cpp"
void boot(){session=toolsession::State{};journal.clear();pendingEnd=false;haveLast=false;activeName="";powered=false;maintenance=false;clockTrusted=false;statusDurable=true;toolStatusRevision=0;restoreToolStatus();recoverToolSession();clockTrusted=true;}
void setup(){flash.clear();prefs.values.clear();prefs.fail=false;writes=failWrite=0;configuration.clear();configuration["role"]="tool";configuration["toolId"]="bbbbbbbbbbbbbbbbbbbbbbbb";configuration["shopId"]="cccccccccccccccccccccccc";configuration["toolName"]="Saw";configuration["shopName"]="Wood";configuration["toolAvailable"]=true;configuration["toolOutOfService"]=false;configuration["shopOutOfService"]=false;boot();statusDurable=true;toolStatusRevision=0;}
Card owner(){Card c;c.uid="00112233";c.member="dddddddddddddddddddddddd";c.holder="Ada L";c.firstName="Ada";c.lastInitial="L";grants[c.member].until=epochMs()+100000;return c;}
JsonDocument status(bool tool,bool shop){JsonDocument d;d["enabled"]=true;d["name"]="Saw";d["shopId"]="cccccccccccccccccccccccc";d["shopName"]="Wood";d["toolOutOfService"]=tool;d["shopOutOfService"]=shop;return d;}
int main(){
  // Power cuts before each end-write boundary must replay the same logical ID.
  for(int cut=1;cut<=4;cut++){
    setup();auto c=owner();String reason;assert(toolScan(c,epochMs()+100000,reason));
    String endId=journal["endId"]|"";
    assert(toolScan(c,epochMs()+100000,reason));assert(!powered);
    failWrite=writes+cut;serviceToolSession();failWrite=0;boot();serviceToolSession();
    assert(!maintenance && !powered);
    JsonDocument e;assert(!deserializeJson(e,flash["/events/session-"+endId]));
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
  String eventPath="/events/session-"+originalEnd;
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
