#include "CacheKey.h"
#include "Reader.h"
#include "secrets.h"
#include <LittleFS.h>
#include <esp_partition.h>
#include <esp_random.h>
#include <mbedtls/gcm.h>
#include <nvs.h>
static uint8_t cacheEncryptionKey[cacheKey::bytes] = {};
bool cacheKeyReady = false;
// An erased reader namespace is necessary but insufficient: its LittleFS volume
// may still contain data encrypted with a lost key.
static bool preferencesEmpty(bool &empty) {
  nvs_handle_t handle;
  if (nvs_open("reader", NVS_READONLY, &handle) != ESP_OK)
    return false;
  size_t count = 0;
  esp_err_t result = nvs_get_used_entry_count(handle, &count);
  nvs_close(handle);
  empty = count == 0;
  return result == ESP_OK;
}
static bool filesystemEmpty() {
  File root = LittleFS.open("/");
  if (!root || !root.isDirectory())
    return false;
  File entry = root.openNextFile();
  while (entry) {
    // An empty events directory may survive an interrupted first boot.
    if (!entry.isDirectory() || String(entry.path()) != "/events")
      return false;
    File child = entry.openNextFile();
    if (child)
      return false;
    entry = root.openNextFile();
  }
  return true;
}
static bool filesystemErased() {
  const esp_partition_t *partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
  if (!partition)
    return false;
  uint8_t block[256];
  for (size_t offset = 0; offset < partition->size; offset += sizeof(block)) {
    size_t count = std::min(sizeof(block), size_t(partition->size - offset));
    if (esp_partition_read(partition, offset, block, count) != ESP_OK)
      return false;
    for (size_t i = 0; i < count; ++i)
      if (block[i] != 0xFF)
        return false;
  }
  return true;
}
static bool storageFailure(const char *message) {
  lastError = message;
  storageOK = false;
  cacheKeyReady = false;
  memset(cacheEncryptionKey, 0, sizeof(cacheEncryptionKey));
  log_e("%s", message); // Never print key material.
  return false;
}
bool initializeStorage() {
  if (!prefs.begin("reader", false))
    return storageFailure("Reader NVS unavailable");
  deviceBlocked = prefs.getBool("blocked", false);
  dropped = prefs.getUInt("dropped", 0);
  bool emptyPreferences = false;
  if (!preferencesEmpty(emptyPreferences))
    return storageFailure("Reader NVS inspection failed");
#if READER_PRODUCTION
#ifdef CACHE_KEY_HEX
  bool usesStoredKey = prefs.isKey("cacheKey");
#else
  bool usesStoredKey = true;
#endif
  if (usesStoredKey) {
#if !defined(CONFIG_NVS_ENCRYPTION) || !CONFIG_NVS_ENCRYPTION
    return storageFailure("Generated cache keys require an NVS-encrypted production core");
#else
    if (!productionReady)
      return storageFailure("Provision production security before generating a cache key");
#endif
  }
#endif
  bool mounted = LittleFS.begin(false);
#if READER_FORMAT_NEW_FS
  // Never format a damaged or used filesystem just because NVS has been erased.
  if (!mounted && emptyPreferences && filesystemErased())
    mounted = LittleFS.format() && LittleFS.begin(false);
#endif
  if (!mounted)
    return storageFailure("Cache filesystem unavailable; no automatic format");
  bool emptyFiles = filesystemEmpty();
  if (!emptyFiles && (prefs.getType("nonce") != PT_U64 || prefs.getULong64("nonce", 0) == 0))
    return storageFailure("Cache files exist without their nonce counter; reprovision required");
#ifdef CACHE_KEY_HEX
  const char *configuredKey = CACHE_KEY_HEX;
#else
  const char *configuredKey = nullptr;
#endif
  auto result = cacheKey::loadOrCreate(prefs, configuredKey, emptyPreferences, emptyFiles,
                                       esp_fill_random, cacheEncryptionKey);
  if (!cacheKey::ready(result)) {
    switch (result) {
    case cacheKey::Result::invalidConfigured:
      return storageFailure("CACHE_KEY_HEX must contain exactly 64 hexadecimal digits");
    case cacheKey::Result::invalidStored:
      return storageFailure("Stored cache key is invalid; reprovision required");
    case cacheKey::Result::mismatch:
      return storageFailure("Configured cache key differs from the stored device key");
    case cacheKey::Result::persistenceFailed:
      return storageFailure("Cache key NVS write/readback failed");
    default:
      return storageFailure("Cache key missing on a used device; reprovision required");
    }
  }
  cacheKeyReady = true;
  storageOK = true;
  if (!LittleFS.exists("/events") && !LittleFS.mkdir("/events"))
    return storageFailure("Cannot create event spool directory");
  recoverStorage();
  loadState();
  return true;
}
String hexBytes(const uint8_t *p, size_t n) {
  String s;
  s.reserve(n * 2);
  for (size_t i = 0; i < n; i++) {
    char b[3];
    snprintf(b, 3, "%02X", p[i]);
    s += b;
  }
  return s;
}
bool decodeHex(const String &s, uint8_t *out, size_t n) {
  if (s.length() != n * 2)
    return false;
  for (size_t i = 0; i < n; i++) {
    int v = 0;
    for (int j = 0; j < 2; j++) {
      char c = s[i * 2 + j];
      int d = c >= '0' && c <= '9'   ? c - '0'
              : c >= 'a' && c <= 'f' ? c - 'a' + 10
              : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                     : -1;
      if (d < 0)
        return false;
      v = v * 16 + d;
    }
    out[i] = v;
  }
  return true;
}
static bool cacheFailure(const String &path, const char *operation) {
  log_e("Cache %s failed: %s", operation, path.c_str());
  storageOK = false;
  return false;
}
bool sealWrite(const String &path, const String &data) {
  Lock lock(fsMutex);
  uint8_t iv[12], tag[16];
  if (!cacheKeyReady)
    return cacheFailure(path, "key unavailable");
  // Reserve each nonce in durable NVS before use; reset requires a new key.
  uint64_t seq = prefs.getULong64("nonce", 0) + 1;
  if (!seq || !checkNvsWrite("nonce", prefs.putULong64("nonce", seq), 8))
    return cacheFailure(path, "nonce reservation");
  memset(iv, 0, 12);
  memcpy(iv + 4, &seq, 8);
  std::vector<uint8_t> cipher(data.length());
  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);
  int rc = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, cacheEncryptionKey, 256);
  if (!rc)
    rc = mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT, data.length(), iv, 12,
                                   (const uint8_t *)path.c_str(), path.length(),
                                   (const uint8_t *)data.c_str(), cipher.data(), 16, tag);
  mbedtls_gcm_free(&ctx);
  if (rc) {
    log_e("Cache cryptographic operation failed: rc=%d", rc);
    return cacheFailure(path, "encryption/authentication");
  }
  File f = LittleFS.open(path + ".tmp", "w");
  if (!f)
    return cacheFailure(path, "open for write");
  bool ok = f.write(iv, 12) == 12 && f.write(tag, 16) == 16 &&
            f.write(cipher.data(), cipher.size()) == cipher.size();
  f.flush();
  f.close();
  if (!ok)
    return cacheFailure(path, "write");
  if (!LittleFS.rename(path + ".tmp", path))
    return cacheFailure(path, "rename");
  return true;
}
bool sealRead(const String &path, String &data) {
  Lock lock(fsMutex);
  File f = LittleFS.open(path, "r");
  if (!f || f.size() < 28 || f.size() > 524288)
    return cacheFailure(path, "open/read size validation");
  uint8_t iv[12], tag[16];
  if (!cacheKeyReady)
    return cacheFailure(path, "key unavailable");
  if (f.read(iv, 12) != 12 || f.read(tag, 16) != 16)
    return cacheFailure(path, "header read");
  size_t n = f.size() - 28;
  std::vector<uint8_t> cipher(n), plain(n + 1);
  if (f.read(cipher.data(), n) != n)
    return cacheFailure(path, "read");
  f.close();
  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);
  int rc = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, cacheEncryptionKey, 256);
  if (!rc)
    rc = mbedtls_gcm_auth_decrypt(&ctx, n, iv, 12, (const uint8_t *)path.c_str(), path.length(),
                                  tag, 16, cipher.data(), plain.data());
  mbedtls_gcm_free(&ctx);
  if (rc) {
    log_e("Cache cryptographic operation failed: rc=%d", rc);
    return cacheFailure(path, "encryption/authentication");
  }
  data = String((const char *)plain.data(), n);
  return true;
}
bool saveState() {
  Lock snapshot(snapshotMutex);
  JsonDocument d;
  {
    Lock l(stateMutex);
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
  String serialized;
  serializeMsgPack(d, serialized);
  if (serialized.length() > 320 * 1024) {
    log_e("Cache snapshot exceeds 320 KiB limit");
    storageOK = false;
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
    return cacheFailure(path, "snapshot readback verification");
  if (!checkNvsWrite("generation", prefs.putUInt("generation", generation), 4))
    return false;
  storageOK = true;
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
  if (deserializeMsgPack(d, raw) || d["schema"] != 1 || d["cards"].size() > 300 ||
      d["grants"].size() > 150) {
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

// Runs before worker tasks: only uncommitted temporary files can be removed.
void recoverStorage() {
  Lock f(fsMutex);
  for (const char *path : {"/state0.tmp", "/state1.tmp"})
    if (LittleFS.exists(path) && !LittleFS.remove(path))
      cacheFailure(path, "temporary file removal");
  std::vector<String> orphaned;
  File dir = LittleFS.open("/events");
  if (!dir) {
    cacheFailure("/events", "directory open");
    return;
  }
  File file = dir.openNextFile();
  while (file) {
    String path = file.path();
    if (path.endsWith(".tmp"))
      orphaned.push_back(path);
    file = dir.openNextFile();
  }
  dir.close();
  for (auto &path : orphaned)
    if (LittleFS.remove(path))
      dropped++;
    else
      cacheFailure(path, "orphan removal");
  if (!orphaned.empty())
    checkNvsWrite("dropped", prefs.putUInt("dropped", dropped), 4);
}
