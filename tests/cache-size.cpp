#include "../MakerspaceReader/FlashProfile.h"
#include <ArduinoJson.h>
#include <cassert>
#include <iostream>
#include <string>
int main() {
  JsonDocument d;
  d["schema"] = 1;
  auto a = d["cards"].to<JsonArray>();
  for (int i = 0; i < int(readerProfile::cards); i++) {
    auto o = a.add<JsonObject>();
    o["uid"] = std::string(20, 'A');
    o["id"] = std::string(24, 'a');
    o["member"] = std::string(24, 'b');
    o["holder"] = std::string(256, 'h');
    o["firstName"] = std::string(64, 'f');
    o["lastInitial"] = std::string(4, 'l');
    o["managesTool"] = true;
    o["status"] = std::string(64, 's');
    o["expiry"] = INT64_MAX;
    o["key"] = std::string(12, 'a');
    o["digest"] = std::string(64, 'b');
    o["FirstScan"] = INT64_MAX;
    o["pending"] = false;
  }
  auto grants = d["grants"].to<JsonArray>();
  for (int i = 0; i < int(readerProfile::grants); i++) {
    auto o = grants.add<JsonObject>();
    o["member"] = std::string(24, 'b');
    o["id"] = std::string(24, 'c');
    o["until"] = INT64_MAX;
  }
  d["config"]["name"] = std::string(48, 'n');
  d["config"]["toolName"] = std::string(96, 't');
  d["config"]["role"] = "tool";
  d["config"]["shopName"] = std::string(96, 's');
  d["config"]["shopId"] = std::string(24, 'a');
  d["config"]["toolId"] = std::string(24, 'b');
  d["config"]["toolOutOfService"] = true;
  d["config"]["shopOutOfService"] = true;
  size_t size = measureMsgPack(d);
  assert(size < readerProfile::snapshotBytes);
  std::cout << "Maximum-size profile cache: " << size << " MessagePack bytes\n";
}
