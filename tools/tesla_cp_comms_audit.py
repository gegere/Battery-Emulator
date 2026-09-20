#!/usr/bin/env python3
"""Audit saved Tesla CAN logs for CP communication warnings and transmit evidence.

No device/network access. Message presence is not proof of receiver acceptance.
The alert mapping follows TESLA-BATTERY.cpp; the DBC/ECU generation must match
before attributing meaning to a reference capture. Gaps include capture loss.
"""

import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
from statistics import median

from tesla_dc_analyzer import read_frames


CANDIDATES = {
    0x7FF: "GTW_carConfig",
    0x339: "VCSEC_authentication (emulator source label)",
    0x221: "VCFRONT_LVPowerState",
    0x3A1: "VCFRONT_vehicleStatus (version-dependent layout)",
    0x333: "UI_chargeRequest",
    0x353: "UI_status",
    0x00C: "UI_status alternate ID in reference DBC",
    0x33A: "UI_rangeSOC",
    0x1F9: "Unmapped reference traffic; no proven warning association",
    0x13D: "CP_chargeStatus",
    0x43D: "CP_chargeStatusLog",
}
ALERT_BITS = {13: 16, 45: 48, 47: 50, 48: 51}


def release_witness(frame):
    """Match emitted profiles, never infer a stop solely from charge-enable."""
    if frame.direction != "tx":
        return None
    data = frame.data
    if (frame.can_id == 0x118 and len(data) == 8
            and data[2] == 0xE9 and data[5] == 0x48 and data[7] == 0x80):
        return "0x118 steady charge profile with stop_requested byte"
    patterns = {
        0x207: ("00 00 00 00 00 28 28 00", "release_active"),
        0x500: ("01 01", "release_active"),
        0x241: ("3c 3c 16 0f 8f 55 00", "release_observed"),
    }
    if frame.can_id in patterns:
        payload, state = patterns[frame.can_id]
        if data == bytes.fromhex(payload):
            return f"0x{frame.can_id:03X} {state} profile"
    return None


def summarize(frames):
    groups = defaultdict(list)
    mux0 = []
    raw_alerts = Counter()
    witnesses = defaultdict(list)
    for frame in sorted(frames, key=lambda f: f.time):
        if frame.can_id in CANDIDATES:
            groups[(frame.bus, frame.direction, frame.can_id)].append(frame)
        if frame.can_id == 0x31E and frame.direction == "rx" and frame.data:
            raw_alerts[frame.data.hex(" ")] += 1
            if len(frame.data) == 8 and frame.data[0] & 15 == 0:
                mux0.append(frame)
        witness = release_witness(frame)
        if witness:
            witnesses[(frame.bus, witness)].append(frame)
    traffic = []
    for (bus, direction, mid), values in sorted(groups.items()):
        gaps = [b.time - a.time for a, b in zip(values, values[1:])]
        traffic.append({
            "bus": bus, "direction": direction, "id": f"0x{mid:03X}",
            "label": CANDIDATES[mid], "count": len(values),
            "first_s": values[0].time, "last_s": values[-1].time,
            "median_observed_gap_s": median(gaps) if gaps else None,
            "max_observed_gap_s": max(gaps) if gaps else None,
            "unique_payloads": len({f.data for f in values}),
            "top_payloads": Counter(f.data.hex(" ") for f in values).most_common(5),
        })
    return {
        "frame_count": len(frames),
        "valid_rx_cp_mux0_count": len(mux0),
        "cp_comms_asserted_counts": {
            f"CP_a{code:03}": sum((int.from_bytes(f.data, "little") >> bit) & 1 for f in mux0)
            for code, bit in ALERT_BITS.items()
        },
        "raw_rx_cp_alert_payloads": raw_alerts.most_common(),
        "candidate_traffic": traffic,
        "candidate_ids_not_observed": [f"0x{mid:03X}" for mid in CANDIDATES
                                       if not any(k[2] == mid for k in groups)],
        "tx_release_profile_witnesses": [
            {"bus": bus, "signature": name, "count": len(values),
             "first_s": values[0].time, "last_s": values[-1].time}
            for (bus, name), values in sorted(witnesses.items())
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    reports = []
    for path in args.inputs:
        reports.append({"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                        **summarize(list(read_frames(path)))})
    args.output.write_text(json.dumps({
        "limitations": ["Snapshot loss affects intervals and absence claims.",
                        "TX on the emulator does not establish receipt on CP-CAN.",
                        "Alert mapping and profile signatures are ECU/firmware dependent.",
                        "Correlation with successful CCS does not establish causal requirements."],
        "reports": reports,
    }, indent=2) + "\n")


if __name__ == "__main__":
    main()
