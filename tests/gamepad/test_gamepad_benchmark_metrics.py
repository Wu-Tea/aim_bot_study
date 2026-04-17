import unittest

from controllers.gamepad.adaptive_delta_gain import AdaptiveDeltaGainConfig
from controllers.gamepad.ai_aim import (
    AIAimConfig,
    AIAimPlugin,
    AdaptiveDeltaGainSubPlugin,
    ManualIntentGuardSubPlugin,
)
from controllers.gamepad.horizontal_assist import HorizontalAimAssist, HorizontalAimAssistConfig
from controllers.gamepad.manual_intent_guard import ManualIntentGuardConfig
from controllers.gamepad.overshoot_guard import OvershootGuard, OvershootGuardConfig
from controllers.gamepad.state import GamepadOutput

from tests.gamepad.benchmark_scenarios import (
    DecelEvent,
    ExpandedTargetState,
    InitialState,
    ScenarioManifest,
    TurnEvent,
    expand_manifest,
    generate_phase1_manifests,
)

from tests.gamepad.benchmark_metrics import (
    BenchmarkAggregateMetrics,
    BenchmarkMetricsConfig,
    BenchmarkMetricDeltas,
    BenchmarkRunSummary,
    BenchmarkScenarioMetrics,
    _FrameRecord,
    _axis_overshoot_count,
    _max_overshoot_px,
    _mean_decel_settle_frames,
    _mean_turn_recovery_frames,
    evaluate_run,
    evaluate_scenario,
    _simulate_closed_loop,
)


class _HorizontalAssistConfigurableSubPlugin:
    def __init__(self, config: HorizontalAimAssistConfig):
        self.assist = HorizontalAimAssist(config)

    def reset(self):
        self.assist.reset()

    def observe_target(self, *, target_dx, target_dy, is_aiming, timestamp):
        self.assist.observe_target(
            target_dx=target_dx,
            is_aiming=is_aiming,
            timestamp=timestamp,
        )

    def apply(self, context):
        feedforward_dx, x_force_bonus = self.assist.compute_adjustment(int(context.manual_rx))
        context.assist_dx += feedforward_dx
        context.x_force_bonus += x_force_bonus


class _OvershootGuardConfigurableSubPlugin:
    def __init__(self, config: OvershootGuardConfig):
        self.guard = OvershootGuard(config)

    def reset(self):
        self.guard.reset()

    def observe_target(self, *, target_dx, target_dy, is_aiming, timestamp):
        self.guard.observe_target(
            target_dx=target_dx,
            target_dy=target_dy,
            is_aiming=is_aiming,
            timestamp=timestamp,
        )

    def apply(self, context):
        adjustment = self.guard.compute_adjustment(
            int(context.manual_rx),
            int(context.manual_ry),
            context.timestamp,
        )
        context.x_desired_scale *= adjustment.x_desired_scale
        context.y_desired_scale *= adjustment.y_desired_scale
        context.x_carry_scale *= adjustment.x_carry_scale
        context.y_carry_scale *= adjustment.y_carry_scale


LEGACY_AI_AIM_CONFIG = AIAimConfig(
    smoothing=0.65,
    max_pixels=130,
    piecewise_mid_pixels=60.0,
    piecewise_max_pixels=230.0,
    piecewise_mid_ratio=0.5,
    piecewise_mid_pixels_y=45.0,
    piecewise_max_pixels_y=180.0,
    piecewise_mid_ratio_y=0.65,
    invert_x=False,
    invert_y=False,
    max_ai_force=0.6,
    max_ai_force_y=0.8,
    deadzone_inner=1.5,
    deadzone_outer=5.0,
    x_deadzone_outer=3.0,
    ai_fade_full=8000,
    ai_delta_gain=0.7,
)
LEGACY_ADAPTIVE_CONFIG = AdaptiveDeltaGainConfig(
    min_error_px=6.0,
    convergence_epsilon_px=0.5,
    trigger_frames=3,
    gain_per_update=0.08,
    decay_per_update=0.12,
    max_bonus=0.6,
    opposing_input_threshold=4500,
    stale_seconds=0.15,
)
LEGACY_HORIZONTAL_CONFIG = HorizontalAimAssistConfig(
    min_error_px=4.0,
    min_velocity_px_per_sec=60.0,
    velocity_filter_alpha=0.45,
    feedforward_lead_seconds=0.02,
    feedforward_gain=0.65,
    max_feedforward_px=6.0,
    catchup_trigger_frames=3,
    catchup_gain_per_update=0.02,
    catchup_max_bonus=0.10,
    catchup_decay=0.04,
    opposing_input_threshold=5000,
    convergence_epsilon_px=0.25,
)
LEGACY_OVERSHOOT_CONFIG = OvershootGuardConfig(
    manual_input_threshold=3500,
    near_error_px=8.0,
    release_error_px=22.0,
    convergence_epsilon_px=0.25,
    convergence_trigger_frames=2,
    convergence_build_per_update=0.22,
    convergence_max_guard=0.50,
    convergence_decay=0.18,
    zero_cross_arm_px=6.0,
    zero_cross_hold_seconds=0.04,
    zero_cross_guard=0.85,
    carry_damp_gain=1.0,
)
MANUAL_INTENT_CONFIG = ManualIntentGuardConfig()


def _legacy_plugin_factory():
    return AIAimPlugin(
        LEGACY_AI_AIM_CONFIG,
        sub_plugins=(
            ManualIntentGuardSubPlugin(MANUAL_INTENT_CONFIG),
            AdaptiveDeltaGainSubPlugin(LEGACY_ADAPTIVE_CONFIG),
            _HorizontalAssistConfigurableSubPlugin(LEGACY_HORIZONTAL_CONFIG),
            _OvershootGuardConfigurableSubPlugin(LEGACY_OVERSHOOT_CONFIG),
        ),
    )


class BenchmarkMetricsContractTests(unittest.TestCase):
    def test_metric_dataclasses_expose_expected_fields(self):
        self.assertEqual(
            list(BenchmarkMetricsConfig.__dataclass_fields__),
            [
                "frame_dt",
                "target_sample_hz",
                "sim_frames",
                "measure_from_frame",
                "max_reticle_speed_pps",
                "stick_max",
                "overshoot_threshold_px",
                "turn_recovery_threshold_px",
                "settle_threshold_px",
                "settle_consecutive_frames",
            ],
        )
        self.assertEqual(
            set(BenchmarkScenarioMetrics.__dataclass_fields__),
            {
                "scenario_key",
                "kind",
                "mean_error_px",
                "p95_error_px",
                "p99_error_px",
                "overshoot_events",
                "max_overshoot_px",
                "mean_recovery_frames_after_turn",
                "mean_settle_frames_after_decel",
            },
        )
        self.assertEqual(
            set(BenchmarkAggregateMetrics.__dataclass_fields__),
            {
                "mean_error_px",
                "p95_error_px",
                "p99_error_px",
                "overshoot_events",
                "max_overshoot_px",
                "mean_recovery_frames_after_turn",
                "mean_settle_frames_after_decel",
            },
        )
        self.assertEqual(
            set(BenchmarkMetricDeltas.__dataclass_fields__),
            {
                "mean_error_px",
                "p95_error_px",
                "p99_error_px",
                "overshoot_events",
                "max_overshoot_px",
                "mean_recovery_frames_after_turn",
                "mean_settle_frames_after_decel",
            },
        )

    def test_default_config_matches_phase1_spec(self):
        config = BenchmarkMetricsConfig()

        self.assertEqual(config.frame_dt, 1.0 / 60.0)
        self.assertIsNone(config.target_sample_hz)
        self.assertEqual(config.sim_frames, 180)
        self.assertEqual(config.measure_from_frame, 60)
        self.assertEqual(config.max_reticle_speed_pps, 1500.0)
        self.assertEqual(config.stick_max, 32767)
        self.assertEqual(config.overshoot_threshold_px, 2.0)
        self.assertEqual(config.turn_recovery_threshold_px, 6.0)
        self.assertEqual(config.settle_threshold_px, 5.0)
        self.assertEqual(config.settle_consecutive_frames, 4)


class BenchmarkScenarioEvaluationTests(unittest.TestCase):
    class _RecordingPlugin:
        def __init__(self):
            self.frames = []

        def reset(self):
            self.frames.clear()

        def apply(self, frame, output):
            self.frames.append(frame)
            if len(self.frames) == 1:
                output.right_x = 32767
                output.right_y = 0

    def make_record(self, frame, *, radial_error_px, error_x=None, error_y=0.0):
        error_x = radial_error_px if error_x is None else error_x
        return _FrameRecord(
            frame=frame,
            target_x=0.0,
            target_y=0.0,
            reticle_x=0.0,
            reticle_y=0.0,
            error_x=error_x,
            error_y=error_y,
            radial_error_px=radial_error_px,
            stick_x=0,
            stick_y=0,
        )

    def test_closed_loop_target_metadata_tracks_current_error_after_reticle_moves(self):
        manifest = ScenarioManifest(
            scenario_key="tracking-metadata-s00",
            kind="steady_turns",
            initial_state=InitialState(
                initial_dx=40.0,
                initial_dy=0.0,
                initial_speed_px_per_sec=0.0,
                initial_heading_deg=0.0,
            ),
            turn_events=(TurnEvent(frame=999, delta_heading_deg=0.0, speed_scale=1.0),),
        )
        plugin = self._RecordingPlugin()

        _simulate_closed_loop(
            manifest,
            config=BenchmarkMetricsConfig(sim_frames=3, measure_from_frame=0),
            plugin_factory=lambda: plugin,
        )

        self.assertEqual(len(plugin.frames), 3)
        second_frame = plugin.frames[1]
        self.assertAlmostEqual(
            second_frame.target.aim_point_x - second_frame.target.screen_center_x,
            second_frame.target_dx,
            places=5,
        )
        self.assertAlmostEqual(
            second_frame.target.aim_point_y - second_frame.target.screen_center_y,
            second_frame.target_dy,
            places=5,
        )

    def test_closed_loop_uses_latest_target_sample_when_sample_rate_is_higher_than_frame_rate(self):
        class _PassiveRecordingPlugin:
            def __init__(self):
                self.frames = []

            def reset(self):
                self.frames.clear()

            def apply(self, frame, output):
                self.frames.append(frame)

        manifest = ScenarioManifest(
            scenario_key="tracking-sample-rate-s00",
            kind="steady_turns",
            initial_state=InitialState(
                initial_dx=0.0,
                initial_dy=0.0,
                initial_speed_px_per_sec=80.0,
                initial_heading_deg=0.0,
            ),
            turn_events=(TurnEvent(frame=999, delta_heading_deg=0.0, speed_scale=1.0),),
        )
        plugin = _PassiveRecordingPlugin()
        config = BenchmarkMetricsConfig(
            sim_frames=4,
            measure_from_frame=0,
            target_sample_hz=80.0,
        )
        sampled_states = expand_manifest(
            manifest,
            1.0 / config.target_sample_hz,
            5,
        )

        _simulate_closed_loop(
            manifest,
            config=config,
            plugin_factory=lambda: plugin,
        )

        self.assertEqual(len(plugin.frames), 4)
        fourth_frame = plugin.frames[3]
        self.assertAlmostEqual(fourth_frame.timestamp, 3 * config.frame_dt, places=6)
        self.assertEqual(fourth_frame.target_revision, 5)
        self.assertAlmostEqual(fourth_frame.target_timestamp, 4 * (1.0 / config.target_sample_hz), places=6)
        self.assertAlmostEqual(fourth_frame.target_dx, sampled_states[4].target_x, places=5)
        self.assertAlmostEqual(fourth_frame.target_dy, sampled_states[4].target_y, places=5)

    def test_higher_rate_sampling_preserves_manifest_event_timing_in_controller_frame_time(self):
        class _PassiveRecordingPlugin:
            def __init__(self):
                self.frames = []

            def reset(self):
                self.frames.clear()

            def apply(self, frame, output):
                self.frames.append(frame)

        manifest = ScenarioManifest(
            scenario_key="tracking-sample-timing-s00",
            kind="steady_turns",
            initial_state=InitialState(
                initial_dx=0.0,
                initial_dy=0.0,
                initial_speed_px_per_sec=120.0,
                initial_heading_deg=0.0,
            ),
            turn_events=(TurnEvent(frame=3, delta_heading_deg=90.0, speed_scale=1.0),),
        )
        plugin = _PassiveRecordingPlugin()

        _simulate_closed_loop(
            manifest,
            config=BenchmarkMetricsConfig(
                sim_frames=4,
                measure_from_frame=0,
                target_sample_hz=120.0,
            ),
            plugin_factory=lambda: plugin,
        )

        self.assertEqual(len(plugin.frames), 4)
        self.assertAlmostEqual(plugin.frames[2].target_dy, 0.0, places=5)
        self.assertGreater(plugin.frames[3].target_dy, 0.0)

    def test_evaluate_scenario_is_deterministic_for_a_stored_manifest(self):
        manifest = generate_phase1_manifests("run-alpha", 12345)[8]
        config = BenchmarkMetricsConfig()

        first = evaluate_scenario(manifest, config=config)
        second = evaluate_scenario(manifest, config=config)

        self.assertEqual(first, second)
        self.assertEqual(first.scenario_key, manifest.scenario_key)
        self.assertEqual(first.kind, manifest.kind)
        self.assertGreaterEqual(first.mean_error_px, 0.0)
        self.assertGreaterEqual(first.p95_error_px, first.mean_error_px)
        self.assertGreaterEqual(first.p99_error_px, first.p95_error_px)
        self.assertGreaterEqual(first.overshoot_events, 0)
        self.assertGreaterEqual(first.max_overshoot_px, 0.0)

    def test_evaluate_scenario_accepts_manifest_instances_directly(self):
        manifest = ScenarioManifest(
            scenario_key="custom-s00",
            kind="turn_then_decel",
            initial_state=InitialState(
                initial_dx=15.0,
                initial_dy=-8.0,
                initial_speed_px_per_sec=250.0,
                initial_heading_deg=0.0,
            ),
            turn_events=(TurnEvent(frame=20, delta_heading_deg=90.0, speed_scale=1.0),),
            decel_events=(DecelEvent(frame=80, duration_frames=20, target_speed_scale=0.0, hard_stop=True),),
        )

        metrics = evaluate_scenario(manifest, config=BenchmarkMetricsConfig())

        self.assertEqual(metrics.scenario_key, "custom-s00")
        self.assertEqual(metrics.kind, "turn_then_decel")
        self.assertIsInstance(metrics.mean_error_px, float)
        self.assertIsInstance(metrics.p95_error_px, float)
        self.assertIsInstance(metrics.p99_error_px, float)
        self.assertIsInstance(metrics.overshoot_events, int)
        self.assertIsInstance(metrics.max_overshoot_px, float)

    def test_overshoot_detection_requires_crossing_magnitude_above_threshold(self):
        self.assertEqual(
            _axis_overshoot_count([4.0, -1.5, 3.0, -2.1, 2.0, -2.0, 3.5], threshold_px=2.0),
            3,
        )

    def test_max_overshoot_ignores_non_qualifying_zero_crossings(self):
        records = [
            self.make_record(0, radial_error_px=4.0, error_x=4.0),
            self.make_record(1, radial_error_px=1.5, error_x=-1.5),
            self.make_record(2, radial_error_px=3.0, error_x=3.0),
            self.make_record(3, radial_error_px=3.75, error_x=-3.75),
        ]

        self.assertEqual(_max_overshoot_px(records, threshold_px=2.0), 3.75)

    def test_turn_recovery_uses_configured_threshold(self):
        manifest = ScenarioManifest(
            scenario_key="turn-recovery-s00",
            kind="steady_turns",
            initial_state=InitialState(
                initial_dx=0.0,
                initial_dy=0.0,
                initial_speed_px_per_sec=120.0,
                initial_heading_deg=0.0,
            ),
            turn_events=(TurnEvent(frame=10, delta_heading_deg=45.0, speed_scale=1.0),),
        )
        records = [
            self.make_record(10, radial_error_px=7.0),
            self.make_record(11, radial_error_px=5.5),
            self.make_record(12, radial_error_px=4.9),
        ]

        self.assertEqual(_mean_turn_recovery_frames(manifest, records, threshold_px=6.0), 1.0)
        self.assertEqual(_mean_turn_recovery_frames(manifest, records, threshold_px=5.0), 2.0)

    def test_decel_settling_requires_inclusive_threshold_and_consecutive_frames(self):
        manifest = ScenarioManifest(
            scenario_key="decel-settle-s00",
            kind="decel_resume",
            initial_state=InitialState(
                initial_dx=0.0,
                initial_dy=0.0,
                initial_speed_px_per_sec=120.0,
                initial_heading_deg=0.0,
            ),
            decel_events=(DecelEvent(frame=20, duration_frames=6, target_speed_scale=0.0, hard_stop=True),),
        )
        records = [
            self.make_record(20, radial_error_px=6.0),
            self.make_record(21, radial_error_px=5.0),
            self.make_record(22, radial_error_px=5.0),
            self.make_record(23, radial_error_px=5.0),
            self.make_record(24, radial_error_px=5.0),
        ]

        self.assertEqual(
            _mean_decel_settle_frames(
                manifest,
                records,
                threshold_px=5.0,
                consecutive_frames=4,
            ),
            1.0,
        )


class BenchmarkRunSummaryTests(unittest.TestCase):
    def test_evaluate_run_preserves_per_scenario_metrics_and_aggregates_them(self):
        manifests = generate_phase1_manifests("run-beta", 98765)[:3]
        summary = evaluate_run("run-beta", manifests, config=BenchmarkMetricsConfig())

        self.assertIsInstance(summary, BenchmarkRunSummary)
        self.assertEqual(summary.run_key, "run-beta")
        self.assertEqual(len(summary.scenario_metrics), 3)
        self.assertEqual(
            [metric.scenario_key for metric in summary.scenario_metrics],
            [manifest.scenario_key for manifest in manifests],
        )
        self.assertGreaterEqual(summary.aggregate.mean_error_px, 0.0)
        self.assertGreaterEqual(summary.aggregate.p95_error_px, summary.aggregate.mean_error_px)
        self.assertGreaterEqual(summary.aggregate.p99_error_px, summary.aggregate.p95_error_px)

    def test_relative_deltas_use_baseline_summary(self):
        baseline = BenchmarkRunSummary(
            run_key="baseline",
            config=BenchmarkMetricsConfig(),
            scenario_metrics=(),
            aggregate=BenchmarkAggregateMetrics(
                mean_error_px=10.0,
                p95_error_px=20.0,
                p99_error_px=30.0,
                overshoot_events=4,
                max_overshoot_px=8.0,
                mean_recovery_frames_after_turn=12.0,
                mean_settle_frames_after_decel=16.0,
            ),
        )
        current = BenchmarkRunSummary(
            run_key="current",
            config=BenchmarkMetricsConfig(),
            scenario_metrics=(),
            aggregate=BenchmarkAggregateMetrics(
                mean_error_px=15.0,
                p95_error_px=30.0,
                p99_error_px=45.0,
                overshoot_events=6,
                max_overshoot_px=12.0,
                mean_recovery_frames_after_turn=9.0,
                mean_settle_frames_after_decel=8.0,
            ),
        )

        deltas = current.relative_deltas(baseline)

        self.assertEqual(
            deltas,
            BenchmarkMetricDeltas(
                mean_error_px=0.5,
                p95_error_px=0.5,
                p99_error_px=0.5,
                overshoot_events=0.5,
                max_overshoot_px=0.5,
                mean_recovery_frames_after_turn=-0.25,
                mean_settle_frames_after_decel=-0.5,
            ),
        )

    def test_default_controller_stays_within_curated_regression_guard_on_state_machine_bundle(self):
        manifests = [
            generate_phase1_manifests("regression", 12345)[6],
            generate_phase1_manifests("regression", 12345)[7],
            generate_phase1_manifests("regression", 12345)[9],
            generate_phase1_manifests("regression", 42345)[8],
            generate_phase1_manifests("regression", 42345)[11],
        ]

        current_metrics = [evaluate_scenario(manifest, config=BenchmarkMetricsConfig()) for manifest in manifests]

        current_mean_error = sum(metric.mean_error_px for metric in current_metrics) / len(current_metrics)
        current_p99 = sum(metric.p99_error_px for metric in current_metrics) / len(current_metrics)
        current_overshoots = sum(metric.overshoot_events for metric in current_metrics)
        current_max_overshoot = max(metric.max_overshoot_px for metric in current_metrics)

        self.assertLess(current_mean_error, 30.0)
        self.assertLess(current_p99, 40.0)
        self.assertLessEqual(current_overshoots, 60)
        self.assertLessEqual(current_max_overshoot, 12.0)

    def test_default_controller_stays_within_global_regression_guard_on_multi_seed_sample(self):
        benchmark_config = BenchmarkMetricsConfig()
        seeds = (12345, 12346, 22345, 32345, 42345, 52345, 62345, 72345, 82345)

        current_runs = [
            evaluate_run(
                f"current-{seed}",
                generate_phase1_manifests(f"current-{seed}", seed),
                config=benchmark_config,
            ).aggregate
            for seed in seeds
        ]

        current_mean_error = sum(run.mean_error_px for run in current_runs) / len(current_runs)
        current_p99 = sum(run.p99_error_px for run in current_runs) / len(current_runs)
        current_recovery = sum(run.mean_recovery_frames_after_turn for run in current_runs) / len(current_runs)
        current_max_overshoot = sum(run.max_overshoot_px for run in current_runs) / len(current_runs)

        self.assertLess(current_mean_error, 20.0)
        self.assertLess(current_p99, 30.0)
        self.assertLessEqual(current_recovery, 80.0)
        self.assertLessEqual(current_max_overshoot, 12.0)


if __name__ == "__main__":
    unittest.main()
