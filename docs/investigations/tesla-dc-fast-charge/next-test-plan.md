# Next investigation steps

Current conclusion: native PLC advertisement alone did not start charging.
Continue from the preserved traces; another identical paid attempt adds little.
All physical work and any later configuration changes belong to a parked,
controlled test with the operator present.

## Resolve the highest-value uncertainties

| Priority | Hypothesis / unresolved condition | Evidence so far | Measurement that distinguishes it |
| --- | --- | --- | --- |
| 1 | Charge-port ECU does not receive or accept the intended configuration / keepalives | Native PLC TX exists locally, yet CP communication alerts persist; capture segment unconfirmed | Identify physical CAN tap; synchronized passive Vehicle CAN and CP-CAN capture with matching timestamps. Compare received 0x7FF mux 3 and relevant keepalives at CP side |
| 2 | CCS-capable ECU is not operationally configured for PLC in this assembly | CCS remains inactive; user confirms capable hardware, exact part/firmware not recorded | Record CP ECU part number, firmware and supported diagnostic configuration/status using the correct service procedure. Confirm modem readiness and decode ECU-specific alerts, including 0x31E mux 2 |
| 3 | Emulator stop/charge states conflict | UI FAULT/EQUIPMENT_STOP coexists with active charge profile; source explains coexistence | On an unplugged bench, record saved stop flag, system state and emitted profiles before/after normal controls. Reproduce and fix state coordination with host tests before another DC session |
| 4 | Pilot/PLC physical link, station authorization or station compatibility fails | Cable/latch seen; repeated candidate SWCAN faults; no captured CCS progression | Obtain station session/error record and CP-side diagnostics. If still unresolved, qualified pilot/PLC measurement with suitable isolated equipment |
| 5 | Fast-charge contactor/HV path failure | Main path closed but fast contactors never requested/allowed to close | Investigate only if negotiation reaches cable check/precharge and valid close requests appear but feedback fails to follow. Current traces do not establish this stage was reached |

Priority reflects the usefulness of the next test, not a quantified probability.
Hardware capability and operational configuration are separate questions; the
user's capability confirmation is retained, not silently reclassified as absent.

## Minimum capture for the next parked session

1. Record CP ECU and battery/PCS identities, harness topology, emulator firmware
   hash, PLC selection, and the exact station/connector/session identifier.
2. Resolve the saved EQUIPMENT_STOP using the normal controls with the connector
   unplugged. Verify the actual state and outgoing profile; a "ready" click is
   not sufficient evidence. Do not bypass the flag to conceal the conflict.
3. Start passive recordings before payment/insertion. Prefer a continuous
   hardware capture with overrun counters. Label each physical bus, bitrate,
   direction convention and clock; the web logger's rx0/tx1 is only one bus.
4. Retain all frames. At minimum inspect 0x7FF, 0x21D, 0x13D/0x43D, 0x20A,
   0x212, 0x232, 0x25D, 0x29D, 0x31E, 0x75D and the emulator's charge profiles.
   Do not filter unknown diagnostics out of the raw capture.
5. Record station screen changes and exact times for authorization, insertion,
   latch, first protocol transition, failure, stop request and removal. Record
   battery voltage/current and station voltage/current with freshness indicators.
6. Stop and save immediately after a repeatable failure. Compare the first
   divergence from a successful, compatible capture before changing anything.

The next capture should answer whether native PLC configuration reaches CP-CAN,
whether PLC/CCS begins, and which side first requests or reports shutdown.
CAN-only evidence may still need station logs or PLC diagnostics.

## Software work before claiming DC support

The current PLC setting is a hardware advertisement. A complete DC integration
needs an explicit state model; it cannot reuse the AC release sequence solely
because L2 works.

| Scenario | Required observable behavior / test |
| --- | --- |
| Start while equipment stop or blocking fault is active | Reject entry with a clear reason; emit no newly enabled charge profile |
| A stop/fault occurs during negotiation | Request the documented shutdown and preserve authoritative ECU feedback; never invent ACCEPT, cable-check pass, isolation result or contactor permission |
| Stop/fault while DC current is flowing | Follow a reviewed shutdown sequence based on actual ECU protocol; verify current decay and fast-contactor/link state before release/handoff |
| AC 0x264 is zero but DC current remains nonzero or unknown | AC readings alone must not qualify DC shutdown as complete |
| DC feedback becomes stale or CAN is lost | Expose unknown/stale state and apply a defined protocol-specific stop strategy; do not relabel stale as zero |
| Inverter permission changes during a session | Preserve a defined stop/handoff path without contradictory drive and charge profiles |
| Physical handle / latch / removal order differs or a sensor disagrees | Retain the mismatch and refuse an unproven handoff; validate the behavior against the real ECU generation |
| Normal DC completion and user-requested stop | Verify orderly current stop, fast-contactor opening and latch release; then restore the intended normal operating profile |
| Reboot and saved configuration | No unintended auto-start; valid PLC setting preserved; invalid selection fails closed to the configured default |
| L2 regression | Existing successful insertion, current regulation, handle release and inverter handoff still work |

The exact shutdown commands and thresholds remain to be established from
compatible documentation and captures. Do not choose them from the reference
car's raw bytes without matching ECU generation, bus placement and semantics.

Candidate milestone sequence: passive visibility and diagnostics → host-tested
state coordination → bench verification without an energized DC session →
supervised DC negotiation → controlled charging and normal/fault stop validation.
No fast-contactor override is part of this plan.
