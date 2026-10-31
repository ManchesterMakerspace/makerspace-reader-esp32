#include "Reader.h"
#include "FlashProfile.h"
#include "BuildInfo.h"
#if __has_include("BuildMetadata.h")
#include "BuildMetadata.h"
#else
#define READER_BUILD_ID "unversioned-direct-build"
#endif
#include <esp_partition.h>
namespace {
const char buildDate[] = __DATE__, buildTime[] = __TIME__, buildFile[] = __FILE__;
const esp_partition_t *partition(esp_partition_type_t type, esp_partition_subtype_t subtype, const char *name) {
 return esp_partition_find_first(type, subtype, name);
}
}
bool checkFlashLayout() {
 const auto *a = partition(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
 const auto *b = partition(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr);
 const auto *c = partition(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
 const auto *k = partition(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS, "nvs_keys");
 return ESP.getFlashChipSize() >= readerProfile::flashBytes && a && b && c && k &&
  a->address == 0x10000 && b->address == 0x10000 + readerProfile::slotBytes &&
  a->size == readerProfile::slotBytes && b->size == readerProfile::slotBytes &&
  c->address == readerProfile::cacheOffset && c->size == readerProfile::cacheBytes &&
  k->address == readerProfile::keysOffset && k->size == 0x1000;
}
void buildStatistics(JsonDocument &d) {
 d["firmware"] = READER_BUILD_ID;
 d["buildDate"] = buildDate; d["buildTime"] = buildTime; d["buildFile"] = buildFile;
 d["configuredFlashBytes"] = uint64_t(READER_CONFIGURED_FLASH_BYTES);
 d["profileFlashBytes"] = readerProfile::flashBytes;
 d["flashBytes"] = ESP.getFlashChipSize();
 d["flashLayoutCompatible"] = checkFlashLayout();
 const auto *a = partition(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
 const auto *c = partition(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
 d["appSlotBytes"] = a ? a->size : 0; d["filesystemBytes"] = c ? c->size : 0;
}
