#pragma once
#include <string>
#include <map>
#include <unordered_map>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cassert>
#include <ArduinoJson.h>
class String : public std::string {
public:
  using std::string::string;
  String(const std::string &s):std::string(s){}
  bool isEmpty() const {return empty();}
  void toLowerCase() {}
  void replace(const char *a,const char *b) {size_t p=0; while((p=find(a,p))!=npos){std::string::replace(p,strlen(a),b);p+=strlen(b);}}
  size_t write(uint8_t c){push_back(char(c));return 1;}
  size_t write(const uint8_t *p,size_t n){append(reinterpret_cast<const char*>(p),n);return n;}
};
inline void convertToJson(const String &s, JsonVariant d){d.set(static_cast<const std::string&>(s));}
struct Card {String uid,member,holder,firstName,lastInitial;bool managesTool=false;};
struct Grant {int64_t until=0;};
extern std::unordered_map<std::string,Grant> grants;
extern JsonDocument configuration;
extern bool maintenance,storageOK,clockTrusted,refreshNeeded;
extern int stateMutex,fsMutex;
struct Lock {explicit Lock(int) {}};
struct Preferences {
  std::map<std::string,std::string> values; bool fail=false;
  String getString(const char *k,const char *d){return values.count(k)?values[k]:d;}
  size_t putString(const char *k,const String &v){if(fail)return 0;values[k]=v;return v.length();}
  uint64_t getULong64(const char *k,uint64_t d){return values.count(k)?std::stoull(values[k]):d;}
  size_t putULong64(const char *k,uint64_t v){if(fail)return 0;values[k]=std::to_string(v);return 8;}
  bool getBool(const char *k,bool d){return values.count(k)?values[k]=="true":d;}
};
extern Preferences prefs;
inline bool checkNvsWrite(const char *,size_t got,size_t expected){if(got!=expected)storageOK=false;return got==expected;}
bool sealWrite(const String &,const String &);
bool sealRead(const String &,String &);
void reserveEventSpace(size_t);
String hexBytes(const uint8_t *,size_t);
void esp_fill_random(void *,size_t);
int64_t epochMs();
void startRelay(int64_t,int64_t,const String &);
void stopRelay(const char *reason = "automatic", const Card *actor = nullptr);
void endToolSession(const char * = "automatic",const Card * = nullptr);


uint64_t reserveEventSequence();
String eventSpoolPath(uint64_t sequence, const String &id);
