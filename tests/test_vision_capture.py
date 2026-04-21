import ctypes
import time
import unittest
from types import SimpleNamespace
from unittest.mock import patch

import numpy as np

from vision.capture import CapturedFrame, ScreenCaptureThread
from vision.dxgi_capture import BGRARegionProcessor, DXGIRegionCaptureBackend, RegionStageSurface


class _FakeBackend:
    def __init__(self, frames):
        self._frames = list(frames)
        self.closed = False

    def grab(self):
        if self._frames:
            return self._frames.pop(0)
        time.sleep(0.001)
        return None

    def close(self):
        self.closed = True


class _FakeImmediateContext:
    def __init__(self):
        self.copy_calls = []

    def CopySubresourceRegion(
        self,
        destination,
        destination_subresource,
        destination_x,
        destination_y,
        destination_z,
        source,
        source_subresource,
        source_box,
    ):
        self.copy_calls.append(
            {
                "destination": destination,
                "destination_subresource": destination_subresource,
                "destination_x": destination_x,
                "destination_y": destination_y,
                "destination_z": destination_z,
                "source": source,
                "source_subresource": source_subresource,
                "source_box": source_box.contents,
            }
        )


class _FakeDuplicator:
    def __init__(self, *, has_update=True):
        self.texture = object()
        self.updated = has_update
        self.released = False

    def update_frame(self):
        return True

    def release_frame(self):
        self.released = True


class _FakeLostDuplicator:
    def __init__(self):
        self.released = False

    def update_frame(self):
        return False

    def release_frame(self):
        self.released = True

    def release(self):
        self.released = True


class _FakeStageSurface:
    def __init__(self):
        self.texture = object()
        self.map_calls = 0
        self.unmap_calls = 0
        self.released = False

    def map(self):
        self.map_calls += 1
        return object()

    def unmap(self):
        self.unmap_calls += 1

    def release(self):
        self.released = True


class _FakeDxgiSurface:
    def __init__(self):
        self.map_calls = 0
        self.unmap_calls = 0
        self.release_calls = 0

    def Map(self, rect, flags):
        self.map_calls += 1

    def Unmap(self):
        self.unmap_calls += 1

    def Release(self):
        self.release_calls += 1


class _FakeTextureWithSurface:
    def __init__(self, surface):
        self.surface = surface
        self.query_calls = 0
        self.release_calls = 0

    def QueryInterface(self, interface):
        self.query_calls += 1
        return self.surface

    def Release(self):
        self.release_calls += 1


class _FakeProcessor:
    def __init__(self):
        self.calls = []

    def process(self, rect, width, height):
        self.calls.append((rect, width, height))
        return np.zeros((height, width, 3), dtype=np.uint8)


def _mapped_rect_from_rows(*rows: bytes):
    payload = b"".join(rows)
    backing = ctypes.create_string_buffer(payload)
    rect = SimpleNamespace(
        Pitch=len(rows[0]),
        pBits=ctypes.addressof(backing),
    )
    return rect, backing


class ScreenCaptureThreadTests(unittest.TestCase):
    @patch("vision.capture.win32api.GetSystemMetrics", side_effect=[1920, 1080])
    def test_capture_thread_starts_at_target_fps_even_when_idle_fps_is_configured(self, _metrics):
        backend = _FakeBackend([])

        with patch("vision.capture.create_capture_backend", return_value=backend):
            thread = ScreenCaptureThread(
                target_fps=80,
                crop_width=640,
                crop_height=512,
            )

        self.assertEqual(thread._target_fps, 80.0)
        thread.stop()

    def test_region_stage_surface_caches_dxgi_surface_interface(self):
        fake_surface = _FakeDxgiSurface()
        fake_texture = _FakeTextureWithSurface(fake_surface)
        with patch.object(RegionStageSurface, "_create_texture", lambda self: setattr(self, "texture", fake_texture)):
            stage_surface = RegionStageSurface(width=64, height=32, device=SimpleNamespace())

        stage_surface.map()
        stage_surface.unmap()
        stage_surface.release()

        self.assertEqual(fake_texture.query_calls, 1)
        self.assertEqual(fake_surface.map_calls, 1)
        self.assertEqual(fake_surface.unmap_calls, 1)
        self.assertEqual(fake_surface.release_calls, 1)
        self.assertEqual(fake_texture.release_calls, 1)

    @patch("vision.capture.win32api.GetSystemMetrics", side_effect=[1920, 1080])
    def test_get_latest_frame_returns_captured_frame_metadata(self, _metrics):
        backend = _FakeBackend([np.zeros((4, 4, 3), dtype=np.uint8)])

        with patch("vision.capture.create_capture_backend", return_value=backend):
            thread = ScreenCaptureThread(target_fps=80, crop_width=640, crop_height=512)
            thread.start()
            try:
                captured, last_seen_id = thread.get_latest_frame(timeout=0.2)
            finally:
                thread.stop()
                thread.join(timeout=1.0)

        self.assertIsInstance(captured, CapturedFrame)
        self.assertEqual(last_seen_id, 1)
        self.assertEqual(captured.frame_id, 1)
        self.assertGreater(captured.captured_at, 0.0)
        self.assertEqual(captured.frame.shape, (4, 4, 3))
        self.assertTrue(backend.closed)

    @patch("vision.capture.win32api.GetSystemMetrics", side_effect=[1920, 1080])
    def test_get_latest_frame_returns_none_when_no_new_frame_arrives_before_timeout(self, _metrics):
        backend = _FakeBackend([np.zeros((2, 2, 3), dtype=np.uint8)])

        with patch("vision.capture.create_capture_backend", return_value=backend):
            thread = ScreenCaptureThread(target_fps=80, crop_width=640, crop_height=512)
            thread.start()
            try:
                first, last_seen_id = thread.get_latest_frame(timeout=0.2)
                second, next_seen_id = thread.get_latest_frame(last_seen_id=last_seen_id, timeout=0.01)
            finally:
                thread.stop()
                thread.join(timeout=1.0)

        self.assertIsNotNone(first)
        self.assertIsNone(second)
        self.assertEqual(next_seen_id, last_seen_id)

    def test_region_backend_copies_only_requested_roi_into_small_surface(self):
        immediate_context = _FakeImmediateContext()
        device = SimpleNamespace(im_context=immediate_context)
        output = SimpleNamespace(
            desc=SimpleNamespace(
                DesktopCoordinates=SimpleNamespace(left=100, top=40),
            ),
            rotation_angle=0,
            resolution=(1920, 1080),
        )
        duplicator = _FakeDuplicator(has_update=True)
        stage_surface = _FakeStageSurface()
        processor = _FakeProcessor()
        backend = DXGIRegionCaptureBackend(
            region=(420, 200, 1060, 712),
            device=device,
            output=output,
            duplicator=duplicator,
            stage_surface=stage_surface,
            processor=processor,
        )

        frame = backend.grab()

        self.assertEqual(frame.shape, (512, 640, 3))
        self.assertEqual(len(immediate_context.copy_calls), 1)
        copy_call = immediate_context.copy_calls[0]
        self.assertEqual(
            (
                copy_call["source_box"].left,
                copy_call["source_box"].top,
                copy_call["source_box"].right,
                copy_call["source_box"].bottom,
            ),
            (320, 160, 960, 672),
        )
        self.assertEqual(len(processor.calls), 1)
        self.assertEqual(processor.calls[0][1:], (640, 512))
        self.assertTrue(duplicator.released)
        self.assertEqual(stage_surface.map_calls, 1)
        self.assertEqual(stage_surface.unmap_calls, 1)

    def test_region_backend_skips_copy_when_duplicator_has_no_new_frame(self):
        immediate_context = _FakeImmediateContext()
        device = SimpleNamespace(im_context=immediate_context)
        output = SimpleNamespace(
            desc=SimpleNamespace(
                DesktopCoordinates=SimpleNamespace(left=0, top=0),
            ),
            rotation_angle=0,
            resolution=(1920, 1080),
        )
        duplicator = _FakeDuplicator(has_update=False)
        stage_surface = _FakeStageSurface()
        processor = _FakeProcessor()
        backend = DXGIRegionCaptureBackend(
            region=(0, 0, 640, 512),
            device=device,
            output=output,
            duplicator=duplicator,
            stage_surface=stage_surface,
            processor=processor,
        )

        frame = backend.grab()

        self.assertIsNone(frame)
        self.assertEqual(immediate_context.copy_calls, [])
        self.assertEqual(processor.calls, [])
        self.assertFalse(duplicator.released)

    def test_region_backend_retries_after_rebuild_failure_without_raising(self):
        immediate_context = _FakeImmediateContext()
        device = SimpleNamespace(im_context=immediate_context)
        output = SimpleNamespace(
            desc=SimpleNamespace(
                DesktopCoordinates=SimpleNamespace(left=0, top=0),
            ),
            rotation_angle=0,
            resolution=(1920, 1080),
            update_desc=lambda: None,
        )
        stage_surface = _FakeStageSurface()
        processor = _FakeProcessor()
        build_attempts = []

        def duplicator_factory(*, output, device):
            build_attempts.append("build")
            if len(build_attempts) == 1:
                raise PermissionError("duplicate output denied")
            return _FakeDuplicator(has_update=True)

        backend = DXGIRegionCaptureBackend(
            region=(0, 0, 640, 512),
            device=device,
            output=output,
            duplicator=_FakeLostDuplicator(),
            stage_surface=stage_surface,
            processor=processor,
            duplicator_factory=duplicator_factory,
            recovery_delay_seconds=0.0,
        )

        with patch("vision.dxgi_capture.RegionStageSurface", return_value=stage_surface):
            first = backend.grab()
            second = backend.grab()

        self.assertIsNone(first)
        self.assertEqual(len(build_attempts), 2)
        self.assertIsNotNone(second)
        self.assertEqual(second.shape, (512, 640, 3))
        self.assertEqual(len(immediate_context.copy_calls), 1)

    def test_bgra_region_processor_rotates_rgb_output_buffers_without_mutating_prior_frame(self):
        processor = BGRARegionProcessor(output_color="RGB", buffer_slots=2)
        first_rect, first_backing = _mapped_rect_from_rows(
            bytes(
                [
                    0,
                    0,
                    255,
                    10,
                    0,
                    255,
                    0,
                    20,
                    1,
                    2,
                    3,
                    4,
                    5,
                    6,
                    7,
                    8,
                ]
            )
        )
        second_rect, second_backing = _mapped_rect_from_rows(
            bytes(
                [
                    255,
                    0,
                    0,
                    30,
                    0,
                    255,
                    255,
                    40,
                    9,
                    10,
                    11,
                    12,
                    13,
                    14,
                    15,
                    16,
                ]
            )
        )
        third_rect, third_backing = _mapped_rect_from_rows(
            bytes(
                [
                    0,
                    0,
                    0,
                    50,
                    255,
                    255,
                    0,
                    60,
                    21,
                    22,
                    23,
                    24,
                    25,
                    26,
                    27,
                    28,
                ]
            )
        )

        first = processor.process(first_rect, width=2, height=1)
        second = processor.process(second_rect, width=2, height=1)

        self.assertIsNot(first, second)
        np.testing.assert_array_equal(
            first,
            np.array([[[255, 0, 0], [0, 255, 0]]], dtype=np.uint8),
        )
        np.testing.assert_array_equal(
            second,
            np.array([[[0, 0, 255], [255, 255, 0]]], dtype=np.uint8),
        )

        third = processor.process(third_rect, width=2, height=1)

        self.assertIs(third, first)
        np.testing.assert_array_equal(
            third,
            np.array([[[0, 0, 0], [0, 255, 255]]], dtype=np.uint8),
        )
        self.assertIsNotNone(first_backing)
        self.assertIsNotNone(second_backing)
        self.assertIsNotNone(third_backing)

    def test_bgra_region_processor_ignores_pitch_padding_when_converting_to_rgb(self):
        processor = BGRARegionProcessor(output_color="RGB")
        rect, backing = _mapped_rect_from_rows(
            bytes(
                [
                    0,
                    0,
                    255,
                    10,
                    0,
                    255,
                    0,
                    20,
                    99,
                    99,
                    99,
                    99,
                    77,
                    77,
                    77,
                    77,
                ]
            ),
            bytes(
                [
                    255,
                    0,
                    0,
                    30,
                    255,
                    255,
                    255,
                    40,
                    66,
                    66,
                    66,
                    66,
                    55,
                    55,
                    55,
                    55,
                ]
            ),
        )

        frame = processor.process(rect, width=2, height=2)

        np.testing.assert_array_equal(
            frame,
            np.array(
                [
                    [[255, 0, 0], [0, 255, 0]],
                    [[0, 0, 255], [255, 255, 255]],
                ],
                dtype=np.uint8,
            ),
        )
        self.assertIsNotNone(backing)


if __name__ == "__main__":
    unittest.main()
