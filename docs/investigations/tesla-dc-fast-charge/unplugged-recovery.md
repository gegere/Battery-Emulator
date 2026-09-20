# Unplugged recovery and capture bundle candidate

This follows the diagnostic-only change `a2699e2d`. It adds a deliberate recovery
path for the retained stop request observed in the saved ChargePoint attempts.
It was prepared and host-tested offline, then installed as recorded below.
It is not a claim of working DC charging.

## State changes

Previously, Prepare to Charge returned immediately whenever charge mode was
already active, including while a previous stop/release sequence was pending.
The control can now restart that pending session after all of these checks:

1. Equipment stop is inactive and emulator system status is not FAULT.
2. CP proximity reports removed, with feedback no older than two seconds and
   a removed observation period of at least two seconds. Reinsertion, an unknown
   state or a feedback gap resets that observation period.
3. A complete HVP `0x20A` report no older than two seconds reports both fast-charge
   contactors OPEN, their set OPEN, both auxiliary contacts open, and fast-link
   energization permission NONE. Unknown, contradictory or stale states fail.
4. The existing AC shutdown condition holds: a recent near-zero AC sample, or
   absent/stale AC feedback after the post-removal freshness window.
5. Inverter contactor permission is allowed.

The two-second limits are local recovery/freshness requirements, not a claim
about Tesla's internal electrical discharge timing. Fast-path open feedback is
required independently of the AC condition. This is recovery after removal; it
does not implement or authorize shutdown of an energized DC session.

The web button and its backend method use the same eligibility check, which is
rechecked when the request executes. When blocked, the diagnostic panel and debug
log explain why. A successful explicit restart resets the old release evidence
and begins the existing initial charge profile. Reinsertion alone never clears a
pending stop. No warning masks or BMS faults are cleared.

Fresh starts are also rejected during equipment stop or system FAULT. Automatic
return to inverter operation retains the ordered handle/latch/removal sequence
and the existing permission conditions, and now additionally requires recent
removed feedback and the same fast-path-open proof. Latched removal from a prior
insertion can no longer authorize that handoff while the connector reports
inserted or CP feedback is stale. These additional checks can deliberately keep
charge mode pending when feedback is unavailable.

The HVP field positions and enum values already existed in the Tesla receiver
and agree with the saved [Model3CAN DBC](https://github.com/joshwardell/model3dbc/blob/master/Model3CAN.dbc).
The test fixture `f6 15 09 82 18 01` is an actual DORA `0x20A` report: main
contactors closed while the fast path is reported open/disabled. That is distinct
from the battery's main output and 14 V supply being on. This remains subject to
the existing ECU-generation interpretation limits.

## Offline evidence export

`tools/tesla_capture_bundle.py` creates a ZIP containing original capture bytes,
a manifest, SHA-256 hashes, the exact analysis-tool sources, communication audits, optional DBC decoding and
operator notes. Each capture requires an explicit physical-segment label and
clock name. It retains original interface numbers, duplicate records and unknown
frames. It does not merge or align clocks. A backwards timestamp is flagged and
timed analysis is withheld until overlapping snapshots or reboot epochs have
been separated. Empty captures are explicitly identified.

For example, after saving a raw capture (the installed v12.5 build uses Export
to .txt; the [v12.6 integration](v12.6-integration.md) uses CAN tools streaming):

```sh
python3 tools/tesla_capture_bundle.py \
  --capture 'Vehicle-CAN,emulator-uptime=/path/to/canlog.txt' \
  --firmware 'exact build label from DORA' \
  --station 'station and connector identifier' \
  --dbc /path/to/Model3CAN.dbc \
  --notes /path/to/session-notes.txt \
  --output /path/to/new-session.zip
```

Record payment/insertion/removal times, relevant UI state, logger cutoff and
per-interface physical wiring in the notes. Firmware/station/segment labels are
operator-supplied metadata. In DORA's historical exports, interface 0 is the
Tesla Vehicle-CAN capture and interface 3 is additional adapter traffic; it is
not a second charge-port capture. The comparison reference is separately CP-CAN.

This is a packaging and quality-reporting tool, not a continuous CAN recorder.
It cannot recover RAM-buffer overwrites or quantify hardware overruns from a
text export. Observed gaps are reported without calling them measured CAN loss.
It refuses to overwrite an existing bundle.

## Validation and deployment boundary

Eight new recovery tests cover the explicit restart, backend recheck after
reinsertion, independent permission gates, missing latch feedback, timer rollover,
freshness gaps, malformed HVP input, each required fast-path field and AC-zero
with unknown/closed fast contactors. Existing L2 tests now supply cyclic HVP/CP
feedback while retaining their original payload/checksum and handoff assertions.
Seven new bundle tests cover preservation, hashes, duplicates, separate clocks,
timestamp regressions, malformed/empty input, optional DBC/notes and no overwrite.

Workspace validation: 352 host tests passed, one existing skip; 23 offline Python
tests passed; LilyGo T-2CAN firmware build passed. The isolated deployed-base
candidate passed 306 host tests with one existing skip and its LilyGo build.
ESP32-S3 image checksum/validation hash and embedded UI identity were verified.
An additive-patch round trip reproduced all nine changed candidate source/test
files. The isolated candidate uses the
deployed `724b05c7` base plus the previously installed PLC and Events patches,
followed only by the diagnostics/recovery patch. Its release manifest records
the final deployed-base test count, image hash and source provenance.

Local artifacts:

- `build/releases/BE_724b05c-plc-recovery_LilygoT-2CAN.ota.bin`
- Matching `.json`, `.sha256`, `.patch` and `.logs/` files in that directory.
- `logs/archives/chargepoint-2026-09-19-recovery-evidence.zip` and `.sha256`,
  preserving both attempted sessions and the separate successful reference.

The ESP-IDF descriptor retains earlier build-environment metadata; the manifest
distinguishes it from the verified source patches, new embedded UI identity and
binary hash. Installation and its remaining validation limits are recorded below.

No device controls, configuration or firmware were changed during this work.
Before a station attempt, verify this candidate in a parked, unplugged bench
session, including continued inverter output, fresh CP/HVP data and the explicit
recovery path. A later supervised L2 regression remains necessary. CP-side
command delivery, DC negotiation and a complete DC shutdown protocol remain
separate unresolved work.

In particular, confirm that HVP fast-link permission becomes NONE after a real
L2 unplug. If it remains AC-enabled, the new strict gate will retain the pending
profile; host tests alone do not establish that ECU transition.

## Installation and startup verification

Installed through DORA's web OTA page on September 19, 2026 local time
(September 20 UTC), after the user confirmed parked, physically unplugged and
ready for the inverter to cycle. The uploaded image was 1,912,336 bytes, SHA-256
`8991f8d0777a766661d053619269c856b2852c643bf6bfbbf66211cb5cebc430`.
OTA reported Update Successful and restarted automatically. At three seconds
uptime, the main page identified
`v12.5.0dev-724b05c-plc-recovery (local/tesla-charge-recovery)` and reported RUNNING.
At 31 seconds it still reported RUNNING, -396 W output and both contactor
permissions allowed.
At 72 seconds, uptime continued increasing and output was -504 W with RUNNING
status and the same normal-profile diagnostic state.

The new panel reported normal inverter profile, no pending stop, recent connector
removal feedback, equipment stop inactive, Prepare to Charge available, and the
fast-charge path open/disabled from recent feedback. The advanced page reported
main contactors CLOSED, BMS DRIVE / UP_FOR_DRIVE, and 14.10–14.25 V DC-DC output.
This verifies startup visibility in the unplugged state. It does not exercise
the retained-stop recovery transition or an L2/DC charging/unplug sequence.

Before and after snapshots show the same nine alerts: BMS_a035, BMS_a055,
BMS_a170, PCS_a024, PCS_a086, CP_a013, CP_a045, CP_a047 and CP_a048. The Events
page retained the two CP warning groups; startup added normal connection/reset
informational events. No faults were cleared and no power controls or charge
commands were used. Main-page OK does not mean all battery diagnostics are clear.
The displayed isolation resistance changed from 10,230 kOhm before restart to
750 kOhm after restart; no conclusion about the cause is drawn from these snapshots.

Pre/post page readings, deployment metadata and hashes are saved under
`logs/tesla-recovery-install-2026-09-19/`; the release manifest now marks the image
deployed. Physical output confirmation and actual recovery/L2 validation are
separate from the software-reported startup checks.

The user subsequently enabled the air-conditioner/inverter-load AC breaker and
explicitly confirmed the air conditioner was running. At 9 minutes 28 seconds
uptime, DORA still reported RUNNING, -468 W output and both contactor permissions
allowed. The sampled output remained within roughly 324–468 W, so these readings
do not establish a distinct compressor load step or maximum inverter capability.
This records user-confirmed post-update load operation; no L2 charger was involved.
