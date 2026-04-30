from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass, replace
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import sys
from typing import Any, Iterable, Sequence

PROJECT_ROOT = Path(__file__).resolve().parent.parent
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from config import load_tuning_config
from tests.gamepad.ads_benchmark_scenarios import AdsScenarioManifest, ADS_FAMILY_COUNTS, generate_ads_manifests
from tests.gamepad.benchmark_scoreboard import ScoreboardRunEntry, extract_baseline_key, update_scoreboard
from tests.mouse.ads_benchmark_metrics import (
    MouseAdsBenchmarkAggregateMetrics,
    MouseAdsBenchmarkConfig,
    compare_against_gamepad_ads,
    evaluate_mouse_ads_run,
    evaluate_mouse_ads_scenario,
)


DEFAULT_ARTIFACT_DIR = PROJECT_ROOT / "artifacts" / "benchmarks" / "mouse_ads"
DEFAULT_SCOREBOARD_PATH = PROJECT_ROOT / "docs" / "project" / "MOUSE_ADS_BENCHMARKS.md"
DEFAULT_SCOREBOARD_TITLE = "Mouse ADS Benchmarks"
DEFAULT_GAMEPAD_REFERENCE_ARTIFACT_DIR = PROJECT_ROOT / "artifacts" / "benchmarks" / "gamepad_ads"
DEFAULT_SCENARIO_LOGIC = (
    "shared ADS manifests from the gamepad ADS suite with mouse-native actuation",
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
    artifact_dir: Path | None = None,
    scoreboard_path: Path | None = None,
    git_metadata: GitMetadata | None = None,
    set_baseline: bool = False,
    benchmark_config: MouseAdsBenchmarkConfig | None = None,
    reference_gamepad_run_key: str | None = None,
    reference_gamepad_artifact_dir: Path | None = None,
    timestamp: str | None = None,
) -> dict[str, Any]:
    artifact_dir = artifact_dir or DEFAULT_ARTIFACT_DIR
    scoreboard_path = scoreboard_path or DEFAULT_SCOREBOARD_PATH
    reference_gamepad_artifact_dir = (
        reference_gamepad_artifact_dir or DEFAULT_GAMEPAD_REFERENCE_ARTIFACT_DIR
    )
    artifact_dir.mkdir(parents=True, exist_ok=True)
    timestamp = timestamp or _utc_timestamp()
    git_metadata = git_metadata or detect_git_metadata(PROJECT_ROOT)
    baseline_key = run_key if set_baseline else extract_baseline_key(scoreboard_path)
    benchmark_config = benchmark_config or MouseAdsBenchmarkConfig()

    manifests = generate_ads_manifests(run_key, run_seed)
    summary = evaluate_mouse_ads_run(
        run_key,
        manifests,
        config=benchmark_config,
    )
    baseline_metrics = _load_baseline_aggregate(
        artifact_dir,
        baseline_key,
        current_run_key=run_key,
    )
    delta_metrics = None if baseline_metrics is None else asdict(
        summary.relative_deltas(baseline_metrics)
    )
    gamepad_comparison = _build_gamepad_comparison(
        summary.aggregate,
        reference_gamepad_run_key=reference_gamepad_run_key,
        reference_gamepad_artifact_dir=reference_gamepad_artifact_dir,
    )

    artifact = {
        "suite": "mouse-ads",
        "run_key": run_key,
        "run_seed": run_seed,
        "baseline_key": baseline_key,
        "timestamp": timestamp,
        "artifact_path": f"artifacts/benchmarks/mouse_ads/{run_key}.json",
        "git_metadata": asdict(git_metadata),
        "benchmark_config": asdict(benchmark_config),
        "scenario_logic": list(DEFAULT_SCENARIO_LOGIC),
        "controller_config_snapshot": _controller_config_snapshot(),
        "aggregate_metrics": asdict(summary.aggregate),
        "relative_deltas_vs_baseline": delta_metrics,
        "gamepad_comparison": gamepad_comparison,
        "scenarios": [
            {
                "manifest": manifest.to_dict(),
                "metrics": asdict(metric),
            }
            for manifest, metric in zip(manifests, summary.scenario_metrics)
        ],
    }

    artifact_path = artifact_dir / f"{run_key}.json"
    artifact_path.write_text(json.dumps(artifact, indent=2), encoding="utf-8")

    all_runs = _load_scoreboard_entries(artifact_dir)
    update_scoreboard(
        scoreboard_path=scoreboard_path,
        latest_run=_artifact_to_scoreboard_entry(artifact),
        all_runs=all_runs,
        baseline_key=baseline_key,
        benchmark_parameters=_benchmark_parameters_snapshot(benchmark_config),
        scenario_logic=DEFAULT_SCENARIO_LOGIC,
        title=DEFAULT_SCOREBOARD_TITLE,
    )
    return artifact


def replay_run_key(
    *,
    run_key: str,
    artifact_dir: Path | None = None,
    benchmark_config: MouseAdsBenchmarkConfig | None = None,
    target_sample_hz: float | None = None,
) -> dict[str, Any]:
    artifact_dir = artifact_dir or DEFAULT_ARTIFACT_DIR
    artifact = _load_artifact(artifact_dir, run_key)
    manifests = tuple(
        AdsScenarioManifest.from_dict(item["manifest"])
        for item in artifact["scenarios"]
    )
    benchmark_config = benchmark_config or _config_from_snapshot(
        artifact["benchmark_config"]
    )
    if target_sample_hz is not None:
        benchmark_config = replace(
            benchmark_config,
            target_sample_hz=target_sample_hz,
        )
    summary = evaluate_mouse_ads_run(
        run_key,
        manifests,
        config=benchmark_config,
    )
    baseline_metrics = _load_baseline_aggregate(
        artifact_dir,
        artifact.get("baseline_key"),
        current_run_key=run_key,
    )
    delta_metrics = None if baseline_metrics is None else asdict(
        summary.relative_deltas(baseline_metrics)
    )
    return {
        "run_key": run_key,
        "suite": "mouse-ads",
        "baseline_key": artifact.get("baseline_key"),
        "benchmark_config": asdict(benchmark_config),
        "aggregate_metrics": asdict(summary.aggregate),
        "relative_deltas_vs_baseline": delta_metrics,
        "gamepad_comparison": artifact.get("gamepad_comparison"),
        "scenarios": [
            {
                "manifest": manifest.to_dict(),
                "metrics": asdict(metric),
            }
            for manifest, metric in zip(manifests, summary.scenario_metrics)
        ],
    }


def replay_scenario_key(
    *,
    scenario_key: str,
    artifact_dir: Path | None = None,
    benchmark_config: MouseAdsBenchmarkConfig | None = None,
    target_sample_hz: float | None = None,
) -> dict[str, Any]:
    artifact_dir = artifact_dir or DEFAULT_ARTIFACT_DIR
    artifact, scenario_payload = _find_scenario_payload(artifact_dir, scenario_key)
    manifest = AdsScenarioManifest.from_dict(scenario_payload["manifest"])
    benchmark_config = benchmark_config or _config_from_snapshot(
        artifact["benchmark_config"]
    )
    if target_sample_hz is not None:
        benchmark_config = replace(
            benchmark_config,
            target_sample_hz=target_sample_hz,
        )
    metrics = evaluate_mouse_ads_scenario(
        manifest,
        config=benchmark_config,
    )
    return {
        "run_key": artifact["run_key"],
        "suite": "mouse-ads",
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
    parser = argparse.ArgumentParser(description="Run or replay the mouse ADS benchmark pipeline.")
    parser.add_argument("--set-baseline", action="store_true")
    parser.add_argument("--replay-run-key")
    parser.add_argument("--replay-scenario-key")
    parser.add_argument("--run-key")
    parser.add_argument("--run-seed", type=int)
    parser.add_argument("--artifact-dir", type=Path)
    parser.add_argument("--scoreboard-path", type=Path)
    parser.add_argument("--target-sample-hz", type=float)
    parser.add_argument("--reference-gamepad-run-key")
    parser.add_argument("--reference-gamepad-artifact-dir", type=Path)
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
                artifact_dir=args.artifact_dir,
                target_sample_hz=args.target_sample_hz,
            )
        elif args.replay_scenario_key:
            result = replay_scenario_key(
                scenario_key=args.replay_scenario_key,
                artifact_dir=args.artifact_dir,
                target_sample_hz=args.target_sample_hz,
            )
        else:
            run_key = args.run_key or _default_run_key()
            run_seed = args.run_seed if args.run_seed is not None else _default_run_seed()
            benchmark_config = None
            if args.target_sample_hz is not None:
                benchmark_config = MouseAdsBenchmarkConfig(
                    target_sample_hz=args.target_sample_hz
                )
            result = run_benchmark(
                run_key=run_key,
                run_seed=run_seed,
                artifact_dir=args.artifact_dir,
                scoreboard_path=args.scoreboard_path,
                set_baseline=args.set_baseline,
                benchmark_config=benchmark_config,
                reference_gamepad_run_key=args.reference_gamepad_run_key,
                reference_gamepad_artifact_dir=args.reference_gamepad_artifact_dir,
            )
        print(json.dumps(result, indent=2))
        return 0
    except FileNotFoundError as exc:
        print(str(exc), file=sys.stderr)
        return 1


def _controller_config_snapshot() -> dict[str, Any]:
    tuning = load_tuning_config()
    return {
        "mouse_ai_aim": asdict(tuning.mouse_ai_aim),
    }


def _benchmark_parameters_snapshot(
    benchmark_config: MouseAdsBenchmarkConfig,
) -> dict[str, Any]:
    snapshot = asdict(benchmark_config)
    snapshot["scenario_count"] = sum(count for _, count in ADS_FAMILY_COUNTS)
    snapshot["input_profile_count"] = 1
    snapshot["input_profiles"] = ["none"]
    for family, count in ADS_FAMILY_COUNTS:
        snapshot[family] = count
    return snapshot


def _build_gamepad_comparison(
    aggregate: MouseAdsBenchmarkAggregateMetrics,
    *,
    reference_gamepad_run_key: str | None,
    reference_gamepad_artifact_dir: Path,
) -> dict[str, Any] | None:
    if reference_gamepad_run_key is None:
        return None
    artifact = _load_artifact(
        reference_gamepad_artifact_dir,
        reference_gamepad_run_key,
    )
    comparison = compare_against_gamepad_ads(
        aggregate,
        artifact["aggregate_metrics"],
    )
    comparison["reference_run_key"] = reference_gamepad_run_key
    comparison["reference_artifact_path"] = artifact.get(
        "artifact_path",
        f"artifacts/benchmarks/gamepad_ads/{reference_gamepad_run_key}.json",
    )
    return comparison


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
) -> MouseAdsBenchmarkAggregateMetrics | None:
    if baseline_key is None or baseline_key == current_run_key:
        return None
    baseline_artifact = _load_artifact(artifact_dir, baseline_key)
    return MouseAdsBenchmarkAggregateMetrics(**baseline_artifact["aggregate_metrics"])


def _load_artifact(artifact_dir: Path, run_key: str) -> dict[str, Any]:
    artifact_path = artifact_dir / f"{run_key}.json"
    if not artifact_path.is_file():
        raise FileNotFoundError(f"Mouse benchmark run key not found: {run_key}")
    return _read_artifact(artifact_path)


def _read_artifact(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def _artifact_paths(artifact_dir: Path) -> list[Path]:
    if not artifact_dir.is_dir():
        return []
    return sorted(artifact_dir.glob("*.json"))


def _find_scenario_payload(
    artifact_dir: Path,
    scenario_key: str,
) -> tuple[dict[str, Any], dict[str, Any]]:
    for path in _artifact_paths(artifact_dir):
        artifact = _read_artifact(path)
        for scenario_payload in artifact.get("scenarios", []):
            manifest = scenario_payload.get("manifest", {})
            if manifest.get("scenario_key") == scenario_key:
                return artifact, scenario_payload
    raise FileNotFoundError(f"Mouse benchmark scenario key not found: {scenario_key}")


def _config_from_snapshot(snapshot: dict[str, Any]) -> MouseAdsBenchmarkConfig:
    return MouseAdsBenchmarkConfig(**snapshot)


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
    return f"mouse-ads-{datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')}"


def _default_run_seed() -> int:
    return int(datetime.now(timezone.utc).timestamp())


if __name__ == "__main__":
    raise SystemExit(main())
