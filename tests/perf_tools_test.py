"""Regression checks for the performance gate's failure paths."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location("check_perf", Path(__file__).parents[1] / "tools/check_perf.py")
perf = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(perf)


class PerfGateTest(unittest.TestCase):
    def setUp(self):
        self.baseline = {"scenarios": {"filter8": {
            "checksum": 42, "peak_blocks": 24, "instructions": 1000, "m4_checksum": 43,
        }}}
        self.host = {"filter8": {
            "scenario": "filter8", "metric": "ns", "average": 100, "maximum": 200,
            "checksum": 42, "peak_blocks": 24,
        }}

    def test_matching_output_passes(self):
        self.assertEqual(perf.check_results(self.baseline, self.host, "ns"), [])

    def test_changed_output_fails(self):
        self.host["filter8"]["checksum"] = 99
        self.assertTrue(perf.check_results(self.baseline, self.host, "ns"))

    def test_pool_growth_fails(self):
        self.host["filter8"]["peak_blocks"] = 25
        self.assertTrue(perf.check_results(self.baseline, self.host, "ns"))

    def test_instruction_gate_allows_five_percent_and_rejects_more(self):
        row = self.host["filter8"]
        row.update(metric="instructions", average=1050, maximum=1100, checksum=43)
        self.assertEqual(perf.check_results(self.baseline, self.host, "instructions"), [])
        row["average"] = 1051
        self.assertTrue(perf.check_results(self.baseline, self.host, "instructions"))

    def test_missing_scenario_and_wrong_metric_fail(self):
        self.assertTrue(perf.check_results(self.baseline, {}, "ns"))
        self.host["filter8"]["metric"] = "instructions"
        self.assertTrue(perf.check_results(self.baseline, self.host, "ns"))

    def test_optional_host_deadline_is_enforced(self):
        self.assertTrue(perf.check_results(self.baseline, self.host, "ns", max_block_us=0.1))

    def test_empty_or_duplicate_reports_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "report"
            path.write_text("make output only\n")
            with self.assertRaises(ValueError):
                perf.read_results(path)
            row = '{"scenario":"filter8","metric":"ns","average":1,"maximum":2,"checksum":42,"peak_blocks":24}\n'
            path.write_text(row + row)
            with self.assertRaises(ValueError):
                perf.read_results(path)


if __name__ == "__main__":
    unittest.main()
