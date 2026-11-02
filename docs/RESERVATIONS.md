# Tool reservations

Reservations are advisory: they never change access eligibility, relay deadlines,
or the two-scan kick policy. They operate only in Tool mode, with trusted UTC.

## Build and deployment

`ReservationConfig.h` defaults `READER_ENABLE_RESERVATIONS` to 1. Define it globally
as 0 to remove the reservation model/service, journal handling, subscriptions,
display fields and scores. Arduino CLI examples:

```powershell
./scripts/build.ps1 -Reservations 1
./scripts/build.ps1 -Reservations 0
```

The disabled output is under `build/profile-16-no-reservations` (or the selected
4/8 MB profile). For Arduino IDE edit the default in `ReservationConfig.h`; do not
define the flag only in the `.ino`, since each `.cpp` is a separate translation unit.
CI covers all flash sizes with/without reservations and optional Wi-Fi.
Disabled builds also inspect the linked ELF and reject reservation services,
playback functions, or score symbols; every build enforces 10% OTA headroom.

The 4 MB build script enables C++ link-time optimization (`-flto`, with `-Os`
at link time) to fit Wi-Fi and reservations together. C sources keep the core's
normal settings because its panic-handler assembly cannot use LTO. The 8/16 MB
compiler settings, all features, and partition layouts are unchanged. Generate
compact images with `scripts/build.ps1`; it checks the actual application `.bin`
size and prints the remaining headroom and exact byte budget.

Deploy Rails reservation lifecycle support first, then ts-data-api, the required
indexes and MQTT ACLs, then the portal and firmware. Configure the API's
`READER_RAILS_URL` and the same >=32-character `READER_RESERVATION_SERVICE_SECRET`
on Rails/API. Neither value belongs in firmware. Keep the Rails internal route
private. No database or certificate credentials are changed by this feature.

Readers advertise `reservationEnabled` in `nfc_readers`. Register all participating
readers before enabling automatic abandonment: each booking freezes the available
equipped readers when monitoring starts. Unequipped tools do not prevent monitoring.
Disabling, moving or losing a participating reader does not silently remove its
attendance obligation; an administrator may need to cancel the booking normally.

## Data and timing

`GET /reader/v1/reservations` returns only the registered tool's approved bookings
overlapping now or starting within four hours, including whole-shop reservations.
Group bookings already contain expanded physical tool IDs. Effective reservability
includes the tool, its shop/groups and existing future bookings. A negative result
is persisted and rechecked after 24 hours. Failed queries never create negative cache entries.

Reads occur on boot/reconnect, MQTT hints and every minute. Pages contain 32 records;
the device retains at most 64, reports incomplete inventory, and suspends claims and
coverage checkpoints if the complete window cannot be represented. MQTT data is
only a hint; identity and times come from the authenticated API.

The arrival interval is `[start - 60 minutes, start + 15 minutes]`, bounded by the
exclusive reservation end. Already-running owner sessions qualify when the window
opens. Other active reservations prevent early association. Attendance claims refer
to immutable tool check-in IDs, and Rails validates owner/scope/time and revisions.
Voluntary logoff releases the entire booking as `cancelled / released_on_logoff`.
A kick, reboot or automatic authorization expiry does not release the booking.

After the inclusive late-arrival boundary, an unclaimed notice disappears locally.
Rails marks it `abandoned` only after every frozen reader has uploaded attendance
through that boundary. Offline devices defer release. Checkpoints cannot overtake
pending attendance or unacknowledged session events. Corruption, audit loss, overflow,
scope changes and missing coverage prevent absence-based release. Fees are retained.

The encrypted `/reservations` journal holds tracked bookings, arrival associations,
negative capability cache, coverage provenance and at most 64 pending operations.
API acknowledgment precedes removal; replay is idempotent. Journal capacity/read
failures mark reservation coverage uncertain without changing tool authorization.
Changing tool/role starts a new coverage interval and preserves any existing loss
uncertainty. Rails invalidates the old scope's checkpoints using its frozen reader
configuration revision; a healthy reassignment can monitor new bookings normally.
Failures of the shared cryptographic key/nonce mechanism still follow the existing
storage safety policy. No reservation journal contains card credentials.

## Presentation

Idle alternates the usual display and current/next reservation every eight seconds.
During use, the earlier of the owner's reservation end and another owner's start
is shown as `Ends in HH:MM:SS` or `Next in HH:MM:SS`. A reached competing start
alternates `Reservation due` with `Next in 00:00:00`; it does not shut off power. Enrollment, kick prompts and
out-of-service indications retain precedence.

The four-hour warning flashes red three times, 250 ms per phase. New records learned
while checked in also request the Donkey Kong Game Start melody; the initial inventory is
silent. Threshold warnings play Think! once at 15 minutes and again at five minutes;
first discovery inside five minutes coalesces missed thresholds into one warning.
Scan feedback interrupts and resumes the reservation score. The display is red only
while Think! actually plays, including rests. Scores use finite hardware note durations.

Authenticated statistics include `reservations.enabled`, `reservable`, `complete`,
`coverageUncertain`, `pending`, `cached`, `tracked` and `lastCheckedAt`. An uncertain
checkpoint is not evidence of a no-show; resolve the cause and handle any affected
booking through the portal instead of clearing coverage records to force release.

## Local validation (2026-11-02)

- `scripts/test.ps1`: passed the full host suite, including reservation boundaries,
  encrypted-journal adapter/replay, display priorities, audio preemption, original
  session recovery, and relay/NFC checks. Focused adapter tests also cover retirement
  of centrally resolved expired records while retaining unresolved monitored bookings.
- ts-data-api: 108 disposable-MongoDB tests passed; representative reservation
  query plans used indexes with no collection scan (2 applicable rows among 2,000
  unrelated records, fewer than 20 documents examined).
- Rails: lifecycle, service, request, serialization, payment and job tests passed;
  the final service/private-API run had 25 examples and zero failures. Swagger was
  regenerated from executable specifications. Concurrent readers exercise atomic
  conflict/retry behavior; offline attendance prevents premature abandonment.
- React: 36 focused tests passed, along with typechecking and the production build.
  Browser fixtures passed at 320, 600, 900 and 1280 px, including keyboard navigation,
  terminal-history visibility, action restrictions and horizontal-overflow checks.
- The held wiki patch passed `git apply --check` against the retrieved wiki pages.
  No production data, deployment, firmware flashing or wiki publication was performed.

Initial Ethernet-only firmware measurements, before the compact LTO fix
(application `.bin` bytes, excluding bootloader/partition images). These did not
cover the Wi-Fi plus reservations combination that subsequently failed CI:

| Flash | Reservations | Application bytes | OTA slot bytes | Headroom |
| --- | --- | ---: | ---: | ---: |
| 4 MB | enabled | 1,195,360 | 1,703,936 | 29.85% |
| 4 MB | disabled | 1,152,784 | 1,703,936 | 32.35% |
| 8 MB | enabled | 1,189,776 | 2,097,152 | 43.27% |
| 8 MB | disabled | 1,147,216 | 2,097,152 | 45.30% |
| 16 MB | enabled | 1,189,776 | 4,194,304 | 71.63% |
| 16 MB | disabled | 1,147,232 | 4,194,304 | 72.65% |

All disabled ELFs passed the reservation-service, MQTT-state and melody symbol
checks. The final scope-coverage adjustment is entirely inside the feature guard;
preprocessing confirms that translation unit remains empty in disabled builds.

### Compact CI build fix (2026-11-02)

The 4 MB + Wi-Fi + reservations CI variant exceeded the 1,533,542-byte budget.
C++ LTO now preserves every feature while fitting the existing 1,703,936-byte
slot; the 10% requirement is unchanged. Validation uses Linux, the pinned core
3.3.12/libraries, and CI's placeholder Wi-Fi credentials:

| 4 MB transport | Reservations | Application bytes | Headroom |
| --- | --- | ---: | ---: |
| Ethernet + Wi-Fi fallback | enabled | 1,461,472 | 14.23% |
| Ethernet + Wi-Fi fallback | disabled | 1,423,472 | 16.46% |
| Ethernet only | enabled | 1,108,432 | 34.95% |
| Ethernet only | disabled | 1,070,608 | 37.17% |

The full Linux host suite also passed with `-Os -flto`. Its session-service test
now compiles both translation units with reservations disabled, avoiding
inconsistent display structure definitions. Firmware builds retain the disabled
reservation symbol check. Physical boot, NFC/audio/display concurrency, network
handoff and OTA checks remain part of hardware acceptance below.

## Hardware acceptance (required before rollout)

The requested replacement for the Gauntlet cue is the classic Donkey Kong
Game Start tune. It and Jeopardy! are implemented as single-tone arrangements.
The Donkey Kong arrangement follows the opening phrase and closing trill in
[MARIOMaSTeR's MIDI](https://themushroomkingdom.net/sounds/mid/dk_gamestart.mid),
raised two octaves for the buzzer. Physical listening and concurrency checks
remain required before production rollout.

- Listen to both cues and verify the intended melody and comfortable volume.
- Scan during warnings and check that playback resumes, red clears correctly, and
  NFC, enrollment, kicks and relay deadlines remain responsive.
- Disconnect each participating reader across the arrival deadline, arrive offline,
  reconnect and verify no booking is abandoned before attendance synchronizes.
- Exercise early logoff, new/edited/cancelled bookings, missing LCD/buzzer, reboot,
  changed tool scope, network recovery and storage exhaustion.
- Verify enabled/disabled firmware size and at least 10% OTA slot headroom on each
  flash profile. Ordinary OTA must not change partition layout.
