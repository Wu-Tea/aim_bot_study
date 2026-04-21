import os
import sys
import unittest
from pathlib import Path

import numpy as np


PROJECT_ROOT = Path(__file__).resolve().parent.parent
NATIVE_BUILD_DIR = PROJECT_ROOT / "native" / "vision_native" / "build" / "Release"


def _load_native_module():
    if not NATIVE_BUILD_DIR.exists():
        raise unittest.SkipTest("native vision build output is not available")

    if hasattr(os, "add_dll_directory"):
        os.add_dll_directory(str(NATIVE_BUILD_DIR))

    sys.path.insert(0, str(NATIVE_BUILD_DIR))
    try:
        import vision_native_cpp  # type: ignore
    except ImportError as exc:  # pragma: no cover - environment-dependent
        raise unittest.SkipTest(f"vision_native_cpp import unavailable: {exc}") from exc
    finally:
        try:
            sys.path.remove(str(NATIVE_BUILD_DIR))
        except ValueError:
            pass

    return vision_native_cpp


class NativeVisionTargetingBridgeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.module = _load_native_module()

    def test_native_target_selector_is_exposed(self):
        self.assertTrue(
            hasattr(self.module, "NativeTargetSelector"),
            "vision_native_cpp should expose NativeTargetSelector for Phase 3B parity work",
        )

    def test_pickup_requires_two_consecutive_frames_before_output(self):
        if not hasattr(self.module, "NativeTargetSelector"):
            self.fail("NativeTargetSelector is missing")

        selector = self.module.NativeTargetSelector(640, 512)
        detections = np.array(
            [
                [280.0, 120.0, 360.0, 320.0, 0.82, 0.0],
            ],
            dtype=np.float32,
        )

        first = selector.select_xyxy(detections)
        self.assertFalse(first["has_target"])
        self.assertEqual(first["boxes_seen"], 1.0)

        result = selector.select_xyxy(detections)

        self.assertTrue(result["has_target"])
        self.assertEqual(result["target_source"], "observed")
        self.assertAlmostEqual(result["target_x"], 320.0, places=3)
        self.assertAlmostEqual(result["target_y"], 196.0, places=3)
        self.assertAlmostEqual(result["dx"], 0.0, places=3)
        self.assertAlmostEqual(result["dy"], -60.0, places=3)
        self.assertEqual(result["boxes_seen"], 1.0)

    def test_low_confidence_pickup_is_rejected(self):
        if not hasattr(self.module, "NativeTargetSelector"):
            self.fail("NativeTargetSelector is missing")

        selector = self.module.NativeTargetSelector(640, 512)
        detections = np.array(
            [
                [280.0, 120.0, 360.0, 320.0, 0.45, 0.0],
            ],
            dtype=np.float32,
        )

        result = selector.select_xyxy(detections)

        self.assertFalse(result["has_target"])
        self.assertEqual(result["boxes_seen"], 1.0)

    def test_switch_requires_two_frames_before_replacing_active_target(self):
        if not hasattr(self.module, "NativeTargetSelector"):
            self.fail("NativeTargetSelector is missing")

        selector = self.module.NativeTargetSelector(640, 512)
        first_target = np.array(
            [
                [280.0, 120.0, 360.0, 320.0, 0.82, 0.0],
            ],
            dtype=np.float32,
        )
        second_target = np.array(
            [
                [420.0, 120.0, 500.0, 320.0, 0.82, 0.0],
            ],
            dtype=np.float32,
        )

        first = selector.select_xyxy(first_target)
        self.assertFalse(first["has_target"])

        locked = selector.select_xyxy(first_target)
        self.assertTrue(locked["has_target"])

        second = selector.select_xyxy(second_target)
        self.assertTrue(second["has_target"])
        self.assertAlmostEqual(second["target_x"], locked["target_x"], places=3)
        self.assertAlmostEqual(second["target_y"], locked["target_y"], places=3)

        third = selector.select_xyxy(second_target)
        self.assertTrue(third["has_target"])
        self.assertAlmostEqual(third["target_x"], 460.0, places=3)
        self.assertAlmostEqual(third["target_y"], 196.0, places=3)

    def test_multi_candidate_prefers_target_closer_to_crosshair(self):
        if not hasattr(self.module, "NativeTargetSelector"):
            self.fail("NativeTargetSelector is missing")

        selector = self.module.NativeTargetSelector(640, 512)
        detections = np.array(
            [
                [40.0, 110.0, 120.0, 320.0, 0.90, 0.0],
                [275.0, 120.0, 355.0, 320.0, 0.74, 0.0],
            ],
            dtype=np.float32,
        )

        warmup = selector.select_xyxy(detections)
        self.assertFalse(warmup["has_target"])

        result = selector.select_xyxy(detections)
        self.assertTrue(result["has_target"])
        self.assertAlmostEqual(result["target_x"], 315.0, places=3)
        self.assertAlmostEqual(result["target_y"], 196.0, places=3)


if __name__ == "__main__":
    unittest.main()
