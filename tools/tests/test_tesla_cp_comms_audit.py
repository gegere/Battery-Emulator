import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tesla_dc_analyzer import Frame
from tesla_cp_comms_audit import release_witness, summarize


def frame(mid, data, direction="rx", bus=0, time=1.0):
    return Frame(time, bus, direction, mid, bytes.fromhex(data))


class CpCommsAuditTests(unittest.TestCase):
    def test_captured_mux0_has_all_four_warnings(self):
        report = summarize([frame(0x31E, "00 00 01 00 08 00 0d 00")])
        self.assertEqual(report["valid_rx_cp_mux0_count"], 1)
        self.assertEqual(set(report["cp_comms_asserted_counts"].values()), {1})

    def test_short_unknown_mux_and_tx_cannot_count_as_warning_reports(self):
        report = summarize([frame(0x31E, "00 00 01"),
                            frame(0x31E, "02 08 00 00 00 00 00 00"),
                            frame(0x31E, "00 00 01 00 08 00 0d 00", "tx")])
        self.assertEqual(report["valid_rx_cp_mux0_count"], 0)
        self.assertEqual(set(report["cp_comms_asserted_counts"].values()), {0})
        self.assertEqual(len(report["raw_rx_cp_alert_payloads"]), 2)

    def test_interface_and_direction_do_not_merge(self):
        report = summarize([frame(0x339, "01", "tx", 0, 1),
                            frame(0x339, "01", "tx", 0, 1.1),
                            frame(0x339, "02", "rx", 0, 1.02),
                            frame(0x339, "03", "tx", 1, 1.05)])
        self.assertEqual(len(report["candidate_traffic"]), 3)
        tx = next(x for x in report["candidate_traffic"] if x["bus"] == 0 and x["direction"] == "tx")
        self.assertAlmostEqual(tx["median_observed_gap_s"], 0.1)

    def test_stop_requires_profile_evidence_not_charge_enable(self):
        self.assertIsNone(release_witness(frame(0x333, "04 30 20 07 02", "tx")))
        self.assertIsNone(release_witness(frame(0x118, "59 8f e9 00 00 48 00 00", "tx")))
        self.assertIsNotNone(release_witness(frame(0x118, "59 8f e9 00 00 48 00 80", "tx")))
        self.assertIsNone(release_witness(frame(0x118, "59 8f e9 00 00 48 00 80")))

    def test_clean_reference_and_empty_input_are_distinct(self):
        self.assertEqual(summarize([])["valid_rx_cp_mux0_count"], 0)
        clean = summarize([frame(0x31E, "00 00 00 00 00 00 00 00")])
        self.assertEqual(clean["valid_rx_cp_mux0_count"], 1)
        self.assertEqual(set(clean["cp_comms_asserted_counts"].values()), {0})


if __name__ == "__main__":
    unittest.main()
