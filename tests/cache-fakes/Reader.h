#pragma once
#include "FlashProfile.h"
#include <string>
#include <unordered_map>
#include <vector>
using String=std::string;
struct Card {String uid, member; bool managesTool=false; uint64_t lastUsed=0;};
struct Grant {};
extern std::unordered_map<std::string,Card> cards;
extern std::unordered_map<std::string,Grant> grants;
extern String activeUid,lastError;
extern bool maintenance;
void trimCardCache();
