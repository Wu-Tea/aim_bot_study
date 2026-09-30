"""Exercise relocated entry points without relying on the caller's directory."""

import json
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


PROJECT_ROOT = Path(__file__).resolve().parents[2]


class RepositoryLayoutTests(unittest.TestCase):
    def test_matrix_wrapper_calls_project_powershell_comparator(self):
        from tools.verify import compare_oscillation_matrix

        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory) / 'baseline'
            candidate = Path(directory) / 'candidate'
            for folder in (base, candidate):
                folder.mkdir()
                identity = {'config_sha256': 'fixture', 'matrix': [{'id': 'case'}],
                            'plant_source': 'assumption'}
                (folder / 'identity.json').write_text(json.dumps(identity))
                report = folder / 'case.json'
                report.write_text('{"fixture": true}')
                execution = [{'id': 'case', 'exit_code': 0,
                              'report_sha256': hashlib.sha256(report.read_bytes()).hexdigest()}]
                (folder / 'execution.json').write_text(json.dumps(execution))

            def run_host(command, **kwargs):
                script = Path(command[command.index('-File') + 1])
                self.assertEqual(script.resolve(),
                                 PROJECT_ROOT / 'scripts/verify/compare_oscillation_matrix.ps1')
                self.assertTrue(script.is_file())
                return subprocess.CompletedProcess(command, 0,
                    json.dumps([{'id': 'case', 'exit_code': 0, 'log': ''}]), '')

            with patch.object(sys, 'argv', ['compare_oscillation_matrix.py',
                                           '--baseline', str(base), '--candidate', str(candidate)]), \
                 patch.object(compare_oscillation_matrix.subprocess, 'run', side_effect=run_host):
                self.assertEqual(compare_oscillation_matrix.main(), 0)

    def run_external(self, arguments):
        env = os.environ.copy()
        env['PYTHONPATH'] = str(PROJECT_ROOT / 'python')
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run(
                [sys.executable, '-B', *arguments], cwd=directory, env=env,
                capture_output=True, text=True, timeout=30,
            )
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout

    def test_fallback_cli_runs_outside_project(self):
        output = self.run_external([str(PROJECT_ROOT / 'python/main.py'), '--help'])
        self.assertIn('--vision-backend', output)
        self.assertIn('--controller-mode', output)

    def test_desktop_preview_resolves_project_assets_outside_project(self):
        output = self.run_external(['-m', 'desktop_app.gui', '--action', 'preview-start'])
        preview = json.loads(output)
        self.assertEqual(Path(preview['config_path']), PROJECT_ROOT / 'config.toml')
        self.assertEqual(Path(preview['executable_path']),
                         PROJECT_ROOT / 'native/build/Release/cod_native_runtime.exe')
        self.assertTrue(preview['model_exists'])

    def test_training_tool_runs_directly_without_pythonpath(self):
        env = os.environ.copy()
        env.pop('PYTHONPATH', None)
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run(
                [sys.executable, '-B', str(PROJECT_ROOT / 'python/tools/train_person_detector.py'), '--help'],
                cwd=directory, env=env, capture_output=True, text=True, timeout=30,
            )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('--data', result.stdout)
        script = PROJECT_ROOT / 'python/tools/train_person_detector.py'
        code = f"import runpy; print(runpy.run_path({str(script)!r})['DEFAULT_DATASET_YAML'])"
        with tempfile.TemporaryDirectory() as directory:
            defaults = subprocess.run(
                [sys.executable, '-B', '-c', code], cwd=directory, env=env,
                capture_output=True, text=True, timeout=30,
            )
        self.assertEqual(defaults.returncode, 0, defaults.stderr)
        self.assertEqual(Path(defaults.stdout.strip()),
                         PROJECT_ROOT / 'training_data/assembled/person_detect_visible/dataset.yaml')


if __name__ == '__main__':
    unittest.main()
