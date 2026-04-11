import time


class PerformanceTracker:
    def __init__(self, enabled: bool = False, log_interval: float = 2.0, clock=None, printer=None):
        self.enabled = enabled
        self.log_interval = log_interval
        self._clock = clock or time.perf_counter
        self._printer = printer or print
        self.reset_window()

    def reset_window(self):
        self._window_start = self._clock()
        self._frame_count = 0
        self._capture_wait_ms = 0.0
        self._infer_ms = 0.0
        self._post_ms = 0.0
        self._boxes_seen = 0
        self._tracking_window_start = None
        self._tracking_frame_count = 0
        self._tracking_capture_wait_ms = 0.0
        self._tracking_infer_ms = 0.0
        self._tracking_post_ms = 0.0
        self._tracking_boxes_seen = 0

    def update(self, capture_wait_ms: float, infer_ms: float, post_ms: float, boxes_seen: int, tracking_active: bool = False):
        if not self.enabled:
            return

        self._frame_count += 1
        self._capture_wait_ms += capture_wait_ms
        self._infer_ms += infer_ms
        self._post_ms += post_ms
        self._boxes_seen += boxes_seen

        now = self._clock()
        if tracking_active:
            if self._tracking_window_start is None:
                self._tracking_window_start = now
            self._tracking_frame_count += 1
            self._tracking_capture_wait_ms += capture_wait_ms
            self._tracking_infer_ms += infer_ms
            self._tracking_post_ms += post_ms
            self._tracking_boxes_seen += boxes_seen

        if now - self._window_start < self.log_interval or self._frame_count == 0:
            return

        self._emit("[Perf][ADS]", now, self._frame_count, self._capture_wait_ms, self._infer_ms, self._post_ms, self._boxes_seen, self._window_start)
        if self._tracking_frame_count > 0 and self._tracking_window_start is not None:
            self._emit(
                "[Perf][TRACK]",
                now,
                self._tracking_frame_count,
                self._tracking_capture_wait_ms,
                self._tracking_infer_ms,
                self._tracking_post_ms,
                self._tracking_boxes_seen,
                self._tracking_window_start,
            )

        self.reset_window()

    def _emit(self, prefix: str, now: float, frame_count: int, wait_sum: float, infer_sum: float, post_sum: float, boxes_sum: float, window_start: float):
        elapsed = max(now - window_start, 1e-9)
        self._printer(
            f"{prefix} "
            f"loop={frame_count / elapsed:.1f} FPS | wait={wait_sum / frame_count:.1f}ms | "
            f"infer={infer_sum / frame_count:.1f}ms | post={post_sum / frame_count:.1f}ms | "
            f"boxes={boxes_sum / frame_count:.1f}"
        )
