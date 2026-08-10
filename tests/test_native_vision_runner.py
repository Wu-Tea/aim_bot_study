import unittest
from unittest.mock import Mock, call as mock_call, patch

from controllers.base_controller import ControllerTarget, ControllerVisionState
from vision.runner import VisionConfig
from vision.native_runner import (
    NativeVisionDebugOverlay,
    _controller_target_from_native_result,
    _resolve_cue_provider,
    _quit_requested,
    process_native_vision,
)


class NativeVisionRunnerMappingTests(unittest.TestCase):
    class _CueController:
        def __init__(self):
            self.calls = 0

        def get_external_cue(self):
            self.calls += 1
            return {"found": True, "x": 323.0, "y": 171.0, "score": 0.81}

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
                aim_authority=False,
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

    def test_controller_target_preserves_native_target_source(self):
        target = _controller_target_from_native_result(
            {
                "target_x": 320.0,
                "target_y": 256.0,
                "screen_center_x": 320.0,
                "screen_center_y": 256.0,
                "has_body_box": False,
                "target_source": "cue_hold",
            }
        )

        self.assertIsNotNone(target)
        self.assertEqual(target.target_source, "cue_hold")
        self.assertEqual(target.target_tier, "cue_hold")
        self.assertTrue(target.aim_authority)
        self.assertFalse(target.fire_authority)

    def test_controller_target_estimates_observed_at_from_native_age(self):
        target = _controller_target_from_native_result(
            {
                "target_x": 331.5,
                "target_y": 201.25,
                "screen_center_x": 320.0,
                "screen_center_y": 256.0,
                "has_body_box": False,
                "age_ms": 36.0,
            },
            received_at=10.0,
        )

        self.assertIsNotNone(target)
        self.assertAlmostEqual(target.observed_at, 9.964, places=6)

    @patch("vision.native_runner.win32api.GetAsyncKeyState")
    def test_quit_check_is_disabled_when_quit_key_is_zero(self, get_async_key_state):
        config = VisionConfig(quit_key_vk=0)

        self.assertFalse(_quit_requested(config))
        get_async_key_state.assert_not_called()

    def test_resolve_cue_provider_prefers_explicit_provider(self):
        explicit_provider = Mock()
        controller = self._CueController()

        self.assertIs(_resolve_cue_provider(controller, explicit_provider), explicit_provider)

    def test_resolve_cue_provider_uses_controller_external_cue_hook(self):
        controller = self._CueController()

        provider = _resolve_cue_provider(controller, None)

        self.assertTrue(callable(provider))
        self.assertEqual(provider(), {"found": True, "x": 323.0, "y": 171.0, "score": 0.81})


class NativeVisionDebugOverlayTests(unittest.TestCase):
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
                "has_external_cue": True,
                "external_cue_x": 330.0,
                "external_cue_y": 152.0,
                "external_cue_score": 0.84,
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

    def test_render_result_writes_tier_and_authority_debug_text(self):
        overlay = NativeVisionDebugOverlay(width=640, height=512, display_window=False)

        with patch("vision.native_runner.cv2.putText") as put_text:
            overlay.render_result(
                {
                    "has_target": True,
                    "auto_fire": False,
                    "dx": 12.0,
                    "dy": -6.0,
                    "target_x": 332.0,
                    "target_y": 250.0,
                    "screen_center_x": 320.0,
                    "screen_center_y": 256.0,
                    "has_body_box": False,
                    "target_source": "associated_weak",
                    "target_tier": "associated_weak",
                    "aim_authority": True,
                    "fire_authority": False,
                    "target_confidence": 0.31,
                    "boxes_seen": 1,
                },
                is_aiming=True,
                auto_fire_active=False,
            )

        texts = [call.args[1] for call in put_text.call_args_list]
        self.assertTrue(
            any("tier=associated_weak src=associated_weak" in text for text in texts),
            texts,
        )
        self.assertTrue(any("conf=0.31 aim=Y fire=N" in text for text in texts), texts)


class NativeVisionProcessTests(unittest.TestCase):
    @patch("vision.native_runner.win32api.GetAsyncKeyState", side_effect=[0x8000])
    @patch("vision.native_runner._load_native_module")
    @patch("vision.native_runner.PerformanceTracker")
    def test_process_native_vision_polls_engine_and_updates_controller(
        self,
        perf_tracker_cls,
        load_native_module,
        _get_async_key_state,
    ):
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
            "preprocess_ms": 0.2,
            "infer_ms": 3.0,
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
        engine.set_aiming.assert_any_call(True)
        controller.update.assert_called_once()
        dx, dy = controller.update.call_args.args
        self.assertEqual((dx, dy), (3.0, -2.0))
        target = controller.update.call_args.kwargs["target"]
        self.assertIsInstance(target, ControllerTarget)
        self.assertIsNotNone(target.observed_at)
        controller.set_auto_fire.assert_called()
        perf_tracker.update.assert_called_once()

    @patch("vision.native_runner.win32api.GetAsyncKeyState", side_effect=[0x8000])
    @patch("vision.native_runner._load_native_module")
    @patch("vision.native_runner.PerformanceTracker")
    @patch("vision.native_runner.AdsAutoFireGate")
    def test_process_native_vision_submits_single_vision_state_when_supported(
        self,
        auto_fire_gate_cls,
        perf_tracker_cls,
        load_native_module,
        _get_async_key_state,
    ):
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
            "body_x1": 280.0,
            "body_y1": 120.0,
            "body_x2": 360.0,
            "body_y2": 320.0,
            "target_source": "observed",
            "target_tier": "observed_strong",
            "aim_authority": True,
            "fire_authority": True,
            "association_stage": "observed",
            "target_confidence": 0.73,
            "has_external_cue": True,
            "wait_ms": 1.0,
            "capture_acquire_ms": 0.7,
            "capture_copy_ms": 0.3,
            "preprocess_ms": 0.2,
            "cuda_map_ms": 0.4,
            "infer_ms": 3.0,
            "output_copy_sync_ms": 0.8,
            "gpu_total_ms": 3.4,
            "output_copy_ms": 0.2,
            "output_wait_ms": 0.6,
            "decode_ms": 0.1,
            "selector_ms": 0.5,
            "cuda_unmap_ms": 0.1,
            "post_ms": 0.4,
            "age_ms": 4.0,
            "boxes_seen": 1,
        }
        native_module = Mock()
        native_module.NativeVisionEngine.return_value = engine
        load_native_module.return_value = native_module
        perf_tracker_cls.return_value = Mock()
        auto_fire_gate = Mock()
        auto_fire_gate.allow_auto_fire.return_value = True
        auto_fire_gate_cls.return_value = auto_fire_gate

        class VisionStateController:
            def __init__(self):
                self.states = []
                self.legacy_calls = []

            def is_aiming(self):
                return True

            def update_vision_state(self, state):
                self.states.append(state)

            def set_auto_fire(self, pressed):
                self.legacy_calls.append(("set_auto_fire", pressed))

            def update(self, *args, **kwargs):
                self.legacy_calls.append(("update", args, kwargs))

            def reset(self):
                self.legacy_calls.append(("reset",))

        controller = VisionStateController()

        process_native_vision(controller=controller)

        self.assertNotIn(("set_auto_fire", True), controller.legacy_calls)
        self.assertFalse(
            any(call[0] == "update" for call in controller.legacy_calls),
            controller.legacy_calls,
        )
        self.assertEqual(len(controller.states), 1)
        state = controller.states[0]
        self.assertIsInstance(state, ControllerVisionState)
        self.assertEqual((state.dx, state.dy), (3.0, -2.0))
        self.assertTrue(state.auto_fire_requested)
        self.assertIsNotNone(state.received_at)
        self.assertIsNotNone(state.submitted_at)
        self.assertGreaterEqual(state.submitted_at, state.received_at)
        self.assertIsNotNone(state.target)
        self.assertEqual(state.target.target_source, "observed")
        auto_fire_gate.allow_auto_fire.assert_called_once()

        perf_kwargs = perf_tracker_cls.return_value.update.call_args.kwargs
        self.assertIn("source_age_ms", perf_kwargs)
        self.assertIn("native_pipeline_ms", perf_kwargs)
        self.assertIn("python_handoff_ms", perf_kwargs)
        self.assertEqual(perf_kwargs["native_pipeline_ms"], 4.0)
        self.assertEqual(perf_kwargs["target_source"], "observed")
        self.assertEqual(perf_kwargs["target_tier"], "observed_strong")
        self.assertTrue(perf_kwargs["aim_authority"])
        self.assertTrue(perf_kwargs["fire_authority"])
        self.assertTrue(perf_kwargs["has_external_cue"])
        self.assertTrue(perf_kwargs["native_auto_fire_requested"])
        self.assertTrue(perf_kwargs["auto_fire_active"])
        self.assertEqual(perf_kwargs["native_capture_acquire_ms"], 0.7)
        self.assertEqual(perf_kwargs["native_capture_copy_ms"], 0.3)
        self.assertEqual(perf_kwargs["native_cuda_map_ms"], 0.4)
        self.assertEqual(perf_kwargs["native_output_copy_sync_ms"], 0.8)
        self.assertEqual(perf_kwargs["native_gpu_total_ms"], 3.4)
        self.assertEqual(perf_kwargs["native_output_copy_ms"], 0.2)
        self.assertEqual(perf_kwargs["native_output_wait_ms"], 0.6)
        self.assertEqual(perf_kwargs["native_decode_ms"], 0.1)
        self.assertEqual(perf_kwargs["native_selector_ms"], 0.5)
        self.assertEqual(perf_kwargs["native_cuda_unmap_ms"], 0.1)
        self.assertEqual(perf_kwargs["target_box_width_px"], 80.0)
        self.assertEqual(perf_kwargs["target_box_height_px"], 200.0)
        self.assertEqual(perf_kwargs["target_edge_margin_px"], 120.0)
        self.assertEqual(perf_kwargs["capture_width_px"], 640.0)
        self.assertEqual(perf_kwargs["capture_height_px"], 512.0)
        self.assertIsNotNone(perf_kwargs["external_cue_ms"])
        self.assertGreaterEqual(perf_kwargs["external_cue_ms"], 0.0)

    @patch("vision.native_runner.win32api.GetAsyncKeyState", side_effect=[0x8000])
    @patch("vision.native_runner._load_native_module")
    @patch("vision.native_runner.PerformanceTracker")
    @patch("vision.native_runner.AdsAutoFireGate")
    def test_process_native_vision_blocks_cue_hold_auto_fire_even_if_native_requests_it(
        self,
        auto_fire_gate_cls,
        perf_tracker_cls,
        load_native_module,
        _get_async_key_state,
    ):
        engine = Mock()
        engine.poll_once.return_value = {
            "has_target": True,
            "auto_fire": True,
            "dx": 5.0,
            "dy": 1.0,
            "target_x": 325.0,
            "target_y": 257.0,
            "screen_center_x": 320.0,
            "screen_center_y": 256.0,
            "has_body_box": True,
            "body_x1": 290.0,
            "body_y1": 120.0,
            "body_x2": 360.0,
            "body_y2": 320.0,
            "target_source": "cue_hold",
            "wait_ms": 1.0,
            "preprocess_ms": 0.2,
            "infer_ms": 3.0,
            "post_ms": 0.4,
            "age_ms": 4.0,
            "boxes_seen": 1,
        }
        native_module = Mock()
        native_module.NativeVisionEngine.return_value = engine
        load_native_module.return_value = native_module
        perf_tracker_cls.return_value = Mock()
        auto_fire_gate = Mock()
        auto_fire_gate.allow_auto_fire.return_value = True
        auto_fire_gate_cls.return_value = auto_fire_gate

        class VisionStateController:
            def __init__(self):
                self.states = []

            def is_aiming(self):
                return True

            def update_vision_state(self, state):
                self.states.append(state)

            def set_auto_fire(self, _pressed):
                return None

            def reset(self):
                return None

        controller = VisionStateController()

        process_native_vision(controller=controller)

        self.assertEqual(len(controller.states), 1)
        state = controller.states[0]
        self.assertFalse(state.auto_fire_requested)
        self.assertFalse(state.fire_authority)
        self.assertIsNotNone(state.target)
        self.assertEqual(state.target.target_source, "cue_hold")
        self.assertEqual(state.target.target_tier, "cue_hold")
        self.assertFalse(state.target.fire_authority)
        auto_fire_gate.allow_auto_fire.assert_called_once()

    @patch("vision.native_runner.time.sleep")
    @patch("vision.native_runner.win32api.GetAsyncKeyState", side_effect=[0, 0x8000])
    @patch("vision.native_runner._load_native_module")
    @patch("vision.native_runner.PerformanceTracker")
    def test_process_native_vision_clears_target_on_active_no_target_frame_and_resets_on_release_and_shutdown(
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
            "dx": 0.0,
            "dy": 0.0,
            "target_x": 320.0,
            "target_y": 256.0,
            "screen_center_x": 320.0,
            "screen_center_y": 256.0,
            "has_body_box": False,
            "wait_ms": 1.0,
            "preprocess_ms": 0.2,
            "color_copy_ms": 0.1,
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
        controller.is_aiming.side_effect = [True, False]

        process_native_vision(controller=controller)

        controller.clear_target.assert_called_once_with()
        self.assertEqual(controller.reset.call_count, 2)
        target_lifecycle_calls = [
            observed_call
            for observed_call in controller.mock_calls
            if observed_call in (mock_call.clear_target(), mock_call.reset())
        ]
        self.assertEqual(
            target_lifecycle_calls,
            [
                mock_call.clear_target(),
                mock_call.reset(),
                mock_call.reset(),
            ],
        )

    @patch("vision.native_runner.win32api.GetAsyncKeyState", side_effect=[0x8000])
    @patch("vision.native_runner._load_native_module")
    @patch("vision.native_runner.PerformanceTracker")
    def test_process_native_vision_falls_back_to_reset_for_legacy_reset_only_controller(
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
            "target_x": 320.0,
            "target_y": 256.0,
            "screen_center_x": 320.0,
            "screen_center_y": 256.0,
            "has_body_box": False,
            "wait_ms": 1.0,
            "preprocess_ms": 0.2,
            "color_copy_ms": 0.1,
            "infer_ms": 3.0,
            "post_ms": 0.4,
            "age_ms": 4.0,
            "boxes_seen": 0,
        }
        native_module = Mock()
        native_module.NativeVisionEngine.return_value = engine
        load_native_module.return_value = native_module
        perf_tracker_cls.return_value = Mock()

        class ResetOnlyController:
            def __init__(self):
                self.reset_calls = 0

            def is_aiming(self):
                return True

            def set_auto_fire(self, _pressed):
                return None

            def update(self, *_args, **_kwargs):
                raise AssertionError("No-target frames should not update the controller")

            def reset(self):
                self.reset_calls += 1

        controller = ResetOnlyController()

        process_native_vision(controller=controller)

        self.assertEqual(controller.reset_calls, 2)

    @patch("vision.native_runner.win32api.GetAsyncKeyState", side_effect=[0x8000])
    @patch("vision.native_runner._load_native_module")
    @patch("vision.native_runner.PerformanceTracker")
    def test_process_native_vision_forwards_external_cue_provider_to_engine(
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
            "target_x": 320.0,
            "target_y": 256.0,
            "screen_center_x": 320.0,
            "screen_center_y": 256.0,
            "has_body_box": False,
            "wait_ms": 1.0,
            "preprocess_ms": 0.2,
            "color_copy_ms": 0.1,
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

        process_native_vision(
            controller=controller,
            cue_provider=lambda: {"found": True, "x": 321.0, "y": 170.0, "score": 0.75},
        )

        engine.set_external_cue.assert_any_call(True, 321.0, 170.0, 0.75)
        perf_kwargs = perf_tracker_cls.return_value.update.call_args.kwargs
        self.assertIsNotNone(perf_kwargs["external_cue_ms"])
        self.assertGreaterEqual(perf_kwargs["external_cue_ms"], 0.0)

    @patch("vision.native_runner.win32api.GetAsyncKeyState", side_effect=[0x8000])
    @patch("vision.native_runner._create_default_cue_provider")
    @patch("vision.native_runner._load_native_module")
    @patch("vision.native_runner.PerformanceTracker")
    def test_process_native_vision_uses_default_sidecar_cue_provider_when_no_other_provider(
        self,
        perf_tracker_cls,
        load_native_module,
        create_default_cue_provider,
        _get_async_key_state,
    ):
        engine = Mock()
        engine.poll_once.return_value = {
            "has_target": False,
            "auto_fire": False,
            "dx": 0.0,
            "dy": 0.0,
            "target_x": 320.0,
            "target_y": 256.0,
            "screen_center_x": 320.0,
            "screen_center_y": 256.0,
            "has_body_box": False,
            "wait_ms": 1.0,
            "preprocess_ms": 0.2,
            "color_copy_ms": 0.1,
            "infer_ms": 3.0,
            "post_ms": 0.4,
            "age_ms": 4.0,
            "boxes_seen": 0,
        }
        native_module = Mock()
        native_module.NativeVisionEngine.return_value = engine
        load_native_module.return_value = native_module
        perf_tracker_cls.return_value = Mock()

        class SidecarCueProvider:
            def __init__(self):
                self.calls = 0
                self.closed = False

            def __call__(self):
                self.calls += 1
                return {"found": True, "x": 318.0, "y": 166.0, "score": 0.73}

            def close(self):
                self.closed = True

        sidecar_provider = SidecarCueProvider()
        create_default_cue_provider.return_value = sidecar_provider

        controller = Mock()
        controller.is_aiming.return_value = True

        process_native_vision(controller=controller)

        create_default_cue_provider.assert_called_once()
        self.assertEqual(sidecar_provider.calls, 1)
        self.assertTrue(sidecar_provider.closed)
        engine.set_external_cue.assert_any_call(True, 318.0, 166.0, 0.73)

    @patch("vision.native_runner.win32api.GetAsyncKeyState", side_effect=[0x8000])
    @patch("vision.native_runner._load_native_module")
    @patch("vision.native_runner.PerformanceTracker")
    def test_process_native_vision_uses_controller_external_cue_hook_when_provider_missing(
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
            "target_x": 320.0,
            "target_y": 256.0,
            "screen_center_x": 320.0,
            "screen_center_y": 256.0,
            "has_body_box": False,
            "has_external_cue": True,
            "external_cue_x": 323.0,
            "external_cue_y": 171.0,
            "external_cue_score": 0.81,
            "wait_ms": 1.0,
            "preprocess_ms": 0.2,
            "color_copy_ms": 0.1,
            "infer_ms": 3.0,
            "post_ms": 0.4,
            "age_ms": 4.0,
            "boxes_seen": 0,
        }
        native_module = Mock()
        native_module.NativeVisionEngine.return_value = engine
        load_native_module.return_value = native_module
        perf_tracker_cls.return_value = Mock()
        class CueController:
            def __init__(self):
                self.calls = 0

            def is_aiming(self):
                return True

            def set_auto_fire(self, _pressed):
                return None

            def reset(self):
                return None

            def clear_target(self):
                return None

            def get_external_cue(self):
                self.calls += 1
                return {"found": True, "x": 323.0, "y": 171.0, "score": 0.81}

        controller = CueController()

        process_native_vision(controller=controller)

        self.assertEqual(controller.calls, 1)
        engine.set_external_cue.assert_any_call(True, 323.0, 171.0, 0.81)


if __name__ == "__main__":
    unittest.main()
