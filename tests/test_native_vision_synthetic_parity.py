import unittest

import numpy as np

from tests.test_native_vision_targeting_bridge import (
    CROP_H,
    CROP_W,
    ENEMY_RGB,
    FRIENDLY_RGB,
    NEUTRAL_RGB,
    _load_native_module,
    _paint_color_above,
)
from vision.enhancement import AimEnhancementPipeline
from vision.targeting import CrosshairPersonHitDetector, ParsedDetections, TargetSelector


def _frame():
    return np.full((CROP_H, CROP_W, 3), NEUTRAL_RGB, dtype=np.uint8)


def _rows(*boxes, confs=None):
    if not boxes:
        return np.empty((0, 6), dtype=np.float32)
    values = []
    for index, box in enumerate(boxes):
        conf = 0.90 if confs is None else confs[index]
        values.append([*box, conf, 0.0])
    return np.array(values, dtype=np.float32)


def _parsed(rows):
    if rows.size == 0:
        return [
            ParsedDetections(
                boxes=np.empty((0, 4), dtype=np.float32),
                confs=np.empty((0,), dtype=np.float32),
            )
        ]
    return [
        ParsedDetections(
            boxes=rows[:, :4].astype(np.float32),
            confs=rows[:, 4].astype(np.float32),
        )
    ]


def _slow_zone_from_result(result):
    if not result["has_body_box"]:
        return None
    x1 = float(result["body_x1"])
    y1 = float(result["body_y1"])
    x2 = float(result["body_x2"])
    y2 = float(result["body_y2"])
    box_w = x2 - x1
    box_h = y2 - y1
    if box_w <= 0.0 or box_h <= 0.0:
        return None
    return (
        x1 + (box_w * 0.22),
        y1 + (box_h * 0.18),
        x2 - (box_w * 0.22),
        y2 - (box_h * 0.20),
    )


def _empty_result():
    return {
        "has_target": False,
        "auto_fire": False,
        "dx": 0.0,
        "dy": 0.0,
        "target_source": "",
        "boxes_seen": 0.0,
    }


class PythonVisionParityPipeline:
    def __init__(self):
        self.selector = TargetSelector(frame_width=CROP_W, frame_height=CROP_H)
        self.detector = CrosshairPersonHitDetector(frame_width=CROP_W, frame_height=CROP_H)
        self.enhancer = AimEnhancementPipeline()
        self._sample_timestamp = 1000.0
        self.selector._sample_clock = self._next_sample_timestamp

    def _next_sample_timestamp(self):
        self._sample_timestamp += 1.0
        return self._sample_timestamp

    def process(self, rows, frame, timestamp):
        detections = _parsed(rows)
        selected = self.selector.select_target(detections, frame)
        auto_fire = self.detector.update(selected, detections, frame)
        result = _empty_result()
        result["boxes_seen"] = float(rows.shape[0])
        result["auto_fire"] = bool(auto_fire)
        if selected is None:
            self.enhancer.reset()
            return result

        dx, dy = self.enhancer.process(selected, timestamp=timestamp)
        result.update(
            {
                "has_target": True,
                "dx": dx,
                "dy": dy,
                "target_x": selected.target_x,
                "target_y": selected.target_y,
                "target_source": selected.source,
                "has_body_box": selected.selected_box is not None,
            }
        )
        if selected.selected_box is not None:
            result["body_box"] = tuple(float(value) for value in selected.selected_box)
        return result


class NativeVisionParityPipeline:
    def __init__(self, module):
        self.selector = module.NativeTargetSelector(CROP_W, CROP_H)
        self.enhancer = module.NativeAimEnhancer()

    def process(self, rows, frame, timestamp):
        result = self.selector.select_xyxy_rgb(rows, frame)
        if result["has_target"]:
            enhanced = self.enhancer.process(
                result["target_x"],
                result["target_y"],
                result["screen_center_x"],
                result["screen_center_y"],
                _slow_zone_from_result(result),
                result["target_source"],
                timestamp,
            )
            result["dx"] = enhanced["dx"]
            result["dy"] = enhanced["dy"]
        else:
            self.enhancer.reset()
        return result


class NativeVisionSyntheticParityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.module = _load_native_module()

    def assert_result_close(self, python_result, native_result, *, step):
        self.assertEqual(
            python_result["has_target"],
            native_result["has_target"],
            f"has_target mismatch at step {step}",
        )
        self.assertEqual(
            python_result["auto_fire"],
            native_result["auto_fire"],
            f"auto_fire mismatch at step {step}",
        )
        self.assertEqual(
            python_result["target_source"],
            native_result["target_source"],
            f"target_source mismatch at step {step}",
        )
        self.assertAlmostEqual(
            python_result["boxes_seen"],
            native_result["boxes_seen"],
            places=3,
            msg=f"boxes_seen mismatch at step {step}",
        )
        if not python_result["has_target"]:
            return
        self.assertAlmostEqual(python_result["dx"], native_result["dx"], delta=0.35, msg=f"dx mismatch at step {step}")
        self.assertAlmostEqual(python_result["dy"], native_result["dy"], delta=0.35, msg=f"dy mismatch at step {step}")

    def assert_sequence_parity(self, sequence):
        python_pipeline = PythonVisionParityPipeline()
        native_pipeline = NativeVisionParityPipeline(self.module)
        for index, (rows, frame, timestamp) in enumerate(sequence):
            python_result = python_pipeline.process(rows, frame, timestamp)
            native_result = native_pipeline.process(rows, frame, timestamp)
            self.assert_result_close(python_result, native_result, step=index)

    def test_center_lock_autofire_and_enhancement_match_python(self):
        frame = _frame()
        lock_box = [280.0, 240.0, 360.0, 380.0]
        rows = _rows(lock_box, confs=[0.95])
        frame_dt = 1.0 / 80.0

        self.assert_sequence_parity(
            [
                (rows, frame, 1.0),
                (rows, frame, 1.0 + frame_dt),
                (_rows(), frame, 1.0 + (frame_dt * 2.0)),
                (_rows(), frame, 1.0 + (frame_dt * 3.0)),
            ]
        )

    def test_color_filter_and_enemy_pickup_match_python(self):
        frame = _frame()
        friendly_box = [300.0, 180.0, 360.0, 320.0]
        enemy_box = [390.0, 180.0, 450.0, 320.0]
        _paint_color_above(frame, friendly_box, FRIENDLY_RGB)
        _paint_color_above(frame, enemy_box, ENEMY_RGB)
        rows = _rows(friendly_box, enemy_box, confs=[0.90, 0.74])

        self.assert_sequence_parity(
            [
                (rows, frame, 1.0),
                (rows, frame, 1.1),
            ]
        )

    def test_occlusion_prediction_and_reacquire_match_python(self):
        frame = _frame()
        first_box = [300.0, 240.0, 340.0, 360.0]
        second_box = [306.0, 244.0, 346.0, 364.0]
        reacquired_box = [312.0, 248.0, 352.0, 368.0]
        frame_dt = 1.0 / 80.0

        self.assert_sequence_parity(
            [
                (_rows(first_box, confs=[0.95]), frame, 1.0),
                (_rows(first_box, confs=[0.95]), frame, 1.0 + frame_dt),
                (_rows(second_box, confs=[0.95]), frame, 1.0 + (frame_dt * 2.0)),
                (_rows(), frame, 1.0 + (frame_dt * 3.0)),
                (_rows(), frame, 1.0 + (frame_dt * 4.0)),
                (_rows(reacquired_box, confs=[0.95]), frame, 1.0 + (frame_dt * 5.0)),
            ]
        )


if __name__ == "__main__":
    unittest.main()
