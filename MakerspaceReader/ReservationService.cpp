#include "ReservationService.h"
#if READER_ENABLE_RESERVATIONS
#include "Audio.h"
#include "Display.h"
#include <LittleFS.h>
#include <esp_timer.h>
#include <algorithm>

namespace reservation {
static Model model;
static JsonDocument journal;
static std::vector<Record> tracked;
static std::vector<Alarm> warningTargets, cueTargets;
static std::string alertSession;
static std::vector<String> topics;
static std::atomic<bool> hinted(true), enabled(false);
static String scope;
static bool uncertain = false, eligible = true, loaded = false, freshJournal = false;
static int64_t coveredFrom = 0, evaluatedThrough = 0, checkedAt = 0, flashAt = -2000;
static uint32_t lossBaseline = 0;
static uint32_t nextNetwork = 0, retryMs = 1000;
static const char *file = "/reservations";
static void encode(JsonObject o,const Record &r) {
  o["id"]=r.id; o["memberId"]=r.member; o["name"]=r.name; o["startAt"]=r.start; o["endAt"]=r.end;
  o["revision"]=r.revision; o["arrivedAt"]=r.arrived; o["monitored"]=r.monitored; o["released"]=r.released;
  o["associatedSession"]=r.associatedSession;
}
static bool decode(JsonVariantConst o,Record &r) {
  r.id=o["id"] | ""; r.member=o["memberId"] | ""; r.name=o["name"] | "";
  r.start=o["startAt"] | int64_t(0); r.end=o["endAt"] | int64_t(0); r.arrived=o["arrivedAt"] | int64_t(0);
  r.revision=o["revision"] | uint32_t(0); r.monitored=o["monitored"] | false; r.released=o["released"] | false;
  r.associatedSession=o["associatedSession"] | "";
  return r.id.size()==24 && r.member.size()==24 && r.name.size()<=64 && r.start>=1700000000000LL && r.end>r.start;
}
static bool save() { // stateMutex held; never performs network or I2C work.
  journal["version"]=1;
  journal["scope"]=scope; journal["uncertain"]=uncertain; journal["coveredFrom"]=coveredFrom;
  journal["lossBaseline"]=lossBaseline; journal["checkedAt"]=checkedAt; journal["eligible"]=eligible;
  auto rows=journal["tracked"].to<JsonArray>();
  for (const auto &r:tracked) encode(rows.add<JsonObject>(),r);
  String raw; serializeJson(journal,raw);
  if (raw.length()>65536 || !sealWrite(file,raw,false)) { uncertain=true; log_e("Reservation journal unavailable; attendance coverage uncertain"); return false; }
  return true;
}
void setup() {
  bool exists, interrupted; { Lock f(fsMutex); exists=LittleFS.exists(file); interrupted=LittleFS.exists(String(file)+".tmp"); }
  lossBaseline=dropped.load();
  if (!exists) { freshJournal=!interrupted; uncertain=interrupted; loaded=true; return; }
  String raw;
  if (!sealRead(file,raw,false) || raw.length()>65536 || deserializeJson(journal,raw) ||
      journal["version"].as<int>()!=1 || journal["tracked"].size()>capacity || journal["operations"].size()>64) {
    uncertain=true; journal.clear(); loaded=true; return;
  }
  scope=journal["scope"] | ""; coveredFrom=journal["coveredFrom"] | int64_t(0);
  uncertain=(journal["uncertain"] | false) || interrupted; eligible=journal["eligible"] | true;
  checkedAt=journal["checkedAt"] | int64_t(0); lossBaseline=journal["lossBaseline"] | uint32_t(0);
  for (auto row:journal["tracked"].as<JsonArray>()) { Record r; if(!decode(row,r)) uncertain=true; else tracked.push_back(r); }
  model.records=tracked; // The first authoritative inventory establishes a silent baseline.
  std::sort(model.records.begin(),model.records.end(),[](const Record &a,const Record &b){return a.start==b.start ? a.id<b.id : a.start<b.start;});
  loaded=true;
}
void refreshHint() { hinted=true; }
bool subscribing() { Lock s(stateMutex); return enabled && eligible; }
std::vector<String> hintTopics() { Lock s(stateMutex); return enabled && eligible ? topics : std::vector<String>{}; }
static Record *tracking(const std::string &id) {
  for(auto &r:tracked) if(r.id==id) return &r;
  return nullptr;
}
static bool queue(JsonDocument &op) {
  String key; serializeJson(op,key);
  auto operations=journal["operations"].is<JsonArray>() ? journal["operations"].as<JsonArray>() : journal["operations"].to<JsonArray>();
  for(auto old:operations) {
    const char *eventField=op["kind"]=="claim" ? "checkinId" : "checkendId";
    if(op["reservationId"].is<const char *>() && op[eventField].is<const char *>() &&
        old["kind"]==op["kind"] && old["reservationId"]==op["reservationId"] &&
        old["revision"]==op["revision"] && old[eventField]==op[eventField]) {
      op.set(old); return save(); // Preserve the first observedAt across a failed durable write.
    }
    String previous; serializeJson(old,previous); if(previous==key) return save();
  }
  if(operations.size()>=64) { uncertain=true; save(); return false; }
  operations.add(op.as<JsonVariantConst>());
  return save();
}
void sessionEnded(JsonVariantConst event) {
  if(!loaded || String(event["reason"] | "")!="logoff" || event["kicked"]==true) return;
  for(auto &r:tracked) {
    if(!r.arrived || r.released || r.associatedSession!=String(event["checkinId"] | "").c_str() ||
        r.member!=String(event["memberId"] | "").c_str() || r.end<=(event["time"] | int64_t(0))) continue;
    JsonDocument op; op["kind"]="release"; op["reservationId"]=r.id; op["revision"]=r.revision;
    op["checkendId"]=event["_id"];
    if(queue(op)) { r.released=true; if(auto *record=model.find(r.id)) record->released=true; }
  }
  save(); hinted=true;
}
void service() {
  if(!loaded) return;
  Lock lock(stateMutex);
  bool active=configuration["role"]=="tool";
  String tool=active ? String(configuration["toolId"] | "") : String("");
  enabled=active;
  if(tool!=scope) {
    scope=tool; model.reset(); tracked.clear(); topics.clear(); eligible=true; checkedAt=0; coveredFrom=0; hinted=true;
    // Pending operations keep their original reservation/session identity.
    // Reset the coverage interval for the new scope; Rails fences the old scope
    // using its frozen tool/configuration revision. Preserve any actual loss.
    save();
  }
  if(!active || !clockTrusted) { evaluatedThrough=0; audio::cancelReservation(); return; }
  const int64_t now=epochMs();
  if(!coveredFrom) { coveredFrom=now; if(freshJournal && tracked.empty() && journal["operations"].size()==0) uncertain=false; freshJournal=false; save(); }
  if(lossBaseline!=dropped.load()) { uncertain=true; save(); lossBaseline=dropped.load(); }
  Session current; captureSession(current); model.setSession(current);
  bool sessionChanged=current.id!=alertSession;
  if(sessionChanged) {
    audio::cancelReservation(); warningTargets.clear(); cueTargets.clear(); alertSession=current.id;
  }
  if(!sessionChanged && evaluatedThrough && now-evaluatedThrough<500) return;
  auto prune=[&](std::vector<Alarm> &targets,audio::MelodyId melody) {
    bool had=!targets.empty();
    targets.erase(std::remove_if(targets.begin(),targets.end(),[&](const Alarm &a){
      auto *r=model.find(a.id);
      return !r || r->released || r->end<=now || a.deadline<=now ||
        (a.own ? !r->arrived || r->member!=current.member || r->end!=a.deadline : r->start!=a.deadline);
    }),targets.end());
    if(had && targets.empty()) audio::cancelReservation(melody);
  };
  prune(warningTargets,audio::MelodyId::Jeopardy); prune(cueTargets,audio::MelodyId::DonkeyKongStart);
  auto decision=model.tick(now);
  if(!decision.claim.empty()) {
    auto *r=model.find(decision.claim);
    JsonDocument op; op["kind"]="claim"; op["reservationId"]=r->id; op["revision"]=r->revision;
    op["checkinId"]=current.id; op["observedAt"]=decision.observedAt;
    if(queue(op)) {
      r->arrived=op["observedAt"].as<int64_t>(); r->associatedSession=current.id;
      if(auto *t=tracking(r->id)) { t->arrived=r->arrived; t->associatedSession=current.id; }
      save(); hinted=true;
    }
  }
  for(auto &r:model.records) {
    if(!current.id.empty() && r.arrived && !r.released && r.member==current.member && now>=r.start-hour && now<r.end && r.associatedSession!=current.id) {
      r.associatedSession=current.id;
      if(auto *t=tracking(r.id)) t->associatedSession=current.id;
      save();
    }
  }
  if(current.id.empty()) audio::cancelReservation();
  if(decision.flash) flashAt=esp_timer_get_time()/1000;
  if(decision.newReservationCue) {cueTargets=decision.cues; audio::playReservation(audio::MelodyId::DonkeyKongStart);}
  if(decision.jeopardy) {warningTargets=decision.warnings; audio::playReservation(audio::MelodyId::Jeopardy);}
  evaluatedThrough=now;
}
void displayStatus(display::Status &s) {
  if(!enabled || !s.trusted) return;
  auto v=model.view(s.utcMs);
  s.reservationPresent=v.present; s.reservationCountdown=v.countdown; s.reservationOwn=v.own; s.reservationDue=v.due;
  s.reservationDeadline=v.deadline; s.reservationStart=v.start; s.reservationEnd=v.end;
  snprintf(s.reservationName,sizeof(s.reservationName),"%s",v.name.c_str());
  int64_t phase=esp_timer_get_time()/1000-flashAt;
  s.reservationFlash=phase>=0 && phase<1500 && (phase/250)%2==0;
  s.reservationPlaying=audio::reservationPlaying()==audio::MelodyId::Jeopardy;
}
void stats(JsonObject out) {
  out["enabled"]=enabled.load(); out["reservable"]=eligible; out["complete"]=model.complete;
  out["coverageUncertain"]=uncertain; out["pending"]=journal["operations"].size();
  out["cached"]=model.records.size(); out["tracked"]=tracked.size(); out["lastCheckedAt"]=checkedAt;
}
static bool sync(JsonDocument &request,JsonDocument &response,bool capable) {
  request["capable"]=capable;
  String body; serializeJson(request,body);
  return api("/reader/v1/reservations/sync",response,body);
}
// No checkpoint may run ahead of an immutable session event's database ACK.
static bool auditDrained() {
  std::vector<String> paths;
  { Lock f(fsMutex); File dir=LittleFS.open("/events"); if(!dir) return false;
    File item=dir.openNextFile(); while(item) { String name=item.name(); if(!name.startsWith("/")) name="/events/"+name; paths.push_back(name); item=dir.openNextFile(); } }
  for(const auto &path:paths) { String raw; JsonDocument e;
    if(path.endsWith(".tmp") || !sealRead(path,raw,false) || deserializeJson(e,raw) || !(e["dbAck"] | false)) return false;
  }
  return true;
}
void network() {
  const uint32_t mono=millis();
  static uint32_t lastAttempt=0;
  if(!loaded || !clockTrusted || (lastAttempt && mono-lastAttempt<1000) || (int32_t(mono-nextNetwork)<0 && !hinted)) return;
  bool capable; String currentScope; int64_t since, checked; bool reservable, pending, haveTracked;
  { Lock s(stateMutex); capable=enabled; currentScope=scope; since=coveredFrom; checked=checkedAt; reservable=eligible; pending=journal["operations"].size()>0; haveTracked=!tracked.empty(); }
  if(!capable && !pending) return;
  if(capable && !reservable && !pending && !haveTracked && checked && epochMs()-checked<86400000) { hinted=false; return; }
  lastAttempt=mono;
  // Refresh hints do not defeat the once-daily negative-cache policy.
  bool refresh=hinted.exchange(false);
  static uint32_t fetched=0;
  bool ok=true;
  // Advertise capability before the first monitor operation freezes participants.
  static String advertisedScope;
  static bool advertised=false;
  if(!advertised || advertisedScope!=currentScope) {
    JsonDocument hello, answer; hello["operations"].to<JsonArray>();
    ok=sync(hello,answer,capable);
    if(ok) {advertised=true;advertisedScope=currentScope;}
  }
  if(ok && capable && (refresh || !fetched || mono-fetched>=60000)) {
    std::vector<Record> rows; std::vector<String> hints; String cursor; int64_t at=0; bool all=false;
    for(unsigned page=0;page<2 && ok;++page) {
      JsonDocument response;
      String url="/reader/v1/reservations";
      if(cursor.length()) url+="?at="+String((long long)at)+"&after="+cursor;
      ok=api(url,response);
      if(!ok || String(response["toolId"] | "")!=currentScope || !response["reservable"].is<bool>() ||
          !response["reservations"].is<JsonArray>() || response["reservations"].size()>32) { ok=false; break; }
      reservable=response["reservable"]; at=response["snapshotAt"] | int64_t(0);
      if(!page) for(auto topic:response["hintTopics"].as<JsonArray>()) {
        String value=topic.as<String>();
        if(hints.size()<4 && value.length()<256 && value.startsWith("reservations/") && value.indexOf('+')<0 && value.indexOf('#')<0) hints.push_back(value);
      }
      for(auto row:response["reservations"].as<JsonArray>()) { Record r; if(!decode(row,r) || rows.size()>=capacity) {ok=false;break;} rows.push_back(r); }
      all=response["complete"] | false; cursor=response["next"] | "";
      if(all) break;
    }
    if(ok) {
      Lock s(stateMutex);
      if(scope!=currentScope) return;
      eligible=reservable; topics=std::move(hints); checkedAt=epochMs(); model.replace(std::move(rows),all,checkedAt);
      for(const auto &r:model.records) {
        auto *t=tracking(r.id);
        if(t) *t=r;
        else if(tracked.size()<capacity) tracked.push_back(r);
        else uncertain=true;
      }
      if(!all) log_e("Reservation inventory exceeds 64 records; coverage suspended");
      save(); fetched=mono;
    }
  }
  // At most four durable operations per pass; a rejected item cannot starve others.
  static size_t cursorOp=0;
  for(unsigned n=0;ok && n<4;++n) {
    JsonDocument request,response; String sent;
    { Lock s(stateMutex); auto ops=journal["operations"].as<JsonArray>(); if(!ops.size()) break;
      cursorOp%=ops.size(); request["operations"].to<JsonArray>().add(ops[cursorOp]); serializeJson(ops[cursorOp],sent); }
    ok=sync(request,response,capable);
    if(ok) {
      const String outcome=response["results"][0]["outcome"] | "";
      Lock s(stateMutex);
      auto ops=journal["operations"].as<JsonArray>();
      if(outcome=="accepted" || outcome=="stale" || outcome=="gone" || outcome=="blocked") {
        for(size_t i=0;i<ops.size();++i) { String value; serializeJson(ops[i],value); if(value==sent) {ops.remove(i);break;} }
        if(outcome!="accepted") {
          if(String(request["operations"][0]["kind"] | "")=="claim") {
            std::string id=request["operations"][0]["reservationId"] | "";
            if(auto *r=model.find(id)) {r->arrived=0;r->associatedSession.clear();}
            if(auto *r=tracking(id)) {r->arrived=0;r->associatedSession.clear();}
          }
          if(outcome!="blocked") uncertain=true;
          hinted=true;
        }
        save();
      } else ++cursorOp;
    } else ++cursorOp;
  }
  // Monitor and reconcile even records that have left the four-hour read window.
  static size_t trackedCursor=0;
  if(ok && capable) {
    JsonDocument request,response;
    bool send=false;
    int64_t checkpointThrough;
    { Lock s(stateMutex); checkpointThrough=evaluatedThrough; }
    // File I/O must not hold the session lock. Freeze the checkpoint's time before
    // scanning so events created during the scan cannot be covered by this proof.
    bool eventsDrained=checkpointThrough && auditDrained();
    {
      Lock s(stateMutex);
      auto ops=request["operations"].to<JsonArray>();
      bool drained=journal["operations"].size()==0 && checkpointThrough && model.complete && eventsDrained && attendanceReady();
      for(size_t examined=0;examined<tracked.size() && ops.size()<16;++examined) {
        trackedCursor%=tracked.size(); auto &r=tracked[trackedCursor++];
        if(r.monitored && (r.arrived || epochMs()<=r.start+grace || !drained)) continue;
        auto op=ops.add<JsonObject>();
        op["reservationId"]=r.id; op["revision"]=r.revision;
        if(!r.monitored) { op["kind"]="monitor"; send=true; }
        else {
          op["kind"]="checkpoint"; op["coveredFrom"]=since; op["through"]=checkpointThrough;
          op["uncertain"]=uncertain || lossBaseline!=dropped.load(); send=true;
        }
      }
    }
    if(send) {
      ok=sync(request,response,capable);
      if(ok) {
        Lock s(stateMutex);
        for(auto result:response["results"].as<JsonArray>()) {
          String id=result["reservationId"] | "";
          auto it=std::find_if(tracked.begin(),tracked.end(),[&](const Record &r){return r.id==id.c_str();});
          if(it!=tracked.end()) {
            String status=result["status"] | "", outcome=result["outcome"] | "";
            if(outcome=="gone" || outcome=="stale" || status!="approved") {
              if(auto *r=model.find(it->id)) r->released=true;
              tracked.erase(it); hinted=true;
            } else {
              it->monitored=result["monitored"] | it->monitored;
              it->arrived=std::max(it->arrived,result["arrivedAt"] | int64_t(0));
              if(auto *r=model.find(it->id)) {r->monitored=it->monitored;r->arrived=it->arrived;}
            }
          }
        }
        save();
      }
    }
  }
  retryMs=ok ? 1000 : std::min(uint32_t(60000),retryMs*2);
  uint32_t wait=ok ? 60000 : retryMs;
  if(ok) {
    Lock s(stateMutex);
    if(!journal["operations"].size()) {
      auto before=tracked.size();
      tracked.erase(std::remove_if(tracked.begin(),tracked.end(),[](const Record &r){return (r.arrived || !r.monitored) && r.end<=epochMs();}),tracked.end());
      if(before!=tracked.size()) save();
    } else wait=1000;
    for(const auto &r:tracked) {
      if(!r.monitored && r.start+grace>=epochMs()) wait=1000;
      if(!r.arrived && r.start+grace>=epochMs()) wait=std::min(wait,uint32_t(std::max<int64_t>(1000,r.start+grace+1-epochMs())));
    }
  }
  nextNetwork=mono+wait;
}
} // namespace reservation
#endif
