import threading
import time
from dataclasses import dataclass

import numpy as np
import win32api

from .dxgi_capture import create_capture_backend


@dataclass(slots=True, frozen=True)
class CapturedFrame:
    frame_id: int
    captured_at: float
    frame: np.ndarray
    roi_ms: float = 0.0

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
    ):
        super().__init__(daemon=True)
        screen_width = win32api.GetSystemMetrics(0)
        screen_height = win32api.GetSystemMetrics(1)
        left = (screen_width - crop_width) // 2
        top = (screen_height - crop_height) // 2

        self.region = (left, top, left + crop_width, top + crop_height)
        self._backend = create_capture_backend(output_color="RGB", region=self.region)
        self.running = True
        self._condition = threading.Condition()
        self._latest_frame: CapturedFrame | None = None
        self._latest_frame_id = 0
        self._target_fps = float(target_fps)
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

                capture_started_at = time.perf_counter()
                frame = self._backend.grab()
                captured_at = time.perf_counter()
                roi_ms = (captured_at - capture_started_at) * 1000.0
                if frame is not None:
                    with self._condition:
                        self._latest_frame_id += 1
                        self._latest_frame = CapturedFrame(
                            frame_id=self._latest_frame_id,
                            captured_at=captured_at,
                            frame=frame,
                            roi_ms=roi_ms,
                        )
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
            self._condition.notify_all()
        if not self.is_alive():
            self._close_backend()
