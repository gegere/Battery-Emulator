# DORA / ChargePoint DC fast-charge investigation

Field session: 2026-09-19. Local branch: `feature/tesla-dc-fast-charge`, created
from `3621c13c`. Status: **DC charging did not start; root cause remains open.**

The native-PLC setting was transmitted after the update, but the recorded
charge-port state never advanced into the CCS discovery / cable-check /
precharge sequence. The strongest next measurement is a passive capture at the
charge-port CAN segment, with the ECU identity and physical capture point
documented. Transmitting a configuration on the emulator's interface does not
prove that the charge-port ECU received or applied it.

The user confirmed CCS/PLC-capable hardware and an original Tesla charge-port
connection to the battery. Its exact part number, firmware, and configuration
acceptance have not been independently established. L2 charging has worked;
this was the first third-party DC attempt. The 14 V DC-DC output is evidence of
the auxiliary supply operating, not evidence of a closed DC fast-charge path.

All work after the user announced departure was offline. No firmware, contactor,
charge-mode, or CAN controls were operated during that investigation.

## Evidence and reproducibility

The original field records and deployed firmware artifacts are preserved in
`logs/archives/chargepoint-2026-09-19-evidence.zip` (64 files, 3,556,344 bytes).
Its SHA-256 is:

```text
fd677518e5ea13704ef44b9aaa9eefeaa55729c9258bc26f6853db17727aa598
```

The ZIP contains a per-file `MANIFEST.json`. Every member was checked against
that manifest and the ZIP CRC. The adjacent `.sha256` file verifies the archive.
Keep this original archive unchanged. Raw captures and build artifacts are
local and ignored by Git; a branch alone is not a backup of them.

The [evidence index](evidence-index.json) records hashes of the principal inputs,
derived reports, reference capture, and deployed image. The later
`logs/archives/chargepoint-2026-09-19-offline-analysis.zip` preserves the offline
analysis and downloaded references separately, with its own manifest and
adjacent checksum. Neither archive has been uploaded.

Run from the repository root with Python 3.10 or later:

```sh
python3 -B -m unittest discover -s tools/tests -p 'test_tesla_dc_analyzer.py' -v
python3 -B tools/tesla_dc_analyzer.py \
  logs/chargepoint-2026-09-19/native-charge-merged.txt \
  --dbc logs/chargepoint-2026-09-19/Model3CAN-reference.dbc \
  --output logs/chargepoint-2026-09-19/analysis/native-plc-analysis.json
```

The [analyzer](../../../tools/tesla_dc_analyzer.py) accepts Battery-Emulator text
and SavvyCAN CSV. It records input hashes, raw payloads, interface/direction,
message inventory, selected decoded state changes, and unknown CP alert muxes.
It neither connects to a device nor transmits CAN. Use the same DBC for the
before/after and reference comparisons; its hash is recorded in each output.

Important interpretation limits:

- DORA's logs are overlapping web-buffer snapshots, deduplicated into merged
  traces. They have gaps and are not lossless recordings. Counts are captured
  frames, not a reliable measure of the complete bus frequency.
- Firmware `format_can_frame()` encodes RX as `2*interface` and TX as
  `2*interface+1`. Thus `rx0` and `tx1` are the **same physical CAN interface**.
- The community DBC is not verified for DORA's exact ECU. A mismatch already
  appears in LED / diagnostic interpretations; raw bytes take precedence over
  labels. In particular, do not infer the pilot waveform from unmatched 0x75D
  multiplexers or infer ECU acceptance from a TX log.
- CAN state reports do not contain the complete PLC conversation on the control
  pilot. The capture cannot identify a failed SLAC or higher-level CCS exchange
  by itself.

## What changed, and what the attempt showed

The firmware change adds a persisted `GTWPLC` hardware selection: 0 = none
(default), 1 = onboard adapter, 2 = native charge port. Values outside 0–2 fall
back to 0 in setup/NVM; the web handler rejects invalid selections before saving.
The selection affects only bits 28–29 of 0x7FF mux 3. It does not implement CCS,
send an invented ACCEPT state, or request fast-charge contactor closure.

| Observation | Before: PLC 0 | After: PLC 2 |
| --- | --- | --- |
| Merged trace | `confirmed-plug-merged.txt` | `native-charge-merged.txt` |
| Captured frames | 26,345 / 200 snapshots | 112,424 / 720 snapshots |
| Device uptime covered | 1197.922–1243.834 s | 211.365–430.135 s after reboot |
| TX 0x7FF mux 3 | `03 00 08 48 00 00 00 12` | `03 00 08 68 00 00 00 12` |
| First complete reinsertion ACCEPT → FAULT interval | 1.499 s | 1.400 s |
| Candidate CCS state | INACTIVE | INACTIVE |
| Fast-charge contactors | OPEN | OPEN |
| Outcome | No DC charging | No DC charging |

The uptime values belong to different boot sessions and must not be joined as
one continuous timeline. The latest paid attempt has this sampled sequence:

| Uptime | Observation |
| --- | --- |
| 349.042 s | Connector present/latched |
| 349.043 s | 0x43D changes from charge inactive to connected; charger type still 0 |
| 349.744 s | Candidate SWCAN ACCEPT |
| 350.145 s | Candidate SWCAN RECEIVE |
| 351.144 s | Candidate SWCAN FAULT |
| Through 363.146 s | Five sampled ACCEPT → RECEIVE → FAULT cycles |
| 364.844 s onward | SWCAN inactive; repeated pilot FAST_CHARGE/NONE observations |

The digital-comms-established bit appears alongside FAULT. It would be incorrect
to say it was always false, or to treat it as proof of a completed CCS session.
The observed ACCEPT belongs to the candidate Tesla SWCAN state field, not to a
missing emulator command that can safely be filled in.

Other recorded state:

- 0x333 TX `04 30 20 07 02`: charge-enable requested; termination target 80%.
- 0x20A RX `f6 15 09 82 18 01`: main contactors closed/economized, fast-charge
  contactors open, fast-link energization not allowed under the reference decode.
- BMS changes DISCONNECTED → NO_POWER and remains in SUPPORT. The post-attempt
  page reports HVIL OK and approximately 324 W discharge.
- 0x29D `00 00 00 20` has the **current-stale** bit set. That bit is not a
  voltage-validity flag. This frame does not establish physical zero voltage at
  the fast-charge terminals.
- CP alert mux 0 is `00 00 01 00 08 00 0d 00`. The deployed decoder reports lost
  GTW, VCSEC, VCFRONT and UI communications, plus door-open-expected-closed.
  Mux 1 briefly has `01 08 00 00 00 00 00 00`, mapped to door-sensor mismatch.
  Mux 2 is `02 08 00 00 00 00 00 00`, currently **unmapped**, and is preserved.

These communication alerts remain significant even though relevant TX frames
exist on the emulator interface. ECU firmware differences, forwarding, cadence,
checksums/counters, or payload acceptance may explain that combination. The
capture does not distinguish them.

## Comparison with a successful CCS capture

Damien Maguire's repository supplies a [2020 RHD Model 3 CP-CAN CCS capture](https://github.com/damienmaguire/Tesla-Model-3-Charge-Port-Controller).
The downloaded `ccs2_complete_ccs_cycle.csv` contains 213,399 frames across
48.876758–152.197159 s. Its SHA-256 and Git blob identity are preserved in the
evidence index and reference download manifest. It is a useful example, not a
payload recipe for DORA's different ECU / connector configuration.

Using the same DBC, the reference advances through CCS CONNECTED (63.420375 s),
SERVICE_DISCOVERY (66.520569), CHARGE_PARAM_DISCOVERY (67.123), CABLE_CHECK
(67.522355), PRECHARGE (73.333281), and ENABLED (74.547380). Fast-contactors change
to CLOSING at 73.330460 and CLOSED at 74.417981. During shutdown they return to
OPEN at 137.483959. Its steady HVP payload is `36 65 2e a2 18 21`.

DORA has no comparable sampled CCS progression. In the reference, 0x43D
recognizes a DC charger near service discovery; DORA remains at candidate
NO_CHARGER_PRESENT even after reporting the cable connected. This supports
investigating the early protocol/configuration path before contactor commands.
It does not isolate the failing component.

The reference is explicitly CP-CAN. The user subsequently confirmed DORA's
emulator is connected only to Vehicle CAN, with the charge-port ECU separately
connected directly to the battery; see the [setup review](setup-review.md). For example,
0x13D appears 10,075 times in the reference and is absent from DORA's latest
capture, while the 0x43D log version appears in both. The differing 0x232 counts
are also consistent with different bus exposure. Snapshot loss and ECU versions
are additional confounders; missing messages are not instructions to inject them.

The project's [Tesla charger integration notes](https://dalathegreat.github.io/Battery-Emulator-Wiki/setup/chargers/tesla_model_3/)
identify CP-CAN between the charge-port ECU and battery penthouse, separately
from Vehicle CAN. The author's suggestion that commands may pass through the
existing Vehicle CAN connection is explicitly an assumption. We have not
verified forwarding of the modified 0x7FF mux 3 to DORA's charge-port ECU.

Tesla's [CCS ECU retrofit procedure](https://service.tesla.com/docs/Model3/ServiceManual/en-us/GUID-49B8570D-6658-40F7-980F-F78C40BA706E.html)
sets `plcSupportType` to Native Charge Port and calls for a vehicle firmware
reinstall following ECU replacement. This supports the setting's meaning but
does not establish that broadcasting that field alone completes configuration
of a standalone assembly. No Tesla ECU flashing is proposed from these logs.

## Separate finding: charge-mode and stop-state coordination

The main page continued to show FAULT / saved EQUIPMENT_STOP before and after
the native-PLC attempt, despite charge mode being enabled and main contactors
closed. Source inspection explains how those states can coexist:

| Source location in `Software/src/battery/TESLA-BATTERY.cpp` | Observed behavior |
| --- | --- |
| `update_values()`, safety checks near line 1130 | Fault/stop/inverter permission changes `vehicleState` |
| `start_charge_mode()`, near line 2433 | Checks support and already-active state; does not check equipment stop or system FAULT |
| `transmit_can()`, charge branches near 2625, 2718, 2823 | Active charge profile takes precedence over normal `vehicleState` profiles |
| `update_charge_mode_stop_sequence()`, near 2570 | A remaining stop/fault also blocks handoff to normal inverter mode after unplug |

This is a concrete state-coordination issue. It is **not established as the cause
of the CCS handshake failure**. The PLC configuration patch does not introduce
or correct it. A fix must distinguish refusal to start a new session from an
orderly stop of an already energized session; simply disabling every transmit
branch on a fault is not an adequate reviewed shutdown design.

The existing release/handoff sequence was derived from successful **AC** traces.
`stop_charge_mode()` keeps charge-enable asserted and selects release profiles;
physical handle/latch/unplug observations gate the subsequent handoff. The
handoff's electrical measurement uses PCS AC-line 0x264 zero/freshness checks,
including an absence timeout after physical removal. It has no independent
DC-current / fast-link-discharge completion gate. This does not prove that the
ECU would unlock under DC load, but AC-line zero alone cannot validate DC shutdown.
DC enablement therefore needs a reviewed DC stop-state model and corresponding
bench tests, as detailed in the [next-test plan](next-test-plan.md).

## Firmware identity and validation

The deployed candidate is based on the previously running
`724b05c7c2884ffde04ebfa930a913efa99eb143`, with the seven-file PLC-setting patch.
It was built separately from this branch's newer `3621c13c` base, so checking out
this branch is **not** a byte-for-byte reconstruction of deployed firmware.

The archived image is `build/releases/BE_724b05c-plc_LilygoT-2CAN.ota.bin`,
1,904,608 bytes, SHA-256:

```text
9082374bf988cbe55d44f577c7d8665e7ef3657dddbea8caf25b248b4a7a0a10
```

UI identity `724b05c-plc (local/tesla-plc-config)` and native-PLC transmission
were verified after installation/reboot. The ESP-IDF descriptor inherited a
cosmetic version from the enclosing newer checkout; the release JSON records
this and the separately verified source base/patch. The archive includes that
manifest, patch, build output, and image verification output.

Validation completed:

- Workspace C++ suite: 329 passed, 1 skipped, 0 failed (330 registered).
- Deployed-base candidate C++ suite: 283 passed, 1 skipped, 0 failed (284 registered).
- Ten new parameterized PLC tests cover selections 0/1/2 and invalid 3/65535 in
  both normal and charge mode. They compare all transmissions, check that only
  the intended PLC bits change, and reject fabricated CP/BMS/HVP status frames.
- LilyGO `lilygo_2CAN_330` firmware build and ESP image integrity verification passed.
- Eleven offline analyzer tests passed, including signed current, stale-current
  handling, mux selection, DLC rejection, bus/direction interpretation, and
  preservation of unknown alert payloads.

These tests validate the bounded configuration change and decoding machinery.
They do not validate DC charging, live shutdown, ECU-specific DBC semantics,
or the web/NVM path through host tests; saved settings and emitted bytes were
observed separately on the device during the parked test.
