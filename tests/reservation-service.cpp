#include "reservation-fakes/Reader.h"
#include "reservation-fakes/LittleFS.h"
#include "../MakerspaceReader/Audio.h"
#include "../MakerspaceReader/Reservation.h"
#include <iostream>
JsonDocument configuration,inventory;
bool clockTrusted=true,failStorage=false,offline=false,loseAck=false;
bool sessionReady=true;
bool serverMonitored=true;
int64_t serverArrival=0;
std::atomic<uint32_t> dropped(0);
int stateMutex=1,fsMutex=2,held=0;
std::map<std::string,std::string> flash;
FakeFS LittleFS;
int64_t utc=1800000000000LL,mono=1000;
int requests=0,reads=0;
std::vector<std::string> uploads;
reservation::Session active;
int64_t epochMs(){return utc;}
uint32_t millis(){return uint32_t(mono);}
int64_t esp_timer_get_time(){return mono*1000;}
bool sealWrite(const String &p,const String &v,bool critical){assert(!critical);if(failStorage)return false;flash[p]=v;return true;}
bool sealRead(const String &p,String &v,bool critical){assert(!critical);if(!flash.count(p))return false;v=flash[p];return true;}
bool api(const String &path,JsonDocument &response,const String &body){
  assert(!held);++requests;if(offline)return false;
  if(path=="/reader/v1/reservations" || path.startsWith("/reader/v1/reservations?")) {++reads;response=inventory;return true;}
  JsonDocument request;assert(!deserializeJson(request,body));auto results=response["results"].to<JsonArray>();
  for(auto op:request["operations"].as<JsonArray>()) {
    String encoded;serializeJson(op,encoded);uploads.push_back(encoded);
    auto result=results.add<JsonObject>();result["reservationId"]=op["reservationId"];result["revision"]=op["revision"];
    result["outcome"]="accepted";result["status"]="approved";
    result["monitored"]=serverMonitored;result["arrivedAt"]=serverArrival;
  }
  return !loseAck;
}
namespace audio {
static MelodyId playing=MelodyId::None;
bool playReservation(MelodyId id){playing=id;return true;}
void cancelReservation(MelodyId id){if(id==MelodyId::None || playing==id)playing=MelodyId::None;}
MelodyId reservationPlaying(){return playing;}
}
namespace reservation {void captureSession(Session &s){s=active;} bool attendanceReady(){return sessionReady;}}
#include "../.test-build/ReservationService.cpp"
static void advance(int64_t ms=61000){utc+=ms;mono+=ms;}
static void boot(){
  using namespace reservation;
  model.reset();journal.clear();tracked.clear();topics.clear();warningTargets.clear();cueTargets.clear();alertSession.clear();
  scope="";uncertain=false;eligible=true;loaded=freshJournal=false;coveredFrom=evaluatedThrough=checkedAt=0;
  nextNetwork=0;retryMs=1000;hinted=true;active={};setup();service();
}
static void seed(bool reservable=true){
  serverMonitored=true;serverArrival=0;
  flash.clear();uploads.clear();failStorage=offline=loseAck=false;dropped=0;clockTrusted=true;advance();
  configuration.clear();configuration["role"]="tool";configuration["toolId"]="bbbbbbbbbbbbbbbbbbbbbbbb";
  inventory.clear();inventory["toolId"]=configuration["toolId"];inventory["reservable"]=reservable;
  inventory["snapshotAt"]=utc;inventory["complete"]=true;
  auto rows=inventory["reservations"].to<JsonArray>();
  if(reservable){auto r=rows.add<JsonObject>();r["id"]="aaaaaaaaaaaaaaaaaaaaaaaa";r["memberId"]="dddddddddddddddddddddddd";
    r["name"]="Ada L";r["startAt"]=utc+1800000;r["endAt"]=utc+5400000;r["revision"]=0;r["monitored"]=true;}
  boot();reservation::network();
}
int main(){
  using namespace reservation;
  seed();active={"cccccccccccccccccccccccc","dddddddddddddddddddddddd",utc};
  int before=requests;service();assert(requests==before); // Session loop never waits on API.
  assert(journal["operations"].size()==1 && tracked[0].arrived==active.started && model.view(utc).own);
  JsonDocument ending;ending["reason"]="logoff";ending["kicked"]=false;ending["checkinId"]=active.id;
  ending["memberId"]=active.member;ending["_id"]="eeeeeeeeeeeeeeeeeeeeeeee";ending["time"]=utc+1000;
  assert(sessionEnded(ending.as<JsonVariantConst>()));assert(journal["operations"].size()==1 && journal["releases"].size()==1 && tracked[0].released);
  offline=true;advance();boot();network();assert(journal["operations"].size()==1 && journal["releases"].size()==1);
  offline=false;loseAck=true;advance();network();assert(journal["operations"].size()==1 && journal["releases"].size()==1);
  loseAck=false;advance();network();assert(journal["operations"].size()==0 && journal["releases"].size()==0 && uploads.size()>=3);
  assert(std::any_of(uploads.begin(),uploads.end(),[](const std::string &s){return s.find("claim")!=std::string::npos;}));
  assert(std::any_of(uploads.begin(),uploads.end(),[](const std::string &s){return s.find("release")!=std::string::npos;}));

  seed();advance(1000);
  inventory["reservations"][0]["startAt"]=utc+3*grace;
  auto earlier=inventory["reservations"].as<JsonArray>().add<JsonObject>();
  earlier["id"]="111111111111111111111111";earlier["memberId"]="222222222222222222222222";
  earlier["name"]="Other";earlier["startAt"]=utc+grace;earlier["endAt"]=utc+2*grace;
  refreshHint();network();
  active={"cccccccccccccccccccccccc","dddddddddddddddddddddddd",utc};service();
  auto *owner=tracking("aaaaaaaaaaaaaaaaaaaaaaaa");
  assert(owner && owner->arrived==active.started && owner->associatedSession==active.id);
  ending["time"]=utc+1000;sessionEnded(ending.as<JsonVariantConst>());
  assert(owner->released && !tracking("111111111111111111111111")->released);
  assert(journal["operations"].size()==1 && journal["releases"].size()==1 && journal["releases"][0]["kind"]=="release");

  for(bool partial : {false,true}) {
    seed();advance(1000);inventory["reservations"][0]["startAt"]=utc-grace;
    boot(); // Scan before the initial inventory fetch completes.
    active={"cccccccccccccccccccccccc","dddddddddddddddddddddddd",utc};
    if(partial) {inventory["complete"]=false;network();}
    service();assert(journal["operations"].size()==0);
    advance(1000);service();assert(journal["operations"].size()==0);
    inventory["complete"]=true;refreshHint();network();advance(500);service();
    assert(tracked[0].arrived==active.started && journal["operations"].size()==1);
    assert(journal["operations"][0]["observedAt"]==active.started);
  }

  // A full attendance queue leaves independent durable room for logoff releases.
  seed();active={"cccccccccccccccccccccccc","dddddddddddddddddddddddd",utc};service();
  for(int i=1;i<64;++i) {JsonDocument op;op["kind"]="claim";op["observedAt"]=i;assert(queue(op));}
  ending["time"]=utc+1000;
  assert(sessionEnded(ending.as<JsonVariantConst>()));
  assert(sessionEnded(ending.as<JsonVariantConst>())); // Replays cannot duplicate the release.
  assert(journal["operations"].size()==64 && journal["releases"].size()==1 && tracked[0].released);
  JsonDocument statistics;stats(statistics.to<JsonObject>());assert(statistics["pending"]==65);
  String retainedRelease;serializeJson(journal["releases"][0],retainedRelease);
  offline=true;advance();boot();network();
  assert(journal["releases"].size()==1 && tracked[0].released);
  active={"ffffffffffffffffffffffff","dddddddddddddddddddddddd",utc};service();
  offline=false;advance(); // Allow the offline retry backoff to expire.
  for(int i=0;i<16;++i) {advance(1000);network();}
  assert(journal["operations"].size()==0 && journal["releases"].size()==1);
  loseAck=true;advance(1000);network();assert(journal["releases"].size()==1);
  loseAck=false;advance(1000);boot();network();
  assert(journal["releases"].size()==0);
  assert(std::count(uploads.begin(),uploads.end(),std::string(retainedRelease))==2);

  seed(false);int readBefore=reads;advance();boot();network();assert(reads==readBefore && !eligible);
  advance(86400001);inventory["reservable"]=true;network();assert(reads==readBefore+1 && eligible);
  offline=true;advance();network();assert(eligible); // Failure never becomes a negative result.

  seed();active={"cccccccccccccccccccccccc","dddddddddddddddddddddddd",utc};failStorage=true;advance(1000);service();
  assert(uncertain && tracked[0].arrived==0); // Journal failure does not invent durable attendance.
  failStorage=false;advance(1000);service();assert(tracked[0].arrived==active.started && journal["operations"].size()==1);
  clockTrusted=false;before=requests;advance();service();network();assert(requests==before && evaluatedThrough==0);

  seed();tracked[0].start=utc-grace-1;model.records=tracked;inventory["reservations"][0]["startAt"]=tracked[0].start;flash["/events/one"]="{\"dbAck\":false}";
  advance();service();uploads.clear();network();assert(uploads.empty());
  flash["/events/one"]="{\"dbAck\":true}";sessionReady=false;advance();service();network();assert(uploads.empty());
  sessionReady=true;advance();service();network();assert(!uploads.empty() && uploads.back().find("checkpoint")!=std::string::npos);
  dropped=1;advance();service();assert(uncertain);

  seed();for(int i=0;i<64;++i){JsonDocument op;op["kind"]="claim";op["observedAt"]=i;assert(queue(op));}
  JsonDocument extra;extra["kind"]="claim";extra["observedAt"]=65;assert(!queue(extra) && uncertain);
  assert(journal["operations"].size()==64);
  configuration["role"]="latch";service();assert(!enabled && model.records.empty() && journal["operations"].size()==64);
  flash["/reservations"]="broken";advance();boot();assert(uncertain);
  seed();flash["/reservations.tmp"]="interrupted write";advance();boot();assert(uncertain);
  seed();active={"cccccccccccccccccccccccc","dddddddddddddddddddddddd",utc};service();
  ending["time"]=utc+1000;failStorage=true;assert(!sessionEnded(ending.as<JsonVariantConst>()));
  failStorage=false;boot();assert(sessionEnded(ending.as<JsonVariantConst>())); // Production startup replays the retained session ending.
  assert(journal["operations"].size()==1 && journal["releases"].size()==1 && tracked[0].released);
  seed();advance(6000000);service();inventory["reservations"].to<JsonArray>();
  serverArrival=utc-5000000;network();assert(tracked.empty()); // Centrally recovered attendance retires expired records.
  seed();advance(6000000);service();inventory["reservations"].to<JsonArray>();
  serverMonitored=false;network();assert(tracked.empty()); // Late discovery cannot initialize historical monitoring.
  seed();advance(6000000);service();inventory["reservations"].to<JsonArray>();
  network();assert(tracked.size()==1); // Unresolved monitored no-shows remain retryable.
  seed();advance();configuration["toolId"]="ffffffffffffffffffffffff";service();
  assert(coveredFrom==utc && !uncertain && tracked.empty()); // New scope starts fresh coverage, not permanent uncertainty.
  uncertain=true;advance();configuration["toolId"]="bbbbbbbbbbbbbbbbbbbbbbbb";service();
  assert(coveredFrom==utc && uncertain); // Reassignment cannot erase actual attendance loss.
  std::cout<<"Production reservation journal replay, offline release, coverage fencing, corruption, overflow and clock loss passed\n";
}
