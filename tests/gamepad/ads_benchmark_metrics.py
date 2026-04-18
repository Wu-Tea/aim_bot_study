from __future__ import annotations

from dataclasses import dataclass
import math
from statistics import mean
from typing import Callable, Sequence

from controllers.base_controller import ControllerTarget
from controllers.gamepad.ai_aim import AIAimConfig, AIAimPlugin
from controllers.gamepad.state import GamepadFrame, GamepadOutput

from tests.gamepad.ads_benchmark_scenarios import (
    AdsScenarioManifest,
    AdsTargetSpec,
    ExpandedAdsFrameState,
    ExpandedAdsTargetState,
    expand_ads_manifest,
)
from tests.gamepad.ads_manual_inputs import AdsManualInputConfig, AdsManualInputGenerator
from tests.gamepad.benchmark_metrics import (
    BENCHMARK_BODY_BOX_HEIGHT,
    BENCHMARK_BODY_BOX_WIDTH,
    BENCHMARK_SCREEN_CENTER_X,
    BENCHMARK_SCREEN_CENTER_Y,
    BENCHMARK_UPPER_BODY_RATIO,
    _relative_change,
)


DEFAULT_ADS_INPUT_PROFILES = ("none", "aligned_follow", "opposing_burst", "overshoot_recover")
_TARGET_SAMPLE_EPSILON = 1e-9


@dataclass(frozen=True, slots=True)
class AdsBenchmarkConfig:
    frame_dt: float = 1.0 / 60.0
    target_sample_hz: float | None = None
    sim_frames: int = 90
    max_reticle_speed_pps: float = 1500.0
    stick_max: int = 32767
    response_delta_threshold_px: float = 1.0
    response_improvement_threshold_px: float = 0.5
    under_target_threshold_px: float = 20.0
    under_target_consecutive_frames: int = 2
    lock_loss_window_frames: int = 12
    lock_loss_grace_frames: int = 2
    wrong_target_margin_px: float = 2.0


@dataclass(frozen=True, slots=True)
class AdsBenchmarkScenarioMetrics:
    scenario_case_key: str
    scenario_key: str
    input_profile: str
    family: str
    wrong_target_snap_rate: float | None
    max_single_frame_camera_delta: float
    lock_loss_after_ads_rate: float | None
    target_localization_latency_ms: float | None
    time_to_under_20px: float | None
    time_to_body_lock: float | None
    reacquire_time_after_occlusion: float | None
    harmful_input_suppression_during_ads: float | None
    wrong_input_recovery_after_ads_frames: float | None


@dataclass(frozen=True, slots=True)
class AdsBenchmarkAggregateMetrics:
    wrong_target_snap_rate: float | None
    max_single_frame_camera_delta: float
    lock_loss_after_ads_rate: float | None
    target_localization_latency_ms: float | None
    time_to_under_20px: float | None
    time_to_body_lock: float | None
    reacquire_time_after_occlusion: float | None
    harmful_input_suppression_during_ads: float | None
    wrong_input_recovery_after_ads_frames: float | None


@dataclass(frozen=True, slots=True)
class AdsBenchmarkMetricDeltas:
    wrong_target_snap_rate: float | None
    max_single_frame_camera_delta: float | None
    lock_loss_after_ads_rate: float | None
    target_localization_latency_ms: float | None
    time_to_under_20px: float | None
    time_to_body_lock: float | None
    reacquire_time_after_occlusion: float | None
    harmful_input_suppression_during_ads: float | None
    wrong_input_recovery_after_ads_frames: float | None


@dataclass(frozen=True, slots=True)
class AdsBenchmarkRunSummary:
    run_key: str
    config: AdsBenchmarkConfig
    input_config: AdsManualInputConfig
    scenario_metrics: tuple[AdsBenchmarkScenarioMetrics, ...]
    aggregate: AdsBenchmarkAggregateMetrics

    def relative_deltas(
        self,
        baseline: AdsBenchmarkAggregateMetrics | "AdsBenchmarkRunSummary",
    ) -> AdsBenchmarkMetricDeltas:
        baseline_metrics = baseline.aggregate if isinstance(baseline, AdsBenchmarkRunSummary) else baseline
        return AdsBenchmarkMetricDeltas(
            wrong_target_snap_rate=_relative_change(
                self.aggregate.wrong_target_snap_rate,
                baseline_metrics.wrong_target_snap_rate,
            ),
            max_single_frame_camera_delta=_relative_change(
                self.aggregate.max_single_frame_camera_delta,
                baseline_metrics.max_single_frame_camera_delta,
            ),
            lock_loss_after_ads_rate=_relative_change(
                self.aggregate.lock_loss_after_ads_rate,
                baseline_metrics.lock_loss_after_ads_rate,
            ),
            target_localization_latency_ms=_relative_change(
                self.aggregate.target_localization_latency_ms,
                baseline_metrics.target_localization_latency_ms,
            ),
            time_to_under_20px=_relative_change(
                self.aggregate.time_to_under_20px,
                baseline_metrics.time_to_under_20px,
            ),
            time_to_body_lock=_relative_change(
                self.aggregate.time_to_body_lock,
                baseline_metrics.time_to_body_lock,
            ),
            reacquire_time_after_occlusion=_relative_change(
                self.aggregate.reacquire_time_after_occlusion,
                baseline_metrics.reacquire_time_after_occlusion,
            ),
            harmful_input_suppression_during_ads=_relative_change(
                self.aggregate.harmful_input_suppression_during_ads,
                baseline_metrics.harmful_input_suppression_during_ads,
            ),
            wrong_input_recovery_after_ads_frames=_relative_change(
                self.aggregate.wrong_input_recovery_after_ads_frames,
                baseline_metrics.wrong_input_recovery_after_ads_frames,
            ),
        )


@dataclass(frozen=True, slots=True)
class AdsFrameRecord:
    frame: int
    timestamp: float
    scenario_key: str
    input_profile: str
    family: str
    localized_target_id: str | None
    controller_mode: str
    engagement_visible: bool
    engagement_error_x_before: float | None
    engagement_error_y_before: float | None
    engagement_error_px_before: float | None
    engagement_error_x_after: float | None
    engagement_error_y_after: float | None
    engagement_error_px_after: float | None
    localized_error_px_before: float | None
    localized_error_px_after: float | None
    distractor_error_px_before: float | None
    distractor_error_px_after: float | None
    reticle_delta_px: float
    manual_x: int
    manual_y: int
    output_x: int
    output_y: int
    ai_x: int
    ai_y: int
    in_opposing_burst: bool
    in_recovery_window: bool


def evaluate_ads_scenario(
    manifest: AdsScenarioManifest,
    *,
    input_profile: str,
    config: AdsBenchmarkConfig | None = None,
    input_config: AdsManualInputConfig | None = None,
    plugin_factory: Callable[[], AIAimPlugin] | None = None,
) -> AdsBenchmarkScenarioMetrics:
    config = config or AdsBenchmarkConfig()
    input_config = input_config or AdsManualInputConfig()
    records = _simulate_ads_closed_loop(
        manifest,
        input_profile=input_profile,
        config=config,
        input_config=input_config,
        plugin_factory=plugin_factory,
    )
    return AdsBenchmarkScenarioMetrics(
        scenario_case_key=f"{manifest.scenario_key}@{input_profile}",
        scenario_key=manifest.scenario_key,
        input_profile=input_profile,
        family=manifest.family,
        wrong_target_snap_rate=_wrong_target_snap_rate(records, manifest, config),
        max_single_frame_camera_delta=max(record.reticle_delta_px for record in records) if records else 0.0,
        lock_loss_after_ads_rate=_lock_loss_after_ads_rate(records, config, plugin_factory),
        target_localization_latency_ms=_target_localization_latency_ms(records, config),
        time_to_under_20px=_time_to_under_threshold_ms(records, config, start_frame=0),
        time_to_body_lock=_time_to_body_lock_ms(records),
        reacquire_time_after_occlusion=_reacquire_time_after_occlusion_ms(records, manifest, config),
        harmful_input_suppression_during_ads=_harmful_input_suppression_during_ads(records),
        wrong_input_recovery_after_ads_frames=_wrong_input_recovery_after_ads_frames(records, config),
    )


def evaluate_ads_run(
    run_key: str,
    manifests: Sequence[AdsScenarioManifest],
    *,
    input_profiles: Sequence[str] = DEFAULT_ADS_INPUT_PROFILES,
    config: AdsBenchmarkConfig | None = None,
    input_config: AdsManualInputConfig | None = None,
    plugin_factory: Callable[[], AIAimPlugin] | None = None,
) -> AdsBenchmarkRunSummary:
    config = config or AdsBenchmarkConfig()
    input_config = input_config or AdsManualInputConfig()
    scenario_metrics = tuple(
        evaluate_ads_scenario(
            manifest,
            input_profile=input_profile,
            config=config,
            input_config=input_config,
            plugin_factory=plugin_factory,
        )
        for manifest in manifests
        for input_profile in input_profiles
    )
    return AdsBenchmarkRunSummary(
        run_key=run_key,
        config=config,
        input_config=input_config,
        scenario_metrics=scenario_metrics,
        aggregate=_aggregate_ads_metrics(scenario_metrics),
    )


def _simulate_ads_closed_loop(
    manifest: AdsScenarioManifest,
    *,
    input_profile: str,
    config: AdsBenchmarkConfig,
    input_config: AdsManualInputConfig,
    plugin_factory: Callable[[], AIAimPlugin] | None = None,
) -> list[AdsFrameRecord]:
    plugin = plugin_factory() if plugin_factory is not None else AIAimPlugin(AIAimConfig())
    plugin.reset()
    generator = AdsManualInputGenerator(manifest, input_profile=input_profile, config=input_config)
    sampled_states, sample_dt = _expand_ads_samples(
        manifest,
        controller_frame_dt=config.frame_dt,
        sim_frames=config.sim_frames,
        target_sample_hz=config.target_sample_hz,
    )

    reticle_x = 0.0
    reticle_y = 0.0
    records: list[AdsFrameRecord] = []

    for controller_frame in range(config.sim_frames):
        timestamp = controller_frame * config.frame_dt
        sample_index, target_timestamp, world_state = _sampled_ads_state_for_controller_frame(
            sampled_states,
            controller_frame=controller_frame,
            controller_frame_dt=config.frame_dt,
            sample_dt=sample_dt,
        )

        engagement_target = _target_state_by_id(world_state, manifest.engagement_target_id)
        localized_target = _target_state_by_id(world_state, world_state.localized_target_id)
        distractor_target = _distractor_target_state(world_state, manifest.engagement_target_id)

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

        manual_input = generator.generate_frame(
            frame=controller_frame,
            error_x=engagement_error_x_before or 0.0,
            error_y=engagement_error_y_before or 0.0,
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
            target_dx=localized_error_x_before or 0.0,
            target_dy=localized_error_y_before or 0.0,
            auto_fire_requested=False,
            target_revision=sample_index + 1,
            target_timestamp=target_timestamp,
            target=_controller_target_for_state(localized_error_x_before, localized_error_y_before),
        )
        output = GamepadOutput()
        plugin.apply(frame, output)

        output_x = _clamp_int(output.right_x, config.stick_max)
        output_y = _clamp_int(output.right_y, config.stick_max)
        ai_x = _clamp_int(output_x - manual_input.manual_right_x, config.stick_max)
        ai_y = _clamp_int(output_y - manual_input.manual_right_y, config.stick_max)

        reticle_delta_x = (output_x / config.stick_max) * config.max_reticle_speed_pps * config.frame_dt
        reticle_delta_y = (-output_y / config.stick_max) * config.max_reticle_speed_pps * config.frame_dt
        reticle_x += reticle_delta_x
        reticle_y += reticle_delta_y

        engagement_error_x_after, engagement_error_y_after, engagement_error_px_after = _error_components(
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
            AdsFrameRecord(
                frame=controller_frame,
                timestamp=timestamp,
                scenario_key=manifest.scenario_key,
                input_profile=input_profile,
                family=manifest.family,
                localized_target_id=world_state.localized_target_id,
                controller_mode=getattr(plugin, "_mode", "manual"),
                engagement_visible=engagement_target is not None and engagement_target.visible,
                engagement_error_x_before=engagement_error_x_before,
                engagement_error_y_before=engagement_error_y_before,
                engagement_error_px_before=engagement_error_px_before,
                engagement_error_x_after=engagement_error_x_after,
                engagement_error_y_after=engagement_error_y_after,
                engagement_error_px_after=engagement_error_px_after,
                localized_error_px_before=localized_error_px_before,
                localized_error_px_after=localized_error_px_after,
                distractor_error_px_before=distractor_error_px_before,
                distractor_error_px_after=distractor_error_px_after,
                reticle_delta_px=math.hypot(reticle_delta_x, reticle_delta_y),
                manual_x=manual_input.manual_right_x,
                manual_y=manual_input.manual_right_y,
                output_x=output_x,
                output_y=output_y,
                ai_x=ai_x,
                ai_y=ai_y,
                in_opposing_burst=manual_input.in_opposing_burst,
                in_recovery_window=manual_input.in_recovery_window,
            )
        )

    return records


def _aggregate_ads_metrics(
    metrics: Sequence[AdsBenchmarkScenarioMetrics],
) -> AdsBenchmarkAggregateMetrics:
    if not metrics:
        raise ValueError("at least one scenario metric is required")
    return AdsBenchmarkAggregateMetrics(
        wrong_target_snap_rate=_mean_non_null(metric.wrong_target_snap_rate for metric in metrics),
        max_single_frame_camera_delta=max(metric.max_single_frame_camera_delta for metric in metrics),
        lock_loss_after_ads_rate=_mean_non_null(metric.lock_loss_after_ads_rate for metric in metrics),
        target_localization_latency_ms=_mean_non_null(metric.target_localization_latency_ms for metric in metrics),
        time_to_under_20px=_mean_non_null(metric.time_to_under_20px for metric in metrics),
        time_to_body_lock=_mean_non_null(metric.time_to_body_lock for metric in metrics),
        reacquire_time_after_occlusion=_mean_non_null(metric.reacquire_time_after_occlusion for metric in metrics),
        harmful_input_suppression_during_ads=_mean_non_null(
            metric.harmful_input_suppression_during_ads for metric in metrics
        ),
        wrong_input_recovery_after_ads_frames=_mean_non_null(
            metric.wrong_input_recovery_after_ads_frames for metric in metrics
        ),
    )


def _wrong_target_snap_rate(
    records: Sequence[AdsFrameRecord],
    manifest: AdsScenarioManifest,
    config: AdsBenchmarkConfig,
) -> float | None:
    if manifest.family != "dual_target_disambiguation":
        return None
    for record in records:
        if record.controller_mode != "ads_snap":
            continue
        if not _is_effective_response(record, config):
            continue
        if (
            record.localized_target_id is not None
            and record.localized_target_id != manifest.engagement_target_id
        ):
            return 1.0
        engagement_improvement = _improvement(record.engagement_error_px_before, record.engagement_error_px_after)
        distractor_improvement = _improvement(record.distractor_error_px_before, record.distractor_error_px_after)
        if distractor_improvement > (engagement_improvement + config.wrong_target_margin_px):
            return 1.0
        return 0.0
    return 0.0


def _lock_loss_after_ads_rate(
    records: Sequence[AdsFrameRecord],
    config: AdsBenchmarkConfig,
    plugin_factory: Callable[[], AIAimPlugin] | None,
) -> float | None:
    first_lock_index = next((index for index, record in enumerate(records) if record.controller_mode == "body_lock"), None)
    if first_lock_index is None:
        return None
    activation_half = _body_lock_activation_half(plugin_factory)
    missed = 0
    for record in records[first_lock_index + 1:first_lock_index + 1 + config.lock_loss_window_frames]:
        if not record.engagement_visible:
            continue
        if record.engagement_error_x_after is None or record.engagement_error_y_after is None:
            continue
        if abs(record.engagement_error_x_after) > activation_half or abs(record.engagement_error_y_after) > activation_half:
            continue
        if record.controller_mode != "body_lock":
            missed += 1
            if missed > config.lock_loss_grace_frames:
                return 1.0
        else:
            missed = 0
    return 0.0


def _target_localization_latency_ms(
    records: Sequence[AdsFrameRecord],
    config: AdsBenchmarkConfig,
) -> float | None:
    first_localized = next((record for record in records if record.localized_target_id is not None), None)
    if first_localized is None:
        return None
    first_response = next((record for record in records if _is_effective_response(record, config)), None)
    if first_response is None:
        return None
    return (first_response.timestamp - first_localized.timestamp) * 1000.0


def _time_to_under_threshold_ms(
    records: Sequence[AdsFrameRecord],
    config: AdsBenchmarkConfig,
    *,
    start_frame: int,
) -> float | None:
    recovery_frame = _recovery_frame_after(records, start_frame=start_frame, config=config)
    if recovery_frame is None:
        return None
    return recovery_frame * config.frame_dt * 1000.0


def _time_to_body_lock_ms(records: Sequence[AdsFrameRecord]) -> float | None:
    first_lock = next((record for record in records if record.controller_mode == "body_lock"), None)
    if first_lock is None:
        return None
    return first_lock.timestamp * 1000.0


def _reacquire_time_after_occlusion_ms(
    records: Sequence[AdsFrameRecord],
    manifest: AdsScenarioManifest,
    config: AdsBenchmarkConfig,
) -> float | None:
    if manifest.family != "reacquire_after_gap":
        return None
    target = manifest.targets[0]
    if not target.gap_windows:
        return None
    gap_end_frame = target.gap_windows[0].start_frame + target.gap_windows[0].duration_frames
    recovery_frame = _recovery_frame_after(records, start_frame=gap_end_frame, config=config)
    if recovery_frame is None:
        return None
    return (recovery_frame - gap_end_frame) * config.frame_dt * 1000.0


def _harmful_input_suppression_during_ads(records: Sequence[AdsFrameRecord]) -> float | None:
    scores: list[float] = []
    for record in records:
        if record.controller_mode != "ads_snap":
            continue
        if record.engagement_error_x_before is None or record.engagement_error_y_before is None:
            continue
        harmful_manual, harmful_output = _harmful_components(record)
        if harmful_manual <= 0.0:
            continue
        scores.append(max(0.0, min(1.0, (harmful_manual - harmful_output) / max(harmful_manual, 1.0))))
    if not scores:
        return None
    return mean(scores)


def _wrong_input_recovery_after_ads_frames(
    records: Sequence[AdsFrameRecord],
    config: AdsBenchmarkConfig,
) -> float | None:
    ads_snap_end = max((record.frame for record in records if record.controller_mode == "ads_snap"), default=None)
    for index, record in enumerate(records):
        previous_in_burst = records[index - 1].in_opposing_burst if index > 0 else False
        if not record.in_opposing_burst or previous_in_burst:
            continue
        eligible = record.controller_mode == "ads_snap"
        if not eligible and ads_snap_end is not None:
            eligible = record.frame <= (ads_snap_end + 8)
        if not eligible:
            continue
        recovery_frame = _recovery_frame_after(records, start_frame=record.frame, config=config)
        if recovery_frame is None:
            return None
        return float(recovery_frame - record.frame)
    return None


def _recovery_frame_after(
    records: Sequence[AdsFrameRecord],
    *,
    start_frame: int,
    config: AdsBenchmarkConfig,
) -> int | None:
    under_target_streak = 0
    body_lock_streak = 0
    for record in records:
        if record.frame < start_frame:
            continue
        if record.engagement_error_px_after is not None and record.engagement_error_px_after <= config.under_target_threshold_px:
            under_target_streak += 1
            if under_target_streak >= config.under_target_consecutive_frames:
                return record.frame
        else:
            under_target_streak = 0

        if record.engagement_visible and record.controller_mode == "body_lock":
            body_lock_streak += 1
            if body_lock_streak >= 2:
                return record.frame
        else:
            body_lock_streak = 0
    return None


def _is_effective_response(record: AdsFrameRecord, config: AdsBenchmarkConfig) -> bool:
    return (
        record.localized_error_px_before is not None
        and record.localized_error_px_after is not None
        and record.reticle_delta_px >= config.response_delta_threshold_px
        and _improvement(record.localized_error_px_before, record.localized_error_px_after) >= config.response_improvement_threshold_px
    )


def _improvement(before: float | None, after: float | None) -> float:
    if before is None or after is None:
        return 0.0
    return before - after


def _harmful_components(record: AdsFrameRecord) -> tuple[float, float]:
    desired_x = record.engagement_error_x_before or 0.0
    desired_y = -(record.engagement_error_y_before or 0.0)
    magnitude = math.hypot(desired_x, desired_y)
    if magnitude <= 1e-6:
        return 0.0, 0.0
    ux = desired_x / magnitude
    uy = desired_y / magnitude
    manual_projection = (record.manual_x * ux) + (record.manual_y * uy)
    output_projection = (record.output_x * ux) + (record.output_y * uy)
    return max(0.0, -manual_projection), max(0.0, -output_projection)


def _body_lock_activation_half(plugin_factory: Callable[[], AIAimPlugin] | None) -> float:
    if plugin_factory is not None:
        plugin = plugin_factory()
        activation_box = getattr(getattr(plugin, "config", None), "body_lock_activation_box_px", 150.0)
        return max(1.0, activation_box * 0.5)
    return 75.0


def _expand_ads_samples(
    manifest: AdsScenarioManifest,
    *,
    controller_frame_dt: float,
    sim_frames: int,
    target_sample_hz: float | None,
) -> tuple[tuple[ExpandedAdsFrameState, ...], float]:
    sample_dt = _target_sample_dt(controller_frame_dt, target_sample_hz)
    sample_manifest = _manifest_resampled_for_frame_dt(
        manifest,
        source_frame_dt=controller_frame_dt,
        target_frame_dt=sample_dt,
    )
    sample_frames = _target_sample_frame_count(
        controller_frame_dt=controller_frame_dt,
        sim_frames=sim_frames,
        sample_dt=sample_dt,
    )
    return expand_ads_manifest(sample_manifest, sample_dt, sample_frames), sample_dt


def _sampled_ads_state_for_controller_frame(
    sampled_states: Sequence[ExpandedAdsFrameState],
    *,
    controller_frame: int,
    controller_frame_dt: float,
    sample_dt: float,
) -> tuple[int, float, ExpandedAdsFrameState]:
    if not sampled_states:
        raise ValueError("sampled_states must not be empty when sim_frames is positive")
    controller_timestamp = controller_frame * controller_frame_dt
    sample_index = min(
        int(math.floor((controller_timestamp / sample_dt) + _TARGET_SAMPLE_EPSILON)),
        len(sampled_states) - 1,
    )
    return sample_index, sample_index * sample_dt, sampled_states[sample_index]


def _manifest_resampled_for_frame_dt(
    manifest: AdsScenarioManifest,
    *,
    source_frame_dt: float,
    target_frame_dt: float,
) -> AdsScenarioManifest:
    if math.isclose(source_frame_dt, target_frame_dt, rel_tol=0.0, abs_tol=_TARGET_SAMPLE_EPSILON):
        return manifest
    return AdsScenarioManifest(
        scenario_key=manifest.scenario_key,
        family=manifest.family,
        engagement_target_id=manifest.engagement_target_id,
        targets=tuple(
            AdsTargetSpec(
                target_id=target.target_id,
                initial_dx=target.initial_dx,
                initial_dy=target.initial_dy,
                velocity_x=target.velocity_x,
                velocity_y=target.velocity_y,
                decel_start_frame=(
                    None
                    if target.decel_start_frame is None
                    else _resampled_frame_index(target.decel_start_frame, source_frame_dt, target_frame_dt)
                ),
                decel_duration_frames=(
                    0
                    if target.decel_start_frame is None
                    else _resampled_duration_frames(target.decel_duration_frames, source_frame_dt, target_frame_dt)
                ),
                decel_target_speed_scale=target.decel_target_speed_scale,
                visible_start_frame=_resampled_frame_index(
                    target.visible_start_frame,
                    source_frame_dt,
                    target_frame_dt,
                ),
                visible_end_frame=(
                    None
                    if target.visible_end_frame is None
                    else _resampled_frame_index(target.visible_end_frame, source_frame_dt, target_frame_dt)
                ),
                gap_windows=tuple(
                    type(gap)(
                        start_frame=_resampled_frame_index(gap.start_frame, source_frame_dt, target_frame_dt),
                        duration_frames=_resampled_duration_frames(gap.duration_frames, source_frame_dt, target_frame_dt),
                    )
                    for gap in target.gap_windows
                ),
            )
            for target in manifest.targets
        ),
        localization_schedule=tuple(
            type(event)(
                frame=_resampled_frame_index(event.frame, source_frame_dt, target_frame_dt),
                target_id=event.target_id,
            )
            for event in manifest.localization_schedule
        ),
    )


def _target_sample_dt(controller_frame_dt: float, target_sample_hz: float | None) -> float:
    if target_sample_hz is None:
        return controller_frame_dt
    if target_sample_hz <= 0.0:
        raise ValueError("target_sample_hz must be positive when provided")
    return 1.0 / target_sample_hz


def _target_sample_frame_count(
    *,
    controller_frame_dt: float,
    sim_frames: int,
    sample_dt: float,
) -> int:
    if sim_frames <= 0:
        return 0
    last_controller_timestamp = (sim_frames - 1) * controller_frame_dt
    return int(math.floor((last_controller_timestamp / sample_dt) + _TARGET_SAMPLE_EPSILON)) + 1


def _resampled_frame_index(frame: int, source_frame_dt: float, target_frame_dt: float) -> int:
    event_timestamp = frame * source_frame_dt
    return max(0, int(math.ceil((event_timestamp / target_frame_dt) - _TARGET_SAMPLE_EPSILON)))


def _resampled_duration_frames(duration_frames: int, source_frame_dt: float, target_frame_dt: float) -> int:
    duration_seconds = duration_frames * source_frame_dt
    return max(1, int(round(duration_seconds / target_frame_dt)))


def _target_state_by_id(
    world_state: ExpandedAdsFrameState,
    target_id: str | None,
) -> ExpandedAdsTargetState | None:
    if target_id is None:
        return None
    for target in world_state.targets:
        if target.target_id == target_id:
            return target
    return None


def _distractor_target_state(
    world_state: ExpandedAdsFrameState,
    engagement_target_id: str,
) -> ExpandedAdsTargetState | None:
    for target in world_state.targets:
        if target.target_id != engagement_target_id:
            return target
    return None


def _error_components(
    target: ExpandedAdsTargetState | None,
    *,
    reticle_x: float,
    reticle_y: float,
) -> tuple[float | None, float | None, float | None]:
    if target is None or not target.visible:
        return None, None, None
    error_x = target.target_x - reticle_x
    error_y = target.target_y - reticle_y
    return error_x, error_y, math.hypot(error_x, error_y)


def _controller_target_for_state(
    error_x: float | None,
    error_y: float | None,
) -> ControllerTarget | None:
    if error_x is None or error_y is None:
        return None
    aim_point_x = BENCHMARK_SCREEN_CENTER_X + error_x
    aim_point_y = BENCHMARK_SCREEN_CENTER_Y + error_y
    body_top = aim_point_y - (BENCHMARK_BODY_BOX_HEIGHT * BENCHMARK_UPPER_BODY_RATIO)
    return ControllerTarget(
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


def _mean_non_null(values: Sequence[float | None]) -> float | None:
    filtered = [value for value in values if value is not None]
    if not filtered:
        return None
    return mean(filtered)


def _clamp_int(value: float, stick_max: int) -> int:
    return int(max(-stick_max, min(stick_max, round(value))))
