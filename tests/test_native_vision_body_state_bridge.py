import unittest

import numpy as np

from tests.test_native_vision_targeting_bridge import _load_native_module


FRAME_W = 160
FRAME_H = 128
CUE_FRAME_W = 320
CUE_FRAME_H = 256


def _textured_frame() -> np.ndarray:
    frame = np.full((FRAME_H, FRAME_W, 3), 22, dtype=np.uint8)
    for y in range(8, FRAME_H - 8, 16):
        for x in range(8, FRAME_W - 8, 16):
            frame[y - 2 : y + 2, x - 2 : x + 2] = (180, 180, 180)
    return frame


def _shift_frame(frame: np.ndarray, dx: int, dy: int) -> np.ndarray:
    shifted = np.zeros_like(frame)
    src_x1 = max(0, -dx)
    src_y1 = max(0, -dy)
    src_x2 = min(frame.shape[1], frame.shape[1] - dx) if dx >= 0 else frame.shape[1]
    src_y2 = min(frame.shape[0], frame.shape[0] - dy) if dy >= 0 else frame.shape[0]
    dst_x1 = max(0, dx)
    dst_y1 = max(0, dy)
    dst_x2 = dst_x1 + max(0, src_x2 - src_x1)
    dst_y2 = dst_y1 + max(0, src_y2 - src_y1)
    shifted[dst_y1:dst_y2, dst_x1:dst_x2] = frame[src_y1:src_y2, src_x1:src_x2]
    return shifted


def _blank_frame() -> np.ndarray:
    return np.full((FRAME_H, FRAME_W, 3), 18, dtype=np.uint8)


def _blank_cue_frame() -> np.ndarray:
    return np.full((CUE_FRAME_H, CUE_FRAME_W, 3), 18, dtype=np.uint8)


def _muzzle_flash_frame() -> np.ndarray:
    frame = _textured_frame()
    frame[94:126, 48:112] = (255, 255, 255)
    return frame


def _yellow_center_frame(*, center_x: int, center_y: int, radius: int = 8) -> np.ndarray:
    frame = _blank_cue_frame()
    y1 = max(0, center_y - radius)
    y2 = min(CUE_FRAME_H, center_y + radius)
    x1 = max(0, center_x - radius)
    x2 = min(CUE_FRAME_W, center_x + radius)
    frame[y1:y2, x1:x2] = (255, 255, 0)
    return frame


def _torso_patch_frame(box: tuple[int, int, int, int], *, patch_offset_x: int = 0, patch_offset_y: int = 0) -> np.ndarray:
    frame = _textured_frame()
    x1, y1, x2, y2 = box
    torso_left = int(round(x1 + ((x2 - x1) * 0.30))) + patch_offset_x
    torso_right = int(round(x2 - ((x2 - x1) * 0.30))) + patch_offset_x
    torso_top = int(round(y1 + ((y2 - y1) * 0.30))) + patch_offset_y
    torso_bottom = int(round(y2 - ((y2 - y1) * 0.28))) + patch_offset_y
    frame[torso_top:torso_bottom, torso_left:torso_right] = (210, 210, 210)
    for y in range(torso_top + 4, torso_bottom - 4, 8):
        for x in range(torso_left + 4, torso_right - 4, 8):
            frame[y - 1 : y + 1, x - 1 : x + 1] = (16, 16, 16)
    return frame


class NativeVisionBodyStateBridgeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.module = _load_native_module()

    def test_native_ego_motion_estimator_is_exposed(self):
        self.assertTrue(
            hasattr(self.module, "NativeEgoMotionEstimator"),
            "vision_native_cpp should expose NativeEgoMotionEstimator for body-state v1",
        )

    def test_native_body_state_tracker_is_exposed(self):
        self.assertTrue(
            hasattr(self.module, "NativeBodyStateTracker"),
            "vision_native_cpp should expose NativeBodyStateTracker for body-state v1",
        )

    def test_native_center_cue_refiner_is_exposed(self):
        self.assertTrue(
            hasattr(self.module, "NativeCenterCueRefiner"),
            "vision_native_cpp should expose NativeCenterCueRefiner for center yellow-cue refinement",
        )

    def test_native_body_state_tracker_exposes_interframe_and_scan_miss_entrypoints(self):
        tracker = self.module.NativeBodyStateTracker(FRAME_W, FRAME_H)

        self.assertTrue(hasattr(tracker, "prime_keyframe_rgb"))
        self.assertTrue(hasattr(tracker, "update_interframe_rgb"))
        self.assertTrue(hasattr(tracker, "update_scan_miss_rgb"))

    def test_ego_motion_estimator_reports_translation_between_frames(self):
        estimator = self.module.NativeEgoMotionEstimator(FRAME_W, FRAME_H)
        frame1 = _textured_frame()
        frame2 = _shift_frame(frame1, 5, -3)
        empty = np.zeros((0, 6), dtype=np.float32)

        cold = estimator.estimate_rgb(frame1, empty)
        result = estimator.estimate_rgb(frame2, empty)

        self.assertEqual(cold["model"], "identity")
        self.assertEqual(cold["confidence"], 0.0)
        self.assertEqual(result["model"], "affine")
        self.assertGreater(result["confidence"], 0.25)
        self.assertAlmostEqual(result["dx"], 5.0, delta=2.0)
        self.assertAlmostEqual(result["dy"], -3.0, delta=2.0)

    def test_ego_motion_estimator_returns_identity_when_background_is_fully_masked(self):
        estimator = self.module.NativeEgoMotionEstimator(FRAME_W, FRAME_H)
        frame1 = _textured_frame()
        frame2 = _shift_frame(frame1, 6, 4)
        full_mask = np.array([[0.0, 0.0, float(FRAME_W), float(FRAME_H), 0.95, 0.0]], dtype=np.float32)

        estimator.estimate_rgb(frame1, np.zeros((0, 6), dtype=np.float32))
        result = estimator.estimate_rgb(frame2, full_mask)

        self.assertEqual(result["model"], "identity")
        self.assertLessEqual(result["confidence"], 0.05)
        self.assertAlmostEqual(result["dx"], 0.0, delta=0.1)
        self.assertAlmostEqual(result["dy"], 0.0, delta=0.1)

    def test_ego_motion_estimator_ignores_muzzle_flash_in_masked_lower_region(self):
        estimator = self.module.NativeEgoMotionEstimator(FRAME_W, FRAME_H)
        frame1 = _textured_frame()
        frame2 = _muzzle_flash_frame()

        estimator.estimate_rgb(frame1, np.zeros((0, 6), dtype=np.float32))
        result = estimator.estimate_rgb(frame2, np.zeros((0, 6), dtype=np.float32))

        self.assertAlmostEqual(result["dx"], 0.0, delta=0.75)
        self.assertAlmostEqual(result["dy"], 0.0, delta=0.75)
        self.assertIn(result["model"], {"identity", "translation", "affine"})

    def test_body_state_tracker_initializes_torso_prior_inside_box(self):
        tracker = self.module.NativeBodyStateTracker(FRAME_W, FRAME_H)
        box = (52, 26, 108, 112)
        frame = _torso_patch_frame(box)

        result = tracker.update_selected_rgb(*box, frame, 0.0, 0.0, 1.0)

        self.assertTrue(result["has_target"])
        self.assertIn(result["body_state_mode"], {"strong", "weak"})
        self.assertGreater(result["anchor_confidence"], 0.0)
        self.assertGreater(result["debug_template_w"], 0.0)
        self.assertGreater(result["debug_template_h"], 0.0)
        self.assertGreaterEqual(len(result["debug_track_points"]), 2)
        self.assertGreaterEqual(result["target_x"], result["torso_x1"])
        self.assertLessEqual(result["target_x"], result["torso_x2"])
        self.assertGreaterEqual(result["target_y"], result["torso_y1"])
        self.assertLessEqual(result["target_y"], result["torso_y2"])

    def test_body_state_tracker_uses_ego_warp_during_short_hold(self):
        tracker = self.module.NativeBodyStateTracker(FRAME_W, FRAME_H)
        box = (52, 26, 108, 112)
        frame = _torso_patch_frame(box)
        observed = tracker.update_selected_rgb(*box, frame, 0.0, 0.0, 1.0)

        held = tracker.update_missing_rgb(frame, 6.0, -2.0, 1.0)

        self.assertTrue(held["has_target"])
        self.assertEqual(held["body_state_mode"], "hold")
        self.assertGreater(held["debug_search_x2"] - held["debug_search_x1"], 0.0)
        self.assertGreater(held["debug_search_y2"] - held["debug_search_y1"], 0.0)
        self.assertAlmostEqual(held["target_x"], observed["target_x"] + 6.0, delta=3.0)
        self.assertAlmostEqual(held["target_y"], observed["target_y"] - 2.0, delta=3.0)

    def test_body_state_tracker_enters_reacquire_when_hold_cues_disappear(self):
        tracker = self.module.NativeBodyStateTracker(FRAME_W, FRAME_H)
        box = (52, 26, 108, 112)
        frame = _torso_patch_frame(box)
        observed = tracker.update_selected_rgb(*box, frame, 0.0, 0.0, 1.0)

        reacquiring = tracker.update_missing_rgb(_blank_frame(), 5.0, 0.0, 1.0)

        self.assertTrue(reacquiring["has_target"])
        self.assertEqual(reacquiring["body_state_mode"], "reacquire")
        self.assertEqual(reacquiring["anchor_source"], "torso_prior")
        self.assertAlmostEqual(reacquiring["target_x"], observed["target_x"] + 5.0, delta=3.0)

    def test_body_state_tracker_keeps_reacquire_when_detector_returns_without_local_cues(self):
        tracker = self.module.NativeBodyStateTracker(FRAME_W, FRAME_H)
        box = (52, 26, 108, 112)
        frame = _torso_patch_frame(box)

        tracker.update_selected_rgb(*box, frame, 0.0, 0.0, 1.0)
        tracker.update_missing_rgb(_blank_frame(), 5.0, 0.0, 1.0)

        returned_box = (57, 26, 113, 112)
        recovered = tracker.update_selected_rgb(*returned_box, _blank_frame(), 0.0, 0.0, 1.0)

        self.assertTrue(recovered["has_target"])
        self.assertEqual(recovered["body_state_mode"], "reacquire")
        self.assertEqual(recovered["anchor_source"], "torso_prior")
        self.assertGreaterEqual(recovered["target_x"], recovered["torso_x1"])
        self.assertLessEqual(recovered["target_x"], recovered["torso_x2"])
        self.assertGreaterEqual(recovered["target_y"], recovered["torso_y1"])
        self.assertLessEqual(recovered["target_y"], recovered["torso_y2"])

    def test_body_state_tracker_limits_pan_stop_overshoot_during_hold(self):
        tracker = self.module.NativeBodyStateTracker(FRAME_W, FRAME_H)
        first_box = (52, 26, 108, 112)
        moved_box = (60, 26, 116, 112)
        first_frame = _torso_patch_frame(first_box)
        moved_frame = _torso_patch_frame(moved_box)

        tracker.update_selected_rgb(*first_box, first_frame, 0.0, 0.0, 1.0)
        observed = tracker.update_selected_rgb(*moved_box, moved_frame, 0.0, 0.0, 1.0)
        held = tracker.update_missing_rgb(moved_frame, 0.0, 0.0, 1.0)

        self.assertEqual(held["body_state_mode"], "hold")
        self.assertLessEqual(abs(held["target_x"] - observed["target_x"]), 2.0)
        self.assertLessEqual(abs(held["target_y"] - observed["target_y"]), 2.0)

    def test_interframe_updates_do_not_consume_scan_miss_budget(self):
        tracker = self.module.NativeBodyStateTracker(FRAME_W, FRAME_H)
        box = (52, 26, 108, 112)
        frame = _torso_patch_frame(box)

        tracker.prime_keyframe_rgb(*box, frame)
        last = None
        for _ in range(8):
            last = tracker.update_interframe_rgb(_blank_frame(), 0.0, 0.0, 1.0)

        self.assertIsNotNone(last)
        self.assertTrue(last["has_target"])
        self.assertNotEqual(last["body_state_mode"], "drop")

    def test_scan_miss_updates_advance_to_drop_after_budget_is_exhausted(self):
        tracker = self.module.NativeBodyStateTracker(FRAME_W, FRAME_H)
        box = (52, 26, 108, 112)
        frame = _torso_patch_frame(box)

        tracker.prime_keyframe_rgb(*box, frame)
        dropped = None
        for _ in range(6):
            dropped = tracker.update_scan_miss_rgb(_blank_frame(), 0.0, 0.0, 1.0)

        self.assertIsNotNone(dropped)
        self.assertFalse(dropped["has_target"])
        self.assertEqual(dropped["body_state_mode"], "drop")

    def test_center_cue_refiner_returns_original_target_when_no_yellow_cue_is_present(self):
        refiner = self.module.NativeCenterCueRefiner(CUE_FRAME_W, CUE_FRAME_H)
        frame = _blank_cue_frame()

        result = refiner.refine_rgb(
            frame,
            160.0,
            128.0,
            160.0,
            128.0,
            120.0,
            90.0,
            200.0,
            210.0,
            132.0,
            112.0,
            188.0,
            198.0,
            "strong",
        )

        self.assertFalse(result["yellow_cue_present"])
        self.assertFalse(result["refiner_applied"])
        self.assertAlmostEqual(result["refined_target_x"], 160.0, places=3)
        self.assertAlmostEqual(result["refined_target_y"], 128.0, places=3)

    def test_center_cue_refiner_applies_y_axis_dominant_correction_inside_center_window(self):
        refiner = self.module.NativeCenterCueRefiner(CUE_FRAME_W, CUE_FRAME_H)
        frame = _yellow_center_frame(center_x=162, center_y=118, radius=10)

        result = refiner.refine_rgb(
            frame,
            160.0,
            128.0,
            160.0,
            128.0,
            120.0,
            90.0,
            200.0,
            210.0,
            132.0,
            112.0,
            188.0,
            198.0,
            "strong",
        )

        self.assertTrue(result["yellow_cue_present"])
        self.assertTrue(result["refiner_applied"])
        self.assertGreater(result["yellow_cue_score"], 0.0)
        self.assertGreater(abs(result["refined_target_y"] - 128.0), abs(result["refined_target_x"] - 160.0))
        self.assertGreaterEqual(result["refined_target_x"], 132.0)
        self.assertLessEqual(result["refined_target_x"], 188.0)
        self.assertGreaterEqual(result["refined_target_y"], 112.0)
        self.assertLessEqual(result["refined_target_y"], 198.0)

    def test_center_cue_refiner_ignores_geometrically_unreasonable_yellow_cue(self):
        refiner = self.module.NativeCenterCueRefiner(CUE_FRAME_W, CUE_FRAME_H)
        frame = _yellow_center_frame(center_x=240, center_y=118, radius=10)

        result = refiner.refine_rgb(
            frame,
            160.0,
            128.0,
            160.0,
            128.0,
            120.0,
            90.0,
            200.0,
            210.0,
            132.0,
            112.0,
            188.0,
            198.0,
            "strong",
        )

        self.assertTrue(result["yellow_cue_present"])
        self.assertFalse(result["refiner_applied"])
        self.assertAlmostEqual(result["refined_target_x"], 160.0, places=3)
        self.assertAlmostEqual(result["refined_target_y"], 128.0, places=3)


if __name__ == "__main__":
    unittest.main()
