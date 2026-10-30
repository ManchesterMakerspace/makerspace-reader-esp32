# Provisioning implementation status

This implementation is incomplete. Do not deploy it as the planned provisioning release.

Implemented in the reader: resolved flash-size profile selection (4/8/16 MB), isolated staged builds, partition generation and bounds tests, 10% image-headroom check, build/physical-flash statistics, startup layout validation, profile-specific storage limits, scoped compact refresh, persisted card recency and privileged/session-owner pinning. GitHub firmware CI has a three-profile matrix.

Implemented in ts-data-api (C:/Users/passp/Documents/Codex/2026-10-27-NFC/ts-data-api): eligibility filtering before snapshot capacity checks, scoped recent snapshot endpoint, privileged-card inclusion, duplicate eligible UID protection, and current tool grants in card lookup. Pre-existing gateway changes were retained.

Validation: complete reader host suite passed, including production cache policy and profile-specific maximum snapshot sizes (95,263 bytes compact / 211,315 bytes full); gateway TypeScript build and all 30 reader tests passed. A firmware compile was started before the latest compact-cache edits; it does not validate those edits. All three final firmware builds and Linux host checks remain required.

Still required: encrypted NVS credential bundle; removal of individual compiled credentials; bootstrap/individual gateway policies; registration/activation and renewal; OpenSSL issuance wrappers; encrypted MongoDB credential vault and key rotation; recovery tickets, nonce epochs, counter recovery and random ordinary-event IDs; inventory/revocation/reactivation CLI; provisioning web UI; migration and commissioning documentation; lifecycle, recovery and fault-injection tests. These must not be described as implemented.

Additional review required: end-to-end scoped-refresh races (including grant revocation racing a prior refresh), card-capacity failure recovery, targeted lookup privilege filtering, and metadata/partition hardware checks. BuildInfo.cpp uses compiler date/time/file plus an always-regenerated staged BuildMetadata.h. No signing keys, database production records, physical device flash, or deployment settings were changed.
