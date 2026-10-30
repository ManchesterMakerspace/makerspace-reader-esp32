#include "Reader.h"
#include <algorithm>
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
 if(cards.size()>readerProfile::cards) { maintenance=true; lastError="Privileged card cache capacity exceeded"; }
 for(auto it=grants.begin();it!=grants.end();) {
   bool used=false; for(const auto &p:cards) if(p.second.member==it->first.c_str()) {used=true;break;}
   if(!used) it=grants.erase(it); else ++it;
 }
}
