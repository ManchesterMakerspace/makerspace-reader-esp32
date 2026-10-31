#pragma once
// Copy to secrets.h; never commit real credentials.
#define WEB_USERNAME "admin"
#define WEB_PASSWORD "REPLACE_WITH_UNIQUE_LONG_PASSWORD"
#define API_URL "https://reader-api.example.invalid"
#define MQTT_URL "mqtts://mqtt.example.invalid:8883"
#define DEVICE_ID "REPLACE_WITH_REGISTERED_DEVICE_ID"
#define SERVER_CA "REPLACE_WITH_PEM_CA_CERTIFICATE"
#define CLIENT_CERT "REPLACE_WITH_PEM_DEVICE_CERTIFICATE"
#define CLIENT_KEY "REPLACE_WITH_PEM_DEVICE_PRIVATE_KEY"
// Optional override: 32-byte per-device hex key. Leave undefined for first-boot
// generation and persistence in reader/cacheKey NVS (encrypted in production).
// #define CACHE_KEY_HEX "REPLACE_WITH_64_HEX_DIGITS"
#define READER_PRODUCTION 0

// Format only fully erased LittleFS when the reader NVS namespace is empty.
#define READER_FORMAT_NEW_FS 1

// Snapshot and card freshness interval; may also be set with compiler -D.
// #define READER_REFRESH_INTERVAL_MS 900000UL

// Optional Ethernet fallback. Define BOTH; otherwise Wi-Fi stays off.
// #define WIFI_SSID "your-network"
// #define WIFI_PSK "your-network-password"
