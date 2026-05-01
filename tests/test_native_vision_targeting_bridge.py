import os
import sys
import unittest
from pathlib import Path

import numpy as np


PROJECT_ROOT = Path(__file__).resolve().parent.parent
NATIVE_BUILD_DIR = PROJECT_ROOT / "native" / "vision_native" / "build" / "Release"
CROP_W = 640
CROP_H = 512
NEUTRAL_RGB = (24, 24, 24)
FRIENDLY_RGB = (0, 255, 0)
ENEMY_RGB = (255, 255, 0)


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


def _frame():
    return np.full((CROP_H, CROP_W, 3), NEUTRAL_RGB, dtype=np.uint8)


def _paint_color_above(frame, box, rgb):
    x1, y1, x2, y2 = box
    box_w = float(x2 - x1)
    box_h = float(y2 - y1)
    cx = (x1 + x2) * 0.5
    roi_h = int(max(12, min(36, box_h * 0.20)))
    roi_w = int(max(24, min(80, box_w * 0.80)))
    roi_bottom = max(0, min(frame.shape[0], int(y1) - 2))
    roi_top = max(0, roi_bottom - roi_h)
    roi_left = max(0, int(cx - roi_w / 2))
    roi_right = min(frame.shape[1], int(cx + roi_w / 2))
    band_h = max(4, (roi_bottom - roi_top) // 3)
    band_top = roi_top + max(0, ((roi_bottom - roi_top) - band_h) // 2)
    band_bottom = min(roi_bottom, band_top + band_h)
    band_pad = max(2, int((roi_right - roi_left) * 0.18))
    band_left = min(roi_right, roi_left + band_pad)
    band_right = max(band_left + 1, roi_right - band_pad)
    frame[band_top:band_bottom, band_left:band_right] = rgb


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

    def test_green_friendly_target_is_filtered_out_with_rgb_frame(self):
        if not hasattr(self.module, "NativeTargetSelector"):
            self.fail("NativeTargetSelector is missing")

        selector = self.module.NativeTargetSelector(CROP_W, CROP_H)
        box = [300.0, 180.0, 360.0, 320.0]
        frame = _frame()
        _paint_color_above(frame, box, FRIENDLY_RGB)
        detections = np.array([[*box, 0.82, 0.0]], dtype=np.float32)

        first = selector.select_xyxy_rgb(detections, frame)
        second = selector.select_xyxy_rgb(detections, frame)

        self.assertFalse(first["has_target"])
        self.assertFalse(second["has_target"])
        self.assertEqual(second["boxes_seen"], 1.0)

    def test_enemy_colored_pickup_can_lock_at_lower_confidence_with_rgb_frame(self):
        if not hasattr(self.module, "NativeTargetSelector"):
            self.fail("NativeTargetSelector is missing")

        selector = self.module.NativeTargetSelector(CROP_W, CROP_H)
        box = [300.0, 180.0, 360.0, 320.0]
        frame = _frame()
        _paint_color_above(frame, box, ENEMY_RGB)
        detections = np.array([[*box, 0.44, 0.0]], dtype=np.float32)

        first = selector.select_xyxy_rgb(detections, frame)
        result = selector.select_xyxy_rgb(detections, frame)

        self.assertFalse(first["has_target"])
        self.assertTrue(result["has_target"])
        self.assertAlmostEqual(result["target_x"], 330.0, places=3)
        self.assertAlmostEqual(result["target_y"], 233.2, places=3)

    def test_friendly_candidate_is_filtered_before_enemy_selection(self):
        if not hasattr(self.module, "NativeTargetSelector"):
            self.fail("NativeTargetSelector is missing")

        selector = self.module.NativeTargetSelector(CROP_W, CROP_H)
        friendly_box = [300.0, 180.0, 360.0, 320.0]
        enemy_box = [390.0, 180.0, 450.0, 320.0]
        frame = _frame()
        _paint_color_above(frame, friendly_box, FRIENDLY_RGB)
        _paint_color_above(frame, enemy_box, ENEMY_RGB)
        detections = np.array(
            [
                [*friendly_box, 0.90, 0.0],
                [*enemy_box, 0.74, 0.0],
            ],
            dtype=np.float32,
        )

        warmup = selector.select_xyxy_rgb(detections, frame)
        result = selector.select_xyxy_rgb(detections, frame)

        self.assertFalse(warmup["has_target"])
        self.assertTrue(result["has_target"])
        self.assertAlmostEqual(result["target_x"], 420.0, places=3)

    def test_partial_occlusion_keeps_observed_box_without_reconstruction(self):
        if not hasattr(self.module, "NativeTargetSelector"):
            self.fail("NativeTargetSelector is missing")

        selector = self.module.NativeTargetSelector(CROP_W, CROP_H)
        full = np.array([[300.0, 240.0, 340.0, 360.0, 0.95, 0.0]], dtype=np.float32)
        clipped = np.array([[304.0, 286.0, 344.0, 362.0, 0.93, 0.0]], dtype=np.float32)

        first = selector.select_xyxy(full)
        locked = selector.select_xyxy(full)
        reconstructed = selector.select_xyxy(clipped)

        self.assertFalse(first["has_target"])
        self.assertTrue(locked["has_target"])
        self.assertTrue(reconstructed["has_target"])
        self.assertEqual(reconstructed["target_source"], "observed")
        self.assertAlmostEqual(reconstructed["body_y1"], 286.0, places=3)
        self.assertAlmostEqual(reconstructed["body_y2"], 362.0, places=3)
        raw_target_y = 286.0 + ((362.0 - 286.0) * 0.38)
        self.assertAlmostEqual(reconstructed["target_y"], raw_target_y, places=3)

    def test_empty_frame_drops_target_instead_of_predicting(self):
        if not hasattr(self.module, "NativeTargetSelector"):
            self.fail("NativeTargetSelector is missing")

        selector = self.module.NativeTargetSelector(CROP_W, CROP_H)
        first_box = np.array([[300.0, 240.0, 340.0, 360.0, 0.95, 0.0]], dtype=np.float32)
        second_box = np.array([[306.0, 244.0, 346.0, 364.0, 0.95, 0.0]], dtype=np.float32)
        empty = np.empty((0, 6), dtype=np.float32)

        selector.select_xyxy(first_box)
        locked = selector.select_xyxy(first_box)
        observed = selector.select_xyxy(second_box)
        lost = selector.select_xyxy(empty)
        still_lost = selector.select_xyxy(empty)

        self.assertTrue(locked["has_target"])
        self.assertTrue(observed["has_target"])
        self.assertFalse(lost["has_target"])
        self.assertFalse(still_lost["has_target"])

    def test_reacquired_target_requires_fresh_confirmation_after_empty_gap(self):
        if not hasattr(self.module, "NativeTargetSelector"):
            self.fail("NativeTargetSelector is missing")

        selector = self.module.NativeTargetSelector(CROP_W, CROP_H)
        first_box = np.array([[300.0, 240.0, 340.0, 360.0, 0.95, 0.0]], dtype=np.float32)
        second_box = np.array([[306.0, 244.0, 346.0, 364.0, 0.95, 0.0]], dtype=np.float32)
        reacquired_box = np.array([[312.0, 248.0, 352.0, 368.0, 0.95, 0.0]], dtype=np.float32)

        selector.select_xyxy(first_box)
        selector.select_xyxy(first_box)
        selector.select_xyxy(second_box)
        lost = selector.select_xyxy(np.empty((0, 6), dtype=np.float32))
        reacquire_first = selector.select_xyxy(reacquired_box)
        reacquired = selector.select_xyxy(reacquired_box)

        self.assertFalse(lost["has_target"])
        self.assertFalse(reacquire_first["has_target"])
        self.assertTrue(reacquired["has_target"])
        self.assertEqual(reacquired["target_source"], "observed")

    def test_autofire_triggers_for_selected_target_inside_fire_zone(self):
        if not hasattr(self.module, "NativeTargetSelector"):
            self.fail("NativeTargetSelector is missing")

        selector = self.module.NativeTargetSelector(CROP_W, CROP_H)
        detections = np.array([[280.0, 240.0, 360.0, 380.0, 0.95, 0.0]], dtype=np.float32)

        first = selector.select_xyxy(detections)
        locked = selector.select_xyxy(detections)

        self.assertFalse(first["has_target"])
        self.assertFalse(first["auto_fire"])
        self.assertTrue(locked["has_target"])
        self.assertTrue(locked["auto_fire"])

    def test_autofire_release_grace_frames_apply_after_selected_target_loss(self):
        if not hasattr(self.module, "NativeTargetSelector"):
            self.fail("NativeTargetSelector is missing")

        selector = self.module.NativeTargetSelector(CROP_W, CROP_H)
        detections = np.array([[280.0, 240.0, 360.0, 380.0, 0.95, 0.0]], dtype=np.float32)
        empty = np.empty((0, 6), dtype=np.float32)

        selector.select_xyxy(detections)
        locked = selector.select_xyxy(detections)
        miss_one = selector.select_xyxy(empty)
        miss_two = selector.select_xyxy(empty)
        miss_three = selector.select_xyxy(empty)
        miss_four = selector.select_xyxy(empty)

        self.assertTrue(locked["auto_fire"])
        self.assertFalse(miss_one["has_target"])
        self.assertTrue(miss_one["auto_fire"])
        self.assertTrue(miss_two["auto_fire"])
        self.assertTrue(miss_three["auto_fire"])
        self.assertFalse(miss_four["auto_fire"])

    def test_autofire_reset_clears_release_grace_state(self):
        if not hasattr(self.module, "NativeTargetSelector"):
            self.fail("NativeTargetSelector is missing")

        selector = self.module.NativeTargetSelector(CROP_W, CROP_H)
        detections = np.array([[280.0, 240.0, 360.0, 380.0, 0.95, 0.0]], dtype=np.float32)

        selector.select_xyxy(detections)
        locked = selector.select_xyxy(detections)
        selector.reset()
        after_reset = selector.select_xyxy(np.empty((0, 6), dtype=np.float32))

        self.assertTrue(locked["auto_fire"])
        self.assertFalse(after_reset["auto_fire"])


if __name__ == "__main__":
    unittest.main()
