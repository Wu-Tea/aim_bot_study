"""Exercise relocated entry points without relying on the caller's directory."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


PROJECT_ROOT = Path(__file__).resolve().parents[2]


class RepositoryLayoutTests(unittest.TestCase):
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
