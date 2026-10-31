#include "cache-fakes/Reader.h"
#include <cassert>
#include <iostream>
#include <functional>
std::unordered_map<std::string,Card> cards;
std::unordered_map<std::string,Grant> grants;
String activeUid,lastError;
bool maintenance=false,refreshNeeded=false,storageOK=true;
JsonDocument configuration;
int stateMutex=0,snapshotMutex=1;
Preferences prefs;
std::map<std::string,String> files;
unsigned writes=0;
bool failWrite=false,failRead=false;
std::function<void()> duringWrite;
bool sealWrite(const String &path,const String &data) {
 ++writes;
 if(duringWrite) {auto fn=duringWrite;duringWrite=nullptr;fn();}
 if(failWrite)return false;
 files[path]=data;return true;
}
bool sealRead(const String &path,String &data) {
 if(failRead || !files.count(path))return false;
 data=files.at(path);return true;
}
Card card(const std::string &uid,bool privileged=false,uint32_t sequence=1) {
 Card c;c.uid=uid;c.id="id-"+uid;c.member="member-"+uid;c.managesTool=privileged;
 c.sourceSequence=sequence;c.first=100;c.lastUsed=sequence;c.refreshedAt=1;
 return c;
}
void add(const Card &c,uint32_t seq=1) {
 cards[c.uid]=c;
 Grant g;g.member=c.member;g.id="grant-"+c.member;g.until=100000;g.sourceSequence=seq;
 grants[c.member]=g;
}
void snapshotCard(JsonDocument &d,const Card &c,bool grant=true) {
 auto o=d["cards"].as<JsonArray>().add<JsonObject>();
 o["uid"]=c.uid;o["_id"]=c.id;o["memberId"]=c.member;o["managesTool"]=c.managesTool;
 if(grant){auto g=d["grants"].as<JsonArray>().add<JsonObject>();g["memberId"]=c.member;g["checkoutId"]="snapshot-grant";g["until"]=200000;}
}
JsonDocument snapshot(){JsonDocument d;d["cards"].to<JsonArray>();d["grants"].to<JsonArray>();return d;}
void reset(){cards.clear();grants.clear();activeUid="";lastError="";configuration.clear();cacheCapacityExceeded=false;refreshNeeded=false;storageOK=true;cacheCapacityRevision=0;prefs={};files.clear();writes=0;failWrite=failRead=false;duringWrite=nullptr;}
void retention() {
 reset();
 for(unsigned i=0;i<100;i++){auto c=card(std::to_string(i));c.lastUsed=i;add(c);}
 cards.at("0").managesTool=true;activeUid="1";
 trimCardCache();
 assert(snapshotRequestedUids().size()<=65);
 if(readerProfile::compact){assert(cards.size()==66);assert(cards.count("0")&&cards.count("1")&&cards.count("99"));assert(!cards.count("2"));assert(grants.size()==cards.size());}
 else assert(cards.size()==100);
}
void scopedRace() {
 reset();add(card("requested"));
 auto requested=snapshotRequestedUids(); // lookup already owns apiMutex here
 auto late=card("late",false,2);late.first=0;add(late,2);activeUid=late.uid;
 auto d=snapshot();snapshotCard(d,cards.at("requested"));
 mergeCacheSnapshot(d.as<JsonVariantConst>(),requested,3,true);
 if(readerProfile::compact) {
  assert(cards.at("late").id==late.id);assert(cards.at("late").refreshedAt==1);
  assert(grants.at(late.member).id=="grant-member-late");
  assert(saveState(true));cards.clear();grants.clear();loadState();
  assert(cards.at("late").id==late.id && grants.count(late.member));
 } else {assert(!cards.count("late"));assert(!grants.count(late.member));}
 // A later requested response removing the grant must revoke it.
 requested=snapshotRequestedUids();d=snapshot();snapshotCard(d,late,false);
 mergeCacheSnapshot(d.as<JsonVariantConst>(),requested,4,true);
 assert(!grants.count(late.member));
 // A newer lookup still wins against an in-flight older snapshot.
 reset();add(card("late",false,7),7);d=snapshot();snapshotCard(d,cards.at("late"),false);
 mergeCacheSnapshot(d.as<JsonVariantConst>(),{"late"},6,true);
 assert(grants.at("member-late").sourceSequence==7);
 // Changing tool or role must not carry grants from the old scope.
 d=snapshot();mergeCacheSnapshot(d.as<JsonVariantConst>(),{},8,false);
 assert(grants.empty());
}
void authoritativeRemoval() {
 reset();auto c=card("old-privileged",true);add(c);
 auto requested=snapshotRequestedUids();assert(requested.empty());
 activeUid=c.uid;assert(snapshotRequestedUids().size()==1);activeUid="";
 auto d=snapshot();mergeCacheSnapshot(d.as<JsonVariantConst>(),requested,2,true);
 assert(cards.at(c.uid).id.empty() && grants.empty());
 // Two cards of one member: explicit absence via the requested card wins
 // over an older grant belonging to a retained unrequested card.
 reset();c=card("requested");add(c);requested=snapshotRequestedUids();
 auto late=card("late",false,2);late.member=c.member;add(late,2);
 d=snapshot();snapshotCard(d,c,false);
 mergeCacheSnapshot(d.as<JsonVariantConst>(),requested,3,true);
 assert(grants.empty());
 // Current response wins when it contains the same member's grant.
 reset();add(late,2);d=snapshot();snapshotCard(d,c,true);
 mergeCacheSnapshot(d.as<JsonVariantConst>(),{},3,true);
 assert(grants.at(c.member).id=="snapshot-grant");
}
void overflowRecovery() {
 reset();
 for(unsigned i=0;i<readerProfile::cards;i++)add(card(std::to_string(i),true));
 // Grants have a lower full-profile limit; this test isolates card overflow.
 grants.clear();assert(saveState());
 auto lastFiles=files;auto generation=prefs.getUInt("generation",0);unsigned lastWrites=writes;
 auto owner=card("owner");activeUid=owner.uid;
 if(readerProfile::compact) cacheScannedCard(owner); else cards[owner.uid]=owner;
 trimCardCache();assert(cards.size()==readerProfile::cards+1);
 assert(!saveState());assert(cacheCapacityExceeded && refreshNeeded && !maintenance && storageOK);
 assert(files==lastFiles && prefs.getUInt("generation",0)==generation && writes==lastWrites);
 // Never generate an oversized request from the overflowing cache.
 auto requested=snapshotRequestedUids();assert(requested.size()==1 && requested[0]=="owner");
 // Additional scans cannot grow an already overflowing cache.
 for(unsigned i=0;i<100;i++) cacheScannedCard(card("extra-"+std::to_string(i),true));
 assert(cards.size()==readerProfile::cards+1);
 // Gateway correction works without a restart, preserves owner and pins,
 // and only a verified authoritative save clears the error.
 auto d=snapshot();snapshotCard(d,owner);snapshotCard(d,cards.at("0"),false);
 mergeCacheSnapshot(d.as<JsonVariantConst>(),requested,2,true);
 assert(cacheCapacityExceeded && !maintenance);
 assert(saveState());assert(cacheCapacityExceeded);
 assert(saveState(true));assert(!cacheCapacityExceeded && lastError.empty());
 cards.clear();grants.clear();loadState();assert(storageOK && cards.size()<=readerProfile::cards && cards.count("owner"));
}
void persistenceFailures() {
 reset();add(card("one"));assert(saveState());auto previous=files;
 for(unsigned i=0;i<=readerProfile::snapshotBytes/60000;i++) configuration["oversized"][i]=std::string(60000,char('a'+i));
 assert(!saveState(true));assert(cacheCapacityExceeded && storageOK && files==previous);
 configuration.clear();
 failWrite=true;assert(!saveState(true));assert(cacheCapacityExceeded && !storageOK);failWrite=false;
 failRead=true;assert(!saveState(true));assert(cacheCapacityExceeded);failRead=false;
 prefs.fail=true;assert(!saveState(true));assert(cacheCapacityExceeded);prefs.fail=false;
 // A new failure during write must survive an older successful refresh.
 duringWrite=[](){failCacheCapacity();};assert(saveState(true));assert(cacheCapacityExceeded);
 assert(saveState(true));assert(!cacheCapacityExceeded && storageOK);
 // Count checks also protect the grant limit (150 vs 300 on full profiles).
 for(unsigned i=0;i<=readerProfile::grants;i++){Grant g;g.member=std::to_string(i);grants[g.member]=g;}
 unsigned before=writes;assert(!saveState());assert(writes==before && cacheCapacityExceeded);
}
void gatewayCapacity() {
 JsonDocument error;error["message"]="Eligible card cache capacity exceeded";
 assert(cacheCapacityResponse("/reader/v1/snapshot/recent",409,error.as<JsonVariantConst>()));
 error["message"]="Checkout cache capacity exceeded";
 assert(cacheCapacityResponse("/reader/v1/snapshot",409,error.as<JsonVariantConst>()));
 assert(!cacheCapacityResponse("/reader/v1/cards/lookup",409,error.as<JsonVariantConst>()));
 assert(!cacheCapacityResponse("/reader/v1/snapshot",403,error.as<JsonVariantConst>()));
 error["message"]="Duplicate eligible card UID";
 assert(!cacheCapacityResponse("/reader/v1/snapshot/recent",409,error.as<JsonVariantConst>()));
 error.clear();assert(!cacheCapacityResponse("/reader/v1/snapshot",409,error.as<JsonVariantConst>()));
}
int main(){gatewayCapacity();retention();scopedRace();authoritativeRemoval();overflowRecovery();persistenceFailures();std::cout<<"Cache merge races, authoritative removals, pinning, bounded recovery and snapshot persistence passed (profile "<<READER_PROFILE_MB<<")\n";}
