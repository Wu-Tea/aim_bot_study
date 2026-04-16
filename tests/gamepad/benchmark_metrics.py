from __future__ import annotations

import math
from dataclasses import dataclass
from statistics import mean
from typing import Callable, Sequence

from controllers.gamepad.ai_aim import AIAimConfig, AIAimPlugin
from controllers.gamepad.state import GamepadFrame, GamepadOutput

from tests.gamepad.benchmark_scenarios import ScenarioManifest, expand_manifest


@dataclass(frozen=True, slots=True)
class BenchmarkMetricsConfig:
    frame_dt: float = 1.0 / 60.0
    sim_frames: int = 180
    measure_from_frame: int = 60
    max_reticle_speed_pps: float = 1500.0
    stick_max: int = 32767
    overshoot_threshold_px: float = 2.0
    turn_recovery_threshold_px: float = 6.0
    settle_threshold_px: float = 5.0
    settle_consecutive_frames: int = 4


@dataclass(frozen=True, slots=True)
class BenchmarkScenarioMetrics:
    scenario_key: str
    kind: str
    mean_error_px: float
    p95_error_px: float
    p99_error_px: float
    overshoot_events: int
    max_overshoot_px: float
    mean_recovery_frames_after_turn: float | None
    mean_settle_frames_after_decel: float | None


@dataclass(frozen=True, slots=True)
class BenchmarkAggregateMetrics:
    mean_error_px: float
    p95_error_px: float
    p99_error_px: float
    overshoot_events: int
    max_overshoot_px: float
    mean_recovery_frames_after_turn: float | None
    mean_settle_frames_after_decel: float | None


@dataclass(frozen=True, slots=True)
class BenchmarkMetricDeltas:
    mean_error_px: float | None
    p95_error_px: float | None
    p99_error_px: float | None
    overshoot_events: float | None
    max_overshoot_px: float | None
    mean_recovery_frames_after_turn: float | None
    mean_settle_frames_after_decel: float | None


@dataclass(frozen=True, slots=True)
class BenchmarkRunSummary:
    run_key: str
    config: BenchmarkMetricsConfig
    scenario_metrics: tuple[BenchmarkScenarioMetrics, ...]
    aggregate: BenchmarkAggregateMetrics

    def relative_deltas(self, baseline: BenchmarkAggregateMetrics | "BenchmarkRunSummary") -> BenchmarkMetricDeltas:
        baseline_metrics = baseline.aggregate if isinstance(baseline, BenchmarkRunSummary) else baseline
        return BenchmarkMetricDeltas(
            mean_error_px=_relative_change(self.aggregate.mean_error_px, baseline_metrics.mean_error_px),
            p95_error_px=_relative_change(self.aggregate.p95_error_px, baseline_metrics.p95_error_px),
            p99_error_px=_relative_change(self.aggregate.p99_error_px, baseline_metrics.p99_error_px),
            overshoot_events=_relative_change(float(self.aggregate.overshoot_events), float(baseline_metrics.overshoot_events)),
            max_overshoot_px=_relative_change(self.aggregate.max_overshoot_px, baseline_metrics.max_overshoot_px),
            mean_recovery_frames_after_turn=_relative_change(
                self.aggregate.mean_recovery_frames_after_turn,
                baseline_metrics.mean_recovery_frames_after_turn,
            ),
            mean_settle_frames_after_decel=_relative_change(
                self.aggregate.mean_settle_frames_after_decel,
                baseline_metrics.mean_settle_frames_after_decel,
            ),
        )


@dataclass(frozen=True, slots=True)
class _FrameRecord:
    frame: int
    target_x: float
    target_y: float
    reticle_x: float
    reticle_y: float
    error_x: float
    error_y: float
    radial_error_px: float
    stick_x: int
    stick_y: int


def evaluate_scenario(
    manifest: ScenarioManifest,
    *,
    config: BenchmarkMetricsConfig | None = None,
    plugin_factory: Callable[[], AIAimPlugin] | None = None,
) -> BenchmarkScenarioMetrics:
    config = config or BenchmarkMetricsConfig()
    records = _simulate_closed_loop(manifest, config=config, plugin_factory=plugin_factory)
    measured_errors = [record.radial_error_px for record in records if record.frame >= config.measure_from_frame]
    if not measured_errors:
        raise ValueError("measure_from_frame leaves no samples to summarize")

    return BenchmarkScenarioMetrics(
        scenario_key=manifest.scenario_key,
        kind=manifest.kind,
        mean_error_px=mean(measured_errors),
        p95_error_px=_nearest_rank_percentile(measured_errors, 0.95),
        p99_error_px=_nearest_rank_percentile(measured_errors, 0.99),
        overshoot_events=_count_overshoots(records, threshold_px=config.overshoot_threshold_px),
        max_overshoot_px=_max_overshoot_px(records, threshold_px=config.overshoot_threshold_px),
        mean_recovery_frames_after_turn=_mean_turn_recovery_frames(
            manifest,
            records,
            threshold_px=config.turn_recovery_threshold_px,
        ),
        mean_settle_frames_after_decel=_mean_decel_settle_frames(
            manifest,
            records,
            threshold_px=config.settle_threshold_px,
            consecutive_frames=config.settle_consecutive_frames,
        ),
    )


def evaluate_run(
    run_key: str,
    manifests: Sequence[ScenarioManifest],
    *,
    config: BenchmarkMetricsConfig | None = None,
    plugin_factory: Callable[[], AIAimPlugin] | None = None,
) -> BenchmarkRunSummary:
    config = config or BenchmarkMetricsConfig()
    scenario_metrics = tuple(
        evaluate_scenario(manifest, config=config, plugin_factory=plugin_factory)
        for manifest in manifests
    )
    return BenchmarkRunSummary(
        run_key=run_key,
        config=config,
        scenario_metrics=scenario_metrics,
        aggregate=_aggregate_scenario_metrics(scenario_metrics),
    )


def _simulate_closed_loop(
    manifest: ScenarioManifest,
    *,
    config: BenchmarkMetricsConfig,
    plugin_factory: Callable[[], AIAimPlugin] | None = None,
) -> list[_FrameRecord]:
    plugin = plugin_factory() if plugin_factory is not None else AIAimPlugin(AIAimConfig())
    plugin.reset()

    expanded_states = expand_manifest(manifest, config.frame_dt, config.sim_frames)
    reticle_x = 0.0
    reticle_y = 0.0
    records: list[_FrameRecord] = []

    for state in expanded_states:
        error_x = state.target_x - reticle_x
        error_y = state.target_y - reticle_y
        timestamp = state.frame * config.frame_dt
        frame = GamepadFrame(
            timestamp=timestamp,
            left_x=0,
            left_y=0,
            manual_right_x=0,
            manual_right_y=0,
            left_trigger=255,
            right_trigger=0,
            buttons={},
            is_aiming=True,
            target_dx=error_x,
            target_dy=error_y,
            auto_fire_requested=False,
            target_revision=state.frame + 1,
            target_timestamp=timestamp,
        )
        output = GamepadOutput()
        plugin.apply(frame, output)

        stick_x = _clamp_int(output.right_x, config.stick_max)
        stick_y = _clamp_int(output.right_y, config.stick_max)
        reticle_x += (stick_x / config.stick_max) * config.max_reticle_speed_pps * config.frame_dt
        reticle_y += (-stick_y / config.stick_max) * config.max_reticle_speed_pps * config.frame_dt

        records.append(
            _FrameRecord(
                frame=state.frame,
                target_x=state.target_x,
                target_y=state.target_y,
                reticle_x=reticle_x,
                reticle_y=reticle_y,
                error_x=error_x,
                error_y=error_y,
                radial_error_px=math.hypot(error_x, error_y),
                stick_x=stick_x,
                stick_y=stick_y,
            )
        )

    return records


def _aggregate_scenario_metrics(metrics: Sequence[BenchmarkScenarioMetrics]) -> BenchmarkAggregateMetrics:
    if not metrics:
        raise ValueError("at least one scenario metric is required")

    recovery_values = [metric.mean_recovery_frames_after_turn for metric in metrics if metric.mean_recovery_frames_after_turn is not None]
    settle_values = [metric.mean_settle_frames_after_decel for metric in metrics if metric.mean_settle_frames_after_decel is not None]

    return BenchmarkAggregateMetrics(
        mean_error_px=mean(metric.mean_error_px for metric in metrics),
        p95_error_px=mean(metric.p95_error_px for metric in metrics),
        p99_error_px=mean(metric.p99_error_px for metric in metrics),
        overshoot_events=sum(metric.overshoot_events for metric in metrics),
        max_overshoot_px=max(metric.max_overshoot_px for metric in metrics),
        mean_recovery_frames_after_turn=mean(recovery_values) if recovery_values else None,
        mean_settle_frames_after_decel=mean(settle_values) if settle_values else None,
    )


def _count_overshoots(records: Sequence[_FrameRecord], *, threshold_px: float) -> int:
    return _axis_overshoot_count([record.error_x for record in records], threshold_px=threshold_px) + _axis_overshoot_count(
        [record.error_y for record in records],
        threshold_px=threshold_px,
    )


def _axis_overshoot_count(errors: Sequence[float], *, threshold_px: float) -> int:
    if len(errors) < 2:
        return 0

    count = 0
    previous = errors[0]
    for current in errors[1:]:
        if _qualifying_overshoot(previous, current, threshold_px=threshold_px) is not None:
            count += 1
            previous = current
            continue
        previous = current
    return count


def _max_overshoot_px(records: Sequence[_FrameRecord], *, threshold_px: float) -> float:
    max_overshoot = 0.0
    for axis_errors in ([record.error_x for record in records], [record.error_y for record in records]):
        previous = axis_errors[0] if axis_errors else 0.0
        for current in axis_errors[1:]:
            overshoot = _qualifying_overshoot(previous, current, threshold_px=threshold_px)
            if overshoot is not None:
                max_overshoot = max(max_overshoot, overshoot)
            previous = current
    return max_overshoot


def _mean_turn_recovery_frames(
    manifest: ScenarioManifest,
    records: Sequence[_FrameRecord],
    *,
    threshold_px: float,
) -> float | None:
    if not manifest.turn_events:
        return None

    recovery_frames: list[float] = []
    for turn_event in manifest.turn_events:
        recovered_frame = next(
            (
                record.frame
                for record in records
                if record.frame >= turn_event.frame and record.radial_error_px < threshold_px
            ),
            None,
        )
        if recovered_frame is not None:
            recovery_frames.append(float(recovered_frame - turn_event.frame))

    return mean(recovery_frames) if recovery_frames else None


def _mean_decel_settle_frames(
    manifest: ScenarioManifest,
    records: Sequence[_FrameRecord],
    *,
    threshold_px: float,
    consecutive_frames: int,
) -> float | None:
    if not manifest.decel_events:
        return None

    settle_frames: list[float] = []
    for decel_event in manifest.decel_events:
        settle_frame = _first_settled_frame(
            records,
            decel_event.frame,
            threshold_px=threshold_px,
            consecutive_frames=consecutive_frames,
        )
        if settle_frame is not None:
            settle_frames.append(float(settle_frame - decel_event.frame))

    return mean(settle_frames) if settle_frames else None


def _first_settled_frame(
    records: Sequence[_FrameRecord],
    start_frame: int,
    *,
    threshold_px: float,
    consecutive_frames: int,
) -> int | None:
    if consecutive_frames <= 0:
        raise ValueError("consecutive_frames must be positive")

    for index, record in enumerate(records):
        if record.frame < start_frame:
            continue
        window = records[index : index + consecutive_frames]
        if len(window) < consecutive_frames:
            return None
        if all(item.radial_error_px <= threshold_px for item in window):
            return record.frame
    return None


def _qualifying_overshoot(previous: float, current: float, *, threshold_px: float) -> float | None:
    if previous > 0.0 and current < 0.0 and abs(current) > threshold_px:
        return abs(current)
    if previous < 0.0 and current > 0.0 and abs(current) > threshold_px:
        return abs(current)
    return None


def _nearest_rank_percentile(values: Sequence[float], percentile: float) -> float:
    if not values:
        raise ValueError("percentile requires at least one value")
    if not 0.0 < percentile <= 1.0:
        raise ValueError("percentile must be in the range (0, 1]")

    ordered = sorted(values)
    index = max(0, min(len(ordered) - 1, math.ceil(percentile * len(ordered)) - 1))
    return ordered[index]


def _relative_change(current: float | None, baseline: float | None) -> float | None:
    if current is None or baseline is None:
        return None
    if baseline == 0.0:
        return 0.0 if current == 0.0 else None
    return (current - baseline) / baseline


def _clamp_int(value: int, limit: int) -> int:
    return max(-limit, min(limit, value))
