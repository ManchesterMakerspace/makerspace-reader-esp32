# Reader protocol and persistent storage

## Meaning of “the API”

The API is **ts-data-api**, the Node HTTP service in the sibling project. Its /reader/v1 routes use the official MongoDB Node driver with server-held Atlas credentials. The ESP32 never runs a MongoDB driver, stores an Atlas password, or receives permission to run arbitrary database queries.

The ESP32 authenticates with its own TLS client certificate and private key. A trusted gateway verifies the certificate against the reader CA and overwrites internal authentication headers with its SHA-256 certificate fingerprint and a gateway-to-API secret. The service maps the fingerprint to enabled:true in nfc_readers. The service must be network-accessible only through that gateway. The browser's compiled-in username/password is a separate management credential.

DEVICE_ID must match the server's immutable reader ObjectId. Names may change; old names are retained server-side so delayed events still replay. The API rejects a configured identity mismatch and the reader blocks cached admission on verified device authorization rejection.

## Classic credential format

Sector 1 is reserved in full:

| Block | Contents |
| --- | --- |
| 4 and 5 | 32 cryptographically random credential bytes |
| 6 | MSP1, big-endian version 1, zero padding to 16 bytes |
| 7 | Unique random Key A, access bytes 78 77 88, GPB 00, unique random Key B |

Access conditions permit data reads with A/B and writes with B; Key B is not readable. The expected digest is SHA-256(raw UID bytes || all 48 data bytes). A scan must complete the selected controller's Classic authentication exchange using Key A, read the three blocks and access bytes, and match the digest. Merely copying a UID fails. Reading and cloning the full credential after defeating Crypto1 remains possible: this format does not repair Classic cryptography.

Only an enabled enrollment station can obtain pending Key B. The server generates and encrypts pending material before RF writes. Repeated begin calls return the same material. Blank factory-key sectors and partially written blocks matching that pending material can resume; unrelated nonblank sectors are not overwritten. After trailer replacement, the station reactivates the card and verifies reads using Key A before commit. Failed writes or lost connectivity leave a pending enrollment for a supervised retry. A damaged trailer may require replacing the card.

Enrollment transactions write fences on both the reader and assigned card, so concurrent station disable/reassignment/release cannot silently commit the old binding. Normal lookup requires the same card ObjectId, UID and member assignment as the active credential. Commit is an attestation by the trusted reader, not remote proof of RF possession.

## NFC transport selection and IRQ

PN7160 at 0x28 has priority. If it is absent, the station selects PN532 at 0x24; GPIO17/18 are shared alternative IRQ/reset connections, so install one module. A failing controller cannot authorize a card. Controller selection and health appear separately in authenticated statistics.

PN7160 rising IRQ wakes the NFC task; it checks HIGH before calling discovery or reading a response. PN532 falling IRQ indicates ready data and is checked LOW before every read. Both paths check levels before sleeping and after acquiring the I2C lock, so an already-asserted line is serviced without waiting for another edge. The ISR only sends a task notification. The 100 ms idle timeout updates removal/maintenance state without an NFC I2C read.

The included PN532 transport implements [NXP UM0701-02](https://www.nxp.com/docs/en/user-guide/141520.pdf): normal SAM mode, ISO14443A discovery, Classic authentication/block operations and target reactivation. Discovery consumes the command ACK and then waits asynchronously for the target response. Each response checks preamble, length, checksums, response command, status and bounds before exposing data. A command ACK alone never counts as successful authentication or writing. Classic 1K/4K SAKs and 4/7-byte UIDs are supported; other detected Type A tags are rejected. Extended frames and unsupported protocols fail closed.

Both transports authenticate sector 1 with Key A, read the credential and trailer, and use the same digest/authorization path. Enrollment uses Key B/factory-key recovery and verifies new credentials before commit. PN532 reactivation retains the controller's selected target; PN7160 additionally checks the returned UID. No controller selection enables UID-only admission.

## Cache choice

RAM unordered_maps provide UID-to-key/expiry/card and member-to-tool-grant lookups. Maximums are 300 cards and 150 grants. The maximum-size fixture uses **211,315 bytes** of MessagePack; realistic shorter names use less. LittleFS snapshots avoid SQLite overhead and hundreds of separately allocated flash files. NVS holds counters, small configuration markers and the generated 32-byte device cache key.

Each snapshot contains configuration, cards, FirstScan, and grants. AES-256-GCM uses a unique device cache key, a nonce reserved durably in NVS before encryption, and the pathname as associated data. The next snapshot is written via a temporary file to the inactive generation, read back and authenticated, then selected with a durable NVS generation counter. At startup only that committed generation is loaded; a corrupted current snapshot never silently rolls back revoked permissions.

If CACHE_KEY_HEX is undefined, startup checks the reader namespace's actual NVS entry count and the filesystem. Only an empty namespace plus no files (an empty /events directory is allowed) permits generation. The boot entropy setup enables the ESP32-S3 internal entropy source before esp_fill_random fills the key. Preferences.putBytes commits reader/cacheKey; length, complete readback and byte equality must succeed before encryption or worker tasks use it. Both sealRead and sealWrite use the resulting RAM key.

Subsequent boots reuse the exact-length blob without RNG calls or NVS rewrites. An interrupted commit leaves either no usable blob on otherwise unused storage or a complete blob reused on reboot; malformed blobs are never replaced automatically. A valid compiled key remains supported for legacy stations without a blob. When both sources exist, they must match. An explicitly invalid compiled value does not request random generation. Removing a legacy compiled key requires deliberate reprovisioning.

Persisted keys in production require CONFIG_NVS_ENCRYPTION and the secure-boot/flash-encryption profile; [Espressif's NVS encryption initialization](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/storage/nvs_encryption.html) protects default NVS using the existing nvs_keys partition. Ordinary bench NVS is unencrypted. No key material appears in HTTP statistics or logs. Failed initialization leaves the relay LOW and starts no NFC or background synchronization task. Management remains available, but configuration mutations are blocked until recovery and restart.

This is crash-consistent application storage, not protection against physical replay of all flash/NVS. Never restore counters to older values while retaining the same encryption key. After a destructive reset/reprovision, rotate the cache key and device identity before reusing event sequence numbers. Firmware OTA must preserve NVS, partition offsets and LittleFS.

An unmounted filesystem can be formatted only when READER_FORMAT_NEW_FS is enabled, the reader namespace has no entries, and the entire LittleFS partition reads as erased (0xFF) flash. A mount failure on non-erased storage is not permission to format. Existing files without a valid nonzero NVS nonce counter fail initialization even with a compiled key. Used/corrupt stores are not automatically formatted. Keys and caches are not exposed in the management UI.

Fifteen-minute HTTPS refresh and MQTT hints keep the RAM maps current. Cache misses or stale entries perform an authoritative lookup. READER_REFRESH_INTERVAL_MS is a compile-time override shared by periodic snapshots and card-cache freshness; it defaults to 900000 milliseconds (15 minutes). MQTT-triggered refreshes retain their existing retry/debounce behavior. A verified unknown card replaces its cached permission; duplicate UID/capacity errors fail instead of delivering a truncated authoritative inventory. During transport outages, previously cached permission remains usable until expiry. Unobserved revocations cannot take effect offline.

FirstScan is station-local UTC epoch milliseconds, initially 0. A physical presentation, even a rejection, records it once; API/MQTT payloads never overwrite it. With no trustworthy time, pending=true distinguishes an observed card from never scanned. If trusted time arrives in the same boot, elapsed monotonic time reconstructs the first observation. If that boot ends first, the original timestamp remains unknown (0/pending), rather than inventing an epoch. Tombstones preserve observations for removed cards while space remains within the 300-entry bound.

## Time and authorization

A valid battery-backed PCF8523 seeds trusted UTC at startup. Otherwise NTP provides only enough approximate time to validate TLS; HTTPS serverTime establishes authorization time. Once trusted, authorization advances from a monotonic timer, independent of NTP wall-clock changes. NTP is stopped after HTTPS confirmation. A cold boot without RTC or API time denies admission.

Both card validity and current member status must be activeMember. Effective expiry is the earlier of card and membership expiry. Starting a tool additionally requires an available tool/shop and a current, non-revoked, non-superseded checkout. An out-of-service flag alone does not revoke a running session. Checkout dates expire at the start of the next day in America/New_York, including DST; null means non-expiring, bounded by card expiry.

## Audit spool and MQTT

Pulse-reader scans create one immutable decision event. Tool sessions create a checkin on start and a checkend on termination; a pending kick confirmation creates neither. No replay can operate the relay. A persisted sequence plus device identity generates a stable 12-byte ObjectId; filenames include a padded sequence for oldest-first eviction. HTTP inserts are idempotent; a conflicting reuse of an ID is rejected.

Each encrypted LittleFS event has independent dbAck and mqttAck flags. Successful checkins require Mongo acknowledgment and MQTT QoS 1 PUBACK before removal. Rejections require Mongo acknowledgment only. Delivery is at least once: a lost acknowledgment can produce a duplicate MQTT message, so consumers deduplicate on _id. A database rejection never discards the record. Retries rotate through queued events so one permanently rejected record does not block later records.

Before appending, even offline, enforce a 3 MiB spool allocation budget and reserve physical space for snapshot work. Oldest queued entries are discarded when necessary; auditLoss persists in NVS and admission continues. Interrupted temporary files are not delivered. The actual number of queued records depends on LittleFS block/metadata overhead. A flash write failure increments audit loss; storage failure cannot guarantee an audit record.

Publish activity/checkins, QoS 1, retain=false. Payload is canonical Extended JSON equivalent to the stored BSON document: _id/deviceId ObjectIds, time Double milliseconds, delay Int32(0), timeOf Date, uid, holder, validity, where, reader, reason and timeQuality. timeOf is null and time is 0 for a rejection before trustworthy time exists. Successful admission always requires trustworthy time.

The service receives plain JSON fields and constructs BSON types. Existing events are never upserted with new content. delay remains zero so MQTT and Mongo contain the same immutable scan-time data.

Subscribe to cards/# and readers/all/invalidate. Both plain JSON and the supplied “update epoch {document: ...}” envelope are accepted as refresh hints on cards/update and cards/insert (also delete). MQTT cannot install keys, restore permissions or alter FirstScan. A hint for the active UID immediately stops a running tool; hints without a UID and readers/all/invalidate conservatively stop any running tool, including on checkout changes. This may stop a tool for a non-revoking update. Lost hints are recovered by periodic HTTPS refresh. Applying an authoritative lookup or snapshot rechecks the active tool under the state lock: a missing/revoked card, removed credential or expired checkout stops it, and a changed effective expiry resets its Ticker countdown.

## OTA and concurrency

All management routes use authentication middleware. OTA chunks also authenticate directly because upload callbacks can run before ordinary route middleware. Same-origin referrer plus a per-boot HttpOnly SameSite cookie guards the stock OTA portal; configuration POSTs require a per-boot form token. The management proxy must preserve Host and Referer.

The relay must be off before OTA or configuration changes. Uploads hold maintenance mode, reject filesystem images, and time out after 120 seconds without activity. A successful firmware update reboots; failed updates release maintenance. Production requires the SDK's signature-verifying image activation path. With SDK rollback enabled, a new image stays in maintenance until NFC/storage/security health checks pass after 30 seconds; it rolls back after 120 seconds if they do not.

Sources: [Arduino as an IDF component](https://docs.espressif.com/projects/arduino-esp32/en/latest/esp-idf_component.html), [ESP32-S3 secure boot](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/security/secure-boot-v2.html), and the pinned vendor source under vendor/libraries.



## Tool sessions (gateway and firmware upgrade)

Tool readers maintain one owner and require a second presentation within 15 seconds
for another eligible member to kick that owner. Eligible kickers have a current tool
checkout, manage its shop, or hold board/admin status; every card still requires
active, unexpired membership and credential verification. Kicking only logs off.
Tool starts remain subject to a checkout and tool/shop availability.

Encrypted /tool-session stores immutable start/end identities and routing context.
Relay shutdown precedes asynchronous end persistence. A boot never restores power;
an unfinished journal produces a reboot checkend with unknown end time (0), unless
an end was already persisted. Storage failure blocks new starts until resolved;
physical power loss and flash failure can still prevent exact timing/audit recovery.

The checkend collection mirrors checkins and adds checkinId, kicked, kickedBy
(memberId, uid, holder), and an end reason. Tool context includes memberId, shopId,
toolId, shopName and toolName as strings. Stable _id values make replay idempotent.
Tool MQTT topics are activity/checkins/<SHOP>/<TOOL> and
activity/checkends/<SHOP>/<TOOL>; percent, slash, plus and hash are percent-escaped.
Both deliveries require Mongo acknowledgment and MQTT PUBACK. Non-tool topics stay
activity/checkins. The session preserves names even when the catalog is renamed.

Tool and shop out-of-service flags are independently persisted in NVS, scoped by ID.
They block starts without interrupting an active session. A true MQTT hint latches
immediately; false hints request an authoritative snapshot before clearing. Gateway
snapshots retain current grants during an outage and distinguish enabled from
available. Subscribe to tools/# and shops/# in addition to existing topics. Deploy
the gateway and broker ACL changes before this firmware. Earlier gateway responses
without enabled deny tool starts. No environment variables are added.
