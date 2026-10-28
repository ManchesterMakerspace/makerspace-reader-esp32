# Validation record — 2026-10-28

## Passed

- Arduino CLI build with ESP32 Arduino 3.3.12, ESP32-S3, 16 MB flash, OPI PSRAM and the custom partition map.
- Program storage: **1,033,568 bytes** of the 4,194,304-byte application slot (24%); emitted application binary: 1,033,712 bytes including image overhead. Static globals: **35,604 bytes**; reported remaining internal memory is 292,076 bytes before runtime allocations.
- Host C++ policy tests exercise all role durations, invalid statuses, unknown/unenrolled cards, exact expiry boundaries, unavailable tools, missing/expired grants, membership bounds on grants, and incomplete/non-data NCI frames.
- Host NFC tests exercise PN7160 priority/PN532 fallback, both IRQ polarities, inactive IRQ without I2C reads, asynchronous discovery ACK/response separation, malformed/truncated/checksum-invalid/stale frames, UID bounds, 7-byte UID authentication, RF error status, block reads/writes and target reactivation/release using a simulated I2C bus.
- Maximum cache fixture: **179,308 MessagePack bytes** for 300 maximum-size cards plus 150 grants; per-snapshot budget is 320 KiB. API text limits use UTF-8 bytes without splitting characters.
- ts-data-api TypeScript compilation.
- Full disposable-replica-set API suite: **25 suites, 84 tests passed**, none pending. Includes existing generic API behavior and new reader authorization/enrollment/expiry/idempotence/capacity cases.
- API and Rails git diff --check; new firmware source whitespace checks; PowerShell setup/build/test syntax checks.
- The installed NFC/OTA dependency files used in compilation match the tracked patched vendor files.
- Held WikiPages patch passes git apply --check against the fetched public troubleshooting page. No wiki content was published.

The local Arduino toolchain was changed from ESP32 core 4.0.0-RC1 to pinned 3.3.12 for this build. scripts/setup.ps1 documents that core installation uses the normal Arduino package directory; application libraries use an isolated project sketchbook.

## Not exercised

No physical PN7160, PN532, card, W5500 link, relay, buzzer, LCD or RTC was used. No flash power-cut/fullness/corruption test, real management-browser authentication flow, MQTT broker interaction, gateway certificate verification, Atlas deployment, certificate provisioning, eFuse operation or OTA transfer was performed.

The production custom core/bootloader and irreversible secure-boot/flash-encryption provisioning were not built or commissioned here. The normal Arduino build was compiled with the production guard enabled; on an unsecured stock core/device it denies access and OTA. Production requires the separately configured secure core and hardware acceptance in COMMISSIONING.md.

Tests do not establish clone resistance for MIFARE Classic.

## Dependency audit

Compatible h3 1.15.11, defu 6.1.7 and jws 3.2.3 fixes were followed by the Zod **4.6.7** migration. npm audit --omit=dev reports **zero vulnerabilities**. The full audit retains 16 inherited development-only findings (9 low, 6 high, 1 critical), outside the Zod fix.

Zod regression tests verify that oversized bounded arrays produce one size error without per-element errors, and oversized sets do not visit child validators. JSON/EJSON BSON values, MongoDB operator keys, all three update-pipeline routes, ObjectId transforms, optional fields, authentication order, HTTP 400 envelopes and environment defaults pass compatibility checks. Existing service schemas do not use the constrained collections from the advisory; legacy request-size limits are unchanged.

The security skill's build and API tests ran in a disposable source copy with the updated installed dependencies and disposable MongoDB replica sets. Commands: npm run build; npm run test:disposable (25 suites / 84 tests); focused migration tests (4 suites / 28 tests); npm audit --omit=dev; git diff --check. The independent candidate review reported no concrete surviving advisory bypass or migration regression.

## Documentation and release

Rails collection, service/task, environment-variable, permissions, card assignment, checkout and route inventories were updated. No Rails runtime behavior, Rails API contract, Swagger artifact, React build or generated API-client release changed. Rails application tests were therefore not run.

The IRQ/PN532 and Zod follow-up updated firmware plan/wiring/protocol/commissioning documentation, the API migration note and the Rails companion hardware note. No new environment variables, collections, permissions, Rails endpoints or user workflows were added by the IRQ/PN532 or Zod changes. Those IRQ/PN532 and Zod changes did not alter the environment-variable inventory or require additional wiki edits/publication.

Deploy the companion service/indexes/gateway and broker ACLs before readers. Review and publish docs/wiki-reader-rollout.patch only with the actual hardware rollout.

## Automatic cache-key follow-up

- The final firmware compiles with the existing compiled-key configuration; the program/global sizes above reflect this revision. The compiled source copies match the final Storage, Reader, Web and CacheKey sources.
- A separate Arduino CLI build also passes with CACHE_KEY_HEX undefined and READER_PRODUCTION=0, using only example credentials in .test-build/auto-sketch/MakerspaceReader. Program storage: 1,039,716 bytes (24%); binary: 1,039,872 bytes; globals: 35,604 bytes. Its compiled source copies also match the final implementation. The original secrets.h was not modified.
- ./scripts/test.ps1 passes all policy, simulated NFC transport and cache-key tests, plus the unchanged 179,308-byte maximum cache fixture.
- Cache-key tests cover first boot, reuse after reboot or interrupted initialization, distinct simulated devices, legacy compiled keys, invalid explicit keys, stored-key/override mismatch, missing keys on used storage, malformed blobs, failed reads, full/write-failing NVS, readback mismatch and an acknowledged-failure commit recovered on reboot. Host persistence is simulated; these are not physical NVS power-cut tests.
- Startup rejects automatic production keys unless the core enables NVS encryption and the device passes the existing production security checks. This custom production core and its on-device NVS encryption remain unverified here.
- No firmware was flashed. Required hardware checks include fresh boot, reboot/OTA retention, decrypting existing FirstScan/spool data, key uniqueness across stations, NVS commit power cuts/fullness/corruption, and refusing to reformat non-erased storage after NVS loss. See COMMISSIONING.md.
- README, plan, protocol, commissioning, the held NFC-reader wiki draft/patch and Rails companion documentation describe the new lifecycle and recovery behavior. The Rails environment-variable inventory now documents optional CACHE_KEY_HEX; no Rails runtime environment variable was added. The wiki remains unpublished until deployment.
- No ts-data-api or Rails runtime code changed in this follow-up. Their application suites were not rerun; the API results above belong to the preceding Zod work.


## Arduino IDE NFC dependency selection

The sketch now includes MakerspacePN7160.h from the bundled, patched ELECHOUSE 3.1.3 library. ElectronicCats v3.1.1 shares the original Electroniccats_PN7150.h name but lacks setI2CPins, setVerboseTrace and setPn7160FixedVbat3V3. Stock ElectronicCats v3.1.1 is not a supported replacement for the patched driver.

- Arduino CLI compilation passed on ESP32 Arduino 3.3.12 with the official ElectronicCats v3.1.1 archive additionally available through --libraries .test-build/upstream-libraries. The build selected only ELECHOUSE_PN7160_PN7161_NFC for NFC; compiled Nfc.cpp uses the unique header. Program size remains 1,033,568 bytes and globals 35,604 bytes.
- ./scripts/test.ps1 passed the policy, simulated NFC, cache-key and maximum cache-size checks.
- The installed NFC sources and every file extracted from .arduino/downloads/MakerspacePN7160.zip match the vendored files by SHA-256. The packaging script parses successfully; changed-file whitespace checks pass.
- README and vendor instructions now document Arduino IDE ZIP installation, duplicate library handling, and verbose dependency verification. No API, environment-variable inventory, or deployed wiki flow changed; no wiki update applies to this development setup correction.
- No physical hardware test or Arduino IDE GUI compilation was performed. The CLI check exercises Arduino dependency resolution with both libraries present; hardware acceptance remains required.
