"""Expose only the DEV desktop isolation helper to test subprocesses."""
import importlib.util
import sys
from pathlib import Path

helper = Path(__file__).resolve().parents[2] / 'python/desktop_app/test_desktop.py'
spec = importlib.util.spec_from_file_location('desktop_app.test_desktop', helper)
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)
