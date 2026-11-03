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
    const auto suffix=std::to_string(i);
    o["uid"] = std::string(20-suffix.size(), 'A')+suffix;
    o["id"] = std::string(24-suffix.size(), 'a')+suffix;
    o["member"] = std::string(24-suffix.size(), 'b')+suffix;
    o["holder"] = std::string(250-suffix.size(), 'h')+"\xE6\xB8\xAC\xE8\xA9\xA6"+suffix;
    o["firstName"] = std::string(60-suffix.size(), 'f')+"\xF0\x9F\x94\xA7"+suffix;
    o["lastInitial"] = "\xF0\x9F\x94\xA7";
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
    const auto suffix=std::to_string(i);
    o["member"] = std::string(24-suffix.size(), 'b')+suffix;
    o["id"] = std::string(24-suffix.size(), 'c')+suffix;
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
  // Reserve both committed snapshots, an interrupted replacement, the event spool,
  // and 64 KiB for journal/filesystem overhead even on the smaller Wi-Fi layout.
  assert(readerProfile::cacheBytes >= 3 * readerProfile::snapshotBytes + readerProfile::eventBytes + 64 * 1024);
  if (readerProfile::compact) {
    assert(readerProfile::slotBytes == 0x1a0000);
    assert(readerProfile::cacheOffset == 0x350000 && readerProfile::cacheBytes == 700 * 1024);
  }
  std::cout << "Maximum-size profile cache: " << size << " MessagePack bytes\n";
}
