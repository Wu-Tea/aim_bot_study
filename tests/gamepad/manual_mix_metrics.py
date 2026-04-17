from __future__ import annotations

from dataclasses import dataclass
from statistics import mean
from typing import Callable, Sequence
import math

from controllers.base_controller import ControllerTarget
from controllers.gamepad.ai_aim import AIAimConfig, AIAimPlugin
from controllers.gamepad.state import GamepadFrame, GamepadOutput

from tests.gamepad.benchmark_metrics import (
    BENCHMARK_BODY_BOX_HEIGHT,
    BENCHMARK_BODY_BOX_WIDTH,
    BENCHMARK_SCREEN_CENTER_X,
    BENCHMARK_SCREEN_CENTER_Y,
    BENCHMARK_UPPER_BODY_RATIO,
    _count_overshoots,
    _max_overshoot_px,
    _mean_decel_settle_frames,
    _mean_turn_recovery_frames,
    _nearest_rank_percentile,
    _relative_change,
)
from tests.gamepad.benchmark_scenarios import ScenarioManifest, expand_manifest
from tests.gamepad.manual_mix_inputs import ManualMixInputConfig, ManualMixInputGenerator


@dataclass(frozen=True, slots=True)
class ManualMixMetricsConfig:
    frame_dt: float = 1.0 / 60.0
    sim_frames: int = 180
    measure_from_frame: int = 60
    max_reticle_speed_pps: float = 1500.0
    stick_max: int = 32767
    overshoot_threshold_px: float = 2.0
    turn_recovery_threshold_px: float = 6.0
    settle_threshold_px: float = 5.0
    settle_consecutive_frames: int = 4
    conflict_manual_threshold: int = 2000
    conflict_ai_threshold: int = 2000
    wrong_input_recovery_threshold_px: float = 8.0
    wrong_input_recovery_consecutive_frames: int = 3


@dataclass(frozen=True, slots=True)
class ManualMixScenarioMetrics:
    scenario_key: str
    manual_seed: int
    kind: str
    mean_error_px: float
    p95_error_px: float
    p99_error_px: float
    overshoot_events: int
    max_overshoot_px: float
    mean_recovery_frames_after_turn: float | None
    mean_settle_frames_after_decel: float | None
    conflict_frames_ratio: float
    wrong_input_recovery_frames: float | None
    manual_yield_score: float | None


@dataclass(frozen=True, slots=True)
class ManualMixAggregateMetrics:
    mean_error_px: float
    p95_error_px: float
    p99_error_px: float
    overshoot_events: int
    max_overshoot_px: float
    mean_recovery_frames_after_turn: float | None
    mean_settle_frames_after_decel: float | None
    conflict_frames_ratio: float
    wrong_input_recovery_frames: float | None
    manual_yield_score: float | None


@dataclass(frozen=True, slots=True)
class ManualMixMetricDeltas:
    mean_error_px: float | None
    p95_error_px: float | None
    p99_error_px: float | None
    overshoot_events: float | None
    max_overshoot_px: float | None
    mean_recovery_frames_after_turn: float | None
    mean_settle_frames_after_decel: float | None
    conflict_frames_ratio: float | None
    wrong_input_recovery_frames: float | None
    manual_yield_score: float | None


@dataclass(frozen=True, slots=True)
class ManualMixRunSummary:
    run_key: str
    config: ManualMixMetricsConfig
    input_config: ManualMixInputConfig
    scenario_metrics: tuple[ManualMixScenarioMetrics, ...]
    aggregate: ManualMixAggregateMetrics

    def relative_deltas(
        self,
        baseline: ManualMixAggregateMetrics | "ManualMixRunSummary",
    ) -> ManualMixMetricDeltas:
        baseline_metrics = baseline.aggregate if isinstance(baseline, ManualMixRunSummary) else baseline
        return ManualMixMetricDeltas(
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
            conflict_frames_ratio=_relative_change(
                self.aggregate.conflict_frames_ratio,
                baseline_metrics.conflict_frames_ratio,
            ),
            wrong_input_recovery_frames=_relative_change(
                self.aggregate.wrong_input_recovery_frames,
                baseline_metrics.wrong_input_recovery_frames,
            ),
            manual_yield_score=_relative_change(
                self.aggregate.manual_yield_score,
                baseline_metrics.manual_yield_score,
            ),
        )


@dataclass(frozen=True, slots=True)
class ManualMixFrameRecord:
    frame: int
    scenario_key: str
    manual_seed: int
    kind: str
    error_x: float
    error_y: float
    radial_error_px: float
    manual_x: int
    manual_y: int
    output_x: int
    output_y: int
    ai_x: int
    ai_y: int
    mode: str
    in_opposing_burst: bool
    measured: bool


def evaluate_manual_mix_scenario(
    manifest: ScenarioManifest,
    *,
    manual_seed: int,
    config: ManualMixMetricsConfig | None = None,
    input_config: ManualMixInputConfig | None = None,
    plugin_factory: Callable[[], AIAimPlugin] | None = None,
) -> ManualMixScenarioMetrics:
    config = config or ManualMixMetricsConfig()
    input_config = input_config or ManualMixInputConfig()
    records = _simulate_manual_mix_closed_loop(
        manifest,
        manual_seed=manual_seed,
        config=config,
        input_config=input_config,
        plugin_factory=plugin_factory,
    )
    measured_errors = [record.radial_error_px for record in records if record.measured]
    if not measured_errors:
        raise ValueError("measure_from_frame leaves no samples to summarize")

    return ManualMixScenarioMetrics(
        scenario_key=manifest.scenario_key,
        manual_seed=manual_seed,
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
        conflict_frames_ratio=_conflict_frames_ratio(
            records,
            min_manual=config.conflict_manual_threshold,
            min_ai=config.conflict_ai_threshold,
        ),
        wrong_input_recovery_frames=_wrong_input_recovery_frames(
            records,
            threshold_px=config.wrong_input_recovery_threshold_px,
            consecutive_frames=config.wrong_input_recovery_consecutive_frames,
        ),
        manual_yield_score=_manual_yield_score(records),
    )


def evaluate_manual_mix_run(
    run_key: str,
    manifests: Sequence[ScenarioManifest],
    *,
    manual_seeds: Sequence[int],
    config: ManualMixMetricsConfig | None = None,
    input_config: ManualMixInputConfig | None = None,
    plugin_factory: Callable[[], AIAimPlugin] | None = None,
) -> ManualMixRunSummary:
    config = config or ManualMixMetricsConfig()
    input_config = input_config or ManualMixInputConfig()
    scenario_metrics = tuple(
        evaluate_manual_mix_scenario(
            manifest,
            manual_seed=manual_seed,
            config=config,
            input_config=input_config,
            plugin_factory=plugin_factory,
        )
        for manifest in manifests
        for manual_seed in manual_seeds
    )
    return ManualMixRunSummary(
        run_key=run_key,
        config=config,
        input_config=input_config,
        scenario_metrics=scenario_metrics,
        aggregate=_aggregate_manual_mix_metrics(scenario_metrics),
    )


def _simulate_manual_mix_closed_loop(
    manifest: ScenarioManifest,
    *,
    manual_seed: int,
    config: ManualMixMetricsConfig,
    input_config: ManualMixInputConfig,
    plugin_factory: Callable[[], AIAimPlugin] | None = None,
) -> list[ManualMixFrameRecord]:
    plugin = plugin_factory() if plugin_factory is not None else AIAimPlugin(AIAimConfig())
    plugin.reset()
    generator = ManualMixInputGenerator(manifest, manual_seed=manual_seed, config=input_config)
    expanded_states = expand_manifest(manifest, config.frame_dt, config.sim_frames)

    reticle_x = 0.0
    reticle_y = 0.0
    records: list[ManualMixFrameRecord] = []

    for state in expanded_states:
        error_x = state.target_x - reticle_x
        error_y = state.target_y - reticle_y
        timestamp = state.frame * config.frame_dt
        manual_input = generator.generate_frame(
            frame=state.frame,
            error_x=error_x,
            error_y=error_y,
        )
        aim_point_x = BENCHMARK_SCREEN_CENTER_X + error_x
        aim_point_y = BENCHMARK_SCREEN_CENTER_Y + error_y
        body_top = aim_point_y - (BENCHMARK_BODY_BOX_HEIGHT * BENCHMARK_UPPER_BODY_RATIO)
        target = ControllerTarget(
            aim_point_x=aim_point_x,
            aim_point_y=aim_point_y,
            screen_center_x=BENCHMARK_SCREEN_CENTER_X,
            screen_center_y=BENCHMARK_SCREEN_CENTER_Y,
            body_box=(
                aim_point_x - (BENCHMARK_BODY_BOX_WIDTH * 0.5),
                body_top,
                aim_point_x + (BENCHMARK_BODY_BOX_WIDTH * 0.5),
                body_top + BENCHMARK_BODY_BOX_HEIGHT,
            ),
        )
        frame = GamepadFrame(
            timestamp=timestamp,
            left_x=0,
            left_y=0,
            manual_right_x=manual_input.manual_right_x,
            manual_right_y=manual_input.manual_right_y,
            left_trigger=255,
            right_trigger=0,
            buttons={},
            is_aiming=True,
            target_dx=error_x,
            target_dy=error_y,
            auto_fire_requested=False,
            target_revision=state.frame + 1,
            target_timestamp=timestamp,
            target=target,
        )
        output = GamepadOutput()
        plugin.apply(frame, output)

        output_x = _clamp_int(output.right_x, config.stick_max)
        output_y = _clamp_int(output.right_y, config.stick_max)
        ai_x = output_x - manual_input.manual_right_x
        ai_y = output_y - manual_input.manual_right_y

        reticle_x += (output_x / config.stick_max) * config.max_reticle_speed_pps * config.frame_dt
        reticle_y += (-output_y / config.stick_max) * config.max_reticle_speed_pps * config.frame_dt

        records.append(
            ManualMixFrameRecord(
                frame=state.frame,
                scenario_key=manifest.scenario_key,
                manual_seed=manual_seed,
                kind=manifest.kind,
                error_x=error_x,
                error_y=error_y,
                radial_error_px=math.hypot(error_x, error_y),
                manual_x=manual_input.manual_right_x,
                manual_y=manual_input.manual_right_y,
                output_x=output_x,
                output_y=output_y,
                ai_x=ai_x,
                ai_y=ai_y,
                mode=manual_input.mode,
                in_opposing_burst=manual_input.in_opposing_burst,
                measured=state.frame >= config.measure_from_frame,
            )
        )

    return records


def _aggregate_manual_mix_metrics(
    metrics: Sequence[ManualMixScenarioMetrics],
) -> ManualMixAggregateMetrics:
    if not metrics:
        raise ValueError("at least one scenario metric is required")

    recovery_values = [metric.mean_recovery_frames_after_turn for metric in metrics if metric.mean_recovery_frames_after_turn is not None]
    settle_values = [metric.mean_settle_frames_after_decel for metric in metrics if metric.mean_settle_frames_after_decel is not None]
    wrong_input_values = [metric.wrong_input_recovery_frames for metric in metrics if metric.wrong_input_recovery_frames is not None]
    yield_values = [metric.manual_yield_score for metric in metrics if metric.manual_yield_score is not None]

    return ManualMixAggregateMetrics(
        mean_error_px=mean(metric.mean_error_px for metric in metrics),
        p95_error_px=mean(metric.p95_error_px for metric in metrics),
        p99_error_px=mean(metric.p99_error_px for metric in metrics),
        overshoot_events=sum(metric.overshoot_events for metric in metrics),
        max_overshoot_px=max(metric.max_overshoot_px for metric in metrics),
        mean_recovery_frames_after_turn=mean(recovery_values) if recovery_values else None,
        mean_settle_frames_after_decel=mean(settle_values) if settle_values else None,
        conflict_frames_ratio=mean(metric.conflict_frames_ratio for metric in metrics),
        wrong_input_recovery_frames=mean(wrong_input_values) if wrong_input_values else None,
        manual_yield_score=mean(yield_values) if yield_values else None,
    )


def _conflict_frames_ratio(
    records: Sequence[ManualMixFrameRecord],
    *,
    min_manual: int,
    min_ai: int,
) -> float:
    measured = [record for record in records if record.measured]
    if not measured:
        return 0.0
    conflicts = [
        record
        for record in measured
        if abs(record.manual_x) >= min_manual
        and abs(record.ai_x) >= min_ai
        and _opposed(record.manual_x, record.ai_x)
    ]
    return len(conflicts) / len(measured)


def _manual_yield_score(records: Sequence[ManualMixFrameRecord]) -> float | None:
    yielded_frames = [record for record in records if record.measured and record.in_opposing_burst]
    if not yielded_frames:
        return None

    scores = [
        max(0.0, min(1.0, 1.0 - (abs(record.ai_x) / max(abs(record.manual_x), 1))))
        for record in yielded_frames
    ]
    return mean(scores)


def _wrong_input_recovery_frames(
    records: Sequence[ManualMixFrameRecord],
    *,
    threshold_px: float,
    consecutive_frames: int,
) -> float | None:
    burst_starts: list[int] = []
    previous_in_burst = False
    for record in records:
        if record.measured and record.in_opposing_burst and not previous_in_burst:
            burst_starts.append(record.frame)
        previous_in_burst = record.in_opposing_burst

    if not burst_starts:
        return None

    recovery_frames: list[float] = []
    measured_records = [record for record in records if record.measured]
    for start_frame in burst_starts:
        settle_frame = _first_recovered_frame(
            measured_records,
            start_frame,
            threshold_px=threshold_px,
            consecutive_frames=consecutive_frames,
        )
        if settle_frame is not None:
            recovery_frames.append(float(settle_frame - start_frame))
    return mean(recovery_frames) if recovery_frames else None


def _first_recovered_frame(
    records: Sequence[ManualMixFrameRecord],
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


def _opposed(lhs: int, rhs: int) -> bool:
    return (lhs > 0 and rhs < 0) or (lhs < 0 and rhs > 0)


def _clamp_int(value: int, limit: int) -> int:
    return max(-limit, min(limit, value))
