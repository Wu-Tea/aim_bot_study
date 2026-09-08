import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('mouse_diagnostics', ROOT / 'scripts/verify/analyze_mouse_diagnostics.py')
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)


class MouseDiagnosticTests(unittest.TestCase):
    def test_real_writer_json_and_analysis(self):
        executable = ROOT / 'native/vision_native/build/Release/cod_native_mouse_diagnostics_tests.exe'
        if not executable.exists():
            self.skipTest('Native diagnostic fixture must be built')
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run([str(executable), directory], capture_output=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            folders = list(Path(directory).iterdir())
            normal = next(p for p in folders if json.loads((p / 'summary.json').read_text())['written'] == 100)
            report = analysis.analyze(normal)
            self.assertTrue(report['complete_log'])
            self.assertEqual(report['counts']['ticks'], 100)
            self.assertEqual(report['count_totals']['submitted'], [5050, -5050])
            self.assertEqual(report['count_totals']['source'], report['count_totals']['submitted'])
            segment = sorted(normal.glob('ticks-*.jsonl'))[-1]
            record = json.loads(segment.read_text().splitlines()[0])
            self.assertEqual(record['point_tolerance_px'], 3)
            self.assertEqual(record['point_inside'], [1, 0])
            self.assertEqual(record['source_aim_px'], [321, 256])
            self.assertEqual(record['selector_generation'], 9)
            self.assertAlmostEqual(record['bodylock_position_u'][0], .025)
            self.assertAlmostEqual(record['bodylock_motion_u'][0], -.2)
            self.assertAlmostEqual(record['bodylock_effective_motion_u'][0], -.0125)
            with segment.open('a') as output:
                output.write('{"type":')
            report = analysis.analyze(normal)
            self.assertFalse(report['complete_log'])
            self.assertEqual(report['counts']['malformed_lines'], 1)


if __name__ == '__main__':
    unittest.main()
