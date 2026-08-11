# Tesla charge-mode CAN trace analysis

This analysis is based on the supplied August 10, 2026 capture set. Raw trace
files are intentionally not stored in the repository.

## Measured Ingenext timing

`TRACE_G_054_CHARGE_COMMAND.trc` is the cleanest capture containing a single
operator command with uninterrupted Ingenext traffic around it. The later
`TRACE_A_INGENEXT_CHARGE.trc` is the reference for an actual successful charge
transition.

| CAN ID | Frames | Raw average | Median | Min–max | Behavior |
| --- | ---: | ---: | ---: | ---: | --- |
| `0x053` | 2,723 | 20.044 ms | 20.000 ms | 19.8–20.3 ms | periodic |
| `0x055` | 5,446 | 10.022 ms | 10.000 ms | 9.7–10.3 ms | periodic |
| `0x056` | 551 | 99.044 ms | 99.000 ms | 98.2–99.8 ms | periodic |
| `0x054` | 1 | — | — | — | manual command |
| `0x118` | 5,446 | 10.022 ms | 10.000 ms | 9.7–10.3 ms | periodic |
| `0x221` | 1,090 | 50.110 ms | 50.100 ms | 48.8–51.4 ms | periodic |
| `0x3A1` | 1,090 | 50.110 ms | 50.100 ms | 48.5–51.7 ms | periodic |
| `0x3C2` | 1,090 | 50.110 ms | 50.100 ms | 48.1–52.1 ms | periodic |

Longer traces contain capture pauses and missing frames, which inflate the raw
average. Their medians and gap-resistant nominal averages agree with the table.

## Payload behavior

### `0x053`

The message has no rolling counter. Ingenext startup uses three observed states,
all at the 20 ms period:

1. `54 30 84 C3 8F 28 46 0D` for roughly 100–140 ms.
2. `D4 30 84 C3 8F 28 46 0D` for roughly 2.8–3.0 seconds.
3. `D4 30 84 CB 8F 28 46 0D` continuously afterward and during successful charge.

The first state was visible in `first 500 ms after 0x054.trc` and
`TRACE_E_FULL_PROCESS.trc`; `TRACE_G` began during state 2. The state changes
preceded the recorded `0x054` command, so the command did not reset or advance
this sequence.

### `0x055`

The 10 ms payload is:

```text
[counter2, state, 00, 00, 00, 00, counter16, checksum]
```

- Byte 0 counts `0, 1, 2, 3` modulo 4 on every frame.
- Byte 1 is normally `0`; it is `1` on the byte-0 `2` phase after `0x053`
  changes from `54 ...` to `D4 ...`.
- Bytes 2–5 are zero in all supplied captures.
- Byte 6 counts `0..15` modulo 16, incrementing after each complete four-frame
  byte-0 cycle (about every 40 ms).
- Byte 7 is an additive checksum:
  `(0x55 + sum(bytes 0..6)) & 0xFF`.

The two counters continue through a recorded `0x054` command; the command does
not restart them.

### `0x056`

The 99 ms payload is:

```text
00 00 00 00 00 00 counter16 checksum
```

- Byte 6 counts `0..15` modulo 16 on every frame.
- Byte 7 is `(0x56 + sum(bytes 0..6)) & 0xFF`, which simplifies to
  `(0x56 + byte[6]) & 0xFF` for the observed payload.
- Its counter also continues through `0x054`; it is independent of `0x055`.

### `0x054`

`TRACE_G_054_CHARGE_COMMAND.trc` contains one manual frame at 13,293.8 ms:

```text
01 00 02 E8 03 30 87 00
```

Other traces show the same payload only when the operator sends it. It is not a
periodic keepalive. Repeated appearances in long process captures are separated
by many seconds and represent repeated manual attempts. This is an
operator-to-Ingenext command; the Tesla battery does not act on it directly.

### Successful `0x118` charge transition

The decisive change in `TRACE_A_INGENEXT_CHARGE.trc` is Ingenext's periodic
`0x118` profile, not the `0x054` operator command:

- During charge startup, byte 1 has high nibble `0x8`, byte 2 is `0x2D`, byte
  5 remains `0x08`, and byte 7 is `0x00`.
- At 17,653.2 ms, byte 2 becomes `0xE9` and byte 5 changes from `0x08` to
  `0x48` while byte 7 remains `0x00`.
- About 1.1 seconds later, the BMS `0x212` status changes to charging.
- At 39,260.8 ms, byte 5 returns from `0x48` to `0x08`; the BMS subsequently
  exits charging.

The unsuccessful `TRACE_G` attempt kept byte 5 at `0x08` and used byte 7
`0x80`. Replaying only `0x053`, `0x055`, and `0x054` therefore cannot reproduce
the successful transition.

The other successful steady profiles observed in `TRACE_A` are:

- `0x221`: alternating mux values `0x20` and `0x21` at 50 ms.
- `0x3C2`: alternating `10 55 55 55 00 00 5D 19` and
  `01 55 15 15 00 00 55 09` at 50 ms.
- `0x3A1`: alternating payload families beginning `03 00 98 6E BE 00` and
  `88 42 0B C8 00 10`, with rolling counter/checksum fields.

## Baseline and conflict comparison

- Battery Emulator baseline: `0x053` and `0x055` are absent.
- Ingenext successful traffic: `0x053` at 20 ms, `0x055` at 10 ms, and `0x056`
  at about 99 ms.
- Full-system failure: overlapping `0x118`, `0x221`, `0x3A1`, and `0x3C2`
  producers create short inter-frame spacings and extra payload variants. This
  supports the protocol-conflict hypothesis rather than an electrical fault.
- In `TRACE_E_FULL_PROCESS.trc`, `0x053`/`0x055` begin at about 74.3 seconds,
  after the Battery Emulator is removed, and execute the same Ingenext startup
  sequence before the later charge command.

## Live replay result

The first finite web-replay experiment injected the measured `0x053`/`0x055`
startup and one `0x054` while Battery Emulator remained active. It did not
enter charge: the BMS stayed in support/no-power state and advertised zero
maximum charge current. The device logged a task-overrun event. This confirms
both that `0x054` is insufficient without Ingenext translating it and that the
web replay path is not appropriate for permanent 10 ms charge traffic.

## Live integrated-firmware result

The first integrated-firmware test reached `ABOUT_TO_CHARGE`, but remained at
zero charge current. The PCS reported `Vcfront Mia`, `Dcdc12 Vsupport Faulted`,
and `Dcdc Lv Rationality`; the low-voltage bus remained near 12 V. A live CAN
capture identified the implementation error: the generic additive checksum
generator had been applied to `0x3A1`, even though that frame uses a different
counter/checksum sequence. The resulting `0x3A1` payloads did not occur in the
successful Ingenext trace.

After replacing that generator with the exact measured 16-frame `0x3A1`
counter/checksum cycle and aligning mux 0 to even counters and mux 1 to odd
counters, the August 10 live test reached the Tesla CAN charge state:

- `BMS_uiChargeStatus`: `CHARGING`
- `BMS_hvState`: `UP_FOR_CHARGE`
- raw BMS maximum charge current: 250 A
- PCS 12 V support: active at 14.22 V, supplying about 33-40 A to the AGM bus
- PCS DCDC support/rationality faults: cleared
- charge-port `0x21D`: `2E 18 49 0C AC 00 60 01`, decoded as EVSE request
  asserted and AC charge state `ENABLED`

The verified live `0x3A1` pair was `88 42 0B C8 00 10 A2 5A` followed by
`03 00 98 6E BE 00 B0 82`, exactly matching the successful Ingenext trace.

This did **not** establish physical AC charging. The charge-port light remained
off and the EVSE showed no power delivery. The roughly 2.1 A / 756 W seen at
the pack is consistent with the PCS DCDC converter supplying the heavily
loaded 12 V AGM bank, not with AC entering through the onboard charger. The
BMS status and charge-port `ENABLED` state are therefore necessary protocol
milestones, but are not proof of OBC energy flow.

The successful Ingenext capture also contains static frame `0x052` at about
100.2 ms:

```text
85 9B E4 27 65 28 30 00
```

It is absent from the Battery Emulator baseline and from the first integrated
firmware. The current public Model 3/Y DBC does not decode it. Because an
unsuccessful trace also contains `0x052`, it is not sufficient by itself. The
second integrated firmware adds this exact static frame only while charge mode
is active.

The second August 10 live test verified `0x052` on the bus at exactly 100 ms.
Starting charge mode again reached the same BMS/PCS CAN state but did not begin
physical charging until the operator pressed the button on the connected EVSE.
After that EVSE-side trigger, the pack stabilized at approximately 2.4-2.5 A
and 864-900 W inward at 363 V. At the same time the PCS DCDC was supplying the
AGM bank with approximately 24.5-25.4 A at 14.1-14.2 V (about 350 W). The
positive inward pack power therefore cannot be explained by the DCDC load and
confirms AC energy flow through the onboard charger. The operator also
confirmed that the physical charge-port light turned green.

The live charge-port status remained `2D 18 21 0C 80 00 60 01`, with AC charge
state `ENABLED` but Tesla SWCAN/digital communication and the decoded EVSE
request bit inactive. This shows that this EVSE can initiate analog-pilot
charging with its local button even when the Tesla digital handshake is not
established. The required operator sequence for this setup is therefore:

1. Connect and power the EVSE.
2. Start Battery Emulator charge mode.
3. Press the EVSE button to initiate power delivery.

The remaining CP lost-communication alerts are not the immediate blocker: the
successful Ingenext trace contains the same missing GTW, VCSEC, VCFRONT, and UI
status bits while physical charging is underway.

## Firmware injector specification

The integrated implementation keeps one producer per overlapping CAN ID and
switches the existing Tesla transmit profile while charge mode is active:

1. Start `0x053` at 20 ms using the observed three-state startup sequence.
2. Start `0x055` at 10 ms with both counters and its additive checksum.
3. Continue or inject `0x056` at 99 ms only if there is no existing producer;
   two producers with unsynchronized counters should not coexist.
4. Inject the measured static `0x052` payload at 100 ms while charge mode is
   active.
5. Replace, rather than duplicate, the normal `0x118`, `0x221`, `0x3A1`, and
   `0x3C2` producers with the successful Ingenext profiles.
6. After the 3.14-second startup stage, set `0x118` byte 2 to `0xE9` and byte 5
   to `0x48`; preserve rolling counters and recompute each checksum.
7. On stop, restore the normal Battery Emulator `0x118` drive profile and stop
   `0x052`.

This is trace-derived, unit tested, and live tested with independently
confirmed inward pack power. BMS status or DCDC current alone must still not be
used as proof of charging; the decisive live evidence was sustained positive
pack power after the EVSE-side start command.
