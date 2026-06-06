import time


_TIMING_METRICS = (
    ("source_age_ms", "src_age"),
    ("native_pipeline_ms", "native"),
    ("python_handoff_ms", "handoff"),
    ("controller_consume_age_ms", "consume"),
    ("output_age_ms", "out_age"),
)

_NATIVE_DETAIL_METRICS = (
    ("native_capture_acquire_ms", "cap_acq"),
    ("native_capture_copy_ms", "cap_copy"),
    ("native_cuda_map_ms", "cuda_map"),
    ("native_output_copy_sync_ms", "out_sync"),
    ("native_gpu_total_ms", "gpu_total"),
    ("native_output_copy_ms", "d2h_copy"),
    ("native_output_wait_ms", "sync_wait"),
    ("native_decode_ms", "decode"),
    ("native_selector_ms", "selector"),
    ("native_enhance_ms", "enhance"),
    ("native_cuda_unmap_ms", "cuda_unmap"),
    ("external_cue_ms", "cue_ms"),
)

_TARGET_BUCKETS = ("obs", "weak", "cue", "pred", "none", "unk")
_NEAR_BOX_WIDTH_RATIO = 0.35
_NEAR_BOX_HEIGHT_RATIO = 0.55
_EDGE_MARGIN_PX = 32.0


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
        self._native_detail_values = {name: [] for name, _label in _NATIVE_DETAIL_METRICS}
        self._tracking_native_detail_values = {name: [] for name, _label in _NATIVE_DETAIL_METRICS}
        self._target_stats = self._new_target_stats()
        self._tracking_target_stats = self._new_target_stats()
        self._box_stats = self._new_box_stats()
        self._tracking_box_stats = self._new_box_stats()

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
        native_capture_acquire_ms: float | None = None,
        native_capture_copy_ms: float | None = None,
        native_cuda_map_ms: float | None = None,
        native_output_copy_sync_ms: float | None = None,
        native_gpu_total_ms: float | None = None,
        native_output_copy_ms: float | None = None,
        native_output_wait_ms: float | None = None,
        native_decode_ms: float | None = None,
        native_selector_ms: float | None = None,
        native_enhance_ms: float | None = None,
        native_cuda_unmap_ms: float | None = None,
        external_cue_ms: float | None = None,
        target_source: str | None = None,
        target_tier: str | None = None,
        aim_authority: bool | None = None,
        fire_authority: bool | None = None,
        has_external_cue: bool | None = None,
        native_auto_fire_requested: bool | None = None,
        auto_fire_active: bool | None = None,
        target_box_width_px: float | None = None,
        target_box_height_px: float | None = None,
        target_edge_margin_px: float | None = None,
        capture_width_px: float | None = None,
        capture_height_px: float | None = None,
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
        native_detail_values = {
            "native_capture_acquire_ms": native_capture_acquire_ms,
            "native_capture_copy_ms": native_capture_copy_ms,
            "native_cuda_map_ms": native_cuda_map_ms,
            "native_output_copy_sync_ms": native_output_copy_sync_ms,
            "native_gpu_total_ms": native_gpu_total_ms,
            "native_output_copy_ms": native_output_copy_ms,
            "native_output_wait_ms": native_output_wait_ms,
            "native_decode_ms": native_decode_ms,
            "native_selector_ms": native_selector_ms,
            "native_enhance_ms": native_enhance_ms,
            "native_cuda_unmap_ms": native_cuda_unmap_ms,
            "external_cue_ms": external_cue_ms,
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
        self._record_timing_values(self._native_detail_values, native_detail_values)
        self._record_target_stats(
            self._target_stats,
            target_source=target_source,
            target_tier=target_tier,
            aim_authority=aim_authority,
            fire_authority=fire_authority,
            has_external_cue=has_external_cue,
            native_auto_fire_requested=native_auto_fire_requested,
            auto_fire_active=auto_fire_active,
        )
        self._record_box_stats(
            self._box_stats,
            target_box_width_px=target_box_width_px,
            target_box_height_px=target_box_height_px,
            target_edge_margin_px=target_edge_margin_px,
            capture_width_px=capture_width_px,
            capture_height_px=capture_height_px,
        )

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
            self._record_timing_values(self._tracking_native_detail_values, native_detail_values)
            self._record_target_stats(
                self._tracking_target_stats,
                target_source=target_source,
                target_tier=target_tier,
                aim_authority=aim_authority,
                fire_authority=fire_authority,
                has_external_cue=has_external_cue,
                native_auto_fire_requested=native_auto_fire_requested,
                auto_fire_active=auto_fire_active,
            )
            self._record_box_stats(
                self._tracking_box_stats,
                target_box_width_px=target_box_width_px,
                target_box_height_px=target_box_height_px,
                target_edge_margin_px=target_edge_margin_px,
                capture_width_px=capture_width_px,
                capture_height_px=capture_height_px,
            )

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
            self._native_detail_values,
            self._target_stats,
            self._box_stats,
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
                self._tracking_native_detail_values,
                self._tracking_target_stats,
                self._tracking_box_stats,
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
        native_detail_values: dict[str, list[float]] | None = None,
        target_stats: dict | None = None,
        box_stats: dict | None = None,
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
            f"{self._format_native_detail_values(native_detail_values)}"
            f"{self._format_target_stats(target_stats)}"
            f"{self._format_box_stats(box_stats)}"
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

    def _format_native_detail_values(self, timing_values: dict[str, list[float]] | None) -> str:
        if not timing_values:
            return ""

        parts = []
        for name, label in _NATIVE_DETAIL_METRICS:
            values = timing_values.get(name) or []
            if not values:
                continue
            ordered = sorted(values)
            p95_index = min(len(ordered) - 1, max(0, int((len(ordered) * 0.95) + 0.999999) - 1))
            average = sum(values) / len(values)
            parts.append(f"{label}={average:.1f}/{ordered[p95_index]:.1f}/{max(values):.1f}ms")
        if not parts:
            return ""
        return " | detail " + " ".join(parts)

    def _new_target_stats(self) -> dict:
        return {
            "tier": {bucket: 0 for bucket in _TARGET_BUCKETS},
            "aim_authority": 0,
            "fire_authority": 0,
            "external_cue": 0,
            "fire_requested": 0,
            "fire_allowed": 0,
            "fire_blocked": 0,
        }

    def _new_box_stats(self) -> dict:
        return {
            "samples": 0,
            "near": 0,
            "edge": 0,
            "near_edge": 0,
        }

    def _record_box_stats(
        self,
        target: dict,
        *,
        target_box_width_px: float | None,
        target_box_height_px: float | None,
        target_edge_margin_px: float | None,
        capture_width_px: float | None,
        capture_height_px: float | None,
    ) -> None:
        if (
            target_box_width_px is None
            or target_box_height_px is None
            or target_edge_margin_px is None
            or capture_width_px is None
            or capture_height_px is None
        ):
            return
        if capture_width_px <= 0.0 or capture_height_px <= 0.0:
            return

        target["samples"] += 1
        near = (
            (float(target_box_width_px) / float(capture_width_px)) >= _NEAR_BOX_WIDTH_RATIO
            or (float(target_box_height_px) / float(capture_height_px)) >= _NEAR_BOX_HEIGHT_RATIO
        )
        edge = float(target_edge_margin_px) <= _EDGE_MARGIN_PX
        if near:
            target["near"] += 1
        if edge:
            target["edge"] += 1
        if near and edge:
            target["near_edge"] += 1

    def _record_target_stats(
        self,
        target: dict,
        *,
        target_source: str | None,
        target_tier: str | None,
        aim_authority: bool | None,
        fire_authority: bool | None,
        has_external_cue: bool | None,
        native_auto_fire_requested: bool | None,
        auto_fire_active: bool | None,
    ) -> None:
        target["tier"][self._target_bucket(target_source, target_tier)] += 1
        if aim_authority:
            target["aim_authority"] += 1
        if fire_authority:
            target["fire_authority"] += 1
        if has_external_cue:
            target["external_cue"] += 1
        if native_auto_fire_requested:
            target["fire_requested"] += 1
            if auto_fire_active:
                target["fire_allowed"] += 1
            else:
                target["fire_blocked"] += 1

    def _format_target_stats(self, target_stats: dict | None) -> str:
        if not target_stats:
            return ""
        tier_counts = target_stats.get("tier") or {}
        tier_text = " ".join(
            f"{bucket}={int(tier_counts.get(bucket, 0))}"
            for bucket in _TARGET_BUCKETS
        )
        return (
            f" | tier {tier_text}"
            f" | auth aim={int(target_stats.get('aim_authority', 0))}"
            f" fire={int(target_stats.get('fire_authority', 0))}"
            f" | cue={int(target_stats.get('external_cue', 0))}"
            f" | fire req={int(target_stats.get('fire_requested', 0))}"
            f" ok={int(target_stats.get('fire_allowed', 0))}"
            f" block={int(target_stats.get('fire_blocked', 0))}"
        )

    def _format_box_stats(self, box_stats: dict | None) -> str:
        if not box_stats or int(box_stats.get("samples", 0)) <= 0:
            return ""
        return (
            f" | box samples={int(box_stats.get('samples', 0))}"
            f" near={int(box_stats.get('near', 0))}"
            f" edge={int(box_stats.get('edge', 0))}"
            f" near_edge={int(box_stats.get('near_edge', 0))}"
        )

    def _target_bucket(self, source: str | None, tier: str | None) -> str:
        source_token = self._token(source)
        tier_token = self._token(tier)
        if not source_token and not tier_token:
            return "none"
        if tier_token in {"none", "lost"}:
            return "none"
        if source_token == "observed" or tier_token in {"observed_strong", "strong_observed", "confirmed"}:
            return "obs"
        if source_token in {"associated_weak", "weak_observed", "low_score"} or tier_token in {
            "associated_weak",
            "weak_observed",
        }:
            return "weak"
        if source_token in {"cue_hold", "yellow_cue"} or tier_token == "cue_hold":
            return "cue"
        if source_token in {"predicted", "projected", "projection"} or tier_token in {
            "predicted",
            "projected",
            "projection",
        }:
            return "pred"
        return "unk"

    @staticmethod
    def _token(value: str | None) -> str:
        if value is None:
            return ""
        return str(value).strip().casefold()
