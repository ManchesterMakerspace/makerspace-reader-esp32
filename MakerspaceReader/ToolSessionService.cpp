#include "Reader.h"
#include "Display.h"
#include "ToolSession.h"
#include "secrets.h"
#include <esp_timer.h>
#include <LittleFS.h>
static toolsession::State session;
static JsonDocument journal;
static bool pendingEnd = false;
static String lastName, lastInitial, activeName;
static int64_t lastEnded = 0;
static bool haveLast = false;
void cancelToolConfirmation(const String &uid) {
  if (uid.isEmpty() || session.candidateUid != uid.c_str()) session.cancelConfirmation();
}
bool toolOutOfService() {
  return (configuration["toolOutOfService"] | false) || (configuration["shopOutOfService"] | false)
      || (prefs.getString("toolOosId", "") == String(configuration["toolId"] | "") && prefs.getBool("toolOos", false))
      || (prefs.getString("shopOosId", "") == String(configuration["shopId"] | "") && prefs.getBool("shopOos", false));
}
void updateToolStatus(JsonVariantConst tool) {
  configuration["toolAvailable"] = tool["enabled"] | false;
  for (const char *key : {"shopId", "shopName"}) configuration[key] = tool[key];
  configuration["toolName"] = tool["name"] | "";
  for (bool shop : {false,true}) {
    const char *key = shop ? "shopOutOfService" : "toolOutOfService";
    if (!tool[key].is<bool>()) continue; // Missing status never clears a latch.
    bool value = tool[key].as<bool>();
    String id=configuration[shop ? "shopId" : "toolId"] | "";
    if (prefs.getString(shop ? "shopOosId" : "toolOosId", "") != id)
      checkNvsWrite(shop ? "shopOosId" : "toolOosId",prefs.putString(shop ? "shopOosId" : "toolOosId",id),id.length());
    configuration[key] = value;
    if (prefs.getBool(shop ? "shopOos" : "toolOos", false) != value)
      checkNvsWrite(shop ? "shopOos" : "toolOos", prefs.putBool(shop ? "shopOos" : "toolOos", value), 1);
  }
}
static String segment(String s) {
  s.replace("%","%25"); s.replace("/","%2F"); s.replace("+","%2B"); s.replace("#","%23"); return s;
}
static String randomId() {
  uint8_t bytes[12]; esp_fill_random(bytes,sizeof(bytes));
  String id=hexBytes(bytes,sizeof(bytes)); id.toLowerCase(); return id;
}
static bool writeJournal(const JsonDocument &d) {
  String raw; serializeJson(d,raw); return sealWrite("/tool-session",raw);
}
static bool spoolSessionEvent(JsonVariantConst doc, const char *collection, const String &topic) {
  JsonDocument e; e["document"].set(doc); e["collection"]=collection;
  e["topic"]=topic; e["dbAck"]=false; e["mqttAck"]=false;
  String raw; serializeJson(e,raw);
  String path="/events/session-" + String(doc["_id"] | "");
  { Lock f(fsMutex); if (LittleFS.exists(path)) return true; }
  reserveEventSpace(8192);
  return sealWrite(path,raw);
}
void endToolSession(const char *reason, const Card *actor) {
  if (!session.active() || pendingEnd) return;
  auto d=journal["end"].to<JsonObject>();
  d.set(journal["start"]);
  d["checkinId"]=journal["start"]["_id"];
  d["_id"]=journal["endId"];
  d["time"]=clockTrusted ? epochMs() : 0;
  d["reason"]=reason;
  d["kicked"]=actor != nullptr;
  if (actor) {
    d["kickedBy"]["memberId"]=actor->member;
    d["kickedBy"]["uid"]=actor->uid;
    d["kickedBy"]["holder"]=actor->holder;
  } else d["kickedBy"]=nullptr;
  lastEnded=d["time"] | int64_t(0);
  lastName=activeName; lastInitial=journal["lastInitial"] | ""; haveLast=true;
  pendingEnd=true; session.end();
}
bool toolScan(const Card &c, int64_t until, String &reason) {
  if (pendingEnd) { reason="sessionPersistencePending"; return false; }
  toolsession::Card actor;
  actor.uid=c.uid.c_str(); actor.member=c.member.c_str(); actor.valid=true;
  auto grant=grants.find(c.member.c_str());
  actor.checkout=grant!=grants.end() && grant->second.until>epochMs();
  actor.managesShop=c.managesTool;
  auto action=session.scan(actor,esp_timer_get_time()/1000,configuration["toolAvailable"] | false,toolOutOfService());
  if (action==toolsession::Action::denied) { reason="toolUnavailable"; return false; }
  if (action==toolsession::Action::confirmKick) { reason="confirmKick"; return true; }
  if (action==toolsession::Action::logoff || action==toolsession::Action::kick) {
    endToolSession(action==toolsession::Action::kick ? "kicked" : "logoff",action==toolsession::Action::kick ? &c : nullptr);
    stopRelay(); reason="sessionEnded"; return true;
  }
  journal.clear();
  String id=randomId(); journal["endId"]=randomId();
  String route=segment(configuration["shopName"] | "")+"/"+segment(configuration["toolName"] | "");
  journal["route"]=route;
  activeName=c.firstName;
  if (activeName.isEmpty()) { char name[17]; display::firstName(c.holder.c_str(),name); activeName=name; }
  journal["firstName"]=activeName; journal["lastInitial"]=c.lastInitial;
  auto d=journal["start"].to<JsonObject>();
  d["_id"]=id; d["uid"]=c.uid; d["holder"]=c.holder; d["memberId"]=c.member;
  d["validity"]="activeMember"; d["reason"]="activeMember";
  d["reader"]=configuration["name"] | DEVICE_ID; d["where"]=configuration["where"] | "doorboto32";
  d["time"]=epochMs(); d["delay"]=0;
  for (const char *key : {"toolId","shopId","toolName","shopName"}) d[key]=configuration[key];
  if (!writeJournal(journal)) {
    reason="sessionPersistenceFailed"; storageOK=false; maintenance=true; return false;
  }
  session.begin(id.c_str(),actor);
  if (!spoolSessionEvent(d,"checkins","activity/checkins/"+route)) {
    endToolSession("startFailed"); reason="sessionPersistenceFailed"; storageOK=false; return false;
  }
  startRelay(until-epochMs(),until,c.uid); reason="activeMember"; return true;
}
void serviceToolSession() {
  JsonDocument copy;
  { Lock s(stateMutex); if (!pendingEnd) return; copy.set(journal); }
  if (!writeJournal(copy)) return;
  String route=copy["route"] | "";
  if (!spoolSessionEvent(copy["start"],"checkins","activity/checkins/"+route) ||
      !spoolSessionEvent(copy["end"],"checkend","activity/checkends/"+route)) return;
  copy["closed"]=true;
  if (!writeJournal(copy)) return;
  { Lock s(stateMutex); journal.set(copy); pendingEnd=false; }
}
void recoverToolSession() {
  { Lock f(fsMutex); if (!LittleFS.exists("/tool-session")) return; }
  String raw;
  if (!sealRead("/tool-session",raw) || deserializeJson(journal,raw)) { maintenance=true; return; }
  activeName=journal["firstName"] | "";
  if (!journal["end"].isNull()) {
    haveLast=true; lastName=activeName; lastInitial=journal["lastInitial"] | "";
    lastEnded=journal["end"]["time"] | int64_t(0);
    pendingEnd=!(journal["closed"] | false); return;
  }
  toolsession::Card owner; owner.uid=journal["start"]["uid"] | ""; owner.member=journal["start"]["memberId"] | "";
  session.begin(journal["start"]["_id"] | "",owner);
  endToolSession("reboot"); journal["end"]["time"]=0; lastEnded=0;
}
void toolDisplayStatus(display::Status &s) {
  s.outOfService=toolOutOfService();
  s.tool=configuration["role"]=="tool";
  s.kickPending=session.confirmationUntil>esp_timer_get_time()/1000;
  snprintf(s.userName,sizeof(s.userName),"%s",activeName.c_str());
  snprintf(s.lastName,sizeof(s.lastName),"%s %s",lastName.c_str(),lastInitial.c_str());
  s.haveLast=haveLast; s.lastEnded=lastEnded;
}
