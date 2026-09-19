import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


SPEC = importlib.util.spec_from_file_location(
    "tesla_dc_analyzer", Path(__file__).resolve().parents[1] / "tesla_dc_analyzer.py"
)
analyzer = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = analyzer
SPEC.loader.exec_module(analyzer)

# Small factual signal fixtures use the Model3CAN reference positions. They
# exercise parsing and extraction without downloading a changing DBC in tests.
DBC = '''BO_ 2047 ID7FFVehicleConfig: 8 VehicleBus
 SG_ GTW_carConfigMux M : 0|8@1+ (1,0) [0|255] "" Receiver
 SG_ GTW_plcSupportType m3 : 28|2@1+ (1,0) [0|2] "" Receiver
BO_ 541 ID21DCP_evseStatus: 8 VehicleBus
 SG_ CP_iecComboState : 60|4@1+ (1,0) [0|12] "" Receiver
 SG_ CP_teslaSwcanState : 34|3@1+ (1,0) [0|6] "" Receiver
BO_ 669 ID29DCP_dcChargeStatus: 4 VehicleBus
 SG_ CP_evseOutputDcCurrent : 0|15@1- (0.0732467,0) [-1200|1200] "A" Receiver
 SG_ CP_evseOutputDcCurrentStale : 29|1@1+ (1,0) [0|1] "" Receiver
BO_ 522 ID20AHVP: 6 VehicleBus
 SG_ HVP_fcContactorSetState : 19|4@1+ (1,0) [0|9] "" Receiver
BO_ 1085 ID43DCP_chargeStatusLog: 6 VehicleBus
 SG_ CP_hvChargeStatus_log : 0|3@1+ (1,0) [0|6] "" Receiver
 SG_ CP_evseChargeType_log : 40|2@1+ (1,0) [0|2] "" Receiver
VAL_ 2047 GTW_plcSupportType 0 "NONE" 2 "NATIVE_CHARGE_PORT";
VAL_ 541 CP_iecComboState 0 "INACTIVE" 8 "ENABLED";
VAL_ 541 CP_teslaSwcanState 1 "ACCEPT" 4 "FAULT";
'''


class AnalyzerTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.dbc = self.root / 'signals.dbc'
        self.dbc.write_text(DBC)
        self.signals, self.names, self.missing = analyzer.load_dbc(self.dbc)

    def frame(self, mid, payload):
        return analyzer.Frame(0, 0, 'rx', mid, bytes.fromhex(payload))

    def test_native_plc_is_only_decoded_in_mux_three(self):
        values = analyzer.decode(self.frame(0x7FF, '03 00 08 68 00 00 00 12'), self.signals)
        self.assertEqual(values['GTW_plcSupportType']['label'], 'NATIVE_CHARGE_PORT')
        self.assertEqual(analyzer.decode(self.frame(0x7FF, '02 00 08 68 00 00 00 12'), self.signals), {})

    def test_accept_is_not_ccs_enabled(self):
        values = analyzer.decode(self.frame(0x21D, '0d 00 49 80 04 00 20 01'), self.signals)
        self.assertEqual(values['CP_teslaSwcanState']['label'], 'ACCEPT')
        self.assertEqual(values['CP_iecComboState']['label'], 'INACTIVE')
        enabled = analyzer.decode(self.frame(0x21D, '3c 00 00 00 00 00 00 80'), self.signals)
        self.assertEqual(enabled['CP_iecComboState']['label'], 'ENABLED')

    def test_dora_fast_contactors_open_reference_closed(self):
        opened = analyzer.decode(self.frame(0x20A, 'f6 15 09 82 18 01'), self.signals)
        closed = analyzer.decode(self.frame(0x20A, '36 65 2e a2 18 21'), self.signals)
        self.assertEqual(opened['HVP_fcContactorSetState']['raw'], 1)
        self.assertEqual(closed['HVP_fcContactorSetState']['raw'], 5)

    def test_connected_log_does_not_imply_dc_charger_recognition(self):
        values = analyzer.decode(self.frame(0x43D, '01 00 ff 1f ff 00'), self.signals)
        self.assertEqual(values['CP_hvChargeStatus_log']['raw'], 1)
        self.assertEqual(values['CP_evseChargeType_log']['raw'], 0)

    def test_signed_current_and_its_stale_flag_are_preserved(self):
        values = analyzer.decode(self.frame(0x29D, 'ff 7f 00 20'), self.signals)
        self.assertEqual(values['CP_evseOutputDcCurrent']['raw'], -1)
        self.assertAlmostEqual(values['CP_evseOutputDcCurrent']['value'], -0.0732467)
        self.assertEqual(values['CP_evseOutputDcCurrentStale']['raw'], 1)

    def test_short_frame_does_not_invent_missing_status_bits(self):
        self.assertEqual(analyzer.decode(self.frame(0x21D, '0c 00'), self.signals), {})

    def test_logger_rx0_tx1_are_same_physical_bus(self):
        path = self.root / 'trace.txt'
        path.write_text('(1.000) rx0 21d [1] 04\n(1.001) tx1 333 [1] 04\n(1.002) rx2 20a [1] 00\n')
        frames = list(analyzer.read_frames(path))
        self.assertEqual([f.bus for f in frames], [0, 0, 1])
        self.assertEqual([f.direction for f in frames], ['rx', 'tx', 'rx'])

    def test_malformed_lengths_and_channel_parity_are_rejected(self):
        path = self.root / 'trace.txt'
        for line in ['(1.000) rx0 21d [8] 04', '(1.000) rx1 21d [1] 04']:
            path.write_text(line)
            with self.assertRaises(ValueError):
                list(analyzer.read_frames(path))

    def test_savvycan_uses_microseconds_and_bus_column(self):
        path = self.root / 'trace.csv'
        path.write_text('Time Stamp,ID,Extended,Dir,Bus,LEN,D1,D2\n1234567,0000021D,false,Rx,2,1,04,00\n')
        frame, = analyzer.read_frames(path)
        self.assertEqual((frame.time, frame.bus, frame.data), (1.234567, 2, b'\x04'))

    def test_unknown_alert_mux_and_both_directions_remain_visible(self):
        rx = self.frame(0x31E, '02 08 00 00 00 00 00 00')
        tx = analyzer.Frame(1, 0, 'tx', rx.can_id, rx.data)
        report = analyzer.analyze([rx, tx], self.signals, self.names)
        self.assertEqual(report['cp_alert_payloads_by_mux']['2'][rx.data.hex(' ')], 1)
        self.assertEqual(len(report['inventory']), 2)

    def test_unsupported_endianness_is_explicit(self):
        self.dbc.write_text(DBC.replace('28|2@1+', '28|2@0+'))
        with self.assertRaisesRegex(ValueError, 'not little-endian'):
            analyzer.load_dbc(self.dbc)


if __name__ == '__main__':
    unittest.main()
