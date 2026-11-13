# Held wiki draft: reader deployment and recovery

Do not publish until the deployment and hardware commissioning are complete.

Use the companion ts-data-api deployment runbook for the Compose API, HAProxy and
Mosquitto stack. Register each reader once and copy its generated private header
into the firmware build. All generated TLS certificates have 20-calendar-year
validity. The examples use test names such as workshop-reader-01 and example.org.

Export the encrypted recovery bundle after initialization, reader provisioning,
and commissioning. It includes master/private keys, service secrets, reader IDs,
and firmware headers; include a consistent database backup to preserve enrolled
card credentials. Store the archive and decryption key separately.

To rebuild, restore rather than initialize. Preserve NVS, LittleFS, cache keys,
nonce counters, journals, and reader identities. Retest against isolated DNS,
MongoDB, Rails, and MQTT using the same enrolled card and a dummy relay. Container
tests and firmware compilation do not replace physical NFC, relay, power-loss,
or secured OTA acceptance.
