#include "Reader.h"
#include <algorithm>
std::atomic<bool> cacheCapacityExceeded(false);
uint32_t cacheCapacityRevision = 0;
void failCacheCapacity() {
 cacheCapacityExceeded = true;
 ++cacheCapacityRevision;
 refreshNeeded = true;
 lastError = "Card cache capacity exceeded";
}
// Privileged cards are always included by the gateway, even when not requested.
std::vector<std::string> snapshotRequestedUids() {
 std::vector<std::string> requested;
 for (const auto &p : cards) if (!p.second.managesTool || p.second.uid == activeUid) requested.push_back(p.first);
 std::sort(requested.begin(), requested.end(), [](const std::string &a, const std::string &b) {
   if (cards.at(a).uid == activeUid) return cards.at(b).uid != activeUid;
   if (cards.at(b).uid == activeUid) return false;
   return cards.at(a).lastUsed == cards.at(b).lastUsed ? a < b : cards.at(a).lastUsed > cards.at(b).lastUsed;
 });
 if (requested.size() > 65) requested.resize(65); // 64 recent ordinary cards + pinned owner
 return requested;
}
void cacheScannedCard(const Card &card) {
 if (!readerProfile::compact) {
   if (cards.count(card.uid.c_str()) || cards.size() < readerProfile::cards) cards[card.uid.c_str()] = card;
   return;
 }
 // Allow one transient pinned overflow, but never grow RAM without bound while
 // waiting for the gateway to correct an oversized privileged set.
 if (cards.count(card.uid.c_str()) || cards.size() < readerProfile::cards + (readerProfile::compact ? 1u : 0u))
   cards[card.uid.c_str()] = card;
 else failCacheCapacity();
 trimCardCache();
}
void trimCardCache() {
 if (!readerProfile::compact) return;
 std::vector<std::string> ordinary;
 for (const auto &p: cards) if (!p.second.managesTool && p.second.uid != activeUid) ordinary.push_back(p.first);
 std::sort(ordinary.begin(),ordinary.end(),[](const std::string &a,const std::string &b) {
   return cards.at(a).lastUsed == cards.at(b).lastUsed ? a < b : cards.at(a).lastUsed < cards.at(b).lastUsed;
 });
 size_t remove = ordinary.size()>64 ? ordinary.size()-64 : 0;
 if (cards.size()>readerProfile::cards) remove=std::max(remove,cards.size()-readerProfile::cards);
 for(size_t i=0;i<remove && i<ordinary.size();i++) cards.erase(ordinary[i]);
 if(cards.size()>readerProfile::cards) failCacheCapacity();
 for(auto it=grants.begin();it!=grants.end();) {
   bool used=false; for(const auto &p:cards) if(p.second.member==it->first.c_str()) {used=true;break;}
   if(!used) it=grants.erase(it); else ++it;
 }
 if(grants.size()>readerProfile::grants) failCacheCapacity();
}

// These two explicit gateway conflicts describe capacity, not revoked identity.
// Unknown conflicts and authentication failures retain their fail-closed policy.
bool cacheCapacityResponse(const String &path, int code, JsonVariantConst error) {
 if (code != 409 || (path != "/reader/v1/snapshot" && path != "/reader/v1/snapshot/recent")) return false;
 String message = error["message"] | "";
 return message == "Eligible card cache capacity exceeded" || message == "Checkout cache capacity exceeded";
}
