# Offline charge-session diagnostics

Added to the investigation branch after the confirmed Vehicle-CAN topology
review. This change is local and has not been installed on DORA.

The Tesla advanced battery page now shows:

- Selected emulator profile: normal inverter, charge, or stop/release pending.
- Whether a stop request remains pending, including an explanation that a new
  Prepare to Charge request cannot restart an already-active stop sequence.
- The last received connector proximity report and its age, with an explicit
  not-received state and a display-only stale threshold of two seconds.
- AC charge-line feedback freshness, explicitly distinguished from proof of DC
  voltage, current or shutdown completion.
- Equipment stop, emulator system FAULT and inverter contactor permission.
- Recorded handle/latch/removal evidence during a pending stop, and a notice
  when a recent connector-inserted report coexists with that pending stop.

These are observations, not new permissions or control gates. No CAN payloads,
transmission scheduling, stop/handoff conditions or fault clearing changed.
Historical stop evidence remains distinct from the latest connector report.
The panel does not establish physical contactor position or CP-side receipt of
Vehicle-CAN commands. The existing Events page and its active warnings remain.

The full-page host test also encountered a null manufacture-date string before
that information was received. The renderer now explicitly displays
"Not received". The host String shim, unlike Arduino String, could not accept
that null pointer; this is not evidence of a new crash on DORA.

## Validation

- Five new host tests exercise missing/malformed feedback, the two-second
  freshness boundary and millisecond rollover, pending stop on reinsertion,
  all three existing handoff permission blocks, normal handoff, and per-instance
  diagnostic rendering. The pending-stop test verifies the emitted 0x118 profile
  remains unchanged; rendering itself emits no CAN frames.
- Full host suite: 344 passed, one existing test skipped, zero failures.
- LilyGo T-2CAN firmware build: passed. This workspace build is not an OTA release
  prepared from the exact deployed firmware baseline.
- Validation logs: `logs/charge-diagnostics-offline/` (local, ignored by Git).

## Remaining work

The stale stop/release behavior is now observable and reproduced by a regression
test, but is not fixed by this change. Next, design and test a deliberate recovery
transition that preserves AC/inverter behavior and requires trustworthy DC state
before any DC handoff. The software must not treat a fresh AC zero reading as
proof that a DC session has ended. CP-side command delivery and protocol readiness
still require a later controlled capture.

Deploy only a reviewed candidate based on the known running firmware, during a
parked, unplugged maintenance window. No device reboot or power control was used
for this offline work.
