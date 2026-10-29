# Makerspace reader ESP32

Arduino IDE firmware for Waveshare ESP32-S3-ETH (16 MB flash / OPI PSRAM), Elechouse PN7160 (or PN532 I2C fallback), and the companion ts-data-api service. The implementation supports authenticated card enrollment and scans, all five relay roles, Ethernet HTTPS/MQTTS, offline caches and audit replay, optional I2C peripherals, authenticated management pages, and ElegantOTA.

[PLAN.md](PLAN.md) is the raw Markdown plan. [Commissioning](docs/COMMISSIONING.md) lists mandatory hardware verification. [Protocol and storage](docs/PROTOCOL.md) explains the data contracts and failure behavior.

## Build

On Windows with Arduino CLI installed:

```powershell
./scripts/setup.ps1
# Edit MakerspaceReader/secrets.h with this station's actual configuration.
./scripts/build.ps1
```

Setup pins ESP32 Arduino **3.3.12** and the libraries; it uses the normal Arduino package directory and an isolated project sketchbook. Installing that core can replace another installed ESP32 core version. It preserves an existing secrets.h. The two patched dependencies are tracked in vendor/libraries with their upstream licenses.

For Arduino IDE, set the sketchbook location to this project's .arduino directory, restart the IDE, and open MakerspaceReader/MakerspaceReader.ino. Select ESP32S3 Dev Module, 16 MB flash, OPI PSRAM, and Custom partition scheme. The sketch's partitions.csv defines two 4 MiB application slots and a 4 MiB LittleFS volume. Do not substitute a default partition map.

The included example credentials are placeholders. Set DEVICE_ID to the registered nfc_readers ObjectId (24 lowercase hexadecimal characters), HTTPS/MQTTS URLs, trusted CA PEM, client certificate/private-key PEM, and a long compiled-in web username/password. Never commit secrets.h or build artifacts.

Leave `CACHE_KEY_HEX` **undefined** to generate a random 32-byte cache key on first boot. The example configuration now does this. Generation requires an empty `reader` NVS namespace and no existing cache/spool files. The key is committed as the `cacheKey` byte blob and read back before use. Restarts and OTA reuse it without rewriting it. An empty string or the old placeholder define is invalid: remove/comment out the define for automatic mode.

An existing station using a compiled key should retain that exact define. It is not copied into NVS automatically; removing it on a used station fails safely. If an NVS key already exists, a compiled override must match it. Missing/malformed keys, mismatches and failed persistence stop NFC admission and background synchronization. Management remains available with `cacheKeyReady=false`; configuration writes return 503 until corrected and restarted. The key is never displayed.

Persisted keys require `CONFIG_NVS_ENCRYPTION=y` and the existing secure production profile. The partition table already reserves encrypted `nvs_keys`. Bench builds with `READER_PRODUCTION=0` can use ordinary, unencrypted NVS. Preserve NVS and LittleFS through upgrades; never erase NVS alone to reset a reader.

**Stock Arduino builds intentionally deny production admission and OTA.** READER_PRODUCTION=1 requires secure boot, flash encryption, and a core compiled with native signed-image verification. For isolated bench tests with a dummy relay, set READER_PRODUCTION=0. That is not a production security profile. See docs/sdkconfig.production.defaults and the commissioning guide for the separately built production core.


### Arduino IDE NFC library selection

ElectronicCats PN7150 v3.1.1 is not the driver used by this sketch. Its shared Electroniccats_PN7150.h name previously let Arduino select it instead of the bundled ELECHOUSE 3.1.3 fork. That causes missing setI2CPins, setVerboseTrace and setPn7160FixedVbat3V3 errors. Do not remove those calls or substitute an unpatched Library Manager release: this project also requires the bundled short-read fixes.

The sketch now includes MakerspacePN7160.h, a unique entry point in our bundled driver. Run scripts/setup.ps1 and use its printed sketchbook directory, or run scripts/package-nfc-library.ps1 and install the printed ZIP through **Sketch > Include Library > Add .ZIP Library** in Arduino IDE. Restart the IDE and compile the updated sketch. If an older ELECHOUSE copy is already installed, replace that copy with the bundled folder; preserve any locally edited copy outside the sketchbook libraries directory first. ElectronicCats v3.1.1 may remain installed for other projects.

Enable verbose compilation output in IDE preferences and verify the selected NFC library is ELECHOUSE_PN7160_PN7161_NFC, not ElectronicCats-PN7150. A missing MakerspacePN7160.h means the bundled library has not been installed into the IDE's active sketchbook. The patched ElegantOTA dependency is also still required; setup.ps1 installs both.

## Wiring

| Function | GPIO / address |
| --- | --- |
| Shared I2C | SDA 1, SCL 2; 100 kHz; 3.3 V logic |
| PN7160 (preferred) | IRQ 17 active HIGH, VEN 18, I2C 0x28 |
| PN532 (fallback) | IRQ 17 active LOW, RSTPD_N 18, default I2C 0x24 |
| W5500 | MOSI 11, MISO 12, SCLK 13, CS 14, IRQ 10, RST 9 |
| Active-HIGH relay driver | GPIO 21; external pull-down; suitable driver/flyback protection |
| Optional Qwiic buzzer | 0x34 |
| Optional RGB SerLCD 16x2 | 0x72 |
| Optional PCF8523 RTC | 0x68; battery-backed UTC |

Connect **one NFC module**, with its IRQ and reset/enable lines wired as shown. Put a PN532 module's switches/jumpers in I2C mode; 0x24 is the 7-bit address (0x48 is the write-address byte). The firmware enables the module before probing 0x28, and tries 0x24 only if PN7160 is absent. A present PN7160 that fails initialization is retried, without switching controllers. Missing/unhealthy controllers retry initialization every three seconds.

PN7160 discovery waits for a rising IRQ on GPIO 17 and queries only while that line is HIGH. A level check also handles IRQ already asserted before the task waits. The interrupt only wakes the NFC task; all I2C work stays in task context under the shared bus lock. PN532 uses falling IRQ/LOW with asynchronous target discovery. Idle waits do not poll either reader over I2C; initialization, recovery and host commands still transmit as needed. The PN532 transport is included in the sketch and needs no extra Arduino library.

The PN7160 configuration assumes the module's normal VDD(UP)=3.3 V / VANT=5 V supply arrangement, not its fixed-VBAT 3.3 V mode. Verify the exact Elechouse board jumpers and power rails before applying power. No Wi-Fi connection is configured.

| Role | Relay behavior |
| --- | --- |
| door | HIGH for 10 seconds |
| garagedoor | HIGH for 30 seconds |
| test | HIGH for 9 seconds |
| latch | HIGH for 5 seconds |
| tool | Toggle on each authorized presentation; OFF on expiry or verified invalidation |

Remove a card for at least 500 ms before the same card triggers another action. Each relay activation arms ESP32 Arduino's bundled Ticker with a fresh one-shot countdown. The callback posts an off event; a separate high-priority task checks the current deadline and switches the relay under the same state lock used by scans. Resetting a pulse replaces its countdown, and a delayed old callback cannot stop the new pulse. Deadline polling remains a fallback, so network timeouts do not extend pulses. Tool relays start OFF after every boot. A physical emergency stop and machine safety circuits remain independent of this access controller.

## Serial diagnostics

Open the serial console at **115200 baud**. The build script selects **Core Debug Level: Info**; select that level (or higher) in Arduino IDE too. log_i() reports discovered RTC, display, buzzer and NFC devices, successful NFC initialization, and network recovery. log_e() reports NFC initialization/runtime failures, Ethernet initialization/link loss and 30-second link/IP startup timeouts, API failures, MQTT connection/publish/PUBACK failures, and NVS/cache/spool errors.

Ethernet outage messages are emitted once per failure episode, with recovery logged separately. NFC retries log every three seconds. API/MQTT retries retain their existing backoff. Diagnostics contain operation names, local file paths and error codes, never credentials, keys, card contents or API response bodies.

## Management

All routes require the compiled-in username/password, including errors, OTA metadata and upload chunks. Serve the reader's HTTP port only on an isolated management network behind HTTPS; HTTP Basic credentials are not encrypted on the reader-to-proxy segment.

- /config: device name, role, tool ID and enrollment flag. Save and synchronize requires API acknowledgment. A rejected/offline change is not applied.
- Disable enrollment locally now works offline and persists until a successful configuration save.
- /stats and /api/local/stats: connectivity, clock, NFC health and selected nfcController (PN7160/PN532/none), relay, cache, memory and audit-loss counters.
- /cache/cards: search UID/holder, 25 records per page, status, expiry, FirstScan and enrollment presence. Never displays keys.
- /cache/checkouts: search member ID and paginate the configured tool's grants.
- /update: authenticated ElegantOTA portal. Filesystem updates are rejected. The relay must be OFF; access stays disabled during update. Native signed-image verification is required in production.

Enrollment is allowed on any role, but only for cards already assigned in Rails. Unknown cards are recorded as rejections and are not modified. Supervise initial enrollment physically, then turn the enrollment flag off.

## Security limits

MIFARE Classic Crypto1 is broken. Unique keys and a protected random credential provide substantially more checking than a UID, but cannot make Classic clone-resistant against a capable attacker. For stronger protection, migrate to a card family supporting modern cryptographic challenge/response, such as DESFire EV3, with a separate protocol implementation.

Readers hold read keys for cached cards and must be trusted devices. Production flash encryption protects those secrets at rest; signed firmware protects the execution path. Neither replaces physical protection or revocation policy. During outages, cached authorization remains valid until expiry; a reader cannot learn new revocations while disconnected.

## Validation

```powershell
./scripts/test.ps1
./scripts/build.ps1
```

The host cache fixture measures 179,308 bytes for 300 maximum-size cards and 150 grants, below the 320 KiB per-snapshot limit. These tests and compilation do not validate RF behavior, actual relay timing, power-loss recovery, or signed production OTA. Record those results using docs/COMMISSIONING.md.

Companion ts-data-api setup and contracts are in its READER_API.md. Deploy the API and indexes before configuring readers. The wiki draft under docs/wiki is held until the system is commissioned and released.


