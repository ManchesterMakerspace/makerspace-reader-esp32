#include "Reader.h"

bool saveState(bool authoritative) {
  Lock snapshot(snapshotMutex);
  JsonDocument d;
  uint32_t capacityRevision;
  {
    Lock l(stateMutex);
    if (cards.size() > readerProfile::cards || grants.size() > readerProfile::grants) {
      failCacheCapacity();
      return false;
    }
    capacityRevision = cacheCapacityRevision;
    d["schema"] = 1;
    d["config"].set(configuration);
    auto a = d["cards"].to<JsonArray>();
    for (auto &pair : cards) {
      auto &c = pair.second;
      auto o = a.add<JsonObject>();
      o["uid"] = c.uid;
      o["id"] = c.id;
      o["member"] = c.member;
      o["holder"] = c.holder;
      o["firstName"] = c.firstName; o["lastInitial"] = c.lastInitial;
      o["managesTool"] = c.managesTool;
      o["status"] = c.status;
      o["expiry"] = c.expiry;
      o["key"] = c.key;
      o["digest"] = c.digest;
      o["FirstScan"] = c.first;
      o["pending"] = c.pending;
      o["lastUsed"] = c.lastUsed;
    }
    auto g = d["grants"].to<JsonArray>();
    for (auto &pair : grants) {
      auto &v = pair.second;
      auto o = g.add<JsonObject>();
      o["member"] = v.member;
      o["id"] = v.id;
      o["until"] = v.until;
    }
  }
  if (d.overflowed()) {
    storageOK = false;
    return false;
  }
  String serialized;
  serializeMsgPack(d, serialized);
  if (serialized.length() > readerProfile::snapshotBytes) {
    Lock l(stateMutex);
    failCacheCapacity();
    log_e("Cache snapshot exceeds profile byte limit");
    return false;
  }
  uint32_t generation = prefs.getUInt("generation", 0) + 1;
  String path = "/state" + String(generation % 2);
  if (!sealWrite(path, serialized)) {
    storageOK = false;
    return false;
  }
  String check;
  if (!sealRead(path, check) || check.length() != serialized.length() ||
      memcmp(check.c_str(), serialized.c_str(), serialized.length()) != 0)
    { storageOK = false; log_e("Cache snapshot readback verification failed"); return false; }
  if (!checkNvsWrite("generation", prefs.putUInt("generation", generation), 4))
    return false;
  storageOK = true;
  if (authoritative) {
    Lock l(stateMutex);
    // A scan can overflow the cache while this snapshot is being written.
    if (capacityRevision == cacheCapacityRevision && cards.size() <= readerProfile::cards && grants.size() <= readerProfile::grants) {
      cacheCapacityExceeded = false;
      if (lastError == "Card cache capacity exceeded") lastError = "";
    }
  }
  return true;
}
void loadState() {
  uint32_t generation = prefs.getUInt("generation", 0);
  if (!generation)
    return;
  String raw;
  if (!sealRead("/state" + String(generation % 2), raw)) {
    lastError = "Cache authentication failed";
    storageOK = false;
    return;
  }
  JsonDocument d;
  if (raw.length() > readerProfile::snapshotBytes || deserializeMsgPack(d, raw) || d["schema"].as<int>() != 1 || d["cards"].size() > readerProfile::cards ||
      d["grants"].size() > readerProfile::grants) {
    log_e("Cache snapshot schema, encoding or capacity validation failed");
    storageOK = false;
    return;
  }
  configuration.set(d["config"]);
  for (JsonObject o : d["cards"].as<JsonArray>()) {
    Card c;
    c.uid = o["uid"] | "";
    c.id = o["id"] | "";
    c.member = o["member"] | "";
    c.holder = o["holder"] | "";
    c.firstName = o["firstName"] | ""; c.lastInitial = o["lastInitial"] | "";
    c.managesTool = o["managesTool"] | false;
    c.status = o["status"] | "";
    c.expiry = o["expiry"] | int64_t(0);
    c.key = o["key"] | "";
    c.digest = o["digest"] | "";
    c.first = o["FirstScan"] | int64_t(0);
    c.pending = o["pending"] | false;
    c.lastUsed = o["lastUsed"] | uint64_t(0);
    cards[c.uid.c_str()] = c;
  }
  for (JsonObject o : d["grants"].as<JsonArray>()) {
    Grant g;
    g.member = o["member"] | "";
    g.id = o["id"] | "";
    g.until = o["until"] | int64_t(0);
    grants[g.member.c_str()] = g;
  }
}

