import argparse
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import export_trt


class ExportTrtTests(unittest.TestCase):
    def test_explicit_output_stages_weights_without_overwriting_sibling_engine(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            weights = root / "best.pt"
            production_engine = root / "best.engine"
            candidate_engine = root / "best_320x256.engine"
            weights.write_bytes(b"weights")
            production_engine.write_bytes(b"production")
            args = argparse.Namespace(
                weights=weights,
                width=320,
                height=256,
                output=candidate_engine,
                device="0",
                workspace=4,
            )

            def fake_export(staged_weights, _args):
                self.assertNotEqual(staged_weights.parent, root)
                exported = staged_weights.with_suffix(".engine")
                exported.write_bytes(b"candidate")
                return exported

            with (
                mock.patch.object(export_trt, "_parse_args", return_value=args),
                mock.patch.object(export_trt, "_run_export", side_effect=fake_export),
            ):
                export_trt.export_trt([])

            self.assertEqual(production_engine.read_bytes(), b"production")
            self.assertEqual(candidate_engine.read_bytes(), b"candidate")


if __name__ == "__main__":
    unittest.main()
