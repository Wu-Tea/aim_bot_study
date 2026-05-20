import tempfile
import unittest
from pathlib import Path

from vision.recoil_collection.calibration import (
    RecoilControlCalibration,
    load_calibration,
    save_calibration,
    stick_from_pixels,
)


class RecoilCalibrationTests(unittest.TestCase):
    def test_stick_from_pixels_uses_measured_response(self):
        calibration = RecoilControlCalibration(
            game="cod22",
            aim_mode="ads",
            stance="standing",
            pixels_per_full_stick_x_per_second=500.0,
            pixels_per_full_stick_y_per_second=1000.0,
            created_at="2026-05-20T00:00:00Z",
        )

        self.assertEqual(stick_from_pixels(50.0, axis="y", duration_ms=100, calibration=calibration), 16384)
        self.assertEqual(stick_from_pixels(-50.0, axis="y", duration_ms=100, calibration=calibration), -16384)

    def test_save_and_load_calibration(self):
        calibration = RecoilControlCalibration(
            game="cod22",
            aim_mode="hipfire",
            stance="standing",
            pixels_per_full_stick_x_per_second=400.0,
            pixels_per_full_stick_y_per_second=800.0,
            created_at="2026-05-20T00:00:00Z",
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "cod22-hipfire-standing.json"
            save_calibration(path, calibration)
            loaded = load_calibration(path)

        self.assertEqual(loaded, calibration)


if __name__ == "__main__":
    unittest.main()
