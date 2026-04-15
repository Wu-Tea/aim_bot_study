import os
import unittest
from unittest.mock import Mock, patch

import numpy as np

from controllers.base_controller import BaseController
from vision.runner import VisionConfig, _resolve_tracking_frame
from vision.targeting import SelectedTarget


class VisionRunnerTests(unittest.TestCase):
    def test_from_env_uses_dataclass_defaults_when_env_is_unset(self):
        with patch.dict(os.environ, {}, clear=True):
            config = VisionConfig.from_env()

        self.assertEqual(config.capture_width, 896)
        self.assertEqual(config.capture_height, 512)
        self.assertEqual(config.capture_fps, 70)
        self.assertEqual(config.conf, 0.40)
        self.assertFalse(config.debug_overlay)
        self.assertFalse(config.debug_save_frames)
        self.assertEqual(config.image_size, (512, 896))

    def test_from_env_allows_runtime_overrides(self):
        with patch.dict(
            os.environ,
            {
                "VISION_CROP_WIDTH": "896",
                "VISION_CROP_HEIGHT": "512",
                "VISION_CAPTURE_FPS": "144",
            },
            clear=True,
        ):
            config = VisionConfig.from_env()

        self.assertEqual(config.capture_width, 896)
        self.assertEqual(config.capture_height, 512)
        self.assertEqual(config.capture_fps, 144)
        self.assertEqual(config.image_size, (512, 896))

    def test_from_env_allows_debug_overlay_override(self):
        with patch.dict(
            os.environ,
            {
                "VISION_DEBUG_OVERLAY": "1",
            },
            clear=True,
        ):
            config = VisionConfig.from_env()

        self.assertTrue(config.debug_overlay)

    def test_from_env_allows_debug_save_override(self):
        with patch.dict(
            os.environ,
            {
                "VISION_DEBUG_SAVE": "1",
            },
            clear=True,
        ):
            config = VisionConfig.from_env()

        self.assertTrue(config.debug_save_frames)

    def test_from_env_supports_legacy_square_overrides(self):
        with patch.dict(
            os.environ,
            {
                "VISION_CROP_SIZE": "512",
                "VISION_TARGET_FPS": "120",
            },
            clear=True,
        ):
            config = VisionConfig.from_env()

        self.assertEqual(config.capture_width, 512)
        self.assertEqual(config.capture_height, 512)
        self.assertEqual(config.capture_fps, 120)

    def test_from_env_allows_model_path_overrides(self):
        with patch.dict(
            os.environ,
            {
                "VISION_MODEL_PATH": "D:/models/person_v1.engine",
                "VISION_FALLBACK_MODEL_PATH": "D:/models/person_v1.pt",
            },
            clear=True,
        ):
            config = VisionConfig.from_env()

        self.assertEqual(config.model_path, "D:/models/person_v1.engine")
        self.assertEqual(config.fallback_model_path, "D:/models/person_v1.pt")


class _AliasController(BaseController):
    def __init__(self):
        self.auto_fire_values = []

    def update(self, dx, dy):
        return None

    def reset(self):
        return None

    def is_aiming(self):
        return False

    def set_auto_fire(self, pressed: bool):
        self.auto_fire_values.append(bool(pressed))


class BaseControllerAliasTests(unittest.TestCase):
    def test_set_auto_rb_forwards_to_set_auto_fire(self):
        controller = _AliasController()

        controller.set_auto_rb(True)

        self.assertEqual(controller.auto_fire_values, [True])


class VisionRunnerTrackingResolutionTests(unittest.TestCase):
    def test_resolve_tracking_frame_keeps_selector_hold_on_empty_detections(self):
        frame = np.zeros((96, 160, 3), dtype=np.uint8)
        held_target = SelectedTarget(
            target_x=80.0,
            target_y=40.0,
            screen_center_x=80.0,
            screen_center_y=48.0,
            score=321.0,
            slow_zone=(66.0, 30.0, 94.0, 76.0),
            fire_zone=(68.0, 26.0, 92.0, 72.0),
        )
        selector = Mock()
        selector.select_target.return_value = held_target
        rb_hit_detector = Mock()
        rb_hit_detector.update.return_value = True
        aim_enhancement = Mock()
        aim_enhancement.process.return_value = (1.5, -2.0)

        resolved = _resolve_tracking_frame(
            frame=frame,
            detections=[],
            target_selector=selector,
            rb_hit_detector=rb_hit_detector,
            aim_enhancement=aim_enhancement,
            timestamp=12.5,
        )

        selector.select_target.assert_called_once_with([], frame)
        selector.reset_tracking.assert_not_called()
        rb_hit_detector.update.assert_called_once_with(held_target, [], frame)
        aim_enhancement.process.assert_called_once_with(held_target, timestamp=12.5)
        self.assertEqual(resolved.selected_target, held_target)
        self.assertTrue(resolved.auto_fire_active)
        self.assertEqual(resolved.best_target_delta, (1.5, -2.0))
        self.assertEqual(resolved.boxes_seen, 0)

    def test_resolve_tracking_frame_does_not_drop_selector_state_when_frame_missing(self):
        selector = Mock()
        rb_hit_detector = Mock()
        aim_enhancement = Mock()

        resolved = _resolve_tracking_frame(
            frame=None,
            detections=[],
            target_selector=selector,
            rb_hit_detector=rb_hit_detector,
            aim_enhancement=aim_enhancement,
            timestamp=8.0,
        )

        selector.select_target.assert_not_called()
        selector.reset_tracking.assert_not_called()
        rb_hit_detector.reset.assert_called_once_with()
        aim_enhancement.reset.assert_called_once_with()
        self.assertIsNone(resolved.selected_target)
        self.assertFalse(resolved.auto_fire_active)
        self.assertIsNone(resolved.best_target_delta)
        self.assertEqual(resolved.boxes_seen, 0)


if __name__ == "__main__":
    unittest.main()
