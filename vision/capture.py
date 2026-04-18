import threading
import time
from dataclasses import dataclass

import dxcam
import numpy as np
import win32api


@dataclass(slots=True, frozen=True)
class CapturedFrame:
    frame_id: int
    captured_at: float
    frame: np.ndarray

    def __array__(self, dtype=None):
        return np.asarray(self.frame, dtype=dtype)

    def __getitem__(self, item):
        return self.frame[item]

    def __getattr__(self, name):
        return getattr(self.frame, name)


class ScreenCaptureThread(threading.Thread):
    def __init__(self, target_fps: int = 90, crop_width: int = 640, crop_height: int = 640):
        super().__init__(daemon=True)
        screen_width = win32api.GetSystemMetrics(0)
        screen_height = win32api.GetSystemMetrics(1)
        left = (screen_width - crop_width) // 2
        top = (screen_height - crop_height) // 2

        self.region = (left, top, left + crop_width, top + crop_height)
        self.camera = dxcam.create(output_color="RGB", region=self.region)
        self.running = True
        self._condition = threading.Condition()
        self._latest_frame: CapturedFrame | None = None
        self._latest_frame_id = 0

        self.camera.start(target_fps=target_fps, video_mode=True)

    def run(self):
        while self.running:
            frame = self.camera.get_latest_frame()
            if frame is None:
                continue
            with self._condition:
                self._latest_frame_id += 1
                self._latest_frame = CapturedFrame(
                    frame_id=self._latest_frame_id,
                    captured_at=time.perf_counter(),
                    frame=frame,
                )
                self._condition.notify_all()

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

    def stop(self):
        self.running = False
        with self._condition:
            self._condition.notify_all()
        self.camera.stop()
