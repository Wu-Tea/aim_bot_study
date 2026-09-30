"""Repository resources are independent of the Python source/import root."""

from pathlib import Path


PYTHON_ROOT = Path(__file__).resolve().parent
PROJECT_ROOT = PYTHON_ROOT.parent
NATIVE_BUILD_DIR = PROJECT_ROOT / 'native/build/Release'
