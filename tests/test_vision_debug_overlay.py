import unittest
from unittest.mock import patch

import numpy as np

from vision.debug_overlay import VisionDebugOverlay
from vision.targeting import ParsedDetections, SelectedTarget, TargetSelector


class VisionDebugOverlayTests(unittest.TestCase):
    def test_render_frame_draws_debug_annotations(self):
        frame = np.full((96, 160, 3), 64, dtype=np.uint8)
        detections = [
            ParsedDetections(
                boxes=np.array([[40, 20, 110, 90]], dtype=np.float32),
                confs=np.array([0.91], dtype=np.float32),
            )
        ]
        selector = TargetSelector(frame_width=160, frame_height=96)
        selected_target = SelectedTarget(
            target_x=75.0,
            target_y=42.0,
            screen_center_x=80.0,
            screen_center_y=48.0,
            score=123.0,
            slow_zone=(54.0, 32.0, 96.0, 82.0),
            fire_zone=(58.0, 26.0, 92.0, 78.0),
        )
        overlay = VisionDebugOverlay(window_name="test")

        rendered = overlay.render_frame(
            frame=frame,
            detections=detections,
            selected_target=selected_target,
            target_selector=selector,
            auto_fire_active=True,
            is_aiming=True,
            best_target_delta=(-5.0, -6.0),
        )

        self.assertEqual(rendered.shape, frame.shape)
        self.assertFalse(np.array_equal(rendered, frame))
        self.assertGreater(int(np.abs(rendered.astype(np.int16) - frame.astype(np.int16)).sum()), 0)

    def test_show_message_resizes_window_to_frame_dimensions(self):
        overlay = VisionDebugOverlay(window_name="test")

        with patch("vision.debug_overlay.cv2.namedWindow") as named_window, patch(
            "vision.debug_overlay.cv2.setWindowProperty"
        ), patch("vision.debug_overlay.cv2.resizeWindow") as resize_window, patch(
            "vision.debug_overlay.cv2.imshow"
        ), patch("vision.debug_overlay.cv2.waitKey", return_value=0):
            overlay.show_message(width=896, height=512, message="Idle")

        named_window.assert_called_once()
        resize_window.assert_called_once_with("test", 896, 512)

    def test_show_can_forward_rendered_frame_to_capture_writer(self):
        frame = np.full((96, 160, 3), 64, dtype=np.uint8)
        detections = [
            ParsedDetections(
                boxes=np.array([[40, 20, 110, 90]], dtype=np.float32),
                confs=np.array([0.91], dtype=np.float32),
            )
        ]
        selector = TargetSelector(frame_width=160, frame_height=96)
        selected_target = SelectedTarget(
            target_x=75.0,
            target_y=42.0,
            screen_center_x=80.0,
            screen_center_y=48.0,
            score=123.0,
            slow_zone=(54.0, 32.0, 96.0, 82.0),
            fire_zone=(58.0, 26.0, 92.0, 78.0),
        )
        writer = unittest.mock.Mock()
        overlay = VisionDebugOverlay(window_name="test", frame_capture=writer)

        with patch.object(overlay, "_present"):
            overlay.show(
                frame=frame,
                detections=detections,
                selected_target=selected_target,
                target_selector=selector,
                auto_fire_active=True,
                is_aiming=True,
                best_target_delta=(-5.0, -6.0),
            )

        writer.save_frame.assert_called_once()

    def test_render_frame_marks_only_exact_selected_box(self):
        frame = np.full((96, 160, 3), 64, dtype=np.uint8)
        detections = [
            ParsedDetections(
                boxes=np.array(
                    [
                        [20, 12, 60, 90],
                        [58, 22, 102, 84],
                    ],
                    dtype=np.float32,
                ),
                confs=np.array([0.61, 0.93], dtype=np.float32),
            )
        ]
        selector = TargetSelector(frame_width=160, frame_height=96)
        selected_target = SelectedTarget(
            target_x=80.0,
            target_y=40.0,
            screen_center_x=80.0,
            screen_center_y=48.0,
            score=456.0,
            slow_zone=(66.0, 30.0, 94.0, 76.0),
            fire_zone=(68.0, 26.0, 92.0, 72.0),
            selected_box=(58.0, 22.0, 102.0, 84.0),
        )
        overlay = VisionDebugOverlay(window_name="test")
        labels = []

        def _capture_text(*args, **kwargs):
            labels.append(args[1])
            return None

        with patch("vision.debug_overlay.cv2.putText", side_effect=_capture_text), patch(
            "vision.debug_overlay.cv2.rectangle"
        ):
            overlay.render_frame(
                frame=frame,
                detections=detections,
                selected_target=selected_target,
                target_selector=selector,
                auto_fire_active=False,
                is_aiming=True,
            )

        self.assertIn("raw neutral 0.61", labels)
        self.assertIn("selected neutral 0.93", labels)
        self.assertNotIn("selected neutral 0.61", labels)

    def test_render_frame_reuses_selector_color_classification_cache(self):
        frame = np.full((96, 160, 3), 64, dtype=np.uint8)
        detections = [
            ParsedDetections(
                boxes=np.array(
                    [
                        [20, 12, 60, 90],
                        [58, 22, 102, 84],
                    ],
                    dtype=np.float32,
                ),
                confs=np.array([0.61, 0.93], dtype=np.float32),
            )
        ]
        selector = TargetSelector(frame_width=160, frame_height=96)
        selector.select_target(detections, frame)
        overlay = VisionDebugOverlay(window_name="test")
        classify_calls = 0
        original = selector._classify_color

        def _counted_classify(box, source_frame):
            nonlocal classify_calls
            classify_calls += 1
            return original(box, source_frame)

        selector._classify_color = _counted_classify
        overlay.render_frame(
            frame=frame,
            detections=detections,
            selected_target=None,
            target_selector=selector,
            auto_fire_active=False,
            is_aiming=True,
        )

        self.assertEqual(classify_calls, 0)


if __name__ == "__main__":
    unittest.main()
