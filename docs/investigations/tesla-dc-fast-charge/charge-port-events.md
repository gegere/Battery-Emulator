# Charge-port diagnostics on the Events page

The user requested this after normal battery output stopped while the charge-port
ECU was powered off. The advanced page reported charge-port communication loss,
HV-chain and external-isolation faults while the main page still displayed OK.
After the user restored charge-port power, a saved advanced-page snapshot showed
the BMS return to STANDBY with those BMS faults cleared. The user subsequently
confirmed the inverter and air conditioner were operating. Firmware and PLC
configuration remained unchanged. This is separate from the unresolved DC
charging negotiation issue.

Before/after diagnostic pages and a passive CAN capture are saved locally under
`logs/output-loss-2026-09-19/`. The normal contactor request was initially blocked
by automatic approval review because the BMS faults were active. Once charge-port
power was restored and those faults cleared, the normal control was retried;
browser confirmation timed out, so its completion cannot be independently
attributed. Final output recovery is user-confirmed.

## Implemented behavior

- Events shows the 96 CP alerts already decoded from 0x31E mux 0/1, with CP codes
  and readable descriptions compiled locally from the existing DTC vocabulary.
  Internet access is not needed to explain the events.
- Alerts share six bounded event rows per battery, each retaining a 16-bit mask
  and listing every reported alert in that group. Different batteries have
  independent event state. Existing event names are preserved.
- A separate charge-port-missing warning combines BMS_a091, BMS_a092 and
  PCS_a023, with guidance to check charge-port low-voltage power and CAN.
  Clearing one reporting ECU's flag does not clear the others' reports.
- Repeated identical frames do not increment counts or refresh the event time.
  A changed mask or recurrence records another occurrence. A fresh clear report
  clears the event while retaining its last description and count in RAM.
- The Events table now labels entries **Active** or **Cleared**, including
  existing event types. Its existing clear-history action still clears the
  emulator's event history; it is not a BMS fault-reset command. A still-reported
  charge-port warning reappears on the next valid report.
- These new diagnostic events have warning severity. They do not introduce
  contactor, charge-mode, reset or fault-clear commands, and do not downgrade an
  existing blocking event.

Examples of messages shown on Events:

```text
Tesla charge port: CP_a013: Lost Comms GTW
Tesla charge port: CP_a045: Lost Comms VCSEC; CP_a047: Lost Comms VCFRONT; CP_a048: Lost Comms UI
Tesla charge-port communication missing. Check charge-port low-voltage power and CAN. Reported by: BMS_a091 BMS_a092 PCS_a023
```

## Limits and validation

Active means the latest valid report remains asserted. Silence is not interpreted
as recovery. Unknown CP multiplexers remain undecoded; this change does not
invent a meaning for the field-session mux 2 payload. Short matrix frames cannot
clear warnings or create alerts from missing bytes. The descriptions use the
existing ECU mapping and retain its firmware-version limitations.

The event history is the existing bounded, in-memory event table, not a complete
chronological record of every individual alert transition. If several alerts in
one group change, that row retains the most recent mask; it does not retain all
previous combinations. Full CAN records remain the source for that history.

Ten new tests exercise the captured payloads, simultaneous alerts, mux boundaries,
the signed event-data high bit, separate packs, malformed input, recovery,
clear-history reassertion, retained stop conditions, and actual Events HTML.
The existing per-battery event tests include all new event triplets.

The branch host suite passed 339 tests with one existing skip. The isolated
deployed-base candidate passed 293 tests with one existing skip. The candidate
backport keeps the old release's smaller per-battery event block and adapts only
the new diagnostics to it. Its patch is saved locally as
`build/tesla-cp-events-724b05c.patch`, applied after the archived PLC patch.
Both the branch and isolated deployed-base candidate passed the LilyGO
`lilygo_2CAN_330` firmware build. The candidate's ESP32-S3 image checksum and
validation hash passed esptool verification. Its UI identity is
`724b05c-plc-events` / `local/tesla-cp-events`.

The installation candidate and its checksums, source patches, provenance and
validation logs are saved locally under `build/releases/`:

- `BE_724b05c-plc-events_LilygoT-2CAN.ota.bin` (1,908,832 bytes)
- SHA-256: `3f4fc718ee2ed00012e1ce29fedd8f9a5b7dc8c5fb9f1669191282d1d692b0aa`
- Matching `.json`, `.sha256`, `.patch` and `.logs/` artifacts.

The ESP-IDF application descriptor derives a version from the enclosing checkout;
the release manifest records that distinction from the actual deployed-base
source and ordered patches. The existing PLC setting and power-control behavior
are retained in this candidate.

The implementation is on `feature/tesla-dc-fast-charge`. After the user confirmed
the inverter was ready to cycle, the candidate was installed through DORA's OTA
page on September 19, 2026 (September 20 UTC). OTA reported success; the new UI
identity and reset uptime were verified. At 24 seconds uptime the main page
reported RUNNING and -468 W; the advanced page reported closed contactors,
BMS DRIVE / UP_FOR_DRIVE and 14.17 V DC-DC output. The Events page displayed
CP_a013, CP_a045, CP_a047 and CP_a048 with readable descriptions and Active state.

The advanced page also reported BMS_a035 isolation, BMS_a055 HV-chain,
BMS_a170 limp mode, PCS_a024 VCFRONT missing and PCS_a086 insufficient cooling.
These remained present on a fresh read; no faults were cleared or overridden.
The main page's OK status is therefore not an all-clear for battery diagnostics.
No before-update advanced snapshot was captured during this installation, so
their onset cannot be attributed from these records. Installation evidence is
saved locally under `logs/cp-events-install-2026-09-19/`, with checksums.
