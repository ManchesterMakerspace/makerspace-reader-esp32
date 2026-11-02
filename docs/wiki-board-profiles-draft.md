# Operator wiki update: board profiles (HOLD FOR DEPLOYMENT)

Apply alongside the existing wiki-reader-rollout.patch only after the compatible
API and firmware are deployed and physical acceptance is recorded. This is a local
draft; no live wiki page has been changed.

Add to the NFC reader configuration/commissioning page:

- Supported boards now include Olimex ESP32-PoE 4 MB WROOM/no PSRAM and Adafruit
  QT Py S3 4 MB/2 MB PSRAM. Wiring and build commands are maintained in the reader
  README and application BoardConfig.h; do not substitute another memory variant.
- QT Py uses STEMMA QT and station Wi-Fi, with both credentials configured before
  flashing. Olimex SD is unused. Wired readers keep Ethernet priority.
- Open authenticated Configuration to view board pins, memory budgets and device
  detection/driver health. Viewing does not probe hardware or display secrets.
- Only QT Py permits station-Wi-Fi configuration edits. Existing authentication,
  CSRF, stopped-relay and server approval requirements remain in force.
- The reader stores up to 16 current/eight-hour bookings for its own tool. Four-hour
  alerts and existing arrival/warning timings are unchanged.
- Never format storage to fit smaller budgets. An incompatible reservation journal
  is retained and mutations suspend. A coverage-gap warning requires booking
  reconciliation; it is not proof of no-show and does not deny ordinary tool use.
- First-flash partition installation and physical acceptance remain commissioning
  tasks. Firmware compilation does not authorize flashing or production rollout.
