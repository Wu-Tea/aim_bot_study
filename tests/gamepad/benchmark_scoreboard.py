from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Mapping, Sequence
import re


BASELINE_KEY_PATTERN = re.compile(r"Baseline Run Key: `([^`]+)`")


@dataclass(frozen=True, slots=True)
class ScoreboardRunEntry:
    run_key: str
    timestamp: str
    artifact_path: str
    git_commit: str | None
    dirty: bool
    aggregate_metrics: Mapping[str, float | int | None]
    delta_metrics: Mapping[str, float | None] | None = None


def update_scoreboard(
    *,
    scoreboard_path: Path,
    latest_run: ScoreboardRunEntry,
    all_runs: Sequence[ScoreboardRunEntry],
    baseline_key: str | None,
    benchmark_parameters: Mapping[str, object],
    scenario_logic: Sequence[str],
) -> str:
    content = render_scoreboard(
        latest_run=latest_run,
        all_runs=all_runs,
        baseline_key=baseline_key,
        benchmark_parameters=benchmark_parameters,
        scenario_logic=scenario_logic,
    )
    scoreboard_path.parent.mkdir(parents=True, exist_ok=True)
    scoreboard_path.write_text(content, encoding="utf-8")
    return content


def render_scoreboard(
    *,
    latest_run: ScoreboardRunEntry,
    all_runs: Sequence[ScoreboardRunEntry],
    baseline_key: str | None,
    benchmark_parameters: Mapping[str, object],
    scenario_logic: Sequence[str],
) -> str:
    runs_by_key = {run.run_key: run for run in all_runs}
    baseline_run = runs_by_key.get(baseline_key) if baseline_key else None
    history_runs = [
        run
        for run in sorted(all_runs, key=lambda item: item.timestamp, reverse=True)
        if baseline_key is not None and run.run_key != baseline_key
    ]

    lines = [
        "# Gamepad Benchmarks",
        "",
        "## Baseline Definition",
        "",
        *_render_baseline_definition(baseline_run),
        "",
        "## Benchmark Parameters",
        "",
        *_render_benchmark_parameters(benchmark_parameters),
        "",
        "## Scenario Logic",
        "",
        *_render_scenario_logic(scenario_logic),
        "",
        "## Latest Run",
        "",
        "### Latest Run Summary",
        "",
        *_render_run_summary(latest_run, baseline_key=baseline_key),
        "",
        "## History vs Baseline",
        "",
        *_render_history(history_runs),
    ]
    return "\n".join(lines).rstrip() + "\n"


def extract_baseline_key(scoreboard_path: Path) -> str | None:
    if not scoreboard_path.is_file():
        return None
    match = BASELINE_KEY_PATTERN.search(scoreboard_path.read_text(encoding="utf-8"))
    return match.group(1) if match else None


def _render_baseline_definition(baseline_run: ScoreboardRunEntry | None) -> list[str]:
    if baseline_run is None:
        return ["No baseline has been recorded yet."]

    return [
        f"- Baseline Run Key: `{baseline_run.run_key}`",
        f"- Timestamp: `{baseline_run.timestamp}`",
        f"- Artifact: `{baseline_run.artifact_path}`",
        f"- Git Commit: `{baseline_run.git_commit or 'unknown'}`",
        f"- Dirty Worktree: `{'true' if baseline_run.dirty else 'false'}`",
    ]


def _render_benchmark_parameters(benchmark_parameters: Mapping[str, object]) -> list[str]:
    return [f"- `{key}`: `{value}`" for key, value in benchmark_parameters.items()]


def _render_scenario_logic(scenario_logic: Sequence[str]) -> list[str]:
    return [f"- {line}" for line in scenario_logic]


def _render_run_summary(run: ScoreboardRunEntry, *, baseline_key: str | None) -> list[str]:
    lines = [
        f"- Run Key: `{run.run_key}`",
        f"- Timestamp: `{run.timestamp}`",
        f"- Artifact: `{run.artifact_path}`",
        f"- Git Commit: `{run.git_commit or 'unknown'}`",
        f"- Dirty Worktree: `{'true' if run.dirty else 'false'}`",
    ]
    if baseline_key is None:
        lines.append("- Baseline Comparison: no baseline available yet")
    else:
        lines.append(f"- Baseline Comparison Key: `{baseline_key}`")

    lines.extend(
        [
            "",
            "| Metric | Value | Delta vs Baseline |",
            "| --- | --- | --- |",
        ]
    )
    for metric_name, metric_value in run.aggregate_metrics.items():
        delta_value = None if run.delta_metrics is None else run.delta_metrics.get(metric_name)
        lines.append(
            f"| `{metric_name}` | `{metric_value}` | `{_format_delta(delta_value)}` |"
        )
    return lines


def _render_history(history_runs: Sequence[ScoreboardRunEntry]) -> list[str]:
    if not history_runs:
        return ["No comparison runs recorded yet."]

    lines = [
        "| Run Key | Timestamp | Artifact | Dirty | Mean Error Delta | P95 Delta | P99 Delta | Overshoot Delta | Max Overshoot Delta | Turn Recovery Delta | Decel Settle Delta |",
        "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |",
    ]
    for run in history_runs:
        delta_metrics = run.delta_metrics or {}
        lines.append(
            "| "
            + " | ".join(
                [
                    f"`{run.run_key}`",
                    run.timestamp,
                    f"`{run.artifact_path}`",
                    "dirty" if run.dirty else "clean",
                    _format_delta(delta_metrics.get("mean_error_px")),
                    _format_delta(delta_metrics.get("p95_error_px")),
                    _format_delta(delta_metrics.get("p99_error_px")),
                    _format_delta(delta_metrics.get("overshoot_events")),
                    _format_delta(delta_metrics.get("max_overshoot_px")),
                    _format_delta(delta_metrics.get("mean_recovery_frames_after_turn")),
                    _format_delta(delta_metrics.get("mean_settle_frames_after_decel")),
                ]
            )
            + " |"
        )
    return lines


def _format_delta(value: float | None) -> str:
    if value is None:
        return "n/a"
    return f"{value:+.2%}"
