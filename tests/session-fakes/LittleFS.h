#pragma once
#include <string>
extern std::map<std::string,std::string> flash;
struct FakeFS {bool exists(const String &s){return flash.count(s)!=0;}};
extern FakeFS LittleFS;
