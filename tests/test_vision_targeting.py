import unittest

import numpy as np

from vision.targeting import ParsedDetections, TargetSelector


CROP = 640
NEUTRAL_RGB = (128, 128, 128)
FRIENDLY_RGB = (0, 255, 0)
ENEMY_RGB = (255, 255, 0)


def _frame():
    return np.full((CROP, CROP, 3), NEUTRAL_RGB, dtype=np.uint8)


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
    frame[roi_top:roi_bottom, roi_left:roi_right] = rgb


def _detections(*boxes, keypoints=None):
    if not boxes:
        return [ParsedDetections(
            boxes=np.empty((0, 4), dtype=np.float32),
            confs=np.empty((0,), dtype=np.float32),
        )]
    np_boxes = np.array(boxes, dtype=np.float32)
    confs = np.full(len(boxes), 0.9, dtype=np.float32)
    np_keypoints = None if keypoints is None else np.array(keypoints, dtype=np.float32)
    return [ParsedDetections(boxes=np_boxes, confs=confs, keypoints=np_keypoints)]


def _person_keypoints(
    *,
    nose=(320.0, 250.0, 0.95),
    left_shoulder=(300.0, 290.0, 0.95),
    right_shoulder=(340.0, 290.0, 0.95),
    left_hip=(305.0, 340.0, 0.95),
    right_hip=(335.0, 340.0, 0.95),
):
    kpts = np.zeros((17, 3), dtype=np.float32)
    kpts[0] = nose
    kpts[5] = left_shoulder
    kpts[6] = right_shoulder
    kpts[11] = left_hip
    kpts[12] = right_hip
    return kpts


class TargetSelectorTests(unittest.TestCase):
    def test_single_distant_target_beyond_flick_threshold_is_selected(self):
        selector = TargetSelector(crop_size=CROP)
        box = [30, 30, 70, 70]
        frame = _frame()
        selected = selector.select_target(_detections(box), frame)
        self.assertIsNotNone(selected)
        self.assertLess(selected.target_x, 100)
        self.assertLess(selected.target_y, 100)

    def test_single_friendly_target_is_filtered_out(self):
        selector = TargetSelector(crop_size=CROP)
        box = [310, 310, 370, 370]
        frame = _frame()
        _paint_color_above(frame, box, FRIENDLY_RGB)
        self.assertIsNone(selector.select_target(_detections(box), frame))

    def test_multi_candidate_picks_closer_target(self):
        selector = TargetSelector(crop_size=CROP)
        near_box = [310, 300, 340, 360]
        far_box = [20, 20, 60, 60]
        frame = _frame()
        selected = selector.select_target(_detections(near_box, far_box), frame)
        self.assertIsNotNone(selected)
        self.assertGreater(selected.target_x, 300)
        self.assertLess(selected.target_x, 360)

    def test_multi_candidate_enemy_color_beats_neutral(self):
        selector = TargetSelector(crop_size=CROP)
        neutral_box = [300, 300, 340, 360]
        enemy_box = [400, 300, 440, 360]
        frame = _frame()
        _paint_color_above(frame, enemy_box, ENEMY_RGB)
        selected = selector.select_target(_detections(neutral_box, enemy_box), frame)
        self.assertIsNotNone(selected)
        self.assertGreater(selected.target_x, 380)

    def test_multi_candidate_first_pickup_flick_still_gated(self):
        selector = TargetSelector(crop_size=CROP)
        box_a = [10, 10, 50, 50]
        box_b = [580, 580, 620, 620]
        frame = _frame()
        self.assertIsNone(selector.select_target(_detections(box_a, box_b), frame))

    def test_multi_candidate_friendly_filtered_enemy_selected(self):
        selector = TargetSelector(crop_size=CROP)
        friendly_box = [310, 310, 350, 370]
        enemy_box = [400, 310, 440, 370]
        frame = _frame()
        _paint_color_above(frame, friendly_box, FRIENDLY_RGB)
        _paint_color_above(frame, enemy_box, ENEMY_RGB)
        selected = selector.select_target(_detections(friendly_box, enemy_box), frame)
        self.assertIsNotNone(selected)
        self.assertGreater(selected.target_x, 380)

    def test_tracking_jump_drops_new_target_after_teleport(self):
        selector = TargetSelector(crop_size=CROP)
        first_box = [300, 300, 340, 360]
        frame = _frame()
        first = selector.select_target(_detections(first_box), frame)
        self.assertIsNotNone(first)
        far_box = [600, 300, 630, 360]
        self.assertIsNone(selector.select_target(_detections(far_box), frame))
        self.assertIsNone(selector.last_target_center)

    def test_no_detections_returns_none_and_resets(self):
        selector = TargetSelector(crop_size=CROP)
        selector.last_target_center = (320.0, 320.0)
        self.assertIsNone(selector.select_target(_detections(), _frame()))
        self.assertIsNone(selector.last_target_center)

    def test_all_friendlies_returns_none(self):
        selector = TargetSelector(crop_size=CROP)
        box_a = [300, 300, 340, 360]
        box_b = [400, 300, 440, 360]
        frame = _frame()
        _paint_color_above(frame, box_a, FRIENDLY_RGB)
        _paint_color_above(frame, box_b, FRIENDLY_RGB)
        self.assertIsNone(selector.select_target(_detections(box_a, box_b), frame))

    def test_pose_target_exposes_torso_slow_zone(self):
        selector = TargetSelector(crop_size=CROP)
        box = [280, 240, 360, 380]
        frame = _frame()
        selected = selector.select_target(
            _detections(box, keypoints=[_person_keypoints()]),
            frame,
        )

        self.assertIsNotNone(selected)
        self.assertIsNotNone(selected.slow_zone)
        slow_left, slow_top, slow_right, slow_bottom = selected.slow_zone
        self.assertLess(slow_left, selector.screen_center_x)
        self.assertGreater(slow_right, selector.screen_center_x)
        self.assertLess(slow_top, selector.screen_center_y)
        self.assertGreater(slow_bottom, selector.screen_center_y)
        self.assertAlmostEqual(selected.target_x, 320.0, places=3)
        self.assertAlmostEqual(selected.target_y, 290.0, places=3)


if __name__ == "__main__":
    unittest.main()
