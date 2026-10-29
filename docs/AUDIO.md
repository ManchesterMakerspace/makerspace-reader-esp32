# Asynchronous buzzer melodies

Audio is optional and uses the SparkFun Qwiic buzzer at I2C address 0x34. The driver owns note duration; the ESP32 schedules note starts, gaps and cancellation. There are no melody delays in the main loop or NFC task.

## Feedback and API

- Accepted scan: Coin, using the supplied 1318/1567/2093 Hz sequence.
- Rejected scan: Mario Died, using the supplied seven-note descending sequence.
- First successful NFC initialization: Power-Up, including success following startup retries. Later recoveries do not replay it.
- Enrollment produces only its final scan feedback, not a second enrollment melody.
- Scan requests remain after card work and audit enqueueing.

Call audio::setup() once after Wire and the shared mutex exist. audio::play(MelodyId, volume = 2) returns immediately: true means a request was recorded, not that playback has begun. Valid melodies are Coin, MarioDied and PowerUp. Volume is 0–4; zero cancels current playback silently. Invalid IDs/volumes return false without replacing a valid request. audio::stop() cancels playback.

The latest request replaces both the active melody and any pending request. Asking for the same melody again restarts it. The mailbox contains only a generation number, melody ID and volume; note tables are immutable and need no caller-owned buffers.

## Scheduling and hardware ownership

Audio.cpp contains the testable sequencer and exact supplied note/duration/gap tables. AudioService.cpp owns the priority-1, unpinned FreeRTOS task and notification mailbox. AudioQwiic.cpp alone owns the buzzer and SparkFun driver calls.

The audio task is below NFC (priority 2) and relay handling (priority 4), above the display (priority 0), and shares priority 1 with network work. It waits using task notifications with a timeout. Producers never take the I2C mutex or perform buzzer I/O.

Each configure/on batch or off operation takes the I2C mutex only if immediately available and releases it before waiting. A busy bus causes a notification-aware 10 ms retry. It uses the existing 100 kHz shared bus and bounded Wire timeout. No reader state lock is taken.

Sounding notes always have positive, finite hardware durations. Frequency zero denotes a rest; the player sends off and waits for the rest duration plus its gap. Durations of zero are rejected to avoid the device's indefinite-tone mode.

Scheduling uses monotonic time after the note command completes. Higher-priority work or bus contention can extend silence between notes. Notes retain their full programmed duration; the player does not skip notes or send catch-up bursts. Task tick rounding can also lengthen gaps slightly.

A notification interrupts the current wait. At the next bus opportunity, replacement sends off before starting the newest melody. An already-started I2C batch cannot be interrupted; at most that batch finishes before the newest request is processed. A delayed worker cannot prolong a note beyond its hardware duration.

## Failure behavior

Missing hardware or task-creation failure disables audio for the boot without changing reader operation. The worker retains a valid task handle when disabled so an in-flight notification cannot target a deleted task. Discovery is logged at Info level.

Configuration, on and off failures log Error messages, abort the melody and issue one bounded best-effort off. There is no retry loop for a failed command. A subsequent request may try again after a runtime I2C failure. No EEPROM settings are saved. The library's blocking sound effects are not used.

## Validation

The host suite includes:
- Exact frequencies, durations and gaps for all three melodies; default/all supported volumes, invalid requests and silence.
- Replacement during notes, gaps and bus contention, same-melody restarts, explicit stop and latest-request overwrite.
- Command latency, delayed scheduling, rests, completion, finite-duration validation and recovery after failures.
- The production Qwiic adapter against fake hardware: missing device, lock contention, configuration/on/off failures and lock release.
- The production service against FreeRTOS fakes: task creation failure, mailbox retention before startup, notification wakeups, tick-rounded waits and disabled hardware.
- Existing NFC, relay, display, policy and storage tests.

Physical acceptance remains to be performed:

- [ ] Listen to all three melodies at volumes 1–4 and verify volume 0 is silent.
- [ ] Trigger another scan during a melody; confirm newest feedback replaces the old sequence.
- [ ] Confirm Power-Up occurs once after initialization, including after initial reader failures.
- [ ] Hold off the worker after a note starts; confirm the hardware automatically silences that note.
- [ ] Exercise simultaneous NFC, display and network work; longer gaps are acceptable, but no main-loop stall or changed relay deadline is acceptable.
- [ ] Verify missing buzzer and runtime I2C failures leave card access and relay expiry operational.
