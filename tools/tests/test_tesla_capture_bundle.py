import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tesla_capture_bundle import build_bundle, parse_capture


class CaptureBundleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.output = self.root / "evidence.zip"

    def capture(self, text, filename="trace.txt", segment="Vehicle-CAN", clock="emulator-uptime"):
        path = self.root / filename
        path.write_bytes(text.encode())
        return {"path": path, "segment": segment, "clock": clock}

    def test_raw_bytes_duplicates_unknown_frames_and_checksums_are_preserved(self):
        text = "# operator note\r\n(1.000) rx0 31e [8] 02 08 00 00 00 00 00 00\r\n"
        text += "(1.001) tx1 555 [1] a5\n" * 2
        capture = self.capture(text)
        manifest = build_bundle([capture], self.output, "test-build")
        self.assertEqual(manifest["captures"][0]["quality"]["exact_duplicate_records_retained"], 1)
        with zipfile.ZipFile(self.output) as archive:
            self.assertEqual(archive.read("captures/01-trace.txt"), text.encode())
            for name, expected in manifest["files"].items():
                data = archive.read(name)
                self.assertEqual(hashlib.sha256(data).hexdigest(), expected["sha256"])
                self.assertEqual(len(data), expected["bytes"])
            self.assertEqual(json.loads(archive.read("manifest.json")), manifest)

    def test_clocks_segments_and_same_basename_are_kept_separate(self):
        first = self.capture("(1.0) rx0 21d [1] 04\n")
        other = self.root / "other"
        other.mkdir()
        second_path = other / "trace.txt"
        second_path.write_text("(90.0) rx0 21d [1] 0c\n")
        second = {"path": second_path, "segment": "CP-CAN", "clock": "independent-reference"}
        manifest = build_bundle([first, second], self.output, "test-build")
        self.assertEqual([c["clock"] for c in manifest["captures"]], ["emulator-uptime", "independent-reference"])
        self.assertEqual([c["segment"] for c in manifest["captures"]], ["Vehicle-CAN", "CP-CAN"])
        self.assertNotEqual(manifest["captures"][0]["archive_path"], manifest["captures"][1]["archive_path"])

    def test_reboot_or_snapshot_overlap_is_flagged_without_invented_chronology(self):
        capture = self.capture("(20.0) rx0 21d [1] 04\n(1.0) rx0 21d [1] 0c\n")
        record = build_bundle([capture], self.output, "test")["captures"][0]
        self.assertEqual(record["quality"]["timestamp_regressions"], 1)
        self.assertIn("analysis_skipped", record)
        self.assertNotIn("communication_audit", record)

    def test_empty_and_malformed_logs_are_not_treated_as_clean_sessions(self):
        record = build_bundle([self.capture("")], self.output, "test")["captures"][0]
        self.assertIn("No frames", record["analysis_skipped"])
        self.output.unlink()
        with self.assertRaises(ValueError):
            build_bundle([self.capture("(1.0) rx0 21d [2] 04\n")], self.output, "test")
        self.assertFalse(self.output.exists())

    def test_existing_evidence_is_not_overwritten(self):
        self.output.write_bytes(b"existing evidence")
        with self.assertRaises(FileExistsError):
            build_bundle([self.capture("")], self.output, "test")
        self.assertEqual(self.output.read_bytes(), b"existing evidence")

    def test_dbc_and_notes_are_included_with_decode_and_missing_signal_notice(self):
        dbc = self.root / "test.dbc"
        dbc.write_text('BO_ 541 CP: 8 VehicleBus\n SG_ CP_proximity : 2|2@1+ (1,0) [0|3] "" Receiver\n')
        notes = self.root / "notes.txt"
        notes.write_text("Plugged in at emulator uptime 1s; logger cutoff=0")
        capture = self.capture("(1.0) rx0 21d [1] 0c\n")
        manifest = build_bundle([capture], self.output, "test", dbc=dbc, notes=notes)
        with zipfile.ZipFile(self.output) as archive:
            self.assertEqual(archive.read("reference/decoder.dbc"), dbc.read_bytes())
            self.assertEqual(archive.read("session-notes.txt"), notes.read_bytes())
            decoded = json.loads(archive.read(manifest["captures"][0]["decoded_analysis"]))
            self.assertEqual(decoded["state_changes"][0]["signals"]["CP_proximity"]["raw"], 3)
            self.assertTrue(decoded["missing_selected_signals"])

    def test_capture_argument_preserves_equals_in_path_and_requires_clock(self):
        result = parse_capture("Vehicle-CAN,uptime=with=equals.txt")
        self.assertEqual(result["path"], Path("with=equals.txt"))
        with self.assertRaises(ValueError):
            build_bundle([], self.output, "test")
        import argparse
        with self.assertRaises(argparse.ArgumentTypeError):
            parse_capture("Vehicle-CAN=file.txt")


if __name__ == "__main__":
    unittest.main()
