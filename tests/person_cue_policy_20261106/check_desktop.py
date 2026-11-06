"""Validate desktop settings against a candidate runtime without replacing dev."""
import argparse
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'python'), str(ROOT / 'python/tests')]
from desktop_app.runtime import RuntimeManager

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--runtime', type=Path, default=ROOT / 'runs/person-cue-policy-20261106/runtime-build/Release/cod_native_runtime.exe')
parser.add_argument('--output', type=Path, default=ROOT / 'runs/person-cue-policy-20261106/desktop-tests.txt')
args = parser.parse_args()
candidate = args.runtime.resolve(strict=True)
original_init = RuntimeManager.__init__

def candidate_init(self, root):
    original_init(self, root)
    self.executable = candidate

suite = unittest.defaultTestLoader.loadTestsFromNames([
    'test_desktop_web_service', 'test_desktop_app',
    'test_desktop_profiles_curves', 'test_desktop_ads_geometry',
])
args.output.parent.mkdir(parents=True, exist_ok=True)
with patch.object(RuntimeManager, '__init__', candidate_init), args.output.open('w', encoding='utf-8') as output:
    result = unittest.TextTestRunner(stream=output, verbosity=2).run(suite)
print(f'desktop: tests={result.testsRun} failures={len(result.failures)} errors={len(result.errors)}')
sys.exit(not result.wasSuccessful())
