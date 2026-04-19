import unittest

import numpy as np

from vision.targeting import CrosshairPersonHitDetector, ParsedDetections, TargetSelector


CROP = 640
NEUTRAL_RGB = (128, 128, 128)
FRIENDLY_RGB = (0, 255, 0)
BLUE_RGB = (0, 0, 255)
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
    band_h = max(4, (roi_bottom - roi_top) // 3)
    band_top = roi_top + max(0, ((roi_bottom - roi_top) - band_h) // 2)
    band_bottom = min(roi_bottom, band_top + band_h)
    band_pad = max(2, int((roi_right - roi_left) * 0.18))
    band_left = min(roi_right, roi_left + band_pad)
    band_right = max(band_left + 1, roi_right - band_pad)
    frame[band_top:band_bottom, band_left:band_right] = rgb


def _paint_full_color_above(frame, box, rgb):
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


def _detections(*boxes, keypoints=None, confs=None):
    if not boxes:
        return [ParsedDetections(
            boxes=np.empty((0, 4), dtype=np.float32),
            confs=np.empty((0,), dtype=np.float32),
        )]
    np_boxes = np.array(boxes, dtype=np.float32)
    np_confs = np.full(len(boxes), 0.9, dtype=np.float32) if confs is None else np.array(confs, dtype=np.float32)
    np_keypoints = None if keypoints is None else np.array(keypoints, dtype=np.float32)
    return [ParsedDetections(boxes=np_boxes, confs=np_confs, keypoints=np_keypoints)]


def _confirm_target(selector, detections, frame, frames=2):
    selected = None
    for _ in range(frames):
        selected = selector.select_target(detections, frame)
    return selected


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
    def test_build_candidates_filters_friendlies_and_uses_box_slow_zone_even_with_keypoints(self):
        selector = TargetSelector(crop_size=CROP)
        frame = _frame()
        friendly_box = [280, 240, 320, 360]
        enemy_box = [320, 240, 360, 360]
        _paint_color_above(frame, friendly_box, FRIENDLY_RGB)
        _paint_color_above(frame, enemy_box, ENEMY_RGB)
        detections = _detections(
            friendly_box,
            enemy_box,
            keypoints=[
                _person_keypoints(),
                _person_keypoints(
                    nose=(340.0, 250.0, 0.95),
                    left_shoulder=(324.0, 290.0, 0.95),
                    right_shoulder=(356.0, 290.0, 0.95),
                    left_hip=(326.0, 340.0, 0.95),
                    right_hip=(354.0, 340.0, 0.95),
                ),
            ],
        )

        candidates = selector._build_candidates(
            detections,
            frame,
            last_target_center=None,
            sample_timestamp=123.0,
        )

        self.assertEqual(len(candidates), 1)
        candidate = candidates[0]
        self.assertEqual(candidate.selected_box, tuple(float(value) for value in enemy_box))
        self.assertEqual(candidate.source, "observed")
        self.assertEqual(candidate.slow_zone, selector._fallback_slow_zone(np.array(enemy_box, dtype=np.float32)))

    def test_single_low_confidence_target_is_filtered_out(self):
        selector = TargetSelector(crop_size=CROP)
        box = [300, 240, 340, 360]
        frame = _frame()

        selected = selector.select_target(_detections(box, confs=[0.10]), frame)

        self.assertIsNone(selected)

    def test_single_small_off_center_target_is_filtered_out_on_first_pickup(self):
        selector = TargetSelector(crop_size=CROP)
        box = [30, 30, 70, 70]
        frame = _frame()
        selected = _confirm_target(selector, _detections(box), frame)
        self.assertIsNone(selected)

    def test_single_friendly_target_is_filtered_out(self):
        selector = TargetSelector(crop_size=CROP)
        box = [310, 310, 370, 370]
        frame = _frame()
        _paint_color_above(frame, box, FRIENDLY_RGB)
        self.assertIsNone(_confirm_target(selector, _detections(box), frame))

    def test_single_blue_target_is_not_filtered_out(self):
        selector = TargetSelector(crop_size=CROP)
        box = [310, 310, 370, 370]
        frame = _frame()
        _paint_color_above(frame, box, BLUE_RGB)

        selected = _confirm_target(selector, _detections(box), frame)

        self.assertIsNotNone(selected)

    def test_multi_candidate_picks_closer_target(self):
        selector = TargetSelector(crop_size=CROP)
        near_box = [310, 300, 340, 360]
        far_box = [20, 20, 60, 60]
        frame = _frame()
        selected = _confirm_target(selector, _detections(near_box, far_box), frame)
        self.assertIsNotNone(selected)
        self.assertGreater(selected.target_x, 300)
        self.assertLess(selected.target_x, 360)

    def test_multi_candidate_pickup_prefers_target_closer_to_crosshair_over_far_higher_confidence(self):
        selector = TargetSelector(crop_size=CROP)
        near_box = [280, 240, 320, 360]
        far_box = [420, 240, 460, 360]
        frame = _frame()

        selected = _confirm_target(
            selector,
            _detections(near_box, far_box, confs=[0.66, 0.95]),
            frame,
        )

        self.assertIsNotNone(selected)
        self.assertLess(selected.target_x, 320.0)

    def test_multi_candidate_enemy_color_beats_neutral_when_crosshair_distance_is_similar(self):
        selector = TargetSelector(crop_size=CROP)
        neutral_box = [280, 300, 320, 360]
        enemy_box = [290, 300, 330, 360]
        frame = _frame()
        _paint_color_above(frame, enemy_box, ENEMY_RGB)
        selected = _confirm_target(selector, _detections(neutral_box, enemy_box), frame)
        self.assertIsNotNone(selected)
        self.assertGreater(selected.target_x, 300)

    def test_enemy_colored_pickup_can_lock_at_lower_confidence(self):
        selector = TargetSelector(crop_size=CROP)
        box = [300, 240, 340, 360]
        frame = _frame()
        _paint_color_above(frame, box, ENEMY_RGB)

        selected = _confirm_target(selector, _detections(box, confs=[0.44]), frame)

        self.assertIsNotNone(selected)

    def test_multi_candidate_first_pickup_flick_still_gated(self):
        selector = TargetSelector(crop_size=CROP)
        box_a = [10, 10, 50, 50]
        box_b = [580, 580, 620, 620]
        frame = _frame()
        self.assertIsNone(_confirm_target(selector, _detections(box_a, box_b), frame))

    def test_multi_candidate_friendly_filtered_enemy_selected(self):
        selector = TargetSelector(crop_size=CROP)
        friendly_box = [310, 310, 350, 370]
        enemy_box = [400, 310, 440, 370]
        frame = _frame()
        _paint_color_above(frame, friendly_box, FRIENDLY_RGB)
        _paint_color_above(frame, enemy_box, ENEMY_RGB)
        selected = _confirm_target(selector, _detections(friendly_box, enemy_box), frame)
        self.assertIsNotNone(selected)
        self.assertGreater(selected.target_x, 380)

    def test_tracking_jump_drops_new_target_after_teleport(self):
        selector = TargetSelector(crop_size=CROP)
        first_box = [300, 300, 340, 360]
        frame = _frame()
        first = _confirm_target(selector, _detections(first_box), frame)
        self.assertIsNotNone(first)
        far_box = [600, 300, 630, 360]
        self.assertEqual(selector.select_target(_detections(far_box), frame), first)
        self.assertEqual(selector.select_target(_detections(far_box), frame), first)
        self.assertIsNone(selector.select_target(_detections(far_box), frame))
        self.assertIsNone(selector.last_target_center)

    def test_no_detections_without_stable_history_returns_none_and_resets(self):
        selector = TargetSelector(crop_size=CROP)
        frame = _frame()
        first = _confirm_target(selector, _detections([300, 240, 340, 360], confs=[0.95]), frame)
        self.assertIsNotNone(first)
        self.assertIsNone(selector.select_target(_detections(), frame))
        self.assertIsNone(selector.last_target_center)

    def test_tracked_target_accepts_lower_confidence_when_it_stays_near_last_center(self):
        selector = TargetSelector(crop_size=CROP)
        frame = _frame()

        first = _confirm_target(selector, _detections([300, 240, 340, 360], confs=[0.95]), frame)
        self.assertIsNotNone(first)

        second = selector.select_target(_detections([306, 242, 346, 362], confs=[0.42]), frame)

        self.assertIsNotNone(second)
        self.assertGreater(second.target_x, first.target_x)

    def test_jump_budgets_scale_with_frame_dimensions(self):
        square = TargetSelector(crop_size=640)
        wide = TargetSelector(frame_width=896, frame_height=512)
        self.assertAlmostEqual(square.max_jump_x, 180.0, places=3)
        self.assertAlmostEqual(square.max_jump_y, 180.0, places=3)
        self.assertGreater(wide.max_jump_x, square.max_jump_x)
        self.assertLess(wide.max_jump_y, square.max_jump_y)

    def test_scaled_constants_preserve_640_square_behavior(self):
        square = TargetSelector(crop_size=640)
        self.assertAlmostEqual(square.tracking_radius, 120.0, places=3)
        self.assertAlmostEqual(square.max_smoothing_jump, 24.0, places=3)
        self.assertAlmostEqual(square.ideal_area, 8000.0, places=3)
        self.assertAlmostEqual(square.max_area_limit, 40000.0, places=3)

    def test_scaled_constants_adapt_to_widescreen_resolution(self):
        wide = TargetSelector(frame_width=896, frame_height=512)
        avg_dim = (896 + 512) / 2.0
        frame_area = 896.0 * 512.0
        self.assertAlmostEqual(wide.tracking_radius, avg_dim * (120.0 / 640.0), places=3)
        self.assertAlmostEqual(wide.max_smoothing_jump, avg_dim * (24.0 / 640.0), places=3)
        self.assertAlmostEqual(wide.ideal_area, frame_area * (8000.0 / (640.0 * 640.0)), places=3)
        self.assertAlmostEqual(wide.max_area_limit, frame_area * (40000.0 / (640.0 * 640.0)), places=3)

    def test_widescreen_multi_candidate_accepts_horizontal_targets_square_would_reject(self):
        selector = TargetSelector(frame_width=896, frame_height=512)
        frame = np.full((512, 896, 3), NEUTRAL_RGB, dtype=np.uint8)
        box_a = [180, 220, 220, 290]
        box_b = [660, 220, 700, 290]
        selected = _confirm_target(selector, _detections(box_a, box_b), frame)
        self.assertIsNotNone(selected)
        self.assertGreater(selected.target_x, 600.0)

    def test_widescreen_multi_candidate_prefers_screen_fraction_closer_target(self):
        selector = TargetSelector(frame_width=896, frame_height=512)
        frame = np.full((512, 896, 3), NEUTRAL_RGB, dtype=np.uint8)
        vertical_box = [428, 82, 468, 142]
        horizontal_box = [584, 238, 624, 298]
        selected = _confirm_target(selector, _detections(vertical_box, horizontal_box), frame)
        self.assertIsNotNone(selected)
        self.assertGreater(selected.target_x, 580.0)
        self.assertGreater(selected.target_y, 240.0)

    def test_all_friendlies_returns_none(self):
        selector = TargetSelector(crop_size=CROP)
        box_a = [300, 300, 340, 360]
        box_b = [400, 300, 440, 360]
        frame = _frame()
        _paint_color_above(frame, box_a, FRIENDLY_RGB)
        _paint_color_above(frame, box_b, FRIENDLY_RGB)
        self.assertIsNone(_confirm_target(selector, _detections(box_a, box_b), frame))

    def test_large_uniform_blue_region_above_box_is_not_treated_as_friendly(self):
        selector = TargetSelector(crop_size=CROP)
        box = [300, 240, 340, 360]
        frame = _frame()
        _paint_full_color_above(frame, box, (80, 180, 255))

        color_bonus, is_friendly = selector._classify_color(np.array(box, dtype=np.float32), frame)

        self.assertEqual(color_bonus, 0.0)
        self.assertFalse(is_friendly)

    def test_box_target_exposes_upper_chest_and_torso_slow_zone_without_keypoints(self):
        selector = TargetSelector(crop_size=CROP)
        box = [280, 240, 360, 380]
        frame = _frame()
        selected = _confirm_target(selector, _detections(box), frame)

        self.assertIsNotNone(selected)
        self.assertIsNotNone(selected.slow_zone)
        slow_left, slow_top, slow_right, slow_bottom = selected.slow_zone
        self.assertLess(slow_left, selector.screen_center_x)
        self.assertGreater(slow_right, selector.screen_center_x)
        self.assertLess(slow_top, selector.screen_center_y)
        self.assertGreater(slow_bottom, selector.screen_center_y)
        self.assertAlmostEqual(selected.target_x, 320.0, places=3)
        expected_target_y = box[1] + ((box[3] - box[1]) * selector.UPPER_CHEST_RATIO)
        self.assertAlmostEqual(selected.target_y, expected_target_y, places=3)

    def test_tracking_small_box_jitter_is_smoothed_before_output(self):
        selector = TargetSelector(crop_size=CROP)
        frame = _frame()

        first = _confirm_target(selector, _detections([300, 240, 340, 360], confs=[0.95]), frame)
        second = selector.select_target(_detections([308, 242, 348, 362], confs=[0.92]), frame)

        self.assertIsNotNone(first)
        self.assertIsNotNone(second)
        self.assertGreater(second.target_x, first.target_x)
        self.assertLess(second.target_x, 328.0)
        self.assertGreater(second.target_y, first.target_y)
        raw_second_target_y = 242.0 + ((362.0 - 242.0) * selector.UPPER_CHEST_RATIO)
        self.assertLess(second.target_y, raw_second_target_y)

    def test_tracking_stays_on_current_target_when_neighbor_score_only_improves_slightly(self):
        selector = TargetSelector(crop_size=CROP)
        frame = _frame()

        first = _confirm_target(
            selector,
            _detections(
                [286, 240, 326, 360],
                [314, 240, 354, 360],
                confs=[0.91, 0.89],
            ),
            frame,
        )
        second = selector.select_target(
            _detections(
                [288, 240, 328, 360],
                [312, 240, 352, 360],
                confs=[0.88, 0.92],
            ),
            frame,
        )
        third = selector.select_target(
            _detections(
                [287, 240, 327, 360],
                [313, 240, 353, 360],
                confs=[0.92, 0.89],
            ),
            frame,
        )

        self.assertIsNotNone(first)
        self.assertIsNotNone(second)
        self.assertIsNotNone(third)
        self.assertLess(first.target_x, 320.0)
        self.assertLess(second.target_x, 320.0)
        self.assertLess(third.target_x, 320.0)

    def test_tracking_requires_two_frames_before_switching_to_new_target(self):
        selector = TargetSelector(crop_size=CROP)
        frame = _frame()

        locked = _confirm_target(selector, _detections([250, 240, 290, 360], confs=[0.92]), frame)
        self.assertIsNotNone(locked)
        self.assertLess(locked.target_x, 290.0)

        contender_detections = _detections(
            [252, 240, 292, 360],
            [300, 240, 340, 360],
            confs=[0.90, 0.90],
        )

        first_switch_frame = selector.select_target(contender_detections, frame)
        second_switch_frame = selector.select_target(contender_detections, frame)

        self.assertIsNotNone(first_switch_frame)
        self.assertIsNotNone(second_switch_frame)
        self.assertLess(first_switch_frame.target_x, 290.0)
        self.assertGreater(second_switch_frame.target_x, 310.0)

    def test_tracking_holds_current_target_for_one_frame_when_only_new_target_appears(self):
        selector = TargetSelector(crop_size=CROP)
        frame = _frame()

        locked = _confirm_target(selector, _detections([250, 240, 290, 360], confs=[0.92]), frame)
        self.assertIsNotNone(locked)

        first_switch_frame = selector.select_target(_detections([300, 240, 340, 360], confs=[0.92]), frame)
        second_switch_frame = selector.select_target(_detections([300, 240, 340, 360], confs=[0.92]), frame)

        self.assertIsNotNone(first_switch_frame)
        self.assertIsNotNone(second_switch_frame)
        self.assertLess(first_switch_frame.target_x, 290.0)
        self.assertGreater(second_switch_frame.target_x, 310.0)

    def test_enemy_colored_challenger_can_break_neutral_lock_even_without_big_distance_gain(self):
        selector = TargetSelector(crop_size=CROP)
        neutral_box = [272, 240, 312, 360]
        enemy_box = [316, 240, 356, 360]

        first_frame = _frame()
        locked = _confirm_target(selector, _detections(neutral_box, confs=[0.90]), first_frame)
        self.assertIsNotNone(locked)
        self.assertLess(locked.target_x, 320.0)

        challenger_frame = _frame()
        _paint_color_above(challenger_frame, enemy_box, ENEMY_RGB)
        contender_detections = _detections(
            neutral_box,
            enemy_box,
            confs=[0.90, 0.90],
        )

        first_switch_frame = selector.select_target(contender_detections, challenger_frame)
        second_switch_frame = selector.select_target(contender_detections, challenger_frame)

        self.assertIsNotNone(first_switch_frame)
        self.assertIsNotNone(second_switch_frame)
        self.assertLess(first_switch_frame.target_x, 320.0)
        self.assertGreater(second_switch_frame.target_x, 320.0)

    def test_pickup_requires_two_consecutive_frames_before_output(self):
        selector = TargetSelector(crop_size=CROP)
        frame = _frame()
        detections = _detections([300, 240, 340, 360], confs=[0.90])

        self.assertIsNone(selector.select_target(detections, frame))
        self.assertIsNotNone(selector.select_target(detections, frame))

    def test_pickup_threshold_is_more_conservative(self):
        selector = TargetSelector(crop_size=CROP)
        frame = _frame()
        detections = _detections([300, 240, 340, 360], confs=[0.60])

        self.assertIsNone(_confirm_target(selector, detections, frame))

    def test_stable_target_prediction_budget_resets_last_target_center_after_third_miss(self):
        selector = TargetSelector(crop_size=CROP)
        frame = _frame()
        locked = _confirm_target(selector, _detections([300, 240, 340, 360], confs=[0.95]), frame)
        self.assertIsNotNone(locked)

        followed_up = selector.select_target(_detections([306, 244, 346, 364], confs=[0.95]), frame)
        self.assertIsNotNone(followed_up)

        predicted_one = selector.select_target(_detections(), frame)
        predicted_two = selector.select_target(_detections(), frame)
        lost = selector.select_target(_detections(), frame)

        self.assertIsNotNone(predicted_one)
        self.assertIsNotNone(predicted_two)
        self.assertEqual(getattr(predicted_one, "source", None), "predicted")
        self.assertEqual(getattr(predicted_two, "source", None), "predicted")
        self.assertIsNone(lost)
        self.assertIsNone(selector.last_target_center)

    def test_partial_occlusion_reconstructs_upper_box_from_recent_stable_height(self):
        selector = TargetSelector(crop_size=CROP)
        frame = _frame()

        locked = _confirm_target(selector, _detections([300, 240, 340, 360], confs=[0.95]), frame)
        self.assertIsNotNone(locked)

        clipped_box = [304, 286, 344, 362]
        reconstructed = selector.select_target(_detections(clipped_box, confs=[0.93]), frame)

        self.assertIsNotNone(reconstructed)
        self.assertLess(reconstructed.selected_box[1], float(clipped_box[1]))
        self.assertAlmostEqual(reconstructed.selected_box[3], float(clipped_box[3]), places=3)
        self.assertEqual(getattr(reconstructed, "source", None), "reconstructed")
        raw_target_y = clipped_box[1] + ((clipped_box[3] - clipped_box[1]) * selector.UPPER_CHEST_RATIO)
        self.assertLess(abs(reconstructed.target_y - locked.target_y), abs(raw_target_y - locked.target_y))

    def test_short_occlusion_prediction_bridges_only_two_frames(self):
        selector = TargetSelector(crop_size=CROP)
        frame = _frame()

        first = _confirm_target(selector, _detections([300, 240, 340, 360], confs=[0.95]), frame)
        second = selector.select_target(_detections([306, 244, 346, 364], confs=[0.95]), frame)

        self.assertIsNotNone(first)
        self.assertIsNotNone(second)

        predicted_one = selector.select_target(_detections(), frame)
        predicted_two = selector.select_target(_detections(), frame)
        lost = selector.select_target(_detections(), frame)

        self.assertIsNotNone(predicted_one)
        self.assertIsNotNone(predicted_two)
        self.assertEqual(getattr(predicted_one, "source", None), "predicted")
        self.assertEqual(getattr(predicted_two, "source", None), "predicted")
        self.assertIsNone(lost)

    def test_resolve_no_candidates_returns_predicted_target_before_reset(self):
        selector = TargetSelector(crop_size=CROP)
        frame = _frame()

        _confirm_target(selector, _detections([300, 240, 340, 360], confs=[0.95]), frame)
        selector.select_target(_detections([306, 244, 346, 364], confs=[0.95]), frame)
        sample_timestamp = selector._sample_clock()

        predicted_one = selector._resolve_no_candidates(sample_timestamp=sample_timestamp)
        predicted_two = selector._resolve_no_candidates(sample_timestamp=sample_timestamp)
        lost = selector._resolve_no_candidates(sample_timestamp=sample_timestamp)

        self.assertIsNotNone(predicted_one)
        self.assertIsNotNone(predicted_two)
        self.assertEqual(getattr(predicted_one, "source", None), "predicted")
        self.assertEqual(getattr(predicted_two, "source", None), "predicted")
        self.assertIsNone(lost)

    def test_reacquired_target_exits_predicted_state_immediately(self):
        selector = TargetSelector(crop_size=CROP)
        frame = _frame()

        _confirm_target(selector, _detections([300, 240, 340, 360], confs=[0.95]), frame)
        selector.select_target(_detections([306, 244, 346, 364], confs=[0.95]), frame)

        predicted = selector.select_target(_detections(), frame)
        reacquired = selector.select_target(_detections([312, 248, 352, 368], confs=[0.95]), frame)

        self.assertIsNotNone(predicted)
        self.assertEqual(getattr(predicted, "source", None), "predicted")
        self.assertIsNotNone(reacquired)
        self.assertEqual(getattr(reacquired, "source", None), "observed")


class CrosshairPersonHitDetectorTests(unittest.TestCase):
    def test_autofire_does_not_trigger_when_selector_filters_out_friendly(self):
        selector = TargetSelector(crop_size=CROP)
        detector = CrosshairPersonHitDetector(crop_size=CROP)
        box = [280, 240, 360, 380]
        frame = _frame()
        _paint_color_above(frame, box, FRIENDLY_RGB)

        selected_target = _confirm_target(selector, _detections(box), frame)

        self.assertIsNone(selected_target)
        self.assertFalse(detector.update(selected_target))

    def test_autofire_triggers_for_selected_target_inside_fire_zone(self):
        selector = TargetSelector(crop_size=CROP)
        detector = CrosshairPersonHitDetector(crop_size=CROP)
        box = [280, 240, 360, 380]

        selected_target = _confirm_target(selector, _detections(box), _frame())

        self.assertIsNotNone(selected_target)
        self.assertFalse(detector.update(None))
        self.assertTrue(detector.update(selected_target))

    def test_autofire_release_grace_frames_apply_after_selected_target_loss(self):
        selector = TargetSelector(crop_size=CROP)
        detector = CrosshairPersonHitDetector(crop_size=CROP, release_grace_frames=2)
        box = [280, 240, 360, 380]

        selected_target = _confirm_target(selector, _detections(box), _frame())

        self.assertTrue(detector.update(selected_target))
        self.assertTrue(detector.update(None))
        self.assertFalse(detector.update(None))

    def test_autofire_does_not_use_centered_detection_without_selected_target(self):
        selector = TargetSelector(crop_size=CROP)
        detector = CrosshairPersonHitDetector(crop_size=CROP, min_conf=0.30, release_grace_frames=1, target_selector=selector)
        box = [280, 240, 360, 380]
        frame = _frame()

        selected_target = _confirm_target(selector, _detections(box, confs=[0.95]), frame)

        self.assertIsNotNone(selected_target)
        self.assertTrue(detector.update(selected_target))
        self.assertFalse(detector.update(None, _detections(box, confs=[0.95]), frame))

    def test_autofire_fallback_still_filters_out_friendlies(self):
        selector = TargetSelector(crop_size=CROP)
        detector = CrosshairPersonHitDetector(crop_size=CROP, min_conf=0.30, target_selector=selector)
        box = [280, 240, 360, 380]
        frame = _frame()
        _paint_color_above(frame, box, FRIENDLY_RGB)

        self.assertFalse(detector.update(None, _detections(box, confs=[0.34]), frame))


if __name__ == "__main__":
    unittest.main()
