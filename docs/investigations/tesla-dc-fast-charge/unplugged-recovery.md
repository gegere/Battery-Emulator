# Unplugged recovery and capture bundle candidate

This follows the diagnostic-only change `a2699e2d`. It adds a deliberate recovery
path for the retained stop request observed in the saved ChargePoint attempts.
It is an offline candidate, not an installed update or a claim of working DC
charging.

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

For example, after using the web page's Export to .txt control:

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
binary hash. This candidate has not been uploaded.

No device controls, configuration or firmware were changed during this work.
Before a station attempt, verify this candidate in a parked, unplugged bench
session, including continued inverter output, fresh CP/HVP data and the explicit
recovery path. A later supervised L2 regression remains necessary. CP-side
command delivery, DC negotiation and a complete DC shutdown protocol remain
separate unresolved work.

In particular, confirm that HVP fast-link permission becomes NONE after a real
L2 unplug. If it remains AC-enabled, the new strict gate will retain the pending
profile; host tests alone do not establish that ECU transition.
