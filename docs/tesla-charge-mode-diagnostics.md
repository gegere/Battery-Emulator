# Tesla Model 3/Y PCS charge-mode diagnostics

Draft for Wiki review. This describes the experimental implementation in
[PR #2976](https://github.com/dalathegreat/Battery-Emulator/pull/2976), checked
against commit `6533a7e2`. MQTT/Home Assistant publication is separate in
[PR #2981](https://github.com/dalathegreat/Battery-Emulator/pull/2981).

## Scope and evidence

Charge mode requires the Model 3/Y battery selection, **Tesla Model 3/Y PCS
charger**, and digital HVIL disabled. The PCS profile uses the battery CAN
interface. These findings apply to the captured/tested configuration; PCS,
charge-port firmware, and EVSE compatibility have not been established generally.

The [complete research log](tesla-charge-mode-trace-analysis.md) preserves the
capture measurements, unsuccessful experiments, and later corrections. Its
historical release-command hypotheses are superseded by the final section,
**Corrected latch/cover interpretation from the 2026-08-12 traces**. Raw captures
are not included in this archive.

Earlier hardware observations and current firmware behavior are distinguished
below. Passing host tests and firmware builds does not establish hardware
validation of the latest split PR firmware.

## Charging and connector removal

### Starting a session

1. Select **Prepare to Charge** to start the charge-session profile and request
   hatch opening. The hatch request is a bounded pulse sequence.
2. Connect/power the EVSE as appropriate for the installed equipment. The EVSE
   used in the documented live test required its physical start button after
   charge mode was enabled before AC power flowed.
3. Confirm actual energy flow. In the earlier successful test, the pack received
   approximately 864–900 W at 363 V while the PCS DC-DC supplied the 12 V bank.
   BMS `CHARGING`, charge-port `ENABLED`, or DC-DC current alone did not establish
   AC charging in preceding tests.

### Removing the connector

1. Keep charge mode active and press the physical handle button.
2. Wait for the connector latch to release, then remove the connector. In the
   reference sequence the PCS removes current before latch movement.
3. The firmware waits for the ordered handle/latch/removal feedback and its
   handoff conditions before returning to normal inverter operation.

**Prepare to Unplug (Optional)** is a manual fallback for arming this workflow.
It does not provide an automatic latch-release command. It is unavailable until
an inserted connector has been observed in the current session. The hatch-opening
request is distinct from connector-latch release.

## CAN checkpoints

| Frame | Diagnostic use | Timing or interpretation |
| --- | --- | --- |
| `0x053` | Charge startup | Captured at 20 ms; three startup states. Current firmware selects steady state after 3.14 s. |
| `0x055` | Charge keepalive | 10 ms; modulo-4 and modulo-16 counters plus additive checksum. |
| `0x056` | Existing charge-port producer | Captured around 99 ms. Firmware fallback uses its 100 ms scheduler after 250 ms without an incoming frame. |
| `0x118` | Charge/drive profile | 10 ms. Captured steady charge uses byte 2 `E9` and byte 5 `48`. Preserve counter/checksum validity. |
| `0x221`, `0x3A1`, `0x3C2` | Supporting charge profile | Captured at 50 ms. `0x3A1` uses the measured counter/checksum cycle, not the generic additive checksum. |
| `0x052`, `0x339` | Supporting charge traffic and VCSEC authorization | Current firmware sends both at 100 ms while charge mode is active. The complete function of `0x052` remains unidentified. |
| `0x333` | Charge enable and hatch request | Current charge-mode path uses 100 ms. Bit 0 requests the hatch; bit 2 enables charge. Clearing bit 2 did not establish software-only latch release. |
| `0x21D` | Connector/handle feedback | Proximity 3: inserted; 2: handle transition; 1: removed, as used by this implementation. |
| `0x25D` | Latch feedback | Either latch control reporting 2, 3, or 4 is accepted as release movement after the handle event. |
| `0x264` | PCS AC line measurement | Voltage, current, power, current limit; minimum six bytes; validity expires after two seconds. |

Capture timing describes observed traffic, not an independently verified Tesla
protocol specification. Avoid drawing a conclusion from one frame in isolation.

## Troubleshooting

| Symptom | Checks |
| --- | --- |
| Prepare to Charge is absent | Check Model 3/Y battery and PCS charger selections, digital HVIL setting, and whether charge mode is already active. |
| BMS reports charging but AC energy is absent | Check EVSE power delivery and its start requirement; inspect PCS AC measurements and pack power. Account for the separate DC-DC load. |
| PCS support faults during startup | Inspect the `0x3A1` mux/counter/checksum sequence. An earlier implementation's generic checksum caused faults; do not assume every similar fault has this cause. |
| Irregular timing or unexpected payload variants | Check for competing CAN producers or simultaneous replay on overlapping IDs. The initial finite web replay did not establish charging and logged a task overrun. |
| Handle does not release the latch | Check charge mode, ongoing `0x339` authorization, prior insertion feedback, the physical handle transition in `0x21D`, and subsequent `0x25D` movement. Hatch opening does not prove latch release. |
| Prepare to Unplug is unavailable | Confirm proximity 3 was observed during this session. Preparing an empty port previously extended the locking pin into the connector opening. |
| Charge mode remains active after unplug | Inspect ordered feedback, AC measurement freshness/values, inverter permission, system faults, and equipment stop. There is no timeout that declares unplug successful. |
| Home Assistant AC sensors are unavailable | With #2981 installed, check emulator connectivity and `charge_line_data_valid`. Last values remain stored when stale; unavailable does not mean zero. |

## Firmware handoff conditions

After handle, latch, and removal have been observed in order, the current code
requires inverter permission, no system fault, no active equipment stop, and
either:

- A fresh `0x264` sample at or below 5 V, 0.5 A, and 100 W; or
- More than two seconds since confirmed removal, with charge-line data stale.

The second condition accommodates PCS units that stop sending `0x264` after
unplug. Stale data alone cannot satisfy the handoff before the physical sequence.
These are implementation thresholds, not a general statement that exposed
conductors are safe to handle.

A missed brief handle frame may be inferred from a later removed indication only
when insertion was previously observed in this session. Latch and removal checks
still apply. The handoff requests the normal DRIVE state directly; fault and
equipment-stop handling retain authority over contactors.

## Useful evidence for a diagnostic report

Record the firmware commit, board, battery/PCS/charge-port identification where
available, EVSE model, and the exact order/timestamps of button presses and
connector movement. Include a CAN capture spanning startup or the complete
handle/latch/removal sequence, the relevant frames above, event logs, and AC/pack
measurements. State whether a conclusion comes from hardware observation, a host
test, or a hypothesis.
