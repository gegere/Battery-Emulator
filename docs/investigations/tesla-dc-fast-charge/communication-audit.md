# Charge-port communication warnings: evidence and remaining tests

Offline follow-up to the September 19, 2026 installation of charge-port Events.
No live firmware, settings, power controls or CAN traffic were changed for this
audit. The question is whether CP_a013, CP_a045, CP_a047 and CP_a048 prevent CCS.

## Conclusions

The available evidence does **not** identify any one of these four warnings as
the proven DC blocker. It does identify two problems that make another identical
charging attempt a poor diagnostic test:

1. Candidate command messages are present in emulator TX while the charge port
   continues to report their sending modules missing. Arrival and acceptance at
   the separate charge-port CAN segment have not been established.
2. Both the PLC-none and native-PLC field captures already contain the emulator's
   stop/release transmit profile. The native capture is in that profile before
   the last insertion. These were not clean fresh-start comparisons.

There is also a concrete omission to investigate: `0x353 UI_status` appears in
the successful reference but is absent from DORA's capture and Tesla transmitter.
It is a candidate for the UI communication warning, not a proven charge-enable
requirement. Adding an arbitrary copy of another car's status would not establish
correct UI behavior or CCS compatibility.

## What the captures show

Inputs and their SHA-256 values are recorded in the generated local report:
`logs/chargepoint-2026-09-19/analysis/cp-comms-audit.json`.

| Capture | Valid received 0x31E mux 0 reports | Reports asserting each of the four warnings |
| --- | ---: | ---: |
| DORA, PLC none | 29 | 29 |
| DORA, native PLC | 114 | 114 |
| Reference complete CCS cycle | 103 | 0 |

All 304 captured reference alert frames, including muxes 1 and 2, have zero alert
payload bits. DORA's mux 2 is preserved as raw data; its meaning remains unknown.
There is no assertion/clear transition separating these four communication
warnings in the DORA recordings, so timing cannot isolate their individual effect.

The successful reference is a different 2020 RHD vehicle on CP-CAN, sourced from
[Damien Maguire's controller repository](https://github.com/damienmaguire/Tesla-Model-3-Charge-Port-Controller).
Its success supplies a comparison, not proof that every frame in it is required.

| Candidate message | DORA native trace | Successful reference | Interpretation |
| --- | --- | --- | --- |
| 0x7FF car configuration | 1,200 TX; 100 ms median observed interval; native PLC in mux 3 | 963 RX; 100 ms median | Configuration is emitted locally. The GTW warning is not proof this particular frame is missing; GTW may require other traffic or a different accepted format. |
| 0x339, labelled VCSEC authentication in emulator source | 1,148 TX; 100 ms median; one fixed payload | 1,031 RX; about 100 ms median; varying payloads | Broad cadence matches. Payload meaning/acceptance and delivery still require validation for this ECU. |
| 0x221 VCFRONT LV state | 2,282 TX; 50 ms median | 2,002 RX; about 50 ms median | It is emitted; CP-side receipt is unknown. |
| 0x3A1 VCFRONT vehicle status | 2,338 TX; 50 ms median | 1,995 RX; about 50 ms median | It is emitted; layouts and payloads differ between generations. |
| 0x333 UI charge request | 1,153 TX; 100 ms median; charge enable asserted | 194 RX; about 500 ms median | The missing UI warning is not explained by absence of this outgoing charge request. |
| 0x353 UI status | Not observed; not implemented by Tesla transmitter | 104 RX; about 1 second median | Concrete candidate missing UI heartbeat/status. Causal link to CP_a048 is unproven. |
| 0x33A UI range/SOC | Not observed; not implemented by Tesla transmitter | 95 RX; about 1 second median | Another UI difference; requirement for CCS is unproven. |

The DORA logs contain snapshot gaps. Medians characterize observed samples and
must not be described as a guarantee of uninterrupted transmission. The audit
keeps bus and direction separate. `rx0` and `tx1` represent the same physical
emulator interface, not the two sides of the battery's CAN gateway.

## Why VCSEC can warn while normal output works

The deployed source transmits `0x339` only inside `charge_mode_active`. Normal
inverter operation does not send it. Thus a VCSEC missing warning outside charge
mode is consistent with the implementation. However, the warning also remained
asserted during the failed charge-mode capture with 1,148 outgoing `0x339`
frames. Merely enabling charge mode is therefore not a demonstrated remedy.

Existing source comments associate that frame with physical handle release in
the earlier AC setup. They do not establish that its fixed payload satisfies
all of DORA's DC authorization requirements. The public
[cpcontrol implementation](https://github.com/mdrobnak/cpcontrol-redacted) is
explicitly redacted; its zeroed IDs and payloads cannot fill these gaps.

## Strong additional finding: the attempted session was already stopping

In the native trace, every one of 11,430 transmitted `0x118` frames has byte 2
`E9`, byte 5 `48`, and byte 7 `80`, from uptime 211.368 to 430.133 seconds.
The deployed source emits byte 7 `80` in this charge profile when
`charge_mode_stop_requested` is true. Three independent signatures agree:

- 1,163 `0x207` release-profile frames, beginning at 211.395 s.
- 1,192 `0x500` release-profile frames, beginning at 211.395 s.
- 1,171 `0x241` post-release-profile frames, beginning at 211.395 s.

The final insertion occurs around 349.042 s. This stop state therefore predates
that insertion by more than two minutes. The earlier PLC-none capture also has
all four signatures. This conclusion is derived from exact deployed-source
branches and raw TX bytes; it does not rely on a guessed Tesla stop-bit meaning.

`start_charge_mode()` returns immediately when charge mode is already active.
`update_charge_mode_stop_sequence()` retains the mode when equipment stop,
system FAULT or inverter permission prevents handoff. Consequently, repeating
Prepare to Charge or reinserting the connector can leave an earlier stop/release
sequence active. The captures do not include its initial trigger, and do not
prove that this state alone caused the failed PLC negotiation.

This must be resolved and made visible before another DC test. A correction
must preserve orderly shutdown and real ECU feedback; clearing the stop flag
on any reinsertion would be unjustified, particularly with an energized DC link.

## Next discriminating work

1. **Resolved by the user:** the emulator is on Vehicle CAN only; the charge-port
   ECU connects directly to the battery. See the [setup review](setup-review.md).
   The project's
   [charger wiring notes](https://dalathegreat.github.io/Battery-Emulator-Wiki/setup/chargers/tesla_model_3/)
   identify these as separate segments and explicitly leave forwarding as an
   assumption. Do not join the two buses to test that assumption.
2. Capture passively at CP-CAN and compare 0x7FF/0x339/0x221/0x3A1/0x333 with the
   emulator-side recording. If they are absent there, investigate forwarding.
   If present, compare ECU generation, complete expected message set, rolling
   counters/checksums and supported payloads before changing transmissions.
3. Identify the CP ECU part/firmware and its operational PLC configuration.
   The user's CCS capability confirmation stands. Hardware capability alone
   does not prove operational configuration; Tesla's
   [retrofit procedure](https://service.tesla.com/docs/Model3/ServiceManual/en-us/GUID-49B8570D-6658-40F7-980F-F78C40BA706E.html)
   includes both configuration and a vehicle firmware reinstall. No ECU flash
   or retrofit action is proposed from that observation.
4. Establish a fresh non-stopping charge state through a reviewed normal state
   transition on an unplugged bench. Verify it from emitted profiles. Check
   the reported battery isolation/HV-chain and PCS cooling alerts separately
   before an energized test; clearing communication warnings is not an all-clear.
5. Only then test narrowly specified missing/status messages with the correct
   semantics and measure both warning recovery and actual CCS state progression.
   A warning clearing is not itself evidence that charging is authorized.

## Reproduce the audit

```sh
python3 -B tools/tesla_cp_comms_audit.py \
  logs/chargepoint-2026-09-19/native-charge-merged.txt \
  logs/chargepoint-2026-09-19/confirmed-plug-merged.txt \
  'logs/chargepoint-2026-09-19/references/Tesla-Model-3-Charge-Port-Controller/CAN Logs/CP_CAN_CCS/ccs2_complete_ccs_cycle.csv' \
  --output logs/chargepoint-2026-09-19/analysis/cp-comms-audit.json
python3 -B -m unittest discover -s tools/tests -p 'test_tesla*.py' -v
```

All 16 offline tests pass. Five new tests cover the captured warning bits,
short/unknown/locally transmitted alert frames, interface/direction separation,
distinguishing charge-enable from stop-profile evidence, and absent versus clean
reports. This work changes only offline tooling and investigation documentation.
