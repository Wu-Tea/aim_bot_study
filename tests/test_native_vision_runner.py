import unittest
from unittest.mock import Mock, patch

import numpy as np

from controllers.base_controller import ControllerTarget
from vision.runner import VisionConfig
from vision.native_runner import (
    NativeVisionDebugOverlay,
    _controller_target_from_native_result,
    _quit_requested,
    process_native_vision,
)


class NativeVisionRunnerMappingTests(unittest.TestCase):
    def test_controller_target_maps_native_body_box_metadata(self):
        target = _controller_target_from_native_result(
            {
                "target_x": 331.5,
                "target_y": 201.25,
                "screen_center_x": 320.0,
                "screen_center_y": 256.0,
                "has_body_box": True,
                "body_x1": 280.0,
                "body_y1": 120.0,
                "body_x2": 360.0,
                "body_y2": 320.0,
            }
        )

        self.assertEqual(
            target,
            ControllerTarget(
                aim_point_x=331.5,
                aim_point_y=201.25,
                screen_center_x=320.0,
                screen_center_y=256.0,
                body_box=(280.0, 120.0, 360.0, 320.0),
            ),
        )

    def test_controller_target_omits_body_box_when_native_result_has_no_box(self):
        target = _controller_target_from_native_result(
            {
                "target_x": 331.5,
                "target_y": 201.25,
                "screen_center_x": 320.0,
                "screen_center_y": 256.0,
                "has_body_box": False,
            }
        )

        self.assertIsNotNone(target)
        self.assertIsNone(target.body_box)

    def test_controller_target_maps_native_target_source(self):
        target = _controller_target_from_native_result(
            {
                "target_x": 331.5,
                "target_y": 201.25,
                "screen_center_x": 320.0,
                "screen_center_y": 256.0,
                "has_body_box": False,
                "target_source": "reconstructed",
            }
        )

        self.assertIsNotNone(target)
        self.assertEqual(target.target_source, "reconstructed")

    @patch("vision.native_runner.win32api.GetAsyncKeyState")
    def test_quit_check_is_disabled_when_quit_key_is_zero(self, get_async_key_state):
        config = VisionConfig(quit_key_vk=0)

        self.assertFalse(_quit_requested(config))
        get_async_key_state.assert_not_called()


class NativeVisionDebugOverlayTests(unittest.TestCase):
    def test_render_result_can_draw_on_real_roi_frame(self):
        overlay = NativeVisionDebugOverlay(width=640, height=512, display_window=False)
        frame = np.full((512, 640, 3), (35, 70, 120), dtype=np.uint8)

        canvas = overlay.render_result(
            {
                "has_target": False,
                "auto_fire": False,
                "target_x": 320.0,
                "target_y": 256.0,
                "screen_center_x": 320.0,
                "screen_center_y": 256.0,
                "wait_ms": 0.0,
                "preprocess_ms": 0.0,
                "infer_ms": 0.0,
                "post_ms": 0.0,
                "age_ms": 0.0,
                "boxes_seen": 0,
            },
            is_aiming=True,
            auto_fire_active=False,
            frame_bgr=frame,
        )

        self.assertEqual(canvas.shape, frame.shape)
        self.assertEqual(tuple(int(v) for v in canvas[420, 560]), (35, 70, 120))

    def test_render_result_draws_synthetic_native_status_canvas(self):
        overlay = NativeVisionDebugOverlay(width=640, height=512, display_window=False)

        canvas = overlay.render_result(
            {
                "has_target": True,
                "auto_fire": True,
                "dx": 12.0,
                "dy": -6.0,
                "target_x": 332.0,
                "target_y": 250.0,
                "screen_center_x": 320.0,
                "screen_center_y": 256.0,
                "has_body_box": True,
                "body_x1": 288.0,
                "body_y1": 128.0,
                "body_x2": 368.0,
                "body_y2": 330.0,
                "target_source": "observed",
                "wait_ms": 1.2,
                "preprocess_ms": 0.2,
                "infer_ms": 3.8,
                "post_ms": 0.4,
                "age_ms": 5.4,
                "boxes_seen": 1,
            },
            is_aiming=True,
            auto_fire_active=True,
        )

        self.assertEqual(canvas.shape, (512, 640, 3))
        self.assertGreater(int(canvas.sum()), 0)

    def test_render_result_draws_torso_band_overlay_when_metadata_is_present(self):
        overlay = NativeVisionDebugOverlay(width=640, height=512, display_window=False)

        canvas = overlay.render_result(
            {
                "has_target": True,
                "auto_fire": False,
                "dx": 6.0,
                "dy": -4.0,
                "target_x": 334.0,
                "target_y": 220.0,
                "screen_center_x": 320.0,
                "screen_center_y": 256.0,
                "has_body_box": True,
                "body_x1": 288.0,
                "body_y1": 128.0,
                "body_x2": 368.0,
                "body_y2": 330.0,
                "target_source": "torso_anchor",
                "torso_x1": 306.0,
                "torso_y1": 168.0,
                "torso_x2": 350.0,
                "torso_y2": 290.0,
                "anchor_confidence": 0.82,
                "ego_confidence": 0.74,
                "body_state_mode": "strong",
                "anchor_source": "torso_anchor",
                "ego_model": "affine",
                "wait_ms": 1.2,
                "preprocess_ms": 0.2,
                "infer_ms": 3.8,
                "post_ms": 0.9,
                "age_ms": 5.9,
                "boxes_seen": 1,
            },
            is_aiming=True,
            auto_fire_active=False,
        )

        self.assertEqual(tuple(int(v) for v in canvas[168, 320]), (255, 120, 0))

    def test_render_result_draws_search_roi_and_tracking_points(self):
        overlay = NativeVisionDebugOverlay(width=640, height=512, display_window=False)
        frame = np.full((512, 640, 3), 30, dtype=np.uint8)

        canvas = overlay.render_result(
            {
                "has_target": True,
                "auto_fire": False,
                "dx": 6.0,
                "dy": -4.0,
                "target_x": 334.0,
                "target_y": 220.0,
                "screen_center_x": 320.0,
                "screen_center_y": 256.0,
                "has_body_box": True,
                "body_x1": 288.0,
                "body_y1": 128.0,
                "body_x2": 368.0,
                "body_y2": 330.0,
                "target_source": "torso_anchor",
                "torso_x1": 306.0,
                "torso_y1": 168.0,
                "torso_x2": 350.0,
                "torso_y2": 290.0,
                "debug_search_x1": 300.0,
                "debug_search_y1": 170.0,
                "debug_search_x2": 360.0,
                "debug_search_y2": 280.0,
                "debug_predicted_x": 326.0,
                "debug_predicted_y": 226.0,
                "debug_patch_x": 332.0,
                "debug_patch_y": 222.0,
                "debug_patch_valid": True,
                "debug_track_points": [320.0, 210.0, 328.0, 224.0],
                "debug_template_w": 13.0,
                "debug_template_h": 13.0,
                "anchor_confidence": 0.82,
                "ego_confidence": 0.74,
                "body_state_mode": "strong",
                "anchor_source": "torso_anchor",
                "ego_model": "affine",
                "wait_ms": 1.2,
                "preprocess_ms": 0.2,
                "infer_ms": 3.8,
                "post_ms": 0.9,
                "age_ms": 5.9,
                "boxes_seen": 1,
            },
            is_aiming=True,
            auto_fire_active=False,
            frame_bgr=frame,
        )

        self.assertEqual(tuple(int(v) for v in canvas[170, 330]), (255, 0, 255))
        self.assertEqual(tuple(int(v) for v in canvas[210, 320]), (0, 255, 0))

    def test_render_result_draws_center_cue_window_and_refined_anchor(self):
        overlay = NativeVisionDebugOverlay(width=640, height=512, display_window=False)
        frame = np.full((512, 640, 3), 30, dtype=np.uint8)

        canvas = overlay.render_result(
            {
                "has_target": True,
                "auto_fire": False,
                "dx": 4.0,
                "dy": -8.0,
                "target_x": 322.0,
                "target_y": 236.0,
                "screen_center_x": 320.0,
                "screen_center_y": 256.0,
                "has_body_box": True,
                "body_x1": 288.0,
                "body_y1": 128.0,
                "body_x2": 368.0,
                "body_y2": 330.0,
                "target_source": "torso_anchor",
                "torso_x1": 306.0,
                "torso_y1": 168.0,
                "torso_x2": 350.0,
                "torso_y2": 290.0,
                "body_state_mode": "strong",
                "yellow_cue_present": True,
                "yellow_cue_score": 0.86,
                "yellow_cue_x": 324.0,
                "yellow_cue_y": 220.0,
                "yellow_roi_x1": 195.0,
                "yellow_roi_y1": 131.0,
                "yellow_roi_x2": 445.0,
                "yellow_roi_y2": 381.0,
                "refiner_applied": True,
                "refined_target_x": 323.0,
                "refined_target_y": 228.0,
                "wait_ms": 1.2,
                "preprocess_ms": 0.2,
                "infer_ms": 3.8,
                "post_ms": 0.9,
                "age_ms": 5.9,
                "boxes_seen": 1,
            },
            is_aiming=True,
            auto_fire_active=False,
            frame_bgr=frame,
        )

        self.assertEqual(tuple(int(v) for v in canvas[131, 320]), (0, 255, 255))
        self.assertEqual(tuple(int(v) for v in canvas[220, 324]), (0, 255, 255))


class NativeVisionProcessTests(unittest.TestCase):
    @patch("vision.native_runner.win32api.GetAsyncKeyState", side_effect=[0x8000])
    @patch("vision.native_runner.VisionConfig.from_env")
    @patch("vision.native_runner._load_native_module")
    @patch("vision.native_runner.PerformanceTracker")
    def test_process_native_vision_polls_engine_and_updates_controller(
        self,
        perf_tracker_cls,
        load_native_module,
        config_from_env,
        _get_async_key_state,
    ):
        config_from_env.return_value = VisionConfig(
            capture_fps=240,
            track_fps=120.0,
            warm_scan_fps=20.0,
            scan_fps=100.0,
            recovery_scan_fps=125.0,
        )
        engine = Mock()
        engine.poll_once.return_value = {
            "has_target": True,
            "auto_fire": True,
            "dx": 3.0,
            "dy": -2.0,
            "target_x": 323.0,
            "target_y": 254.0,
            "screen_center_x": 320.0,
            "screen_center_y": 256.0,
            "has_body_box": True,
            "body_x1": 300.0,
            "body_y1": 120.0,
            "body_x2": 360.0,
            "body_y2": 320.0,
            "target_source": "observed",
            "wait_ms": 1.0,
            "capture_ms": 1.0,
            "acquire_ms": 0.6,
            "copy_ms": 0.4,
            "preprocess_ms": 0.2,
            "infer_ms": 3.0,
            "decode_ms": 0.1,
            "post_ms": 0.4,
            "age_ms": 4.0,
            "boxes_seen": 1,
        }
        native_module = Mock()
        native_module.NativeVisionEngine.return_value = engine
        load_native_module.return_value = native_module
        perf_tracker = Mock()
        perf_tracker_cls.return_value = perf_tracker
        controller = Mock()
        controller.is_aiming.return_value = True

        process_native_vision(controller=controller)

        native_module.NativeVisionEngine.assert_called_once()
        args = native_module.NativeVisionEngine.call_args.args
        self.assertEqual(args[:5], (640, 512, 0, -1, 4))
        self.assertAlmostEqual(args[5], 1000.0 / 120.0)
        self.assertAlmostEqual(args[6], 50.0)
        self.assertAlmostEqual(args[7], 10.0)
        self.assertAlmostEqual(args[8], 8.0)
        controller.update.assert_called_once()
        dx, dy = controller.update.call_args.args
        self.assertEqual((dx, dy), (3.0, -2.0))
        self.assertIsInstance(controller.update.call_args.kwargs["target"], ControllerTarget)
        controller.set_auto_fire.assert_called()
        perf_tracker.update.assert_called_once()
        self.assertEqual(
            perf_tracker.update.call_args.kwargs["stage_ms"],
            {
                "capture": 1.0,
                "acquire": 0.6,
                "copy": 0.4,
                "pre": 0.2,
                "infer": 3.0,
                "decode": 0.1,
                "post": 0.3,
            },
        )
        self.assertNotIn("wait_ms", perf_tracker.update.call_args.kwargs)
        self.assertNotIn("roi_ms", perf_tracker.update.call_args.kwargs)
        engine.set_mode.assert_any_call("active_track")

    @patch("vision.native_runner.win32api.GetAsyncKeyState", side_effect=[0x8000])
    @patch("vision.native_runner._load_native_module")
    @patch("vision.native_runner.PerformanceTracker")
    def test_process_native_vision_clears_target_instead_of_resetting_for_no_target_frame(
        self,
        perf_tracker_cls,
        load_native_module,
        _get_async_key_state,
    ):
        engine = Mock()
        engine.poll_once.return_value = {
            "has_target": False,
            "auto_fire": False,
            "dx": 0.0,
            "dy": 0.0,
            "wait_ms": 1.0,
            "infer_ms": 3.0,
            "post_ms": 0.4,
            "age_ms": 4.0,
            "boxes_seen": 0,
        }
        native_module = Mock()
        native_module.NativeVisionEngine.return_value = engine
        load_native_module.return_value = native_module
        perf_tracker_cls.return_value = Mock()
        controller = Mock()
        controller.is_aiming.return_value = True

        process_native_vision(controller=controller)

        controller.clear_target.assert_called_once_with()
        self.assertEqual(controller.reset.call_count, 1)

    @patch("vision.native_runner.time.sleep")
    @patch("vision.native_runner.win32api.GetAsyncKeyState", side_effect=[0x8000])
    @patch("vision.native_runner._load_native_module")
    @patch("vision.native_runner.PerformanceTracker")
    def test_process_native_vision_runs_warm_scan_without_controller_output_when_not_aiming(
        self,
        perf_tracker_cls,
        load_native_module,
        _get_async_key_state,
        _sleep,
    ):
        engine = Mock()
        engine.poll_once.return_value = {
            "has_target": False,
            "auto_fire": False,
            "engine_mode": "warm_scan",
            "scan_ran": True,
            "scan_age_ms": 0.0,
            "scan_reason": "interval",
            "keyframe_age_ms": 0.0,
            "prewarm_used": False,
            "wait_ms": 1.0,
            "infer_ms": 3.0,
            "post_ms": 0.4,
            "age_ms": 4.0,
            "boxes_seen": 2,
        }
        native_module = Mock()
        native_module.NativeVisionEngine.return_value = engine
        load_native_module.return_value = native_module
        perf_tracker_cls.return_value = Mock()
        controller = Mock()
        controller.is_aiming.return_value = False

        process_native_vision(controller=controller)

        engine.set_mode.assert_any_call("warm_scan")
        engine.poll_once.assert_called_once()
        controller.update.assert_not_called()
        controller.clear_target.assert_not_called()
        controller.set_auto_fire.assert_called_with(False)


if __name__ == "__main__":
    unittest.main()
