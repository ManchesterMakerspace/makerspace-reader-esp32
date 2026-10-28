# Makerspace ESP32 reader implementation plan

## Architecture
Create an Arduino IDE project for Waveshare ESP32-S3-ETH and Elechouse PN7160, with PN532 I2C fallback.
Use Ethernet only, HTTPS to ts-data-api, and the built-in ESP-MQTT client.
The API is the existing ts-data-api Node service, extended with restricted reader routes.
Authenticate devices with individual client certificates at a trusted TLS proxy; map certificates to immutable reader IDs.
Keep Atlas credentials and credential-encryption keys on the server.

## Authentication and enrollment
Require activeMember, unexpired membership, unique per-card Classic Key A authentication, and protected credential verification.
Classic hardening prevents UID-only acceptance but does not provide strong clone resistance.
Reserve sector 1: blocks 4-5 contain a random 32-byte credential; block 6 contains format/version.
Key A permits reads; Key B permits credential/trailer changes.
Any role can enable enrollment. Enroll assigned cards only, online, using recoverable pending server credentials.
Unknown cards are rejected and left untouched. No UID-only fallback.
Initial enrollment assumes physical supervision.

## Relay and authorization
door: 10 seconds; garagedoor: 30; test: 9; latch: 5; tool: toggle.
Require a valid checkout for the configured tool, excluding revoked/superseded records.
Checkout expiration is end of date in America/New_York.
Immediately disable a running tool on known expiry or verified revocation.
Start LOW on boot and never replay relay actions from event storage.
Deduplicate presentations until removal for 500 ms.

## Storage and offline operation
Cache at most 300 cards and 150 checkouts in RAM.
Use encrypted MessagePack snapshots in LittleFS with two generations and fail-closed committed-generation selection.
When CACHE_KEY_HEX is undefined, generate 32 hardware-random bytes only for an empty reader NVS namespace and empty filesystem; persist/read back reader/cacheKey before use.
Reuse the key on restart/OTA; preserve compiled-key compatibility and reject missing/malformed keys on used devices.
Require NVS encryption for persisted keys in production. Auto-format only erased LittleFS with an empty reader namespace.
Reserve 4 MiB LittleFS: 640 KiB snapshot budgets, 3 MiB event spool, remaining working space.
Refresh every five minutes and on MQTT invalidation; allow cached permission until card/checkout expiry during outages.
Maintain independent MongoDB and MQTT delivery acknowledgments.
Use immutable ObjectIds and canonical event payloads with BSON Date/Double/Int32 types.
Drop oldest queued events on overflow, preserve authorization caches, count audit loss, continue admission.
FirstScan is a station-local UTC millisecond timestamp, zero before presentation.
Persist it once; preserve through refresh, enrollment, reboot, and firmware OTA.
Never accept FirstScan from the API or MQTT. Preserve pending observations when time is unknown.

## Hardware
I2C SDA1/SCL2, PN7160 IRQ17/VEN18, relay21 active HIGH.
W5500 MOSI11/MISO12/SCLK13/CS14/IRQ10/RST9.
Probe PN7160 0x28 first; if absent, use PN532 at 7-bit I2C address 0x24.
PN532 reuses GPIO17 for active-LOW IRQ and GPIO18 for RSTPD_N; connect one NFC module.
Wake discovery from PN7160 rising IRQ/HIGH or PN532 falling IRQ/LOW. Recheck the level before reads and keep I2C outside the ISR.
Use the same Classic authentication, credential verification and enrollment policy for both transports.
Probe buzzer 0x34, SerLCD 0x72, PCF8523 0x68.
Optional peripherals must not prevent access. Without trusted time, deny admission.
Correct PN7160 transport response/timeout handling before enabling authentication.

## Management and OTA
Use ESPAsyncWebServer and ElegantOTA async mode.
Require compiled-in nonempty username/password for all HTML, JSON, assets, metadata, and OTA operations.
Paths: /, /config, /stats, /cache/cards, /cache/checkouts, /update.
Protect mutations against CSRF; escape all user data; never expose keys or passwords.
Serve management through an HTTPS proxy on the management network.
Synchronize authorization-affecting configuration with the server before activation.
Allow immediate local disabling of enrollment.
Use firmware-only signed OTA into the inactive slot, reject filesystem uploads.
Reject OTA while relay HIGH; hold LOW during update; preserve cache and event storage.
Use secure boot and encrypted device secrets in production.

## MQTT and API
Publish successful events to activity/checkins with QoS 1, non-retained.
Subscribe cards/# and readers/all/invalidate.
Accept prefixed update/insert epoch JSON messages as refresh hints, never as authority to extend permission.
Expose bounded card lookup/refresh, tool grants, config, enrollment begin/commit, and idempotent event ingestion.
Reader credentials must never authorize unrestricted defaults endpoints.
Add change-stream invalidation with periodic refresh as recovery.

## Verification and rollout
Test wrong keys, copied UID, stale responses, malformed frames, interrupted enrollment,
expiry/DST, revocation, relay timing, filesystem corruption/fullness, independent outages,
duplicates, every management route, CSRF, FirstScan persistence, and interrupted OTA.
Compile Arduino firmware and TypeScript; run API tests against disposable MongoDB.
Bench-test real hardware with dummy relay load before production.
Update API and firmware docs, Rails inventories, and wiki documentation before deployment.


## Implementation status

The software implementation is complete. Build and test evidence is recorded in
[docs/VALIDATION.md](docs/VALIDATION.md). Physical acceptance and production
security provisioning remain explicit commissioning requirements in
[docs/COMMISSIONING.md](docs/COMMISSIONING.md).


