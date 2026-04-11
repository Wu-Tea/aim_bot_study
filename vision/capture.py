import threading
import time

import dxcam
import win32api


class ScreenCaptureThread(threading.Thread):
    def __init__(self, target_fps: int = 90, crop_size: int = 640):
        super().__init__(daemon=True)
        screen_width = win32api.GetSystemMetrics(0)
        screen_height = win32api.GetSystemMetrics(1)
        left = (screen_width - crop_size) // 2
        top = (screen_height - crop_size) // 2

        self.region = (left, top, left + crop_size, top + crop_size)
        self.camera = dxcam.create(output_color="RGB", region=self.region)
        self.running = True
        self._condition = threading.Condition()
        self._latest_frame = None
        self._latest_frame_id = 0

        self.camera.start(target_fps=target_fps, video_mode=True)

    def run(self):
        while self.running:
            frame = self.camera.get_latest_frame()
            if frame is None:
                continue
            with self._condition:
                self._latest_frame = frame
                self._latest_frame_id += 1
                self._condition.notify()

    def get_latest_frame(self, last_seen_id: int = 0, timeout: float = 0.1):
        deadline = time.perf_counter() + timeout
        with self._condition:
            while self.running and self._latest_frame_id <= last_seen_id:
                remaining = deadline - time.perf_counter()
                if remaining <= 0:
                    return None, last_seen_id
                self._condition.wait(timeout=remaining)

            if self._latest_frame_id <= last_seen_id or self._latest_frame is None:
                return None, last_seen_id

            return self._latest_frame, self._latest_frame_id

    def stop(self):
        self.running = False
        with self._condition:
            self._condition.notify_all()
        self.camera.stop()
