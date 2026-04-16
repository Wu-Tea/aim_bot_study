from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
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
from tests.gamepad.benchmark_metrics import BenchmarkAggregateMetrics, BenchmarkMetricsConfig, evaluate_run, evaluate_scenario
from tests.gamepad.benchmark_scenarios import ScenarioManifest, generate_phase1_manifests
from tests.gamepad.benchmark_scoreboard import ScoreboardRunEntry, extract_baseline_key, update_scoreboard

DEFAULT_ARTIFACT_DIR = PROJECT_ROOT / "artifacts" / "benchmarks" / "gamepad"
DEFAULT_SCOREBOARD_PATH = PROJECT_ROOT / "docs" / "project" / "GAMEPAD_BENCHMARKS.md"
DEFAULT_SCENARIO_LOGIC = (
    "steady_turns: 8 scenarios with one or more heading changes and no hard stop",
    "turn_then_decel: 8 scenarios with a turn followed by a deceleration event",
    "decel_resume: 8 scenarios with a deceleration event and optional resume",
)


@dataclass(frozen=True, slots=True)
class GitMetadata:
    commit: str | None
    dirty: bool


def run_benchmark(
    *,
    run_key: str,
    run_seed: int,
    artifact_dir: Path = DEFAULT_ARTIFACT_DIR,
    scoreboard_path: Path = DEFAULT_SCOREBOARD_PATH,
    git_metadata: GitMetadata | None = None,
    set_baseline: bool = False,
    benchmark_config: BenchmarkMetricsConfig | None = None,
    timestamp: str | None = None,
) -> dict[str, Any]:
    artifact_dir.mkdir(parents=True, exist_ok=True)
    benchmark_config = benchmark_config or BenchmarkMetricsConfig()
    timestamp = timestamp or _utc_timestamp()
    git_metadata = git_metadata or detect_git_metadata(PROJECT_ROOT)
    baseline_key = run_key if set_baseline else extract_baseline_key(scoreboard_path)

    manifests = generate_phase1_manifests(run_key, run_seed)
    summary = evaluate_run(run_key, manifests, config=benchmark_config)
    baseline_metrics = _load_baseline_aggregate(artifact_dir, baseline_key, current_run_key=run_key)
    delta_metrics = None if baseline_metrics is None else asdict(summary.relative_deltas(baseline_metrics))

    artifact = _build_artifact(
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
    )
    return artifact


def replay_run_key(
    *,
    run_key: str,
    artifact_dir: Path = DEFAULT_ARTIFACT_DIR,
    benchmark_config: BenchmarkMetricsConfig | None = None,
) -> dict[str, Any]:
    artifact = _load_artifact(artifact_dir, run_key)
    manifests = tuple(ScenarioManifest.from_dict(item["manifest"]) for item in artifact["scenarios"])
    benchmark_config = benchmark_config or _config_from_snapshot(artifact["benchmark_config"])
    summary = evaluate_run(run_key, manifests, config=benchmark_config)
    baseline_metrics = _load_baseline_aggregate(
        artifact_dir,
        artifact.get("baseline_key"),
        current_run_key=run_key,
    )
    delta_metrics = None if baseline_metrics is None else asdict(summary.relative_deltas(baseline_metrics))
    return {
        "run_key": run_key,
        "baseline_key": artifact.get("baseline_key"),
        "benchmark_config": asdict(benchmark_config),
        "aggregate_metrics": asdict(summary.aggregate),
        "relative_deltas_vs_baseline": delta_metrics,
        "scenarios": _scenario_payloads(manifests, summary),
    }


def replay_scenario_key(
    *,
    scenario_key: str,
    artifact_dir: Path = DEFAULT_ARTIFACT_DIR,
    benchmark_config: BenchmarkMetricsConfig | None = None,
) -> dict[str, Any]:
    artifact, scenario_payload = _find_scenario_payload(artifact_dir, scenario_key)
    manifest = ScenarioManifest.from_dict(scenario_payload["manifest"])
    benchmark_config = benchmark_config or _config_from_snapshot(artifact["benchmark_config"])
    metrics = evaluate_scenario(manifest, config=benchmark_config)
    return {
        "run_key": artifact["run_key"],
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
    parser.add_argument("--set-baseline", action="store_true", help="Run the suite and mark the resulting run as baseline.")
    parser.add_argument("--replay-run-key", help="Replay every stored scenario for an existing run key.")
    parser.add_argument("--replay-scenario-key", help="Replay a stored scenario manifest by scenario key.")
    parser.add_argument("--run-key", help="Optional explicit run key for normal benchmark runs.")
    parser.add_argument("--run-seed", type=int, help="Optional explicit RNG seed for scenario generation.")
    parser.add_argument("--artifact-dir", type=Path, default=DEFAULT_ARTIFACT_DIR)
    parser.add_argument("--scoreboard-path", type=Path, default=DEFAULT_SCOREBOARD_PATH)
    args = parser.parse_args(list(argv) if argv is not None else None)

    replay_flags = [bool(args.replay_run_key), bool(args.replay_scenario_key)]
    if sum(replay_flags) > 1:
        parser.error("--replay-run-key and --replay-scenario-key are mutually exclusive")
    if args.set_baseline and any(replay_flags):
        parser.error("--set-baseline cannot be combined with replay arguments")

    try:
        if args.replay_run_key:
            result = replay_run_key(
                run_key=args.replay_run_key,
                artifact_dir=args.artifact_dir,
            )
        elif args.replay_scenario_key:
            result = replay_scenario_key(
                scenario_key=args.replay_scenario_key,
                artifact_dir=args.artifact_dir,
            )
        else:
            run_key = args.run_key or _default_run_key()
            run_seed = args.run_seed if args.run_seed is not None else _default_run_seed()
            result = run_benchmark(
                run_key=run_key,
                run_seed=run_seed,
                artifact_dir=args.artifact_dir,
                scoreboard_path=args.scoreboard_path,
                set_baseline=args.set_baseline,
            )
        print(json.dumps(result, indent=2))
        return 0
    except FileNotFoundError as exc:
        print(str(exc), file=sys.stderr)
        return 1


def _build_artifact(
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
) -> BenchmarkAggregateMetrics | None:
    if baseline_key is None or baseline_key == current_run_key:
        return None
    baseline_artifact = _load_artifact(artifact_dir, baseline_key)
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
            if manifest.get("scenario_key") == scenario_key:
                return artifact, scenario_payload
    raise FileNotFoundError(f"Benchmark scenario key not found: {scenario_key}")


def _config_from_snapshot(snapshot: dict[str, Any]) -> BenchmarkMetricsConfig:
    config_fields = BenchmarkMetricsConfig.__dataclass_fields__
    return BenchmarkMetricsConfig(
        **{field_name: snapshot[field_name] for field_name in config_fields}
    )


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
