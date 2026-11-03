# Makerspace reader agent handbook

Adapted from the [Makerspace Rails handbook](https://github.com/ManchesterMakerspace/makerspace-rails-2026/blob/master/AGENTS.md), with firmware-specific commands and obligations.

## 1. Repository purpose and boundaries

This repository implements ESP32 access readers, NFC enrollment, relay sessions,
offline authorization caches, audit replay, optional Tool reservations, and local
management pages. Rails owns reservation lifecycle mutations; ts-data-api scopes
device reads and forwards authenticated synchronization operations.

Inspect `git status --short` before editing. Preserve existing changes and secrets.
The reader, ts-data-api, Rails, and React are independent checkouts. Use explicit
working directories and read each repository's handbook before editing it.
This root file is the canonical reader handbook; do not assume other agent
instruction files exist.

## 2. Setup and common commands

Use PowerShell, Arduino CLI, and the pinned core/libraries in `scripts/setup.ps1`.
Run `./scripts/setup.ps1` to install dependencies, then configure the ignored
`MakerspaceReader/secrets.h` using `secrets.example.h`. Never print or commit real
credentials, certificates, keys, or credential-bearing firmware images.

| Command | Purpose |
| --- | --- |
| `./scripts/test.ps1` | Host tests; requires a C++ compiler and installed ArduinoJson headers. |
| `./scripts/build.ps1 -Reservations 1` | Staged default firmware build with reservations. |
| `./scripts/build.ps1 -Reservations 0` | Build and verify reservation compile-out. |
| `./scripts/build.ps1 -Fqbn '<supported board options>'` | Board-specific build; use the documented configuration. |
| `git diff --check` | Whitespace validation, including documentation changes. |

Scripts and CI are authoritative for dependency versions and build commands.
Keep dependency updates scoped. Placeholder credentials permit compilation, not
live integration tests. Use the vendored NFC driver and ElegantOTA patches; do not
substitute similarly named upstream libraries or skip library discovery.

## 3. Architecture and implementation conventions

Keep testable decision models separate from firmware/network adapters. Preserve
task priorities, lock ordering, bounded I2C operations, and asynchronous network
work. NFC/session hooks must not wait for API responses. Relay deadlines and
authorization must remain independent of reservation notifications.

### Mandatory board-header ownership

Maintain `MakerspaceReader/BoardConfig.h` as the sole application-owned source for
board-dependent pins, capabilities, and resource limits. Add or change board
definitions there; do not introduce scattered GPIO literals, duplicate pin maps,
or board-name checks in services. Hardware adapters consume the header's settings
and capabilities. Keep partition layout definitions separate. Update affected
tests, build coverage, startup diagnostics, `/config`, and wiring documentation in
the same change.

Serial and web diagnostics must derive from the same configuration/inventory used
by drivers. Do not duplicate the complete pin table in this handbook. Adding a
board requires GPIO conflict/output-capability checks, memory-budget validation,
and reservation-enabled/disabled builds. Arduino core board definitions remain
toolchain inputs; validate them against application expectations.

Preserve durable, idempotent audit/attendance operations. Missing local attendance
never proves abandonment. Preserve encrypted journal contents through failures,
upgrades, and profile reductions. Never weaken authentication, relay timing,
production security, or the 10% OTA-headroom requirement to make a build pass.

## 4. Documentation maintenance

Documentation is part of implementation. Apply every relevant row:

| Change | Update |
| --- | --- |
| Board, pins, memory, build options | `README.md`, commissioning instructions, affected profile tests and CI. |
| Configuration or credentials | README configuration instructions and `secrets.example.h`; document defaults without secrets. |
| HTTP/MQTT or persistence contracts | `docs/PROTOCOL.md`, affected API tests and companion documentation. |
| Audio or display behavior | `docs/AUDIO.md` or `docs/DISPLAY.md`. |
| Reservations | `docs/RESERVATIONS.md`, including lifecycle and overflow semantics. |
| Validation or rollout | `docs/VALIDATION.md` and `docs/COMMISSIONING.md`; distinguish measured results from pending checks. |
| Operator-facing workflows | Prepare matching [WikiPages](https://github.com/ManchesterMakerspace/WikiPages) changes using test data. |

Preserve exact filename casing. Wiki master publishes immediately: hold unreleased
instructions, do not publish drafts, and report held changes in completion notes.
The current board rollout draft is `docs/wiki-board-profiles-draft.md`.
Keep any future companion agent guidance aligned with this canonical handbook.

## 5. Testing and completion

Start with affected host tests, then run the full host suite for shared behavior.
Build affected board/flash/network combinations with reservations both enabled and
disabled. Check actual binary headroom, partition tables, and compile-out symbols.
Hardware changes require physical startup, NFC, relay, network, I2C, and OTA
acceptance; compilation and mocks do not constitute bench validation.

Report checks actually run, failures, unavailable prerequisites, pending hardware
coverage, documentation/configuration changes, and deployment dependencies.
Documentation-only work needs command/link/casing checks and `git diff --check`,
not application rebuilds.

## 6. Cross-repository changes

Trace firmware contracts through ts-data-api and Rails; inspect React consumers
when lifecycle/status output changes. Preserve authentication, reader/tool scope,
API casing/envelopes, backward compatibility, and Rails mutation ownership.
Use disposable databases and test credentials for API integration tests. Follow
each companion handbook for its executable specifications and generated artifacts.
Deploy compatible server contracts before dependent firmware. A build does not
publish packages, deploy services, or authorize a production data operation.

## 7. Operational constraints and pitfalls

Protect NVS keys, nonce counters, LittleFS, attendance journals, and audit records.
Never format storage, erase NVS, burn eFuses, flash hardware, or run production
maintenance as routine validation. Preserve the distinction between bench and
production security profiles. Ordinary OTA must not migrate partition layouts.

Before deleting apparently unused code, inspect ISRs, callbacks, Arduino hooks,
conditional builds, vendored-library hooks, tests, documentation, and history.
Search companion consumers before removing endpoints or protocol fields. Recent
unconnected work may be unfinished rather than dead. Keep unrelated cleanup and
dependency changes out of the task.
