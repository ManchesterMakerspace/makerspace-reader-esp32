#include "cache-fakes/Reader.h"
#include <cassert>
#include <iostream>
std::unordered_map<std::string,Card> cards;
std::unordered_map<std::string,Grant> grants;
String activeUid,lastError;bool maintenance=false;
int main() {
 for(unsigned i=0;i<100;i++){Card c;c.uid=std::to_string(i);c.member=c.uid;c.lastUsed=i;cards[c.uid]=c;grants[c.member]={};}
 cards.at("0").managesTool=true;activeUid="1";
 trimCardCache();
 if(readerProfile::compact){assert(cards.size()==66);assert(cards.count("0")&&cards.count("1")&&cards.count("99"));assert(!cards.count("2"));assert(grants.size()==cards.size());}
 else assert(cards.size()==100);
 cards.clear();for(unsigned i=0;i<129;i++){Card c;c.uid=std::to_string(i);c.managesTool=true;cards[c.uid]=c;}
 trimCardCache();assert(cards.size()==129);assert(maintenance==readerProfile::compact);
 std::cout<<"Cache pinning, recent eviction, grant pruning and capacity checks passed\n";
}
