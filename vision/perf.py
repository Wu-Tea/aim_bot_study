import time


class PerformanceTracker:
    def __init__(self, enabled: bool = False, log_interval: float = 2.0, clock=None, printer=None):
        self.enabled = enabled
        self.log_interval = log_interval
        self._clock = clock or time.perf_counter
        self._printer = printer or print
        self._stage_order = []
        self.reset_window()

    def reset_window(self):
        self._window_start = self._clock()
        self._frame_count = 0
        self._stage_sums = {}
        self._age_ms = 0.0
        self._boxes_seen = 0
        self._tracking_window_start = None
        self._tracking_frame_count = 0
        self._tracking_stage_sums = {}
        self._tracking_age_ms = 0.0
        self._tracking_boxes_seen = 0

    def update(
        self,
        *,
        roi_ms: float | None = None,
        yolo_ms: float | None = None,
        post_ms: float | None = None,
        stage_ms: dict[str, float] | None = None,
        boxes_seen: int,
        age_ms: float,
        tracking_active: bool = False,
    ):
        if not self.enabled:
            return

        metrics = stage_ms or {
            "roi": 0.0 if roi_ms is None else roi_ms,
            "yolo": 0.0 if yolo_ms is None else yolo_ms,
            "post": 0.0 if post_ms is None else post_ms,
        }
        self._frame_count += 1
        self._accumulate_stage_sums(self._stage_sums, metrics)
        self._age_ms += age_ms
        self._boxes_seen += boxes_seen

        now = self._clock()
        if tracking_active:
            if self._tracking_window_start is None:
                self._tracking_window_start = now
            self._tracking_frame_count += 1
            self._accumulate_stage_sums(self._tracking_stage_sums, metrics)
            self._tracking_age_ms += age_ms
            self._tracking_boxes_seen += boxes_seen

        if now - self._window_start < self.log_interval or self._frame_count == 0:
            return

        self._emit(
            "[Perf][ADS]",
            now,
            self._frame_count,
            self._stage_sums,
            self._age_ms,
            self._boxes_seen,
            self._window_start,
        )
        if self._tracking_frame_count > 0 and self._tracking_window_start is not None:
            self._emit(
                "[Perf][TRACK]",
                now,
                self._tracking_frame_count,
                self._tracking_stage_sums,
                self._tracking_age_ms,
                self._tracking_boxes_seen,
                self._tracking_window_start,
            )

        self.reset_window()

    def _accumulate_stage_sums(self, destination: dict[str, float], metrics: dict[str, float]):
        for name, value in metrics.items():
            if name not in self._stage_order:
                self._stage_order.append(name)
            destination[name] = destination.get(name, 0.0) + value

    def _emit(
        self,
        prefix: str,
        now: float,
        frame_count: int,
        stage_sums: dict[str, float],
        age_sum: float,
        boxes_sum: float,
        window_start: float,
    ):
        elapsed = max(now - window_start, 1e-9)
        stage_parts = [
            f"{name}={stage_sums[name] / frame_count:.1f}ms"
            for name in self._stage_order
            if name in stage_sums
        ]
        self._printer(
            f"{prefix} "
            f"loop={frame_count / elapsed:.1f} FPS | {' | '.join(stage_parts)} | "
            f"age={age_sum / frame_count:.1f}ms | boxes={boxes_sum / frame_count:.1f}"
        )
