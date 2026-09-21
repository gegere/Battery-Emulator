# Next investigation steps

The source branch now includes v12.6.0; see the [integration record](v12.6-integration.md)
for validation, deployment status and the new CAN tools streaming workflow.

## September 20 readiness check after v12.6 installation

At 9 minutes 43 seconds uptime, DORA reports RUNNING, -928 W output and a normal
inverter profile, with no pending stop and recent removed/open-fast-path feedback.
The operator has confirmed AC loads are running. PCS DC-DC temperature fell to
31.7 degrees C (31.1 on a follow-up), and PCS_a086 insufficient cooling is no
longer listed. This is progress, but not clearance for an energized DC test.

Eight ECU alerts remain, including BMS_a035 isolation, BMS_a055 HV-chain model
and BMS_a170 limp mode. Isolation resistance displays 0 kOhm on two page reads.
The previous 10,230 kOhm display is also misleading: the
[reference DBC](https://github.com/joshwardell/model3dbc/blob/master/Model3CAN.dbc)
defines raw 1023 for `BMS_isolationResistance` as SNA. The current decoder/UI
multiplies this unavailable value by ten and presents it as a resistance.
It must not be used as evidence of good insulation. Physical insulation and
the applicability of the decoder to this ECU remain unverified.

Before another energized attempt:

1. Validate the reported insulation condition and investigate the active BMS
   isolation/HV-chain/limp alerts. Obtain the history/results of an independent
   HV insulation assessment by a qualified person, including which components
   were connected. Correct the SNA display and verify raw CAN/status semantics;
   neither clearing a flag nor changing a display repairs an insulation fault.
   The [project's insulation guidance](https://dalathegreat.github.io/Battery-Emulator-Wiki/setup/hardware/insulation_monitoring/)
   describes how transformerless inverters can affect readings. That is a
   possible explanation to investigate, not a diagnosis of this installation
   or a reason to disable protection. Do not disconnect protective earth.
2. Once the hardware condition is established, validate the actual
   charge/stop/unplug recovery cycle under controlled conditions. An L2
   regression checks the existing AC integration; it cannot validate a DC
   shutdown. Fresh startup feedback alone does not exercise pending-stop recovery.
3. Validate a saved passive CAN stream with v12.6 and establish the DC-specific
   current/voltage limits and shutdown behavior before testing energy transfer.
   Investigate CP-side receipt/configuration and the first CCS transition using
   the capture plan below. A paid public-charger insertion is not inherently
   a negotiation-only test: successful negotiation can proceed to energization.

No charge command, fault clear or contactor command was sent for this readiness
check. Evidence is saved as `readiness-*.txt` under
`logs/tesla-v126-install-2026-09-20/`.

## Earlier investigation findings

Current conclusion: native PLC advertisement alone did not start charging.
Continue from the preserved traces; another identical paid attempt adds little.
The [communication audit](communication-audit.md) also establishes that both
field captures were already emitting stop/release profiles. Resolve that state
and verify a fresh session before comparing communication changes at a station.
All physical work and any later configuration changes belong to a parked,
controlled test with the operator present.

The [offline diagnostic panel](offline-diagnostics.md) now exposes the selected
profile, pending stop, connector/AC feedback age and handoff permission conditions
in a locally tested candidate. The subsequent [unplugged recovery change](unplugged-recovery.md)
adds explicit restart eligibility and stricter handoff feedback checks. It was
installed and its unplugged startup verified on September 19. Actual recovery
and L2 unplug cycles still require parked validation before another station attempt.

## Resolve the highest-value uncertainties

| Priority | Hypothesis / unresolved condition | Evidence so far | Measurement that distinguishes it |
| --- | --- | --- | --- |
| 1 | Charge-port ECU does not receive or accept the intended configuration / keepalives | Native PLC TX exists on user-confirmed Vehicle CAN, yet CP communication alerts persist; charge port connects separately to battery | Synchronized passive Vehicle CAN and CP-CAN capture with matching timestamps. Compare received 0x7FF mux 3 and relevant keepalives at CP side |
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
