"""Known bad captures must not be accepted as physical exclusion proof."""
import csv
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('oracle', ROOT / 'scripts/verify/analyze_mouse_virtual_relay.py')
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)
FIELDS = ['qpc', 'kind', 'mode', 'x', 'y', 'buttons', 'wheel', 'hwheel']


class RelayOracleTests(unittest.TestCase):
    def evaluate(self, mutation=None, mode=0):
        source = [dict(zip(FIELDS, [20 + i * 3, 1, mode, 10 if i % 2 else -10, 0, 0, 0, 0])) for i in range(8)]
        sent = []
        raw = []
        for row in source:
            sent.append({**row, 'qpc': row['qpc'] + 1, 'kind': 2,
                         'x': row['x'] * (0 if mode == 1 else -1 if mode == 2 else 1)})
            raw.append({**sent[-1], 'qpc': row['qpc'] + 2, 'kind': 4})
        records = [v for pair in zip(source, sent) for v in pair] + raw
        state = dict(simulation=False, forced_worker_exit=False, worker_exit=0, error=0,
                     receiver_error=False, diagnostics_truncated=False, ready_observed=True,
                     armed_at=10, released_at=100)
        if mutation:
            mutation(records, state)
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            (directory / 'session.json').write_text(json.dumps(state), encoding='utf-8')
            with (directory / 'events.csv').open('w', encoding='utf-8', newline='') as handle:
                writer = csv.DictWriter(handle, fieldnames=FIELDS)
                writer.writeheader(); writer.writerows(records)
            return oracle.analyze(directory)

    def test_valid_pass_block_and_invert(self):
        for mode in (0, 1, 2):
            self.assertTrue(self.evaluate(mode=mode)['desktop_interval_passed'])

    def test_zero_without_source_is_not_proof(self):
        result = self.evaluate(lambda r, s: r.clear(), mode=1)
        self.assertIn('insufficient_physical_source_activity', result['problems'])

    def test_physical_leak_even_when_virtual_matches(self):
        result = self.evaluate(lambda r, s: r.append({**r[0], 'kind': 3}))
        self.assertIn('physical_input_leaked_to_receiver', result['problems'])

    def test_duplicate_or_missing_receipt(self):
        self.assertFalse(self.evaluate(lambda r, s: r.append(dict(r[-1])))['desktop_interval_passed'])
        self.assertFalse(self.evaluate(lambda r, s: r.pop())['desktop_interval_passed'])

    def test_mapper_and_receiver_agreeing_on_wrong_motion(self):
        def mutate(rows, state):
            for row in rows:
                if row['kind'] in (2, 4):
                    row['x'] *= 2
        self.assertIn('source_to_output_mismatch', self.evaluate(mutate)['problems'])

    def test_truncation_failure_simulation_all_rejected(self):
        for key in ('simulation', 'forced_worker_exit', 'receiver_error', 'diagnostics_truncated', 'error'):
            self.assertFalse(self.evaluate(lambda r, s: s.update({key: True}))['desktop_interval_passed'])


if __name__ == '__main__':
    unittest.main()
