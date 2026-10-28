# Commissioning and hardware acceptance

No device has been flashed or physically tested by this implementation task. Compilation and host/API tests are not hardware acceptance.

## Provisioning order

1. Deploy ts-data-api with the reader environment settings, least-privilege Atlas access and the authenticated TLS gateway. Run scripts/reader-indexes.cjs after auditing existing reader names/fingerprints for duplicates.
2. Issue a unique TLS client certificate per station. Register its lowercase SHA-256 DER fingerprint, immutable ObjectId, unique name, role, enabled flag and optional tool ObjectId in nfc_readers. Enrollment defaults off. Give the MQTT broker the same reader CA and an ACL allowing only activity/checkins writes and cards/# plus readers/all/invalidate reads.
3. Configure the station's secrets.h. Leave CACHE_KEY_HEX undefined for per-device generation after production NVS encryption is provisioned, or supply a unique 64-hex-digit key. Preserve generated keys, counters and LittleFS during upgrades. Keep an existing compiled key unchanged until deliberate reprovisioning. The web password is distinct from certificates and the gateway secret.
4. For the first bench run, use READER_PRODUCTION=0 and a dummy relay load. Confirm the 16 MB flash / OPI PSRAM board variant, supply rails, pull-ups and relay driver before attaching real equipment.
5. Build the production Arduino core/bootloader using ESP-IDF 5.5 and the supplied security configuration checklist. Standard Library Manager/core installation does not apply sdkconfig changes. Use Espressif's Arduino library builder or Arduino-as-IDF-component workflow; retain the pinned application libraries and partition layout. Verify the generated CONFIG_SECURE_SIGNED_APPS setting and bootloader size/partition offsets. Provision secure-boot signing keys, flash encryption and NVS encryption using the organization's controlled process.
6. Sign each production application with the secure-boot key using the matching Espressif tooling. Upload the signed application image through ElegantOTA; never upload a merged factory image or a filesystem image. Do not change partition offsets during OTA. Keep signing keys offline; only public verification material belongs on devices.
7. Serve port 80 through an HTTPS management proxy on an isolated network. Preserve Host and Referer; allow firmware uploads up to the 4 MiB slot limit. Do not expose the reader's HTTP listener directly to the Internet.
8. Complete every acceptance item below, record the board/module revisions and results, then enable a real access relay. Publish the held wiki instructions only with the deployment.

## Required bench checks

- [ ] W5500 DHCP, cable removal/reconnection, HTTPS hostname/CA checks, correct client identity, broker reconnect and wrong/revoked certificate rejection.
- [ ] PN7160 electrical configuration, IRQ17/VEN18 wiring, initialization, tag detection and reactivation on actual Classic 1K and 4K cards. With a logic analyzer, verify no idle NFC reads while IRQ is LOW; a rising/already-HIGH IRQ wakes discovery. Test lost/stale notifications, disconnected IRQ, stuck IRQ and recovery after an I2C error.
- [ ] PN7160 disconnected: PN532 in I2C mode responds at 7-bit 0x24, with IRQ17 active LOW and RSTPD_N18. Confirm initialization from cold power/LowVbat, asynchronous discovery, 4/7-byte Classic UIDs, Key A reads, Key B enrollment and reactivation. Run wrong-key, credential mismatch, interrupted enrollment and held-card/removal tests on both controllers.
- [ ] PN7160 present but failing initialization does not select PN532; neither module present remains unhealthy and cannot grant access. Check nfcController/health statistics and three-second recovery retries. Install one NFC module; do not tie opposite-polarity IRQ outputs together.
- [ ] Correct Key A succeeds. Wrong key, copied UID with wrong credential, wrong access bits, malformed/short/stale NCI responses and unsupported tags never energize the relay.
- [ ] Enrollment writes only reserved sector 1. Factory blank, partial data write, interrupted trailer write, removed card, API outage after RF write, repeated begin/commit and changed assignment all recover or reject safely.
- [ ] Enrollment disabled locally takes effect; unknown/unassigned cards remain untouched; pending enrollment is supervised. Normal readers never receive Key B.
- [ ] Every role's pulse duration under slow API/MQTT/flash traffic. A held card does not repeatedly toggle; removal for 500 ms permits a new scan. Tool expiry, checkout revocation, tool/shop disable and role changes stop a running tool.
- [ ] All optional I2C devices present individually/together/absent; unplugged device or stuck bus does not prolong a relay pulse. Verify SerLCD contrast/backlight, buzzer duration and RTC oscillator/battery detection.
- [ ] Valid RTC permits authorized offline scans after reboot; missing/stopped RTC plus unavailable API denies them. NTP manipulation cannot extend authorization after time becomes trusted.
- [ ] FirstScan remains unchanged after API/MQTT updates, enrollment, reboot and OTA. A same-boot pending observation resolves when time arrives; a prior-boot unknown time stays explicitly unknown.
- [ ] With CACHE_KEY_HEX undefined, fresh NVS/erased LittleFS creates one 32-byte key and cacheKeyReady becomes true. Reboot and OTA preserve key identity and decrypt existing FirstScan/spool data. Different stations get different keys. Confirm production NVS encryption without exposing keys in logs.
- [ ] Interrupt power before/during/after NVS key commit and readback. Check NVS-full/write failures, malformed blobs, override mismatch and invalid defined keys leave access disabled. Deleting NVS while retaining cache files must not generate a replacement or format LittleFS. A damaged non-erased filesystem must not be formatted.
- [ ] Fill 300 card/150 grant caches and measure free heap/PSRAM. Confirm AES-GCM corruption rejects cache data and no old generation resurrects permission. Inject power cuts before/after temporary-file rename and NVS generation commit.
- [ ] Independent API and MQTT outages; HTTP 401/403/409/500 and Mongo write rejection. Records survive reboot, retries do not duplicate Mongo rows, MQTT duplicates share _id, and replay never activates a relay.
- [ ] Fill flash while offline. Oldest events are dropped, auditLoss increments, snapshots remain writable, and cached admission continues. Interrupt append/ack writes; verify recovery and physical flash wear behavior.
- [ ] Without credentials: /, /config, /stats, /api/local/stats, both cache pages, /update, /ota/metadata, /ota/start, every upload chunk, completion and unknown paths all deny access. Verify wrong credentials, cross-origin start/upload and invalid form tokens.
- [ ] Valid signed OTA succeeds; unsigned/tampered/wrong-key/truncated images, empty upload, filesystem upload and interrupted transfer fail. Relay stays LOW during update, watchdog releases failed sessions, old firmware boots after power loss, and new-image health failure triggers rollback.
- [ ] Secure boot/flash encryption/NVS encryption are actually enabled; the production guard reports ready. Confirm debug/download restrictions and recovery procedure using a sacrificial board before irreversible eFuse operations.

## Operational recovery

API errors retain the spool. Correct configuration and credentials, then allow automatic retry. A verified disabled reader stays blocked across reboot until a successful authoritative snapshot clears it. A rejected enrollment commit remains pending for supervised retry.

Do not “repair” an unreadable cache by restoring old counters. Preserve evidence, reprovision with a new cache key and new device identity if counters are erased, and accept that unsent records may be lost. A full physical backup is sensitive because it contains encrypted credentials and device firmware secrets.

No production credentials, certificate issuance, Atlas mutation, relay actuation, eFuse programming or OTA deployment was performed during software validation.


