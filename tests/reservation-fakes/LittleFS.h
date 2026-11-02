#pragma once
#include "Reader.h"
extern std::map<std::string,std::string> flash;
struct File {
  bool valid=false; String path; std::vector<String> entries; size_t cursor=0;
  explicit operator bool()const{return valid;}
  String name()const{return path;}
  File openNextFile(){File f;if(cursor<entries.size()){f.valid=true;f.path=entries[cursor++];}return f;}
};
struct FakeFS {
  bool exists(const String &s){return flash.count(s)>0;}
  File open(const char *){File f;f.valid=true;for(const auto &entry:flash)if(entry.first.find("/events/")==0)f.entries.push_back(entry.first);return f;}
};
extern FakeFS LittleFS;
