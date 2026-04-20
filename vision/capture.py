import threading
import time

import numpy as np
import win32api

from .dxgi_capture import create_capture_backend


class CapturedFrame:
    __slots__ = (
        "frame_id",
        "captured_at",
        "_frame",
        "_frame_shape",
        "native_frame",
        "_frame_loader",
        "_roi_loader",
        "_frame_release",
        "_native_released",
    )

    def __init__(
        self,
        *,
        frame_id: int,
        captured_at: float,
        frame: np.ndarray | None,
        frame_shape: tuple[int, ...] | None = None,
        native_frame: object | None = None,
        frame_loader=None,
        roi_loader=None,
        frame_release=None,
    ):
        self.frame_id = int(frame_id)
        self.captured_at = float(captured_at)
        self._frame = frame
        self._frame_shape = tuple(frame.shape) if frame is not None else tuple(frame_shape) if frame_shape is not None else None
        self.native_frame = native_frame
        self._frame_loader = frame_loader
        self._roi_loader = roi_loader
        self._frame_release = frame_release
        self._native_released = False

    @property
    def frame(self) -> np.ndarray | None:
        if self._frame is None and self._frame_loader is not None:
            self._frame = self._frame_loader()
            self._frame_shape = tuple(self._frame.shape)
            self.release_native_resources()
        return self._frame

    def peek_frame(self) -> np.ndarray | None:
        return self._frame

    @property
    def shape(self):
        frame = self.peek_frame()
        if frame is not None:
            return frame.shape
        if self._frame_shape is None:
            raise AttributeError("CapturedFrame shape is unavailable.")
        return self._frame_shape

    def can_extract_roi(self) -> bool:
        return self.peek_frame() is not None or self._roi_loader is not None or self._frame_loader is not None

    def get_roi_rgb(self, left: int, top: int, right: int, bottom: int):
        frame = self.peek_frame()
        if frame is not None:
            return frame[top:bottom, left:right]
        if self._roi_loader is not None:
            return self._roi_loader(left, top, right, bottom)
        frame = self.frame
        return frame[top:bottom, left:right]

    def release_native_resources(self):
        if self._native_released:
            return
        self._native_released = True
        if self._frame_release is not None:
            self._frame_release()

    def __array__(self, dtype=None):
        return np.asarray(self.frame, dtype=dtype)

    def __getitem__(self, item):
        return self.frame[item]

    def __getattr__(self, name):
        return getattr(self.frame, name)


class ScreenCaptureThread(threading.Thread):
    def __init__(
        self,
        target_fps: int = 90,
        crop_width: int = 640,
        crop_height: int = 640,
        idle_fps: int | None = None,
        enable_native_frames: bool = False,
    ):
        super().__init__(daemon=True)
        screen_width = win32api.GetSystemMetrics(0)
        screen_height = win32api.GetSystemMetrics(1)
        left = (screen_width - crop_width) // 2
        top = (screen_height - crop_height) // 2

        self.region = (left, top, left + crop_width, top + crop_height)
        self._backend = create_capture_backend(
            output_color="RGB",
            region=self.region,
            emit_native_frames=enable_native_frames,
        )
        self.running = True
        self._condition = threading.Condition()
        self._latest_frame: CapturedFrame | None = None
        self._latest_frame_id = 0
        self._target_fps = float(idle_fps if idle_fps is not None else target_fps)
        self._schedule_reset = True
        self._backend_closed = False

    def run(self):
        next_capture_at = time.perf_counter()
        try:
            while True:
                with self._condition:
                    if not self.running:
                        return
                    if self._schedule_reset:
                        next_capture_at = time.perf_counter()
                        self._schedule_reset = False
                    current_fps = self._target_fps
                    if current_fps <= 0.0:
                        self._condition.wait()
                        continue
                    remaining = next_capture_at - time.perf_counter()
                    if remaining > 0.0:
                        self._condition.wait(timeout=remaining)
                        continue

                backend_frame = self._backend.grab()
                captured_at = time.perf_counter()
                if backend_frame is not None:
                    frame = getattr(backend_frame, "frame", backend_frame)
                    frame_shape = getattr(backend_frame, "frame_shape", None)
                    native_frame = getattr(backend_frame, "native_frame", None)
                    frame_loader = getattr(backend_frame, "frame_loader", None)
                    roi_loader = getattr(backend_frame, "roi_loader", None)
                    frame_release = getattr(backend_frame, "frame_release", None)
                    with self._condition:
                        previous_frame = self._latest_frame
                        self._latest_frame_id += 1
                        self._latest_frame = CapturedFrame(
                            frame_id=self._latest_frame_id,
                            captured_at=captured_at,
                            frame=frame,
                            frame_shape=frame_shape,
                            native_frame=native_frame,
                            frame_loader=frame_loader,
                            roi_loader=roi_loader,
                            frame_release=frame_release,
                        )
                        if previous_frame is not None:
                            previous_frame.release_native_resources()
                        self._condition.notify_all()

                interval = 1.0 / max(current_fps, 1.0)
                next_capture_at = max(next_capture_at + interval, captured_at)
        finally:
            self._close_backend()

    def get_latest_frame(self, last_seen_id: int = 0, timeout: float = 0.1):
        deadline = time.perf_counter() + timeout
        with self._condition:
            while self.running and self._latest_frame_id <= last_seen_id:
                remaining = deadline - time.perf_counter()
                if remaining <= 0:
                    return None, last_seen_id
                self._condition.wait(timeout=remaining)

            if self._latest_frame is None or self._latest_frame.frame_id <= last_seen_id:
                return None, last_seen_id

            return self._latest_frame, self._latest_frame.frame_id

    def set_target_fps(self, target_fps: float):
        with self._condition:
            next_fps = max(0.0, float(target_fps))
            if self._target_fps == next_fps:
                return
            self._target_fps = next_fps
            self._schedule_reset = True
            self._condition.notify_all()

    def _close_backend(self):
        if self._backend_closed:
            return
        self._backend_closed = True
        self._backend.close()

    def stop(self):
        with self._condition:
            self.running = False
            if self._latest_frame is not None:
                self._latest_frame.release_native_resources()
            self._condition.notify_all()
        if not self.is_alive():
            self._close_backend()
