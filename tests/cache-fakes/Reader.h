#pragma once
#include "FlashProfile.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <map>
#include <cstring>
#include <ArduinoJson.h>
class String : public std::string {
public:
 using std::string::string;
 String(const std::string &s):std::string(s){}
 explicit String(uint32_t n):std::string(std::to_string(n)){}
 bool isEmpty() const {return empty();}
 size_t write(uint8_t c){push_back(char(c));return 1;}
 size_t write(const uint8_t *p,size_t n){append(reinterpret_cast<const char*>(p),n);return n;}
};
inline void convertToJson(const String &s,JsonVariant d){d.set(static_cast<const std::string&>(s));}
struct Card {
  String uid, id, member, holder, status, key, digest;
  int64_t expiry = 0, first = 0;
  bool pending = false, managesTool = false;
  String firstName, lastInitial;
  int64_t observedUs = 0;
  uint32_t refreshedAt = 0, sourceSequence = 0;
  uint64_t lastUsed = 0;
};
struct Grant {
  String member, id;
  int64_t until = 0;
  uint32_t sourceSequence = 0;
};

extern std::unordered_map<std::string,Card> cards;
extern std::unordered_map<std::string,Grant> grants;
extern String activeUid,lastError;
extern std::atomic<bool> cacheCapacityExceeded;
extern uint32_t cacheCapacityRevision;
extern bool maintenance,refreshNeeded,storageOK;
extern JsonDocument configuration;
extern int stateMutex,snapshotMutex;
struct Lock {explicit Lock(int) {}};
struct Preferences {
 std::map<std::string,uint32_t> values;
 bool fail=false;
 uint32_t getUInt(const char *k,uint32_t fallback){return values.count(k)?values[k]:fallback;}
 size_t putUInt(const char *k,uint32_t v){if(fail)return 0;values[k]=v;return 4;}
};
extern Preferences prefs;
inline bool checkNvsWrite(const char *,size_t got,size_t expected){if(got!=expected)storageOK=false;return got==expected;}
#define log_e(...) ((void)0)
inline uint32_t millis(){return 100;}
bool sealWrite(const String &,const String &);
bool sealRead(const String &,String &);
bool saveState(bool authoritative=false);
void loadState();
void trimCardCache();
void failCacheCapacity();
void cacheScannedCard(const Card &);
std::vector<std::string> snapshotRequestedUids();
bool mergeCacheSnapshot(JsonVariantConst,const std::vector<std::string>&,uint32_t,bool);

bool cacheCapacityResponse(const String &, int, JsonVariantConst);
