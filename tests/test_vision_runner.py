import os
import unittest
from unittest.mock import Mock, patch

import numpy as np

from controllers.base_controller import BaseController, ControllerTarget
from vision.runner import TrackingFrameResolution, VisionConfig, _resolve_tracking_frame, process_vision
from vision.targeting import SelectedTarget


class VisionRunnerTests(unittest.TestCase):
    def test_from_env_uses_dataclass_defaults_when_env_is_unset(self):
        with patch.dict(os.environ, {}, clear=True):
            config = VisionConfig.from_env()

        self.assertEqual(config.capture_width, 640)
        self.assertEqual(config.capture_height, 512)
        self.assertEqual(config.capture_fps, 80)
        self.assertEqual(config.conf, 0.40)
        self.assertFalse(config.debug_overlay)
        self.assertFalse(config.debug_save_frames)
        self.assertEqual(config.image_size, (512, 640))

    def test_from_env_allows_runtime_overrides(self):
        with patch.dict(
            os.environ,
            {
                "VISION_CROP_WIDTH": "896",
                "VISION_CROP_HEIGHT": "512",
                "VISION_CAPTURE_FPS": "144",
                "VISION_IDLE_CAPTURE_FPS": "12",
                "VISION_FAST_PREPROCESSOR": "native",
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
        self.updates = []

    def update(self, dx, dy, target=None):
        self.updates.append((dx, dy, target))

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

    def test_update_can_receive_controller_target_metadata(self):
        controller = _AliasController()
        target = ControllerTarget(
            aim_point_x=320.0,
            aim_point_y=210.0,
            screen_center_x=320.0,
            screen_center_y=256.0,
            body_box=(282.0, 128.0, 358.0, 316.0),
        )

        controller.update(8.0, -4.0, target=target)

        self.assertEqual(len(controller.updates), 1)
        self.assertEqual(controller.updates[0][0:2], (8.0, -4.0))
        self.assertEqual(controller.updates[0][2], target)


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


class _FakeCaptureThread:
    last_instance = None

    def __init__(self, *args, **kwargs):
        self.started = False
        self.stopped = False
        self.join_timeout = None
        self.target_fps = kwargs.get("target_fps")
        self.target_fps_calls = []
        self.frames = [
            (np.ones((4, 4, 3), dtype=np.uint8), 1),
            (np.full((4, 4, 3), 2, dtype=np.uint8), 2),
        ]
        _FakeCaptureThread.last_instance = self

    def start(self):
        self.started = True

    def get_latest_frame(self, last_seen_id=0, timeout=0.1):
        if self.frames:
            return self.frames.pop(0)
        return None, last_seen_id

    def stop(self):
        self.stopped = True

    def join(self, timeout=None):
        self.join_timeout = timeout

    def set_target_fps(self, target_fps):
        self.target_fps_calls.append(target_fps)


class _FakeInferenceThread:
    last_instance = None
    scripted_results = None

    def __init__(self, *args, **kwargs):
        self.started = False
        self.stop_called = False
        self.join_timeout = None
        self.resume_calls = 0
        self.pause_calls = []
        if self.scripted_results is None:
            self.results = [
                (
                    Mock(
                        frame_id=7,
                        captured_at=20.0,
                        inferred_at=20.01,
                        frame=np.ones((4, 4, 3), dtype=np.uint8),
                        detections=[],
                        infer_ms=6.5,
                    ),
                    7,
                ),
                (
                    Mock(
                        frame_id=8,
                        captured_at=21.0,
                        inferred_at=21.01,
                        frame=np.full((4, 4, 3), 2, dtype=np.uint8),
                        detections=[],
                        infer_ms=6.0,
                    ),
                    8,
                ),
            ]
        else:
            self.results = list(self.scripted_results)
        _FakeInferenceThread.last_instance = self

    def start(self):
        self.started = True

    def resume(self):
        self.resume_calls += 1

    def pause(self, clear_result=True):
        self.pause_calls.append(clear_result)

    def get_latest_result(self, last_seen_id=0, timeout=0.1):
        if self.results:
            next_result = self.results.pop(0)
            if next_result is None:
                return None, last_seen_id
            return next_result
        return None, last_seen_id

    def stop(self):
        self.stop_called = True

    def join(self, timeout=None):
        self.join_timeout = timeout


class VisionRunnerPipelineTests(unittest.TestCase):
    @patch("vision.runner.time.sleep", return_value=None)
    @patch("vision.runner.win32api.GetAsyncKeyState", side_effect=[0, 0x8000])
    @patch("vision.runner._warmup_model", return_value=None)
    @patch("vision.runner._load_model", return_value=object())
    @patch("vision.runner._resolve_tracking_frame")
    @patch("vision.runner.PerformanceTracker")
    @patch("vision.runner.CrosshairPersonHitDetector")
    @patch("vision.runner.AimEnhancementPipeline")
    @patch("vision.runner.TargetSelector")
    def test_process_vision_routes_frame_and_detections_through_inference_thread(
        self,
        target_selector_cls,
        aim_enhancement_cls,
        rb_hit_detector_cls,
        perf_tracker_cls,
        resolve_tracking_frame,
        _load_model,
        _warmup_model,
        _get_async_key_state,
        _sleep,
    ):
        perf_tracker = Mock()
        perf_tracker_cls.return_value = perf_tracker
        resolve_tracking_frame.return_value = TrackingFrameResolution(
            selected_target=None,
            auto_fire_active=False,
            best_target_delta=None,
            boxes_seen=0,
        )
        controller = Mock()
        controller.is_aiming.side_effect = [True, False, True]

        with patch("vision.runner.ScreenCaptureThread", _FakeCaptureThread), patch(
            "vision.runner.InferenceThread", _FakeInferenceThread, create=True
        ), patch(
            "vision.runner.VisionConfig.from_env",
            return_value=VisionConfig(frame_timeout=0.01, idle_sleep=0.0),
        ):
            process_vision(controller=controller)

        inference = _FakeInferenceThread.last_instance
        self.assertIsNotNone(inference)
        self.assertTrue(inference.started)
        self.assertEqual(inference.resume_calls, 2)
        self.assertEqual(inference.pause_calls, [True])
        self.assertTrue(inference.stop_called)
        self.assertEqual(inference.join_timeout, 1.0)

        resolved_frames = [
            call.kwargs["frame"]
            for call in resolve_tracking_frame.call_args_list
            if call.kwargs["frame"] is not None
        ]
        self.assertEqual(len(resolved_frames), 2)
        self.assertEqual(int(resolved_frames[0][0, 0, 0]), 1)
        self.assertEqual(int(resolved_frames[1][0, 0, 0]), 2)
        perf_tracker.update.assert_called()
        self.assertIn("wait_ms", perf_tracker.update.call_args.kwargs)
        self.assertIn("age_ms", perf_tracker.update.call_args.kwargs)
        self.assertGreaterEqual(perf_tracker.update.call_args.kwargs["age_ms"], 0.0)

    @patch("vision.runner.time.sleep", return_value=None)
    @patch("vision.runner.win32api.GetAsyncKeyState", side_effect=[0, 0x8000])
    @patch("vision.runner._warmup_model", return_value=None)
    @patch("vision.runner._load_model", return_value=object())
    @patch("vision.runner._resolve_tracking_frame")
    @patch("vision.runner.PerformanceTracker")
    @patch("vision.runner.CrosshairPersonHitDetector")
    @patch("vision.runner.AimEnhancementPipeline")
    @patch("vision.runner.TargetSelector")
    def test_process_vision_keeps_capture_rate_high_across_idle_and_ads(
        self,
        target_selector_cls,
        aim_enhancement_cls,
        rb_hit_detector_cls,
        perf_tracker_cls,
        resolve_tracking_frame,
        _load_model,
        _warmup_model,
        _get_async_key_state,
        _sleep,
    ):
        perf_tracker_cls.return_value = Mock()
        resolve_tracking_frame.return_value = TrackingFrameResolution(
            selected_target=None,
            auto_fire_active=False,
            best_target_delta=None,
            boxes_seen=0,
        )
        controller = Mock()
        controller.is_aiming.side_effect = [False, True, False, True]

        with patch("vision.runner.ScreenCaptureThread", _FakeCaptureThread), patch(
            "vision.runner.InferenceThread", _FakeInferenceThread, create=True
        ), patch(
            "vision.runner.VisionConfig.from_env",
            return_value=VisionConfig(
                capture_fps=80,
                frame_timeout=0.01,
                idle_sleep=0.0,
            ),
        ):
            process_vision(controller=controller)

        capture = _FakeCaptureThread.last_instance
        self.assertIsNotNone(capture)
        self.assertEqual(capture.target_fps, 80)
        self.assertEqual(capture.target_fps_calls, [])

    @patch("vision.runner.time.sleep", return_value=None)
    @patch("vision.runner.win32api.GetAsyncKeyState", side_effect=[0, 0x8000])
    @patch("vision.runner._warmup_model", return_value=None)
    @patch("vision.runner._load_model", return_value=object())
    @patch("vision.runner._resolve_tracking_frame")
    @patch("vision.runner.PerformanceTracker")
    @patch("vision.runner.CrosshairPersonHitDetector")
    @patch("vision.runner.AimEnhancementPipeline")
    @patch("vision.runner.TargetSelector")
    def test_process_vision_treats_inference_gap_as_empty_detection_frame(
        self,
        target_selector_cls,
        aim_enhancement_cls,
        rb_hit_detector_cls,
        perf_tracker_cls,
        resolve_tracking_frame,
        _load_model,
        _warmup_model,
        _get_async_key_state,
        _sleep,
    ):
        perf_tracker_cls.return_value = Mock()
        controller = Mock()
        controller.is_aiming.side_effect = [True, True, True]
        gap_target = SelectedTarget(
            target_x=82.0,
            target_y=41.0,
            screen_center_x=80.0,
            screen_center_y=48.0,
            score=321.0,
            selected_box=(60.0, 20.0, 104.0, 86.0),
            slow_zone=(66.0, 30.0, 94.0, 76.0),
            fire_zone=(68.0, 26.0, 92.0, 72.0),
            source="predicted",
        )
        resolve_tracking_frame.side_effect = [
            TrackingFrameResolution(
                selected_target=SelectedTarget(
                    target_x=80.0,
                    target_y=40.0,
                    screen_center_x=80.0,
                    screen_center_y=48.0,
                    score=300.0,
                ),
                auto_fire_active=False,
                best_target_delta=(1.0, -1.0),
                boxes_seen=1,
            ),
            TrackingFrameResolution(
                selected_target=gap_target,
                auto_fire_active=False,
                best_target_delta=(0.5, -0.5),
                boxes_seen=0,
            ),
            TrackingFrameResolution(
                selected_target=SelectedTarget(
                    target_x=84.0,
                    target_y=42.0,
                    screen_center_x=80.0,
                    screen_center_y=48.0,
                    score=330.0,
                ),
                auto_fire_active=False,
                best_target_delta=(1.5, -1.5),
                boxes_seen=1,
            ),
        ]
        first_result = (
            Mock(
                frame_id=7,
                captured_at=20.0,
                inferred_at=20.01,
                frame=np.ones((4, 4, 3), dtype=np.uint8),
                detections=[],
                infer_ms=6.5,
            ),
            7,
        )
        second_result = (
            Mock(
                frame_id=8,
                captured_at=21.0,
                inferred_at=21.01,
                frame=np.full((4, 4, 3), 2, dtype=np.uint8),
                detections=[],
                infer_ms=6.0,
            ),
            8,
        )
        _FakeInferenceThread.scripted_results = [first_result, None, second_result]

        try:
            with patch("vision.runner.ScreenCaptureThread", _FakeCaptureThread), patch(
                "vision.runner.InferenceThread", _FakeInferenceThread, create=True
            ), patch(
                "vision.runner.VisionConfig.from_env",
                return_value=VisionConfig(frame_timeout=0.01, idle_sleep=0.0),
            ):
                process_vision(controller=controller)
        finally:
            _FakeInferenceThread.scripted_results = None

        self.assertEqual(len(resolve_tracking_frame.call_args_list), 3)
        first_call = resolve_tracking_frame.call_args_list[0].kwargs
        second_call = resolve_tracking_frame.call_args_list[1].kwargs
        self.assertIsNotNone(first_call["frame"])
        self.assertIsNotNone(second_call["frame"])
        self.assertIs(second_call["frame"], first_call["frame"])
        self.assertEqual(second_call["detections"], [])
        self.assertEqual(controller.update.call_count, 3)
        self.assertEqual(controller.reset.call_count, 1)


if __name__ == "__main__":
    unittest.main()
