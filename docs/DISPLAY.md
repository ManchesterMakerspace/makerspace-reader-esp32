# Optional display service

The LCD is optional. The reader still authorizes cards, drives the relay, records events and sounds the buzzer without it. Only DisplaySerLcd.cpp includes SerLCD.h.

## Screens and timing

Rows are padded to 16 columns to remove old text. Row 1 reserves its last five columns for time; labels occupy the first eleven.

| State | Row 1 label | Row 2 | RGB |
| --- | --- | --- | --- |
| Starting (2 seconds) | Starting | Reader | 0,0,255 |
| Authorized (2 seconds) | Welcome | First name | 0,255,0 |
| Membership expired (3 seconds) | Membership | Expired | 255,0,0 |
| Checkout expired (3 seconds) | Checkout | Expired | 255,0,0 |
| Unknown/revoked (3 seconds) | Unknown Fob | blank | 255,0,0 |
| Other denial (3 seconds) | Access | Denied | 255,0,0 |
| Membership warning (3 seconds) | Membership | Expires in Nd | 255,128,0 |
| Checkout warning (3 seconds) | Checkout | Expires in Nd | 255,128,0 |
| Enrollment pending | Enrolling | Keep holding | 255,128,0 |
| Enrollment succeeded (2 seconds) | Enrollment | Succeeded | 0,255,0 |
| Enrollment failed (3 seconds) | Enrollment | Failed | 255,0,0 |
| Pulse active, messages finished | Access on | Hold fob to read | 0,255,0 |
| Idle | blank | Hold fob to read | 16,16,16 |
| Idle with error | blank | Error: CODE | 128,64,64 |

An enrollment result is published around the existing enrollment function, so failure at API begin, RF writing/verification, or commit always terminates progress. Success still requires verification and API commit. Success precedes the final scan result; failure suppresses the following generic denial. New presentations replace older sequences; delayed older results are ignored.

The first whitespace-delimited holder word is clipped to 16 safe ASCII characters. Control bytes, non-ASCII bytes, and the SerLCD command prefix "|" are removed. No credentials, UID, card contents or persistent records enter the display mailbox.

Warnings follow the greeting, membership first. The final authorization lock protects capture of the effective card/membership expiry and the tool grant's actual cached "until" value. Positive remaining validity of at most 72 hours / 336 hours triggers membership / checkout warnings. Days round up. Missing, nonpositive or distant unbounded checkout deadlines do not warn. No API/cache schema is added.

## Clocks and health

Without a running relay, trusted UTC is shown as New York HH:MM. The presentation-only conversion uses the current US rule (second Sunday in March / first Sunday in November, effective since 2007), including both DST transitions. It never changes process TZ, RTC values or authorization timestamps. The clock is blank without trusted time.

Pulse roles show ceiling-rounded remaining seconds from the existing monotonic deadline. Tool runs show monotonic elapsed HH:MM from activation, saturating at 99:59. Relay OFF removes these fields on the next render. An authorization refresh can change a tool deadline without resetting its elapsed-time origin. Display code never writes relay state or extends a deadline.

After transient messages, errors take priority over the idle/pulse prompt: NET (no link/IP), DB (API unavailable or latest operation failed), MQ (MQTT disconnected/uninitialized), then NTP (no trusted authorization time). Deliberately stopped NTP is healthy when trusted time remains available. Recovery restores the prompt automatically.

## Backend and concurrency contract

- Display.h defines copied presentation events, status snapshots, a pure Model, frames and Backend. Display.cpp contains layout and timing; DisplayService.cpp adapts FreeRTOS/state snapshots; DisplaySerLcd.cpp owns all LCD hardware calls.
- The bounded mailbox retains both enrollment terminal state and final scan result. Producers only copy fixed-size data under a short critical section; they never wait for rendering, I2C or message durations.
- The display task runs at idle priority and polls at 50 ms. Reader-state and I2C mutex acquisition are nonblocking. No state/mailbox lock is held during LCD I/O. Busy state or bus defers the frame.
- SerLCD probes 0x72 once after Wire setup, using the shared 100 kHz bus and its existing 50 ms transfer timeout. Absence selects NullBackend. A busy bus defers detection.
- The library performs one startup clear. Subsequent renders use only changed rows/colors, setFastBacklight(), and at most one color or padded row per pass. Existing library delays and bounded transfers run only in the display task. An already-started I2C transfer cannot be preempted; NFC contention must be verified on the bench.
- To replace the display, provide detectBackend() returning a Backend (nullptr means retry detection, NullBackend means absent). Backend::draw receives two NUL-terminated padded rows and 24-bit RGB. It returns true only when the complete frame is applied, false for busy/partial work. Preserve differential writes and bounded nonblocking lock acquisition; do not acquire stateMutex from a backend. No NFC/network/relay changes are needed.
- Timing uses monotonic milliseconds supplied to the model. A display starved by NFC activity may skip expired transient screens; it must never hold up authorization to show them.

## Validation and bench acceptance

Host coverage lives in tests/display.cpp and tests/display-backend.cpp; the latter compiles the production SerLCD backend against bus/LCD fakes. Existing relay tests check deadlines and shutdown behavior. Run scripts/test.ps1 and scripts/build.ps1.

Physical acceptance remains to be performed on a board:

- [ ] LCD present: starting screen, colors, correct 16-column positions and no repeated clearing/flicker.
- [ ] LCD absent/unplugged: NFC, buzzer, relay expiry and audit delivery still operate.
- [ ] Scan during display updates and enroll while display wants the bus: no lock inversion, NFC transport failures or relay timing changes.
- [ ] Enrollment failure at API begin, RF write, RF verification, and API commit: progress ends red for three seconds; success is green for two seconds before scan feedback.
- [ ] Membership/checkout warning boundaries, simultaneous warnings, long/empty/unsafe names, rapid new presentations and stale text removal.
- [ ] NET, DB, MQ, NTP individually and together, then recovery in priority order.
- [ ] Winter/summer and DST boundaries using a controlled trusted-time source; RTC/authorization stay UTC.
- [ ] Measure all four pulse durations during greetings/warnings and network outages. Verify fresh scan resets, expiry removes countdown, tool elapsed clock survives grant refresh and clears on OFF.
