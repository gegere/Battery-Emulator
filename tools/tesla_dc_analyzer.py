#!/usr/bin/env python3
"""Offline Tesla charging trace analysis; never opens a CAN or network interface.

Reads Battery-Emulator text logs or SavvyCAN CSV and a caller-supplied DBC.
Only explicitly selected, little-endian signals are decoded. Signal names and
enums are hypotheses for an ECU whose firmware does not match the supplied DBC.
"""

import argparse
import csv
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
from collections import Counter, defaultdict


SELECTED = {
    0x13D: {"CP_hvChargeStatus", "CP_chargeShutdownRequest", "CP_evseChargeType",
            "CP_vehiclePrechargeRequired"},
    0x43D: {"CP_hvChargeStatus_log", "CP_chargeShutdownRequest_log", "CP_evseChargeType_log",
            "CP_vehiclePrechargeRequired_log"},
    0x20A: {
        "HVP_packContactorSetState", "HVP_fcContactorSetState",
        "HVP_fcContNegativeState", "HVP_fcContPositiveState",
        "HVP_fcLinkAllowedToEnergize", "HVP_hvilStatus",
    },
    0x21D: {
        "CP_proximity", "CP_pilot", "CP_teslaSwcanState",
        "CP_iecComboState", "CP_digitalCommsEstablished", "CP_teslaDcState",
    },
    0x212: {"BMS_state", "BMS_uiChargeStatus", "BMS_contactorState"},
    0x232: {"BMS_fcContactorRequest", "BMS_packContactorRequest"},
    0x7FF: {"GTW_plcSupportType"},
    0x333: {"UI_chargeEnableRequest", "UI_chargeTerminationPct"},
    0x29D: {"CP_evseOutputDcVoltage", "CP_evseOutputDcCurrent", "CP_evseOutputDcCurrentStale"},
}


@dataclass(frozen=True)
class Frame:
    time: float
    bus: int
    direction: str
    can_id: int
    data: bytes


@dataclass(frozen=True)
class Signal:
    name: str
    start: int
    width: int
    signed: bool
    factor: float
    offset: float
    mux: int | None
    choices: dict


def load_dbc(path):
    """Extract the supported subset, failing if a selected field is big-endian."""
    signals = defaultdict(list)
    enums = {}
    names = {}
    text = Path(path).read_text()
    for mid, name, pairs in re.findall(r'^VAL_ (\d+) (\w+) (.*);', text, re.M):
        enums[(int(mid), name)] = {
            int(value): label for value, label in re.findall(r'(-?\d+) "([^"]*)"', pairs)
        }
    mid = None
    pattern = re.compile(
        r' SG_ (\w+)(?: (M|m\d+))? : (\d+)\|(\d+)@(\d)([+-]) '
        r'\(([^,]+),([^\)]+)\)'
    )
    for line in text.splitlines():
        message = re.match(r'BO_ (\d+) (\S+):', line)
        if message:
            mid = int(message[1])
            names[mid] = message[2]
        match = pattern.match(line)
        if not match or match[1] not in SELECTED.get(mid, set()):
            continue
        name, mux, start, width, endian, sign, factor, offset = match.groups()
        if endian != '1':
            raise ValueError(f"Selected signal {name} is not little-endian")
        if mux and (mid != 0x7FF or not mux.startswith('m')):
            raise ValueError(f"Unsupported multiplexing for {name}")
        signals[mid].append(Signal(
            name, int(start), int(width), sign == '-', float(factor), float(offset),
            int(mux[1:]) if mux else None, enums.get((mid, name), {}),
        ))
    missing = sorted(
        name for mid, wanted in SELECTED.items()
        for name in wanted - {s.name for s in signals[mid]}
    )
    return signals, names, missing


def read_frames(path):
    path = Path(path)
    if path.suffix.lower() == '.csv':
        with path.open(newline='') as source:
            for row in csv.DictReader(source):
                length = int(row['LEN'])
                if not 0 <= length <= 64:
                    raise ValueError(f"Invalid CSV length: {length}")
                direction = row['Dir'].lower()
                if direction not in ('rx', 'tx'):
                    raise ValueError(f"Unknown CSV direction: {direction}")
                yield Frame(
                    int(row['Time Stamp']) / 1_000_000, int(row['Bus']), direction,
                    int(row['ID'], 16), bytes(int(row[f'D{i}'], 16) for i in range(1, length + 1)),
                )
        return
    pattern = re.compile(r'\(([\d.]+)\) (rx|tx)(\d+) ([\da-f]+) \[(\d+)\]((?: [\da-f]{2})*)', re.I)
    for number, line in enumerate(path.read_text().splitlines(), 1):
        if not line.strip() or line.startswith('#'):
            continue
        match = pattern.fullmatch(line.strip())
        if not match:
            raise ValueError(f"Malformed frame at {path}:{number}")
        timestamp, direction, channel, mid, length, payload = match.groups()
        direction = direction.lower()
        channel = int(channel)
        if channel % 2 != (direction == 'tx'):
            raise ValueError(f"Unexpected Battery-Emulator direction/channel at line {number}")
        data = bytes.fromhex(payload)
        if len(data) != int(length) or len(data) > 64:
            raise ValueError(f"DLC mismatch at line {number}")
        # Firmware format_can_frame encodes RX as 2*interface, TX as 2*interface+1.
        yield Frame(float(timestamp), channel // 2, direction, int(mid, 16), data)


def decode(frame, signals):
    raw = int.from_bytes(frame.data, 'little')
    decoded = {}
    for signal in signals.get(frame.can_id, []):
        if signal.start + signal.width > len(frame.data) * 8:
            continue
        # The selected 0x7FF mux is the low byte; no other multiplexed frame is decoded.
        if signal.mux is not None and (not frame.data or frame.data[0] != signal.mux):
            continue
        value = (raw >> signal.start) & ((1 << signal.width) - 1)
        if signal.signed and value & (1 << (signal.width - 1)):
            value -= 1 << signal.width
        decoded[signal.name] = {
            'raw': value,
            'value': value * signal.factor + signal.offset,
            'label': signal.choices.get(value),
        }
    # The DBC stale flag applies to CURRENT. Do not relabel it as a voltage-validity bit.
    return decoded


def analyze(frames, signals, names):
    frames = sorted(frames, key=lambda frame: frame.time)
    inventory = Counter()
    lengths = defaultdict(set)
    state_changes = []
    previous = {}
    alerts = defaultdict(Counter)
    for frame in frames:
        key = (frame.bus, frame.direction, frame.can_id)
        inventory[key] += 1
        lengths[key].add(len(frame.data))
        if frame.can_id == 0x31E and frame.direction == 'rx' and frame.data:
            alerts[frame.data[0] & 0x0F][frame.data.hex(' ')] += 1
        values = decode(frame, signals)
        if not values or previous.get(key) == values:
            continue
        previous[key] = values
        state_changes.append({
            'time_s': frame.time, 'bus': frame.bus, 'direction': frame.direction,
            'id': f'0x{frame.can_id:03X}', 'data': frame.data.hex(' '), 'signals': values,
        })
    return {
        'frame_count': len(frames),
        'first_time_s': frames[0].time if frames else None,
        'last_time_s': frames[-1].time if frames else None,
        'inventory': [
            {'bus': b, 'direction': d, 'id': f'0x{i:03X}', 'name': names.get(i),
             'count': count, 'lengths': sorted(lengths[(b, d, i)])}
            for (b, d, i), count in sorted(inventory.items())
        ],
        'state_changes': state_changes,
        'cp_alert_payloads_by_mux': {str(mux): dict(counts) for mux, counts in sorted(alerts.items())},
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--dbc', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    signals, names, missing = load_dbc(args.dbc)
    result = analyze(read_frames(args.trace), signals, names)
    result['provenance'] = {
        'trace': str(args.trace), 'trace_sha256': hashlib.sha256(args.trace.read_bytes()).hexdigest(),
        'dbc': str(args.dbc), 'dbc_sha256': hashlib.sha256(args.dbc.read_bytes()).hexdigest(),
        'missing_selected_signals': missing,
        'limitations': [
            'DBC interpretations require validation against the actual ECU firmware.',
            'Snapshot-derived traces contain gaps; absence of a frame is not proof it was never sent.',
            'A transmitted frame is not proof of ECU acceptance.',
            'Vehicle CAN and CP-CAN are different segments; confirm the physical capture point.',
            'CAN does not expose the complete CCS PLC exchange on the control pilot.',
            'Unknown alert multiplexers are preserved as raw payloads, not discarded.',
        ],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(f"{result['frame_count']} frames; {len(result['state_changes'])} state changes; {args.output}")
    if missing:
        print('Missing DBC fields: ' + ', '.join(missing))


if __name__ == '__main__':
    main()
