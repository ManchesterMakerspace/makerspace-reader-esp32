#pragma once
#include <ArduinoJson.h>
#include <atomic>
#include <cassert>
#include <cstring>
#include <map>
#include <string>
#include <vector>
class String:public std::string {
public:
  using std::string::string;
  String(const std::string &s):std::string(s){}
  explicit String(long long n):std::string(std::to_string(n)){}
  bool startsWith(const char *s)const{return find(s)==0;}
  bool endsWith(const char *s)const{return size()>=strlen(s) && compare(size()-strlen(s),strlen(s),s)==0;}
  int indexOf(char c)const{auto p=find(c);return p==npos?-1:int(p);}
  size_t write(uint8_t c){push_back(char(c));return 1;}
  size_t write(const uint8_t *p,size_t n){append(reinterpret_cast<const char*>(p),n);return n;}
};
inline void convertToJson(const String &s,JsonVariant d){d.set(static_cast<const std::string&>(s));}
inline void convertFromJson(JsonVariantConst s,String &d){d=s.as<std::string>();}
extern JsonDocument configuration;
extern bool clockTrusted;
extern std::atomic<uint32_t> dropped;
extern int stateMutex,fsMutex,held;
struct Lock { explicit Lock(int){++held;} ~Lock(){--held;} };
int64_t epochMs();
uint32_t millis();
bool sealWrite(const String &,const String &,bool);
bool sealRead(const String &,String &,bool);
bool api(const String &,JsonDocument &,const String &body=String());
#define log_e(...) ((void)0)

struct Preferences {
 bool gap=false;
 bool getBool(const char *,bool fallback) {return gap || fallback;}
 size_t putBool(const char *,bool v) {gap=v; return 1;}
};
extern Preferences prefs;
