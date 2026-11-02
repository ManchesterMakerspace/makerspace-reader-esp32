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
  model.reset();journal.clear();tracked.clear();associations.clear();topics.clear();warningTargets.clear();cueTargets.clear();alertSession.clear();
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

  // An ending before the first complete inventory outlives new scans and reboot.
  for(bool partial : {false,true}) {
    seed();advance(1000);boot();
    int64_t started=utc;
    inventory["reservations"][0]["startAt"]=started-grace;
    active={"cccccccccccccccccccccccc","dddddddddddddddddddddddd",started};
    if(partial) {inventory["complete"]=false;network();}
    service();assert(journal["operations"].size()==0);
    ending["time"]=started+1000;
    failStorage=true;assert(!sessionEnded(ending.as<JsonVariantConst>(),started));
    assert(journal["endings"].size()==0);
    failStorage=false;assert(sessionEnded(ending.as<JsonVariantConst>(),started));
    assert(sessionEnded(ending.as<JsonVariantConst>(),started));
    assert(journal["endings"].size()==1);
    active={"ffffffffffffffffffffffff","222222222222222222222222",utc};service();
    advance(2000);boot();assert(journal["endings"].size()==1);
    active={"ffffffffffffffffffffffff","222222222222222222222222",utc};
    inventory["complete"]=false;network();service();
    assert(journal["endings"].size()==1 && journal["operations"].size()==0);
    inventory["complete"]=true;refreshHint();advance(1000);uploads.clear();network();service();
    assert(journal["endings"].size()==0 && model.session.id==active.id);
    int claims=0,releases=0;
    for(const auto &raw:uploads) {
      JsonDocument op;assert(!deserializeJson(op,raw));
      if(op["kind"]=="claim") {++claims;assert(op["checkinId"]==ending["checkinId"] && op["observedAt"]==started);}
      if(op["kind"]=="release") {++releases;assert(op["checkendId"]==ending["_id"]);}
      assert(op["kind"]!="checkpoint");
    }
    assert(claims==1 && releases==1 && tracked[0].released);
  }

  // A running visit can qualify when the arrival window opens, even if its scan
  // was too early. Non-voluntary endings retain attendance without releasing.
  for(bool kicked : {false,true}) {
    seed();inventory["reservations"][0]["startAt"]=utc+hour+1000;
    boot();int64_t started=utc;ending["time"]=utc+2000;ending["kicked"]=kicked;
    assert(sessionEnded(ending.as<JsonVariantConst>(),started));
    advance(3000);uploads.clear();network();
    int claims=0,releases=0;
    for(const auto &raw:uploads) {
      JsonDocument op;assert(!deserializeJson(op,raw));
      if(op["kind"]=="claim") {++claims;assert(op["observedAt"]==started+1000);}
      if(op["kind"]=="release") ++releases;
    }
    assert(claims==1 && releases==(kicked ? 0 : 1) && journal["endings"].size()==0);
  }
  ending["kicked"]=false;

  // Multiple completed visits retain independent identities while another owner
  // uses the tool, including when inventory arrives after a booking has ended.
  seed();boot();int64_t firstStarted=utc;
  ending["time"]=utc+1000;assert(sessionEnded(ending.as<JsonVariantConst>(),utc));
  auto second=inventory["reservations"].as<JsonArray>().add<JsonObject>();
  second.set(inventory["reservations"][0]);
  second["id"]="111111111111111111111111";second["memberId"]="222222222222222222222222";
  second["startAt"]=utc+2*hour;second["endAt"]=utc+3*hour;
  JsonDocument secondEnding;secondEnding.set(ending);
  secondEnding["_id"]="333333333333333333333333";secondEnding["checkinId"]="444444444444444444444444";
  secondEnding["memberId"]=second["memberId"];secondEnding["time"]=utc+2*hour;
  assert(sessionEnded(secondEnding.as<JsonVariantConst>(),utc+hour+grace));
  advance(2*hour+1000);boot();assert(journal["endings"].size()==2);
  active={"ffffffffffffffffffffffff","555555555555555555555555",utc};
  uploads.clear();network();service();
  assert(journal["endings"].size()==0 && journal["operations"].size()==0 && journal["releases"].size()==0);
  int deferredClaims=0,deferredReleases=0;
  for(const auto &raw:uploads) {
    JsonDocument op;assert(!deserializeJson(op,raw));
    if(op["kind"]=="claim") {
      ++deferredClaims;
      if(op["reservationId"]=="aaaaaaaaaaaaaaaaaaaaaaaa") assert(op["observedAt"]==firstStarted && op["checkinId"]==ending["checkinId"]);
      else assert(op["observedAt"]==firstStarted+hour+grace && op["checkinId"]==secondEnding["checkinId"]);
    }
    if(op["kind"]=="release") ++deferredReleases;
  }
  assert(deferredClaims==2 && deferredReleases==2);

  // A queued ending keeps its association while release storage is full. A new
  // visit by the same member cannot steal that association during retries.
  seed();int64_t deferredStart=utc;ending["time"]=utc+1000;
  assert(sessionEnded(ending.as<JsonVariantConst>(),deferredStart));
  for(int i=0;i<64;++i) {JsonDocument op;op["kind"]="release";op["checkendId"]=i;assert(queue(op));}
  active={"ffffffffffffffffffffffff","dddddddddddddddddddddddd",utc};advance(1000);service();
  assert(journal["endings"].size()==1 && tracked[0].associatedSession==String(ending["checkinId"] | "").c_str());
  boot();assert(journal["endings"].size()==1);
  journal["releases"].as<JsonArray>().clear();assert(save()); // Simulate draining the release backlog.
  model.complete=true;advance(1000);service();
  assert(journal["endings"].size()==0 && journal["releases"].size()==1 && tracked[0].released);

  // A full claim queue must not persist an arrival without its operation.
  seed();
  for(int i=0;i<64;++i) {JsonDocument op;op["kind"]="claim";op["observedAt"]=i;assert(queue(op));}
  ending["time"]=utc+1000;assert(sessionEnded(ending.as<JsonVariantConst>(),utc));
  active={"ffffffffffffffffffffffff","222222222222222222222222",utc};service();
  assert(journal["endings"].size()==1 && tracked[0].arrived==0);
  boot();assert(tracked[0].arrived==0 && journal["endings"].size()==1);
  journal["operations"].as<JsonArray>().clear();assert(save());
  model.complete=true;advance(1000);service();
  assert(journal["endings"].size()==0 && journal["operations"].size()==1 && journal["releases"].size()==1);

  // Full monitoring storage cannot orphan a newly claimed reservation. Its
  // association is committed with the claim and survives ACK, refresh and reboot.
  seed();
  Record old=tracked[0];old.start=utc-hour;old.end=utc-1;
  tracked.clear();
  for(int i=0;i<64;++i) {old.id=std::string(22,'1')+(i<10 ? "0" : "")+std::to_string(i);tracked.push_back(old);}
  assert(save());
  inventory["reservations"][0]["id"]="999999999999999999999999";
  refreshHint();advance(1000);network();
  assert(tracked.size()==capacity && !tracking("999999999999999999999999"));
  active={"cccccccccccccccccccccccc","dddddddddddddddddddddddd",utc};service();
  assert(associations.size()==1 && associations[0].associatedSession==active.id);
  JsonDocument durable;assert(!deserializeJson(durable,flash["/reservations"]));
  assert(durable["operations"].size()==1 && durable["associations"][0]["associatedSession"]==active.id);
  advance(1000);network();assert(journal["operations"].size()==0);
  advance(1000);boot();assert(associations.size()==1);
  ending["time"]=utc+1000;
  assert(sessionEnded(ending.as<JsonVariantConst>()));
  assert(journal["releases"].size()==1 && journal["releases"][0]["reservationId"]=="999999999999999999999999");
  advance(1000);network();assert(journal["releases"].size()==0);

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
