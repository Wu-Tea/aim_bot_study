from __future__ import annotations

import argparse
from dataclasses import MISSING, asdict, dataclass, replace
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import sys
from typing import Any, Iterable

PROJECT_ROOT = Path(__file__).resolve().parent.parent
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from config import load_tuning_config
from tests.gamepad.ads_benchmark_metrics import (
    AdsBenchmarkAggregateMetrics,
    AdsBenchmarkConfig,
    DEFAULT_ADS_INPUT_PROFILES,
    evaluate_ads_run,
    evaluate_ads_scenario,
)
from tests.gamepad.ads_benchmark_scenarios import AdsScenarioManifest, ADS_FAMILY_COUNTS, generate_ads_manifests
from tests.gamepad.ads_manual_inputs import AdsManualInputConfig
from tests.gamepad.benchmark_metrics import BenchmarkAggregateMetrics, BenchmarkMetricsConfig, evaluate_run, evaluate_scenario
from tests.gamepad.benchmark_scenarios import ScenarioManifest, generate_phase1_manifests
from tests.gamepad.benchmark_scoreboard import ScoreboardRunEntry, extract_baseline_key, update_scoreboard
from tests.gamepad.manual_mix_inputs import ManualMixInputConfig
from tests.gamepad.manual_mix_metrics import (
    ManualMixAggregateMetrics,
    ManualMixMetricsConfig,
    evaluate_manual_mix_run,
    evaluate_manual_mix_scenario,
)

DEFAULT_SUITE = "phase1"
DEFAULT_ARTIFACT_DIR = PROJECT_ROOT / "artifacts" / "benchmarks" / "gamepad"
DEFAULT_SCOREBOARD_PATH = PROJECT_ROOT / "docs" / "project" / "GAMEPAD_BENCHMARKS.md"
DEFAULT_MANUAL_MIX_ARTIFACT_DIR = PROJECT_ROOT / "artifacts" / "benchmarks" / "gamepad_manual_mix"
DEFAULT_MANUAL_MIX_SCOREBOARD_PATH = PROJECT_ROOT / "docs" / "project" / "GAMEPAD_MANUAL_MIX_BENCHMARKS.md"
DEFAULT_ADS_ARTIFACT_DIR = PROJECT_ROOT / "artifacts" / "benchmarks" / "gamepad_ads"
DEFAULT_ADS_SCOREBOARD_PATH = PROJECT_ROOT / "docs" / "project" / "GAMEPAD_ADS_BENCHMARKS.md"
DEFAULT_SCOREBOARD_TITLE = "Gamepad Benchmarks"
DEFAULT_MANUAL_MIX_SCOREBOARD_TITLE = "Gamepad Manual-Mix Benchmarks"
DEFAULT_ADS_SCOREBOARD_TITLE = "Gamepad ADS Benchmarks"
DEFAULT_MANUAL_MIX_SEEDS = (1, 2, 3)
DEFAULT_SCENARIO_LOGIC = (
    "steady_turns: 8 scenarios with one or more heading changes and no hard stop",
    "turn_then_decel: 8 scenarios with a turn followed by a deceleration event",
    "decel_resume: 8 scenarios with a deceleration event and optional resume",
)
DEFAULT_MANUAL_MIX_SCENARIO_LOGIC = (
    *DEFAULT_SCENARIO_LOGIC,
    "manual-mix: 3 deterministic manual-input seeds per manifest using aligned, wobble, opposing, recover, and vertical-jitter modes",
)
DEFAULT_ADS_SCENARIO_LOGIC = (
    "single_static_offset: 8 scenarios with ADS engaged on a stationary offset target",
    "single_strafe_then_decel: 8 scenarios with lateral target motion that brakes during ADS",
    "single_diagonal_then_decel: 8 scenarios with diagonal motion and a short settle phase",
    "reacquire_after_gap: 6 scenarios where the engagement target disappears and reappears mid-ADS",
    "dual_target_disambiguation: 6 scenarios with an engagement target plus a distractor and a localization schedule",
)


@dataclass(frozen=True, slots=True)
class GitMetadata:
    commit: str | None
    dirty: bool


def run_benchmark(
    *,
    run_key: str,
    run_seed: int,
    suite: str = DEFAULT_SUITE,
    artifact_dir: Path | None = None,
    scoreboard_path: Path | None = None,
    git_metadata: GitMetadata | None = None,
    set_baseline: bool = False,
    benchmark_config: BenchmarkMetricsConfig | ManualMixMetricsConfig | AdsBenchmarkConfig | None = None,
    manual_input_config: ManualMixInputConfig | AdsManualInputConfig | None = None,
    manual_seeds: Iterable[int] | None = None,
    timestamp: str | None = None,
) -> dict[str, Any]:
    artifact_dir = artifact_dir or _default_artifact_dir(suite)
    scoreboard_path = scoreboard_path or _default_scoreboard_path(suite)
    artifact_dir.mkdir(parents=True, exist_ok=True)
    timestamp = timestamp or _utc_timestamp()
    git_metadata = git_metadata or detect_git_metadata(PROJECT_ROOT)
    baseline_key = run_key if set_baseline else extract_baseline_key(scoreboard_path)

    if suite == "ads":
        benchmark_config = benchmark_config or AdsBenchmarkConfig()
        manual_input_config = manual_input_config or AdsManualInputConfig()
        manifests = generate_ads_manifests(run_key, run_seed)
        input_profiles = tuple(DEFAULT_ADS_INPUT_PROFILES)
        summary = evaluate_ads_run(
            run_key,
            manifests,
            input_profiles=input_profiles,
            config=benchmark_config,
            input_config=manual_input_config,
        )
        baseline_metrics = _load_baseline_aggregate(
            artifact_dir,
            baseline_key,
            current_run_key=run_key,
            suite=suite,
        )
        delta_metrics = None if baseline_metrics is None else asdict(summary.relative_deltas(baseline_metrics))
        artifact = _build_ads_artifact(
            run_key=run_key,
            run_seed=run_seed,
            timestamp=timestamp,
            baseline_key=baseline_key,
            git_metadata=git_metadata,
            benchmark_config=benchmark_config,
            input_config=manual_input_config,
            input_profiles=input_profiles,
            manifests=manifests,
            summary=summary,
            delta_metrics=delta_metrics,
        )
        benchmark_parameters = _ads_parameters_snapshot(
            benchmark_config,
            manual_input_config,
            input_profiles,
        )
        scenario_logic = DEFAULT_ADS_SCENARIO_LOGIC
        scoreboard_title = DEFAULT_ADS_SCOREBOARD_TITLE
    elif suite == "manual-mix":
        manifests = generate_phase1_manifests(run_key, run_seed)
        benchmark_config = benchmark_config or ManualMixMetricsConfig()
        manual_input_config = manual_input_config or ManualMixInputConfig()
        manual_seeds = tuple(manual_seeds or DEFAULT_MANUAL_MIX_SEEDS)
        summary = evaluate_manual_mix_run(
            run_key,
            manifests,
            manual_seeds=manual_seeds,
            config=benchmark_config,
            input_config=manual_input_config,
        )
        baseline_metrics = _load_baseline_aggregate(
            artifact_dir,
            baseline_key,
            current_run_key=run_key,
            suite=suite,
        )
        delta_metrics = None if baseline_metrics is None else asdict(summary.relative_deltas(baseline_metrics))
        artifact = _build_manual_mix_artifact(
            run_key=run_key,
            run_seed=run_seed,
            timestamp=timestamp,
            baseline_key=baseline_key,
            git_metadata=git_metadata,
            benchmark_config=benchmark_config,
            input_config=manual_input_config,
            manual_seeds=manual_seeds,
            manifests=manifests,
            summary=summary,
            delta_metrics=delta_metrics,
        )
        benchmark_parameters = _manual_mix_parameters_snapshot(
            benchmark_config,
            manual_input_config,
            manual_seeds,
        )
        scenario_logic = DEFAULT_MANUAL_MIX_SCENARIO_LOGIC
        scoreboard_title = DEFAULT_MANUAL_MIX_SCOREBOARD_TITLE
    else:
        manifests = generate_phase1_manifests(run_key, run_seed)
        benchmark_config = benchmark_config or BenchmarkMetricsConfig()
        summary = evaluate_run(run_key, manifests, config=benchmark_config)
        baseline_metrics = _load_baseline_aggregate(
            artifact_dir,
            baseline_key,
            current_run_key=run_key,
            suite=suite,
        )
        delta_metrics = None if baseline_metrics is None else asdict(summary.relative_deltas(baseline_metrics))
        artifact = _build_phase1_artifact(
            run_key=run_key,
            run_seed=run_seed,
            timestamp=timestamp,
            baseline_key=baseline_key,
            git_metadata=git_metadata,
            benchmark_config=benchmark_config,
            manifests=manifests,
            summary=summary,
            delta_metrics=delta_metrics,
        )
        benchmark_parameters = _benchmark_parameters_snapshot(benchmark_config)
        scenario_logic = DEFAULT_SCENARIO_LOGIC
        scoreboard_title = DEFAULT_SCOREBOARD_TITLE

    artifact_path = artifact_dir / f"{run_key}.json"
    artifact_path.write_text(json.dumps(artifact, indent=2), encoding="utf-8")

    all_runs = _load_scoreboard_entries(artifact_dir)
    update_scoreboard(
        scoreboard_path=scoreboard_path,
        latest_run=_artifact_to_scoreboard_entry(artifact),
        all_runs=all_runs,
        baseline_key=baseline_key,
        benchmark_parameters=benchmark_parameters,
        scenario_logic=scenario_logic,
        title=scoreboard_title,
    )
    return artifact


def replay_run_key(
    *,
    run_key: str,
    suite: str = DEFAULT_SUITE,
    artifact_dir: Path | None = None,
    benchmark_config: BenchmarkMetricsConfig | ManualMixMetricsConfig | AdsBenchmarkConfig | None = None,
    target_sample_hz: float | None = None,
) -> dict[str, Any]:
    artifact_dir = artifact_dir or _default_artifact_dir(suite)
    artifact = _load_artifact(artifact_dir, run_key)
    suite = artifact.get("suite", suite)
    if suite == "manual-mix":
        manifests = _unique_manifests_from_artifact(artifact)
        benchmark_config = benchmark_config or _manual_mix_config_from_snapshot(artifact["benchmark_config"])
        if target_sample_hz is not None:
            benchmark_config = replace(benchmark_config, target_sample_hz=target_sample_hz)
        manual_input_config = _manual_input_config_from_snapshot(artifact["manual_input_config"])
        manual_seeds = tuple(artifact.get("manual_seeds", DEFAULT_MANUAL_MIX_SEEDS))
        summary = evaluate_manual_mix_run(
            run_key,
            manifests,
            manual_seeds=manual_seeds,
            config=benchmark_config,
            input_config=manual_input_config,
        )
        baseline_metrics = _load_baseline_aggregate(
            artifact_dir,
            artifact.get("baseline_key"),
            current_run_key=run_key,
            suite=suite,
        )
        delta_metrics = None if baseline_metrics is None else asdict(summary.relative_deltas(baseline_metrics))
        return {
            "run_key": run_key,
            "suite": suite,
            "baseline_key": artifact.get("baseline_key"),
            "benchmark_config": asdict(benchmark_config),
            "manual_input_config": asdict(manual_input_config),
            "manual_seeds": list(manual_seeds),
            "aggregate_metrics": asdict(summary.aggregate),
            "relative_deltas_vs_baseline": delta_metrics,
            "scenarios": _manual_mix_scenario_payloads(manifests, manual_seeds, summary),
        }
    if suite == "ads":
        manifests = _unique_ads_manifests_from_artifact(artifact)
        benchmark_config = benchmark_config or _ads_config_from_snapshot(artifact["benchmark_config"])
        if target_sample_hz is not None:
            benchmark_config = replace(benchmark_config, target_sample_hz=target_sample_hz)
        manual_input_snapshot = artifact.get("manual_input_config")
        manual_input_config = (
            AdsManualInputConfig()
            if manual_input_snapshot is None
            else _ads_manual_input_config_from_snapshot(manual_input_snapshot)
        )
        input_profiles = tuple(artifact.get("input_profiles", DEFAULT_ADS_INPUT_PROFILES))
        summary = evaluate_ads_run(
            run_key,
            manifests,
            input_profiles=input_profiles,
            config=benchmark_config,
            input_config=manual_input_config,
        )
        baseline_metrics = _load_baseline_aggregate(
            artifact_dir,
            artifact.get("baseline_key"),
            current_run_key=run_key,
            suite=suite,
        )
        delta_metrics = None if baseline_metrics is None else asdict(summary.relative_deltas(baseline_metrics))
        return {
            "run_key": run_key,
            "suite": suite,
            "baseline_key": artifact.get("baseline_key"),
            "benchmark_config": asdict(benchmark_config),
            "manual_input_config": asdict(manual_input_config),
            "input_profiles": list(input_profiles),
            "aggregate_metrics": asdict(summary.aggregate),
            "relative_deltas_vs_baseline": delta_metrics,
            "scenarios": _ads_scenario_payloads(manifests, input_profiles, summary),
        }

    manifests = tuple(ScenarioManifest.from_dict(item["manifest"]) for item in artifact["scenarios"])
    benchmark_config = benchmark_config or _config_from_snapshot(artifact["benchmark_config"])
    if target_sample_hz is not None:
        benchmark_config = replace(benchmark_config, target_sample_hz=target_sample_hz)
    summary = evaluate_run(run_key, manifests, config=benchmark_config)
    baseline_metrics = _load_baseline_aggregate(
        artifact_dir,
        artifact.get("baseline_key"),
        current_run_key=run_key,
        suite=suite,
    )
    delta_metrics = None if baseline_metrics is None else asdict(summary.relative_deltas(baseline_metrics))
    return {
        "run_key": run_key,
        "suite": suite,
        "baseline_key": artifact.get("baseline_key"),
        "benchmark_config": asdict(benchmark_config),
        "aggregate_metrics": asdict(summary.aggregate),
        "relative_deltas_vs_baseline": delta_metrics,
        "scenarios": _scenario_payloads(manifests, summary),
    }


def replay_scenario_key(
    *,
    scenario_key: str,
    suite: str = DEFAULT_SUITE,
    artifact_dir: Path | None = None,
    benchmark_config: BenchmarkMetricsConfig | ManualMixMetricsConfig | AdsBenchmarkConfig | None = None,
    target_sample_hz: float | None = None,
) -> dict[str, Any]:
    artifact_dir = artifact_dir or _default_artifact_dir(suite)
    artifact, scenario_payload = _find_scenario_payload(artifact_dir, scenario_key)
    suite = artifact.get("suite", suite)
    if suite == "manual-mix":
        manifest = ScenarioManifest.from_dict(scenario_payload["manifest"])
        benchmark_config = benchmark_config or _manual_mix_config_from_snapshot(artifact["benchmark_config"])
        if target_sample_hz is not None:
            benchmark_config = replace(benchmark_config, target_sample_hz=target_sample_hz)
        manual_input_config = _manual_input_config_from_snapshot(artifact["manual_input_config"])
        manual_seed = scenario_payload["manual_seed"]
        metrics = evaluate_manual_mix_scenario(
            manifest,
            manual_seed=manual_seed,
            config=benchmark_config,
            input_config=manual_input_config,
        )
        return {
            "run_key": artifact["run_key"],
            "suite": suite,
            "scenario_key": scenario_key,
            "manual_seed": manual_seed,
            "benchmark_config": asdict(benchmark_config),
            "manual_input_config": asdict(manual_input_config),
            "metrics": asdict(metrics),
        }
    if suite == "ads":
        manifest = AdsScenarioManifest.from_dict(scenario_payload["manifest"])
        benchmark_config = benchmark_config or _ads_config_from_snapshot(artifact["benchmark_config"])
        if target_sample_hz is not None:
            benchmark_config = replace(benchmark_config, target_sample_hz=target_sample_hz)
        manual_input_snapshot = artifact.get("manual_input_config")
        manual_input_config = (
            AdsManualInputConfig()
            if manual_input_snapshot is None
            else _ads_manual_input_config_from_snapshot(manual_input_snapshot)
        )
        input_profile = scenario_payload.get("input_profile") or _ads_input_profile_from_case_key(
            scenario_payload.get("scenario_case_key"),
        )
        metrics = evaluate_ads_scenario(
            manifest,
            input_profile=input_profile,
            config=benchmark_config,
            input_config=manual_input_config,
        )
        return {
            "run_key": artifact["run_key"],
            "suite": suite,
            "scenario_key": scenario_key,
            "benchmark_config": asdict(benchmark_config),
            "manual_input_config": asdict(manual_input_config),
            "input_profile": input_profile,
            "metrics": asdict(metrics),
        }

    manifest = ScenarioManifest.from_dict(scenario_payload["manifest"])
    benchmark_config = benchmark_config or _config_from_snapshot(artifact["benchmark_config"])
    if target_sample_hz is not None:
        benchmark_config = replace(benchmark_config, target_sample_hz=target_sample_hz)
    metrics = evaluate_scenario(manifest, config=benchmark_config)
    return {
        "run_key": artifact["run_key"],
        "suite": suite,
        "scenario_key": scenario_key,
        "benchmark_config": asdict(benchmark_config),
        "metrics": asdict(metrics),
    }


def detect_git_metadata(project_root: Path) -> GitMetadata:
    commit = _run_git(project_root, "rev-parse", "HEAD")
    dirty_output = _run_git(project_root, "status", "--short")
    return GitMetadata(
        commit=commit if commit else None,
        dirty=bool(dirty_output),
    )


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run or replay the gamepad benchmark pipeline.")
    parser.add_argument(
        "--suite",
        choices=(DEFAULT_SUITE, "manual-mix", "ads"),
        default=DEFAULT_SUITE,
        help="Benchmark suite to run or replay.",
    )
    parser.add_argument("--set-baseline", action="store_true", help="Run the suite and mark the resulting run as baseline.")
    parser.add_argument("--replay-run-key", help="Replay every stored scenario for an existing run key.")
    parser.add_argument("--replay-scenario-key", help="Replay a stored scenario manifest by scenario key.")
    parser.add_argument("--run-key", help="Optional explicit run key for normal benchmark runs.")
    parser.add_argument("--run-seed", type=int, help="Optional explicit RNG seed for scenario generation.")
    parser.add_argument(
        "--target-sample-hz",
        type=float,
        help="Optional target indexing/sample rate in Hz. Controller execution remains at the suite frame_dt.",
    )
    parser.add_argument("--artifact-dir", type=Path)
    parser.add_argument("--scoreboard-path", type=Path)
    args = parser.parse_args(list(argv) if argv is not None else None)

    replay_flags = [bool(args.replay_run_key), bool(args.replay_scenario_key)]
    if sum(replay_flags) > 1:
        parser.error("--replay-run-key and --replay-scenario-key are mutually exclusive")
    if args.set_baseline and any(replay_flags):
        parser.error("--set-baseline cannot be combined with replay arguments")
    if args.target_sample_hz is not None and args.target_sample_hz <= 0.0:
        parser.error("--target-sample-hz must be positive")

    try:
        if args.replay_run_key:
            result = replay_run_key(
                run_key=args.replay_run_key,
                suite=args.suite,
                artifact_dir=args.artifact_dir,
                target_sample_hz=args.target_sample_hz,
            )
        elif args.replay_scenario_key:
            result = replay_scenario_key(
                scenario_key=args.replay_scenario_key,
                suite=args.suite,
                artifact_dir=args.artifact_dir,
                target_sample_hz=args.target_sample_hz,
            )
        else:
            run_key = args.run_key or _default_run_key()
            run_seed = args.run_seed if args.run_seed is not None else _default_run_seed()
            benchmark_config = None
            if args.target_sample_hz is not None:
                if args.suite == "manual-mix":
                    benchmark_config = ManualMixMetricsConfig(target_sample_hz=args.target_sample_hz)
                elif args.suite == "ads":
                    benchmark_config = AdsBenchmarkConfig(target_sample_hz=args.target_sample_hz)
                else:
                    benchmark_config = BenchmarkMetricsConfig(target_sample_hz=args.target_sample_hz)
            result = run_benchmark(
                run_key=run_key,
                run_seed=run_seed,
                suite=args.suite,
                artifact_dir=args.artifact_dir,
                scoreboard_path=args.scoreboard_path,
                set_baseline=args.set_baseline,
                benchmark_config=benchmark_config,
            )
        print(json.dumps(result, indent=2))
        return 0
    except FileNotFoundError as exc:
        print(str(exc), file=sys.stderr)
        return 1


def _build_phase1_artifact(
    *,
    run_key: str,
    run_seed: int,
    timestamp: str,
    baseline_key: str | None,
    git_metadata: GitMetadata,
    benchmark_config: BenchmarkMetricsConfig,
    manifests: tuple[ScenarioManifest, ...] | list[ScenarioManifest],
    summary: Any,
    delta_metrics: dict[str, float | None] | None,
) -> dict[str, Any]:
    manifests = tuple(manifests)
    return {
        "suite": DEFAULT_SUITE,
        "run_key": run_key,
        "run_seed": run_seed,
        "baseline_key": baseline_key,
        "timestamp": timestamp,
        "artifact_path": f"artifacts/benchmarks/gamepad/{run_key}.json",
        "git_metadata": asdict(git_metadata),
        "benchmark_config": _benchmark_parameters_snapshot(benchmark_config),
        "scenario_logic": list(DEFAULT_SCENARIO_LOGIC),
        "controller_config_snapshot": _controller_config_snapshot(),
        "aggregate_metrics": asdict(summary.aggregate),
        "relative_deltas_vs_baseline": delta_metrics,
        "scenarios": _scenario_payloads(manifests, summary),
    }


def _build_manual_mix_artifact(
    *,
    run_key: str,
    run_seed: int,
    timestamp: str,
    baseline_key: str | None,
    git_metadata: GitMetadata,
    benchmark_config: ManualMixMetricsConfig,
    input_config: ManualMixInputConfig,
    manual_seeds: Sequence[int],
    manifests: tuple[ScenarioManifest, ...] | list[ScenarioManifest],
    summary: Any,
    delta_metrics: dict[str, float | None] | None,
) -> dict[str, Any]:
    manifests = tuple(manifests)
    manual_seeds = tuple(manual_seeds)
    return {
        "suite": "manual-mix",
        "run_key": run_key,
        "run_seed": run_seed,
        "baseline_key": baseline_key,
        "timestamp": timestamp,
        "artifact_path": f"artifacts/benchmarks/gamepad_manual_mix/{run_key}.json",
        "git_metadata": asdict(git_metadata),
        "benchmark_config": asdict(benchmark_config),
        "manual_input_config": asdict(input_config),
        "manual_seeds": list(manual_seeds),
        "scenario_logic": list(DEFAULT_MANUAL_MIX_SCENARIO_LOGIC),
        "controller_config_snapshot": _controller_config_snapshot(),
        "aggregate_metrics": asdict(summary.aggregate),
        "relative_deltas_vs_baseline": delta_metrics,
        "scenarios": _manual_mix_scenario_payloads(manifests, manual_seeds, summary),
    }


def _build_ads_artifact(
    *,
    run_key: str,
    run_seed: int,
    timestamp: str,
    baseline_key: str | None,
    git_metadata: GitMetadata,
    benchmark_config: AdsBenchmarkConfig,
    input_config: AdsManualInputConfig,
    input_profiles: Sequence[str],
    manifests: tuple[AdsScenarioManifest, ...] | list[AdsScenarioManifest],
    summary: Any,
    delta_metrics: dict[str, float | None] | None,
) -> dict[str, Any]:
    manifests = tuple(manifests)
    input_profiles = tuple(input_profiles)
    return {
        "suite": "ads",
        "run_key": run_key,
        "run_seed": run_seed,
        "baseline_key": baseline_key,
        "timestamp": timestamp,
        "artifact_path": f"artifacts/benchmarks/gamepad_ads/{run_key}.json",
        "git_metadata": asdict(git_metadata),
        "benchmark_config": asdict(benchmark_config),
        "manual_input_config": asdict(input_config),
        "input_profiles": list(input_profiles),
        "scenario_logic": list(DEFAULT_ADS_SCENARIO_LOGIC),
        "controller_config_snapshot": _controller_config_snapshot(),
        "aggregate_metrics": asdict(summary.aggregate),
        "relative_deltas_vs_baseline": delta_metrics,
        "scenarios": _ads_scenario_payloads(manifests, input_profiles, summary),
    }


def _scenario_payloads(
    manifests: tuple[ScenarioManifest, ...] | list[ScenarioManifest],
    summary: Any,
) -> list[dict[str, Any]]:
    return [
        {
            "manifest": manifest.to_dict(),
            "metrics": asdict(metric),
        }
        for manifest, metric in zip(manifests, summary.scenario_metrics)
    ]


def _manual_mix_scenario_payloads(
    manifests: tuple[ScenarioManifest, ...] | list[ScenarioManifest],
    manual_seeds: Sequence[int],
    summary: Any,
) -> list[dict[str, Any]]:
    payloads: list[dict[str, Any]] = []
    metrics_iter = iter(summary.scenario_metrics)
    for manifest in manifests:
        for manual_seed in manual_seeds:
            metric = next(metrics_iter)
            payloads.append(
                {
                    "scenario_case_key": f"{manifest.scenario_key}@seed{manual_seed}",
                    "manifest": manifest.to_dict(),
                    "manual_seed": manual_seed,
                    "metrics": asdict(metric),
                }
            )
    return payloads


def _ads_scenario_payloads(
    manifests: tuple[AdsScenarioManifest, ...] | list[AdsScenarioManifest],
    input_profiles: Sequence[str],
    summary: Any,
) -> list[dict[str, Any]]:
    payloads: list[dict[str, Any]] = []
    metrics_iter = iter(summary.scenario_metrics)
    for manifest in manifests:
        for input_profile in input_profiles:
            metric = next(metrics_iter)
            payloads.append(
                {
                    "scenario_case_key": f"{manifest.scenario_key}@{input_profile}",
                    "manifest": manifest.to_dict(),
                    "input_profile": input_profile,
                    "metrics": asdict(metric),
                }
            )
    return payloads


def _controller_config_snapshot() -> dict[str, Any]:
    tuning = load_tuning_config()
    return {
        "gamepad_ai_aim": asdict(tuning.gamepad_ai_aim),
        "adaptive_delta_gain": asdict(tuning.adaptive_delta_gain),
    }


def _benchmark_parameters_snapshot(benchmark_config: BenchmarkMetricsConfig) -> dict[str, Any]:
    snapshot = asdict(benchmark_config)
    snapshot["scenario_count"] = 24
    snapshot["steady_turns"] = 8
    snapshot["turn_then_decel"] = 8
    snapshot["decel_resume"] = 8
    return snapshot


def _manual_mix_parameters_snapshot(
    benchmark_config: ManualMixMetricsConfig,
    input_config: ManualMixInputConfig,
    manual_seeds: Sequence[int],
) -> dict[str, Any]:
    snapshot = asdict(benchmark_config)
    snapshot["scenario_count"] = 24
    snapshot["steady_turns"] = 8
    snapshot["turn_then_decel"] = 8
    snapshot["decel_resume"] = 8
    snapshot["manual_seed_count"] = len(tuple(manual_seeds))
    snapshot["manual_seeds"] = list(manual_seeds)
    snapshot["manual_input_config"] = asdict(input_config)
    return snapshot


def _ads_parameters_snapshot(
    benchmark_config: AdsBenchmarkConfig,
    input_config: AdsManualInputConfig,
    input_profiles: Sequence[str],
) -> dict[str, Any]:
    snapshot = asdict(benchmark_config)
    snapshot["scenario_count"] = sum(count for _, count in ADS_FAMILY_COUNTS)
    for family, count in ADS_FAMILY_COUNTS:
        snapshot[family] = count
    snapshot["input_profile_count"] = len(tuple(input_profiles))
    snapshot["input_profiles"] = list(input_profiles)
    snapshot["manual_input_config"] = asdict(input_config)
    return snapshot


def _load_scoreboard_entries(artifact_dir: Path) -> tuple[ScoreboardRunEntry, ...]:
    return tuple(
        _artifact_to_scoreboard_entry(artifact)
        for artifact in sorted(
            (_read_artifact(path) for path in _artifact_paths(artifact_dir)),
            key=lambda item: item["timestamp"],
        )
    )


def _artifact_to_scoreboard_entry(artifact: dict[str, Any]) -> ScoreboardRunEntry:
    return ScoreboardRunEntry(
        run_key=artifact["run_key"],
        timestamp=artifact["timestamp"],
        artifact_path=artifact["artifact_path"],
        git_commit=artifact["git_metadata"].get("commit"),
        dirty=bool(artifact["git_metadata"].get("dirty")),
        aggregate_metrics=artifact["aggregate_metrics"],
        delta_metrics=artifact.get("relative_deltas_vs_baseline"),
    )


def _load_baseline_aggregate(
    artifact_dir: Path,
    baseline_key: str | None,
    *,
    current_run_key: str,
    suite: str,
) -> BenchmarkAggregateMetrics | ManualMixAggregateMetrics | AdsBenchmarkAggregateMetrics | None:
    if baseline_key is None or baseline_key == current_run_key:
        return None
    baseline_artifact = _load_artifact(artifact_dir, baseline_key)
    if suite == "manual-mix":
        return ManualMixAggregateMetrics(**baseline_artifact["aggregate_metrics"])
    if suite == "ads":
        return AdsBenchmarkAggregateMetrics(**baseline_artifact["aggregate_metrics"])
    return BenchmarkAggregateMetrics(**baseline_artifact["aggregate_metrics"])


def _load_artifact(artifact_dir: Path, run_key: str) -> dict[str, Any]:
    artifact_path = artifact_dir / f"{run_key}.json"
    if not artifact_path.is_file():
        raise FileNotFoundError(f"Benchmark run key not found: {run_key}")
    return _read_artifact(artifact_path)


def _read_artifact(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def _artifact_paths(artifact_dir: Path) -> list[Path]:
    if not artifact_dir.is_dir():
        return []
    return sorted(artifact_dir.glob("*.json"))


def _find_scenario_payload(artifact_dir: Path, scenario_key: str) -> tuple[dict[str, Any], dict[str, Any]]:
    for path in _artifact_paths(artifact_dir):
        artifact = _read_artifact(path)
        for scenario_payload in artifact.get("scenarios", []):
            manifest = scenario_payload.get("manifest", {})
            if (
                manifest.get("scenario_key") == scenario_key
                or scenario_payload.get("scenario_case_key") == scenario_key
            ):
                return artifact, scenario_payload
    raise FileNotFoundError(f"Benchmark scenario key not found: {scenario_key}")


def _config_from_snapshot(snapshot: dict[str, Any]) -> BenchmarkMetricsConfig:
    return _dataclass_from_snapshot(BenchmarkMetricsConfig, snapshot)


def _manual_mix_config_from_snapshot(snapshot: dict[str, Any]) -> ManualMixMetricsConfig:
    return _dataclass_from_snapshot(ManualMixMetricsConfig, snapshot)


def _manual_input_config_from_snapshot(snapshot: dict[str, Any]) -> ManualMixInputConfig:
    return _dataclass_from_snapshot(ManualMixInputConfig, snapshot)


def _ads_config_from_snapshot(snapshot: dict[str, Any]) -> AdsBenchmarkConfig:
    return _dataclass_from_snapshot(AdsBenchmarkConfig, snapshot)


def _ads_manual_input_config_from_snapshot(snapshot: dict[str, Any]) -> AdsManualInputConfig:
    return _dataclass_from_snapshot(AdsManualInputConfig, snapshot)


def _dataclass_from_snapshot(config_type: Any, snapshot: dict[str, Any]) -> Any:
    values: dict[str, Any] = {}
    for field_name, field_def in config_type.__dataclass_fields__.items():
        if field_name in snapshot:
            values[field_name] = snapshot[field_name]
            continue
        if field_def.default is not MISSING:
            values[field_name] = field_def.default
            continue
        if field_def.default_factory is not MISSING:
            values[field_name] = field_def.default_factory()
            continue
        raise KeyError(f"Missing required snapshot field: {field_name}")
    return config_type(**values)


def _default_artifact_dir(suite: str) -> Path:
    if suite == "manual-mix":
        return DEFAULT_MANUAL_MIX_ARTIFACT_DIR
    if suite == "ads":
        return DEFAULT_ADS_ARTIFACT_DIR
    return DEFAULT_ARTIFACT_DIR


def _default_scoreboard_path(suite: str) -> Path:
    if suite == "manual-mix":
        return DEFAULT_MANUAL_MIX_SCOREBOARD_PATH
    if suite == "ads":
        return DEFAULT_ADS_SCOREBOARD_PATH
    return DEFAULT_SCOREBOARD_PATH


def _unique_manifests_from_artifact(artifact: dict[str, Any]) -> tuple[ScenarioManifest, ...]:
    manifests_by_key: dict[str, ScenarioManifest] = {}
    for payload in artifact.get("scenarios", []):
        manifest = ScenarioManifest.from_dict(payload["manifest"])
        manifests_by_key.setdefault(manifest.scenario_key, manifest)
    return tuple(manifests_by_key.values())


def _unique_ads_manifests_from_artifact(artifact: dict[str, Any]) -> tuple[AdsScenarioManifest, ...]:
    manifests_by_key: dict[str, AdsScenarioManifest] = {}
    for payload in artifact.get("scenarios", []):
        manifest = AdsScenarioManifest.from_dict(payload["manifest"])
        manifests_by_key.setdefault(manifest.scenario_key, manifest)
    return tuple(manifests_by_key.values())


def _ads_input_profile_from_case_key(scenario_case_key: str | None) -> str:
    if scenario_case_key:
        scenario_key, separator, input_profile = scenario_case_key.rpartition("@")
        if separator and scenario_key and input_profile:
            return input_profile
    return DEFAULT_ADS_INPUT_PROFILES[0]


def _run_git(project_root: Path, *args: str) -> str:
    try:
        completed = subprocess.run(
            ["git", *args],
            cwd=project_root,
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError:
        return ""
    if completed.returncode != 0:
        return ""
    return completed.stdout.strip()


def _utc_timestamp() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def _default_run_key() -> str:
    return f"gamepad-{datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')}"


def _default_run_seed() -> int:
    return int(datetime.now(timezone.utc).timestamp())


if __name__ == "__main__":
    raise SystemExit(main())
