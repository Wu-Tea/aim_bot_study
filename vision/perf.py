import time


_TIMING_METRICS = (
    ("source_age_ms", "src_age"),
    ("native_pipeline_ms", "native"),
    ("python_handoff_ms", "handoff"),
    ("controller_consume_age_ms", "consume"),
    ("output_age_ms", "out_age"),
)


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
        self._wait_ms = 0.0
        self._preprocess_ms = 0.0
        self._color_copy_ms = 0.0
        self._infer_ms = 0.0
        self._post_ms = 0.0
        self._age_ms = 0.0
        self._boxes_seen = 0
        self._tracking_window_start = None
        self._tracking_frame_count = 0
        self._tracking_wait_ms = 0.0
        self._tracking_preprocess_ms = 0.0
        self._tracking_color_copy_ms = 0.0
        self._tracking_infer_ms = 0.0
        self._tracking_post_ms = 0.0
        self._tracking_age_ms = 0.0
        self._tracking_boxes_seen = 0
        self._timing_values = {name: [] for name, _label in _TIMING_METRICS}
        self._tracking_timing_values = {name: [] for name, _label in _TIMING_METRICS}

    def update(
        self,
        wait_ms: float,
        preprocess_ms: float,
        color_copy_ms: float,
        infer_ms: float,
        post_ms: float,
        boxes_seen: int,
        age_ms: float,
        tracking_active: bool = False,
        source_age_ms: float | None = None,
        native_pipeline_ms: float | None = None,
        python_handoff_ms: float | None = None,
        controller_consume_age_ms: float | None = None,
        output_age_ms: float | None = None,
    ):
        if not self.enabled:
            return

        timing_values = {
            "source_age_ms": source_age_ms,
            "native_pipeline_ms": native_pipeline_ms,
            "python_handoff_ms": python_handoff_ms,
            "controller_consume_age_ms": controller_consume_age_ms,
            "output_age_ms": output_age_ms,
        }

        self._frame_count += 1
        self._wait_ms += wait_ms
        self._preprocess_ms += preprocess_ms
        self._color_copy_ms += color_copy_ms
        self._infer_ms += infer_ms
        self._post_ms += post_ms
        self._age_ms += age_ms
        self._boxes_seen += boxes_seen
        self._record_timing_values(self._timing_values, timing_values)

        now = self._clock()
        if tracking_active:
            if self._tracking_window_start is None:
                self._tracking_window_start = now
            self._tracking_frame_count += 1
            self._tracking_wait_ms += wait_ms
            self._tracking_preprocess_ms += preprocess_ms
            self._tracking_color_copy_ms += color_copy_ms
            self._tracking_infer_ms += infer_ms
            self._tracking_post_ms += post_ms
            self._tracking_age_ms += age_ms
            self._tracking_boxes_seen += boxes_seen
            self._record_timing_values(self._tracking_timing_values, timing_values)

        if now - self._window_start < self.log_interval or self._frame_count == 0:
            return

        self._emit(
            "[Perf][ADS]",
            now,
            self._frame_count,
            self._wait_ms,
            self._preprocess_ms,
            self._color_copy_ms,
            self._infer_ms,
            self._post_ms,
            self._age_ms,
            self._boxes_seen,
            self._window_start,
            self._timing_values,
        )
        if self._tracking_frame_count > 0 and self._tracking_window_start is not None:
            self._emit(
                "[Perf][TRACK]",
                now,
                self._tracking_frame_count,
                self._tracking_wait_ms,
                self._tracking_preprocess_ms,
                self._tracking_color_copy_ms,
                self._tracking_infer_ms,
                self._tracking_post_ms,
                self._tracking_age_ms,
                self._tracking_boxes_seen,
                self._tracking_window_start,
                self._tracking_timing_values,
            )

        self.reset_window()

    def _emit(
        self,
        prefix: str,
        now: float,
        frame_count: int,
        wait_sum: float,
        preprocess_sum: float,
        color_copy_sum: float,
        infer_sum: float,
        post_sum: float,
        age_sum: float,
        boxes_sum: float,
        window_start: float,
        timing_values: dict[str, list[float]] | None = None,
    ):
        elapsed = max(now - window_start, 1e-9)
        self._printer(
            f"{prefix} "
            f"loop={frame_count / elapsed:.1f} FPS | wait={wait_sum / frame_count:.1f}ms | "
            f"pre={preprocess_sum / frame_count:.1f}ms | "
            f"copy={color_copy_sum / frame_count:.1f}ms | "
            f"infer={infer_sum / frame_count:.1f}ms | post={post_sum / frame_count:.1f}ms | "
            f"age={age_sum / frame_count:.1f}ms | boxes={boxes_sum / frame_count:.1f}"
            f"{self._format_timing_values(timing_values)}"
        )

    def _record_timing_values(self, target: dict[str, list[float]], values: dict[str, float | None]) -> None:
        for name, value in values.items():
            if value is None:
                continue
            target[name].append(float(value))

    def _format_timing_values(self, timing_values: dict[str, list[float]] | None) -> str:
        if not timing_values:
            return ""

        parts = []
        for name, label in _TIMING_METRICS:
            values = timing_values.get(name) or []
            if not values:
                continue
            ordered = sorted(values)
            p95_index = min(len(ordered) - 1, max(0, int((len(ordered) * 0.95) + 0.999999) - 1))
            average = sum(values) / len(values)
            parts.append(f"{label}={average:.1f}/{ordered[p95_index]:.1f}/{max(values):.1f}ms")
        if not parts:
            return ""
        return " | " + " | ".join(parts)
