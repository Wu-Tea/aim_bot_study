from __future__ import annotations

from dataclasses import dataclass
import math
from statistics import mean
from typing import Any, Mapping, Sequence

from controllers.base_controller import ControllerTarget
from controllers.mouse.ai_aim import AIAimPlugin
from controllers.mouse.state import MouseFrame, MouseOutput

from tests.gamepad.ads_benchmark_metrics import (
    _distractor_target_state,
    _error_components,
    _expand_ads_samples,
    _sampled_ads_state_for_controller_frame,
    _target_state_by_id,
)
from tests.gamepad.ads_benchmark_scenarios import AdsScenarioManifest
from tests.gamepad.benchmark_metrics import _relative_change


SHARED_GAMEPAD_ADS_METRICS = (
    "wrong_target_snap_rate",
    "max_single_frame_camera_delta",
    "target_localization_latency_ms",
    "time_to_under_20px",
    "reacquire_time_after_occlusion",
)


@dataclass(frozen=True, slots=True)
class MouseAdsBenchmarkConfig:
    frame_dt: float = 1.0 / 60.0
    target_sample_hz: float | None = None
    sim_frames: int = 90
    response_delta_threshold_px: float = 1.0
    response_improvement_threshold_px: float = 0.5
    under_target_threshold_px: float = 20.0
    under_target_consecutive_frames: int = 2
    settle_threshold_px: float = 10.0
    settle_consecutive_frames: int = 3
    wrong_target_margin_px: float = 2.0


@dataclass(frozen=True, slots=True)
class MouseAdsScenarioMetrics:
    scenario_key: str
    family: str
    wrong_target_snap_rate: float | None
    max_single_frame_camera_delta: float
    target_localization_latency_ms: float | None
    time_to_under_20px: float | None
    time_to_stabilize_ms: float | None
    reacquire_time_after_occlusion: float | None
    settle_time_after_under_10px_ms: float | None
    under_20_escape_count: float | None
    post_under_20_axis_flip_count: float | None


@dataclass(frozen=True, slots=True)
class MouseAdsBenchmarkAggregateMetrics:
    wrong_target_snap_rate: float | None
    max_single_frame_camera_delta: float
    target_localization_latency_ms: float | None
    time_to_under_20px: float | None
    time_to_stabilize_ms: float | None
    reacquire_time_after_occlusion: float | None
    settle_time_after_under_10px_ms: float | None
    under_20_escape_count: float | None
    post_under_20_axis_flip_count: float | None


@dataclass(frozen=True, slots=True)
class MouseAdsBenchmarkMetricDeltas:
    wrong_target_snap_rate: float | None
    max_single_frame_camera_delta: float | None
    target_localization_latency_ms: float | None
    time_to_under_20px: float | None
    time_to_stabilize_ms: float | None
    reacquire_time_after_occlusion: float | None
    settle_time_after_under_10px_ms: float | None
    under_20_escape_count: float | None
    post_under_20_axis_flip_count: float | None


@dataclass(frozen=True, slots=True)
class MouseAdsBenchmarkRunSummary:
    run_key: str
    config: MouseAdsBenchmarkConfig
    scenario_metrics: tuple[MouseAdsScenarioMetrics, ...]
    aggregate: MouseAdsBenchmarkAggregateMetrics

    def relative_deltas(
        self,
        baseline: MouseAdsBenchmarkAggregateMetrics | "MouseAdsBenchmarkRunSummary",
    ) -> MouseAdsBenchmarkMetricDeltas:
        baseline_metrics = baseline.aggregate if isinstance(
            baseline,
            MouseAdsBenchmarkRunSummary,
        ) else baseline
        return MouseAdsBenchmarkMetricDeltas(
            wrong_target_snap_rate=_relative_change(
                self.aggregate.wrong_target_snap_rate,
                baseline_metrics.wrong_target_snap_rate,
            ),
            max_single_frame_camera_delta=_relative_change(
                self.aggregate.max_single_frame_camera_delta,
                baseline_metrics.max_single_frame_camera_delta,
            ),
            target_localization_latency_ms=_relative_change(
                self.aggregate.target_localization_latency_ms,
                baseline_metrics.target_localization_latency_ms,
            ),
            time_to_under_20px=_relative_change(
                self.aggregate.time_to_under_20px,
                baseline_metrics.time_to_under_20px,
            ),
            time_to_stabilize_ms=_relative_change(
                self.aggregate.time_to_stabilize_ms,
                baseline_metrics.time_to_stabilize_ms,
            ),
            reacquire_time_after_occlusion=_relative_change(
                self.aggregate.reacquire_time_after_occlusion,
                baseline_metrics.reacquire_time_after_occlusion,
            ),
            settle_time_after_under_10px_ms=_relative_change(
                self.aggregate.settle_time_after_under_10px_ms,
                baseline_metrics.settle_time_after_under_10px_ms,
            ),
            under_20_escape_count=_relative_change(
                self.aggregate.under_20_escape_count,
                baseline_metrics.under_20_escape_count,
            ),
            post_under_20_axis_flip_count=_relative_change(
                self.aggregate.post_under_20_axis_flip_count,
                baseline_metrics.post_under_20_axis_flip_count,
            ),
        )


@dataclass(frozen=True, slots=True)
class MouseAdsFrameRecord:
    frame: int
    scenario_key: str
    family: str
    localized_target_id: str | None
    controller_mode: str
    engagement_error_px_before: float | None
    engagement_error_px_after: float | None
    engagement_error_x_after: float | None
    engagement_error_y_after: float | None
    localized_error_px_before: float | None
    localized_error_px_after: float | None
    distractor_error_px_before: float | None
    distractor_error_px_after: float | None
    reticle_delta_px: float


def evaluate_mouse_ads_scenario(
    manifest: AdsScenarioManifest,
    *,
    config: MouseAdsBenchmarkConfig | None = None,
    plugin_factory: Any | None = None,
) -> MouseAdsScenarioMetrics:
    config = config or MouseAdsBenchmarkConfig()
    records = _simulate_mouse_ads_closed_loop(
        manifest,
        config=config,
        plugin_factory=plugin_factory,
    )
    first_localized = next(
        (record for record in records if record.localized_target_id is not None),
        None,
    )
    first_effective = next(
        (record for record in records if _is_effective_response(record, config)),
        None,
    )
    target_localization_latency_ms = None
    if first_localized is not None and first_effective is not None:
        target_localization_latency_ms = (
            first_effective.frame - first_localized.frame
        ) * config.frame_dt * 1000.0

    time_to_under_20px = None
    recovery_frame = _recovery_frame_after(records, start_frame=0, config=config)
    if recovery_frame is not None:
        time_to_under_20px = recovery_frame * config.frame_dt * 1000.0

    time_to_stabilize_ms = None
    first_stabilize = next(
        (record for record in records if record.controller_mode == "stabilize"),
        None,
    )
    if first_stabilize is not None:
        time_to_stabilize_ms = first_stabilize.frame * config.frame_dt * 1000.0

    reacquire_time_after_occlusion = None
    if manifest.family == "reacquire_after_gap" and manifest.targets[0].gap_windows:
        gap = manifest.targets[0].gap_windows[0]
        gap_end_frame = gap.start_frame + gap.duration_frames
        reacquire_frame = _recovery_frame_after(
            records,
            start_frame=gap_end_frame,
            config=config,
        )
        if reacquire_frame is not None:
            reacquire_time_after_occlusion = (
                reacquire_frame - gap_end_frame
            ) * config.frame_dt * 1000.0

    settle_time_after_under_10px_ms = None
    under_20_escape_count = None
    post_under_20_axis_flip_count = None
    if recovery_frame is not None:
        settle_frame = _settle_frame_after(
            records,
            start_frame=recovery_frame,
            config=config,
        )
        if settle_frame is not None:
            settle_time_after_under_10px_ms = (
                settle_frame - recovery_frame
            ) * config.frame_dt * 1000.0
        under_20_escape_count = float(
            _under_20_escape_count_after(
                records,
                start_frame=recovery_frame,
                config=config,
            )
        )
        post_under_20_axis_flip_count = float(
            _post_under_20_axis_flip_count(
                records,
                start_frame=recovery_frame,
            )
        )

    return MouseAdsScenarioMetrics(
        scenario_key=manifest.scenario_key,
        family=manifest.family,
        wrong_target_snap_rate=_wrong_target_snap_rate(records, manifest, config),
        max_single_frame_camera_delta=max(
            record.reticle_delta_px for record in records
        ) if records else 0.0,
        target_localization_latency_ms=target_localization_latency_ms,
        time_to_under_20px=time_to_under_20px,
        time_to_stabilize_ms=time_to_stabilize_ms,
        reacquire_time_after_occlusion=reacquire_time_after_occlusion,
        settle_time_after_under_10px_ms=settle_time_after_under_10px_ms,
        under_20_escape_count=under_20_escape_count,
        post_under_20_axis_flip_count=post_under_20_axis_flip_count,
    )


def evaluate_mouse_ads_run(
    run_key: str,
    manifests: Sequence[AdsScenarioManifest],
    *,
    config: MouseAdsBenchmarkConfig | None = None,
    plugin_factory: Any | None = None,
) -> MouseAdsBenchmarkRunSummary:
    config = config or MouseAdsBenchmarkConfig()
    scenario_metrics = tuple(
        evaluate_mouse_ads_scenario(
            manifest,
            config=config,
            plugin_factory=plugin_factory,
        )
        for manifest in manifests
    )
    return MouseAdsBenchmarkRunSummary(
        run_key=run_key,
        config=config,
        scenario_metrics=scenario_metrics,
        aggregate=_aggregate_mouse_ads_metrics(scenario_metrics),
    )


def compare_against_gamepad_ads(
    mouse: MouseAdsBenchmarkAggregateMetrics,
    gamepad_aggregate: Mapping[str, float | None],
) -> dict[str, Any]:
    metric_deltas = {
        metric_name: _relative_change(
            getattr(mouse, metric_name),
            gamepad_aggregate.get(metric_name),
        )
        for metric_name in SHARED_GAMEPAD_ADS_METRICS
    }
    return {
        "shared_metrics": list(SHARED_GAMEPAD_ADS_METRICS),
        "mouse_only_metrics": [
            "time_to_stabilize_ms",
            "settle_time_after_under_10px_ms",
            "under_20_escape_count",
            "post_under_20_axis_flip_count",
        ],
        "metric_deltas": metric_deltas,
    }


def _simulate_mouse_ads_closed_loop(
    manifest: AdsScenarioManifest,
    *,
    config: MouseAdsBenchmarkConfig,
    plugin_factory: Any | None = None,
) -> list[MouseAdsFrameRecord]:
    plugin = plugin_factory() if plugin_factory is not None else AIAimPlugin()
    plugin.reset()
    sampled_states, sample_dt = _expand_ads_samples(
        manifest,
        controller_frame_dt=config.frame_dt,
        sim_frames=config.sim_frames,
        target_sample_hz=config.target_sample_hz,
    )

    reticle_x = 0.0
    reticle_y = 0.0
    remainder_x = 0.0
    remainder_y = 0.0
    records: list[MouseAdsFrameRecord] = []

    for controller_frame in range(config.sim_frames):
        timestamp = controller_frame * config.frame_dt
        sample_index, target_timestamp, world_state = _sampled_ads_state_for_controller_frame(
            sampled_states,
            controller_frame=controller_frame,
            controller_frame_dt=config.frame_dt,
            sample_dt=sample_dt,
        )
        engagement_target = _target_state_by_id(
            world_state,
            manifest.engagement_target_id,
        )
        localized_target = _target_state_by_id(
            world_state,
            world_state.localized_target_id,
        )
        distractor_target = _distractor_target_state(
            world_state,
            manifest.engagement_target_id,
        )

        engagement_error_x_before, engagement_error_y_before, engagement_error_px_before = _error_components(
            engagement_target,
            reticle_x=reticle_x,
            reticle_y=reticle_y,
        )
        localized_error_x_before, localized_error_y_before, localized_error_px_before = _error_components(
            localized_target,
            reticle_x=reticle_x,
            reticle_y=reticle_y,
        )
        _, _, distractor_error_px_before = _error_components(
            distractor_target,
            reticle_x=reticle_x,
            reticle_y=reticle_y,
        )

        frame = MouseFrame(
            timestamp=timestamp,
            manual_dx=0.0,
            manual_dy=0.0,
            is_aiming=True,
            target_dx=localized_error_x_before or 0.0,
            target_dy=localized_error_y_before or 0.0,
            auto_fire_requested=False,
            target=_controller_target_for_state(
                localized_error_x_before,
                localized_error_y_before,
            ),
            target_revision=sample_index + 1,
            target_timestamp=target_timestamp,
        )
        output = MouseOutput()
        plugin.apply(frame, output)

        remainder_x += output.move_dx
        remainder_y += output.move_dy
        inject_x = int(remainder_x)
        inject_y = int(remainder_y)
        remainder_x -= inject_x
        remainder_y -= inject_y
        reticle_x += inject_x
        reticle_y += inject_y

        (
            engagement_error_x_after,
            engagement_error_y_after,
            engagement_error_px_after,
        ) = _error_components(
            engagement_target,
            reticle_x=reticle_x,
            reticle_y=reticle_y,
        )
        _, _, localized_error_px_after = _error_components(
            localized_target,
            reticle_x=reticle_x,
            reticle_y=reticle_y,
        )
        _, _, distractor_error_px_after = _error_components(
            distractor_target,
            reticle_x=reticle_x,
            reticle_y=reticle_y,
        )

        records.append(
            MouseAdsFrameRecord(
                frame=controller_frame,
                scenario_key=manifest.scenario_key,
                family=manifest.family,
                localized_target_id=world_state.localized_target_id,
                controller_mode=getattr(plugin, "_mode", "manual"),
                engagement_error_px_before=engagement_error_px_before,
                engagement_error_px_after=engagement_error_px_after,
                engagement_error_x_after=engagement_error_x_after,
                engagement_error_y_after=engagement_error_y_after,
                localized_error_px_before=localized_error_px_before,
                localized_error_px_after=localized_error_px_after,
                distractor_error_px_before=distractor_error_px_before,
                distractor_error_px_after=distractor_error_px_after,
                reticle_delta_px=math.hypot(inject_x, inject_y),
            )
        )

    return records


def _aggregate_mouse_ads_metrics(
    metrics: Sequence[MouseAdsScenarioMetrics],
) -> MouseAdsBenchmarkAggregateMetrics:
    if not metrics:
        raise ValueError("at least one mouse ADS scenario metric is required")
    return MouseAdsBenchmarkAggregateMetrics(
        wrong_target_snap_rate=_mean_non_null(
            metric.wrong_target_snap_rate for metric in metrics
        ),
        max_single_frame_camera_delta=max(
            metric.max_single_frame_camera_delta for metric in metrics
        ),
        target_localization_latency_ms=_mean_non_null(
            metric.target_localization_latency_ms for metric in metrics
        ),
        time_to_under_20px=_mean_non_null(
            metric.time_to_under_20px for metric in metrics
        ),
        time_to_stabilize_ms=_mean_non_null(
            metric.time_to_stabilize_ms for metric in metrics
        ),
        reacquire_time_after_occlusion=_mean_non_null(
            metric.reacquire_time_after_occlusion for metric in metrics
        ),
        settle_time_after_under_10px_ms=_mean_non_null(
            metric.settle_time_after_under_10px_ms for metric in metrics
        ),
        under_20_escape_count=_mean_non_null(
            metric.under_20_escape_count for metric in metrics
        ),
        post_under_20_axis_flip_count=_mean_non_null(
            metric.post_under_20_axis_flip_count for metric in metrics
        ),
    )


def _wrong_target_snap_rate(
    records: Sequence[MouseAdsFrameRecord],
    manifest: AdsScenarioManifest,
    config: MouseAdsBenchmarkConfig,
) -> float | None:
    if manifest.family != "dual_target_disambiguation":
        return None
    first_response = next(
        (record for record in records if _is_effective_response(record, config)),
        None,
    )
    if first_response is None:
        return 0.0
    if (
        first_response.localized_target_id is not None
        and first_response.localized_target_id != manifest.engagement_target_id
    ):
        return 1.0
    engagement_improvement = _improvement(
        first_response.engagement_error_px_before,
        first_response.engagement_error_px_after,
    )
    distractor_improvement = _improvement(
        first_response.distractor_error_px_before,
        first_response.distractor_error_px_after,
    )
    if distractor_improvement > (
        engagement_improvement + config.wrong_target_margin_px
    ):
        return 1.0
    return 0.0


def _recovery_frame_after(
    records: Sequence[MouseAdsFrameRecord],
    *,
    start_frame: int,
    config: MouseAdsBenchmarkConfig,
) -> int | None:
    under_target_streak = 0
    for record in records:
        if record.frame < start_frame:
            continue
        if (
            record.engagement_error_px_after is not None
            and record.engagement_error_px_after <= config.under_target_threshold_px
        ):
            under_target_streak += 1
            if under_target_streak >= config.under_target_consecutive_frames:
                return record.frame
        else:
            under_target_streak = 0
    return None


def _settle_frame_after(
    records: Sequence[MouseAdsFrameRecord],
    *,
    start_frame: int,
    config: MouseAdsBenchmarkConfig,
) -> int | None:
    settle_streak = 0
    for record in records:
        if record.frame < start_frame:
            continue
        if (
            record.engagement_error_px_after is not None
            and record.engagement_error_px_after <= config.settle_threshold_px
        ):
            settle_streak += 1
            if settle_streak >= config.settle_consecutive_frames:
                return record.frame
        else:
            settle_streak = 0
    return None


def _under_20_escape_count_after(
    records: Sequence[MouseAdsFrameRecord],
    *,
    start_frame: int,
    config: MouseAdsBenchmarkConfig,
) -> int:
    previous_under: bool | None = None
    escape_count = 0
    for record in records:
        if record.frame < start_frame or record.engagement_error_px_after is None:
            continue
        current_under = (
            record.engagement_error_px_after <= config.under_target_threshold_px
        )
        if previous_under is True and not current_under:
            escape_count += 1
        previous_under = current_under
    return escape_count


def _post_under_20_axis_flip_count(
    records: Sequence[MouseAdsFrameRecord],
    *,
    start_frame: int,
) -> int:
    x_flips = _axis_flip_count(
        (
            record.engagement_error_x_after
            for record in records
            if record.frame >= start_frame
        )
    )
    y_flips = _axis_flip_count(
        (
            record.engagement_error_y_after
            for record in records
            if record.frame >= start_frame
        )
    )
    return x_flips + y_flips


def _is_effective_response(
    record: MouseAdsFrameRecord,
    config: MouseAdsBenchmarkConfig,
) -> bool:
    return (
        record.localized_error_px_before is not None
        and record.localized_error_px_after is not None
        and record.reticle_delta_px >= config.response_delta_threshold_px
        and _improvement(
            record.localized_error_px_before,
            record.localized_error_px_after,
        ) >= config.response_improvement_threshold_px
    )


def _improvement(before: float | None, after: float | None) -> float:
    if before is None or after is None:
        return 0.0
    return before - after


def _axis_flip_count(values: Sequence[float | None] | Any) -> int:
    previous_sign = 0
    flips = 0
    for value in values:
        sign = _value_sign(value)
        if sign == 0:
            continue
        if previous_sign != 0 and sign != previous_sign:
            flips += 1
        previous_sign = sign
    return flips


def _value_sign(value: float | None) -> int:
    if value is None:
        return 0
    if value > 0.0:
        return 1
    if value < 0.0:
        return -1
    return 0


def _controller_target_for_state(
    error_x: float | None,
    error_y: float | None,
) -> ControllerTarget | None:
    if error_x is None or error_y is None:
        return None
    aim_point_x = 320.0 + error_x
    aim_point_y = 256.0 + error_y
    body_top = aim_point_y - (180.0 * 0.38)
    return ControllerTarget(
        aim_point_x=aim_point_x,
        aim_point_y=aim_point_y,
        screen_center_x=320.0,
        screen_center_y=256.0,
        body_box=(
            aim_point_x - 42.0,
            body_top,
            aim_point_x + 42.0,
            body_top + 180.0,
        ),
        target_source="observed",
    )


def _mean_non_null(values: Sequence[float | None]) -> float | None:
    filtered = [value for value in values if value is not None]
    if not filtered:
        return None
    return mean(filtered)
