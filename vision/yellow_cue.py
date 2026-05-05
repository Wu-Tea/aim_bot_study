import time

import cv2
import numpy as np

from .capture import ScreenCaptureThread


LOWER_YELLOW = np.array([20, 120, 120], dtype=np.uint8)
UPPER_YELLOW = np.array([35, 255, 255], dtype=np.uint8)
MIN_COMPONENT_AREA = 4
MAX_COMPONENT_AREA = 96
MIN_COMPONENT_FILL = 0.25
MAX_COMPONENT_ASPECT = 4.0


def detect_yellow_cue(frame: np.ndarray) -> dict | None:
    if frame is None or frame.size == 0:
        return None
    if frame.ndim != 3 or frame.shape[2] != 3:
        raise ValueError("detect_yellow_cue expects an RGB uint8 frame with shape [H,W,3]")

    hsv = cv2.cvtColor(frame, cv2.COLOR_RGB2HSV)
    mask = cv2.inRange(hsv, LOWER_YELLOW, UPPER_YELLOW)
    if cv2.countNonZero(mask) < MIN_COMPONENT_AREA:
        return None

    num_labels, _, stats, centroids = cv2.connectedComponentsWithStats(mask, connectivity=8)
    height, width = mask.shape
    center_x = width * 0.5
    center_y = height * 0.5

    best_cue = None
    best_rank = None
    for label in range(1, num_labels):
        area = int(stats[label, cv2.CC_STAT_AREA])
        if area < MIN_COMPONENT_AREA or area > MAX_COMPONENT_AREA:
            continue

        component_w = int(stats[label, cv2.CC_STAT_WIDTH])
        component_h = int(stats[label, cv2.CC_STAT_HEIGHT])
        if component_w <= 0 or component_h <= 0:
            continue

        aspect = max(component_w / float(component_h), component_h / float(component_w))
        if aspect > MAX_COMPONENT_ASPECT:
            continue

        fill_ratio = area / float(component_w * component_h)
        if fill_ratio < MIN_COMPONENT_FILL:
            continue

        cue_x = float(centroids[label][0])
        cue_y = float(centroids[label][1])
        distance_penalty = np.hypot(
            (cue_x - center_x) / max(float(width), 1.0),
            (cue_y - center_y) / max(float(height), 1.0),
        )
        rank = area + (fill_ratio * 8.0) - (distance_penalty * 4.0)
        if best_rank is None or rank > best_rank:
            confidence = min(1.0, (area / 24.0) * 0.55 + fill_ratio * 0.45)
            best_rank = rank
            best_cue = {
                "found": True,
                "x": cue_x,
                "y": cue_y,
                "score": float(max(0.0, confidence)),
            }

    return best_cue


class ScreenCaptureCueProvider:
    def __init__(
        self,
        *,
        capture_width: int = 640,
        capture_height: int = 512,
        target_fps: int = 80,
        frame_timeout: float = 0.0,
        stale_after_seconds: float = 0.08,
        capture_thread=None,
        detector=detect_yellow_cue,
        clock=None,
    ):
        self.capture_thread = capture_thread or ScreenCaptureThread(
            target_fps=target_fps,
            crop_width=capture_width,
            crop_height=capture_height,
        )
        self._detector = detector
        self._clock = clock or time.perf_counter
        self._frame_timeout = max(0.0, float(frame_timeout))
        self._stale_after_seconds = max(0.0, float(stale_after_seconds))
        self._last_seen_id = 0
        self._last_cue = None
        self._last_cue_at = 0.0
        self._closed = False

        start = getattr(self.capture_thread, "start", None)
        if callable(start):
            start()

    def __call__(self):
        if self._closed:
            return None

        frame_packet, frame_id = self.capture_thread.get_latest_frame(
            self._last_seen_id,
            timeout=self._frame_timeout,
        )
        if frame_packet is not None:
            self._last_seen_id = frame_id
            frame = getattr(frame_packet, "frame", frame_packet)
            self._last_cue = self._detector(frame)
            self._last_cue_at = self._clock()

        if self._last_cue is None:
            return None
        if (self._clock() - self._last_cue_at) > self._stale_after_seconds:
            return None
        return self._last_cue

    def close(self):
        if self._closed:
            return
        self._closed = True
        stop = getattr(self.capture_thread, "stop", None)
        if callable(stop):
            stop()
        join = getattr(self.capture_thread, "join", None)
        if callable(join):
            join(timeout=0.5)
