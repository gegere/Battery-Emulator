#!/usr/bin/env python3
"""Package saved CAN captures with provenance and offline analysis. No device I/O.

Example: --capture Vehicle-CAN,emulator-uptime=canlog.txt --firmware BUILD
         --output session.zip [--dbc Model3CAN.dbc] [--notes session-notes.txt]
Each capture keeps its own clock and physical-segment label. No synchronization,
deduplication or merging is performed. Raw files are preserved byte for byte.
"""

import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import tempfile
import zipfile

from tesla_cp_comms_audit import summarize
from tesla_dc_analyzer import analyze, load_dbc, read_frames


LIMITATIONS = [
    "Snapshot exports may omit frames; this bundle cannot recover overwritten data.",
    "Observed gaps are not measured CAN loss; hardware overrun counts are unknown.",
    "Capture clocks are not synchronized or aligned by this tool.",
    "Emulator RX/TX are directions on one interface, not Vehicle CAN versus CP-CAN.",
    "Message presence does not establish delivery or acceptance on a different bus.",
    "DBC meanings and release signatures must match the device firmware.",
    "The bundle is diagnostic evidence, not authorization to charge or close contactors.",
]


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def json_bytes(value):
    return (json.dumps(value, indent=2, allow_nan=False) + "\n").encode()


def parse_capture(value):
    labels, separator, filename = value.partition("=")
    segment, comma, clock = labels.partition(",")
    if not separator or not comma or not segment.strip() or not clock.strip() or not filename:
        raise argparse.ArgumentTypeError("Use SEGMENT,CLOCK=PATH for each --capture")
    return {"segment": segment.strip(), "clock": clock.strip(), "path": Path(filename)}


def capture_quality(frames):
    if any(not math.isfinite(f.time) or f.time < 0 for f in frames):
        raise ValueError("Capture timestamps must be finite and nonnegative")
    differences = [b.time - a.time for a, b in zip(frames, frames[1:])]
    identities = Counter((f.time, f.bus, f.direction, f.can_id, f.data) for f in frames)
    return {
        "frame_count": len(frames),
        "first_record_time_s": frames[0].time if frames else None,
        "last_record_time_s": frames[-1].time if frames else None,
        "timestamp_regressions": sum(gap < 0 for gap in differences),
        "max_observed_forward_gap_s": max((gap for gap in differences if gap >= 0), default=None),
        "exact_duplicate_records_retained": sum(count - 1 for count in identities.values()),
        "observed_interfaces": sorted({f.bus for f in frames}),
        "interface_mapping_notice": "Segment is a user-supplied capture label; map each interface in session notes",
        "hardware_overrun_count": None,
        "logger_cutoff_filter": "unknown; record the configured value in session notes",
    }


def build_bundle(captures, output, firmware, station="Not recorded", dbc=None, notes=None):
    """Validate/analyze frozen copies, then exclusively create an evidence archive."""
    if not captures:
        raise ValueError("At least one capture is required")
    output = Path(output)
    if output.exists():
        raise FileExistsError(output)
    payloads = {
        f"tools/{name}": Path(__file__).with_name(name).read_bytes()
        for name in ("tesla_capture_bundle.py", "tesla_cp_comms_audit.py", "tesla_dc_analyzer.py")
    }
    records = []
    decoder = None
    # Parsing frozen copies ensures the analyzed bytes match the archived hash,
    # even if the caller is still writing a source capture.
    with tempfile.TemporaryDirectory(prefix="tesla-capture-bundle-") as directory:
        temp = Path(directory)
        if dbc is not None:
            dbc_data = Path(dbc).read_bytes()
            payloads["reference/decoder.dbc"] = dbc_data
            frozen_dbc = temp / "decoder.dbc"
            frozen_dbc.write_bytes(dbc_data)
            decoder = load_dbc(frozen_dbc)
        if notes is not None:
            payloads["session-notes.txt"] = Path(notes).read_bytes()
        for index, capture in enumerate(captures, 1):
            source = Path(capture["path"])
            data = source.read_bytes()
            frozen = temp / f"capture-{index}{source.suffix}"
            frozen.write_bytes(data)
            frames = list(read_frames(frozen))
            quality = capture_quality(frames)
            raw_name = f"captures/{index:02d}-{source.name}"
            payloads[raw_name] = data
            record = {
                "source_path": str(source.resolve()), "archive_path": raw_name,
                "segment": capture["segment"], "clock": capture["clock"],
                "sha256": sha256(data), "quality": quality,
            }
            # Sorting across a reboot/wrap or overlapping snapshots would invent
            # a chronology. Keep raw data and ask for explicit epoch separation.
            if quality["timestamp_regressions"]:
                record["analysis_skipped"] = "Clock regression: separate epochs/overlaps before timed analysis"
            elif not frames:
                record["analysis_skipped"] = "No frames; empty input is not evidence of healthy communication"
            else:
                audit_name = f"analysis/{index:02d}-communication.json"
                payloads[audit_name] = json_bytes(summarize(frames))
                record["communication_audit"] = audit_name
                if decoder is not None:
                    signals, names, missing = decoder
                    decoded_name = f"analysis/{index:02d}-decoded.json"
                    payloads[decoded_name] = json_bytes({
                        **analyze(frames, signals, names), "missing_selected_signals": missing,
                    })
                    record["decoded_analysis"] = decoded_name
            records.append(record)
    manifest = {
        "schema_version": 1, "created_utc": datetime.now(timezone.utc).isoformat(),
        "firmware_identity_user_supplied": firmware, "station_user_supplied": station,
        "clock_alignment": "None; retain each capture's time base",
        "limitations": LIMITATIONS, "captures": records,
        "files": {name: {"bytes": len(data), "sha256": sha256(data)} for name, data in payloads.items()},
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive creation prevents overwriting a previous evidence bundle or input.
    with output.open("xb") as destination:
        try:
            with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED) as archive:
                for name, data in payloads.items():
                    archive.writestr(name, data)
                archive.writestr("manifest.json", json_bytes(manifest))
        except Exception:
            output.unlink()
            raise
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", action="append", required=True, type=parse_capture)
    parser.add_argument("--firmware", required=True, help="Exact running build label/hash, or explicitly unknown")
    parser.add_argument("--station", default="Not recorded")
    parser.add_argument("--dbc", type=Path)
    parser.add_argument("--notes", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        manifest = build_bundle(args.capture, args.output, args.firmware, args.station, args.dbc, args.notes)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"Bundle not created: {error}\n")
    print(f"Saved {len(manifest['captures'])} capture(s): {args.output}")
    print(f"SHA-256: {sha256(args.output.read_bytes())}")


if __name__ == "__main__":
    main()
