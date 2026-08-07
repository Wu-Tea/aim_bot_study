from __future__ import annotations

import argparse
import glob
import json
import math
from pathlib import Path
from statistics import median
from typing import Any, Iterable


DEFAULT_LATENCIES = (
    "capture_to_result",
    "source_present_to_result",
    "source_present_to_vigem",
    "source_present_to_cuda_submit",
    "copy_to_cuda_submit",
    "cuda_submit_wait",
    "cuda_submit_phase_late",
    "gpu_total",
    "preprocess",
    "infer",
    "output_wait",
    "sync_queue_residual",
    "color_copy",
    "cuda_unmap",
)


def finite_number(value: Any) -> float | None:
    if not isinstance(value, (int, float)):
        return None
    number = float(value)
    return number if math.isfinite(number) else None


def expand_paths(patterns: Iterable[str]) -> list[Path]:
    paths: list[Path] = []
    for pattern in patterns:
        matches = [Path(value) for value in glob.glob(pattern)]
        if matches:
            paths.extend(matches)
        else:
            path = Path(pattern)
            if path.exists():
                paths.append(path)
    return sorted(set(path.resolve() for path in paths))


def load_records(paths: Iterable[Path], min_aiming_ratio: float) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    for path in paths:
        with path.open("r", encoding="utf-8") as stream:
            for line_number, line in enumerate(stream, start=1):
                line = line.strip()
                if not line:
                    continue
                try:
                    record = json.loads(line)
                except json.JSONDecodeError as exc:
                    raise ValueError(f"{path}:{line_number}: invalid JSON: {exc}") from exc
                if record.get("type") != "runtime_perf_summary":
                    continue
                aiming_ratio = finite_number(record.get("aiming_ratio"))
                if aiming_ratio is None or aiming_ratio < min_aiming_ratio:
                    continue
                records.append(record)
    return records


def weighted_mean(values: Iterable[tuple[float, float]]) -> float | None:
    numerator = 0.0
    denominator = 0.0
    for value, weight in values:
        if weight <= 0.0:
            continue
        numerator += value * weight
        denominator += weight
    return numerator / denominator if denominator > 0.0 else None


def summarize_latency(records: list[dict[str, Any]], name: str) -> dict[str, float] | None:
    values: list[dict[str, float]] = []
    for record in records:
        raw = record.get("latency_ms", {}).get(name)
        if not isinstance(raw, dict):
            continue
        count = finite_number(raw.get("n"))
        mean = finite_number(raw.get("mean"))
        p50 = finite_number(raw.get("p50"))
        p95 = finite_number(raw.get("p95"))
        maximum = finite_number(raw.get("max"))
        if count is None or count <= 0 or mean is None or p50 is None or p95 is None:
            continue
        values.append(
            {
                "n": count,
                "mean": mean,
                "p50": p50,
                "p95": p95,
                "max": maximum if maximum is not None else p95,
            }
        )
    if not values:
        return None
    return {
        "n": sum(value["n"] for value in values),
        "mean": weighted_mean((value["mean"], value["n"]) for value in values) or 0.0,
        "window_p50_median": median(value["p50"] for value in values),
        "window_p95_median": median(value["p95"] for value in values),
        "window_p95_worst": max(value["p95"] for value in values),
        "max": max(value["max"] for value in values),
    }


def summarize(records: list[dict[str, Any]]) -> dict[str, Any]:
    active_weighted: list[tuple[float, float]] = []
    accumulation_weighted: list[tuple[float, float]] = []
    submit_wait_applied_weighted: list[tuple[float, float]] = []
    target_phase_weighted: list[tuple[float, float]] = []
    adaptive_exploring_weighted: list[tuple[float, float]] = []
    submit_modes = sorted(
        {
            str(mode)
            for record in records
            if (mode := record.get("cuda_submit_phase_mode")) is not None
        }
    )
    cuda_submit_phases = sorted(
        {
            int(phase)
            for record in records
            if (phase := finite_number(record.get("cuda_submit_phase_us"))) is not None
        }
    )
    for record in records:
        window_ms = finite_number(record.get("window_ms")) or 0.0
        aiming_ratio = finite_number(record.get("aiming_ratio")) or 0.0
        active_seconds = window_ms * aiming_ratio / 1000.0
        active_hz = finite_number(record.get("vision_active_hz"))
        if active_hz is not None and active_seconds > 0.0:
            active_weighted.append((active_hz, active_seconds))
            active_frames = active_hz * active_seconds
            accumulated = finite_number(record.get("accumulated_active_gt1_pct"))
            if accumulated is not None:
                accumulation_weighted.append((accumulated, active_frames))
            wait_applied = finite_number(
                record.get("cuda_submit_wait_applied_active_pct")
            )
            if wait_applied is None:
                wait_applied = finite_number(
                    record.get("cuda_submit_wait_applied_pct")
                )
            if wait_applied is not None:
                submit_wait_applied_weighted.append((wait_applied, active_frames))
            target_phase = finite_number(
                record.get("cuda_submit_target_phase_active_mean_us")
            )
            if target_phase is not None:
                target_phase_weighted.append((target_phase, active_frames))
            exploring = finite_number(
                record.get("cuda_submit_adaptive_exploring_active_pct")
            )
            if exploring is not None:
                adaptive_exploring_weighted.append((exploring, active_frames))

    latency: dict[str, Any] = {}
    for name in DEFAULT_LATENCIES:
        value = summarize_latency(records, name)
        if value is not None:
            latency[name] = value
    return {
        "windows": len(records),
        "cuda_submit_phase_mode": (
            submit_modes[0]
            if len(submit_modes) == 1
            else (submit_modes if submit_modes else None)
        ),
        "cuda_submit_phase_us": (
            cuda_submit_phases[0]
            if len(cuda_submit_phases) == 1
            else (cuda_submit_phases if cuda_submit_phases else None)
        ),
        "cuda_submit_wait_applied_pct": weighted_mean(
            submit_wait_applied_weighted
        ),
        "cuda_submit_target_phase_active_mean_us": weighted_mean(
            target_phase_weighted
        ),
        "cuda_submit_adaptive_exploring_active_pct": weighted_mean(
            adaptive_exploring_weighted
        ),
        "vision_active_hz": weighted_mean(active_weighted),
        "accumulated_active_gt1_pct": weighted_mean(accumulation_weighted),
        "writer_queue_dropped_total": max(
            (int(record.get("writer_queue_dropped_total", 0)) for record in records),
            default=0,
        ),
        "latency_ms": latency,
    }


def percent_change(baseline: float | None, candidate: float | None) -> float | None:
    if baseline is None or candidate is None or baseline == 0.0:
        return None
    return 100.0 * (candidate / baseline - 1.0)


def comparison(baseline: dict[str, Any], candidate: dict[str, Any]) -> dict[str, Any]:
    result: dict[str, Any] = {
        "cuda_submit_phase_mode": {
            "baseline": baseline.get("cuda_submit_phase_mode"),
            "candidate": candidate.get("cuda_submit_phase_mode"),
        },
        "cuda_submit_phase_us": {
            "baseline": baseline.get("cuda_submit_phase_us"),
            "candidate": candidate.get("cuda_submit_phase_us"),
        },
        "cuda_submit_wait_applied_pct": {
            "baseline": baseline.get("cuda_submit_wait_applied_pct"),
            "candidate": candidate.get("cuda_submit_wait_applied_pct"),
        },
        "cuda_submit_target_phase_active_mean_us": {
            "baseline": baseline.get("cuda_submit_target_phase_active_mean_us"),
            "candidate": candidate.get("cuda_submit_target_phase_active_mean_us"),
        },
        "cuda_submit_adaptive_exploring_active_pct": {
            "baseline": baseline.get("cuda_submit_adaptive_exploring_active_pct"),
            "candidate": candidate.get("cuda_submit_adaptive_exploring_active_pct"),
        },
        "vision_active_hz": {
            "baseline": baseline.get("vision_active_hz"),
            "candidate": candidate.get("vision_active_hz"),
            "change_pct": percent_change(
                baseline.get("vision_active_hz"), candidate.get("vision_active_hz")
            ),
        },
        "accumulated_active_gt1_pct": {
            "baseline": baseline.get("accumulated_active_gt1_pct"),
            "candidate": candidate.get("accumulated_active_gt1_pct"),
            "change_pct": percent_change(
                baseline.get("accumulated_active_gt1_pct"),
                candidate.get("accumulated_active_gt1_pct"),
            ),
        },
        "latency_ms": {},
    }
    names = sorted(
        set(baseline.get("latency_ms", {})) | set(candidate.get("latency_ms", {}))
    )
    for name in names:
        before = baseline.get("latency_ms", {}).get(name)
        after = candidate.get("latency_ms", {}).get(name)
        if before is None or after is None:
            continue
        result["latency_ms"][name] = {
            "baseline_mean": before["mean"],
            "candidate_mean": after["mean"],
            "mean_change_pct": percent_change(before["mean"], after["mean"]),
            "baseline_window_p95_median": before["window_p95_median"],
            "candidate_window_p95_median": after["window_p95_median"],
            "p95_change_pct": percent_change(
                before["window_p95_median"], after["window_p95_median"]
            ),
        }
    return result


def format_value(value: float | None, suffix: str = "") -> str:
    return "n/a" if value is None else f"{value:.3f}{suffix}"


def print_report(result: dict[str, Any]) -> None:
    mode = result["cuda_submit_phase_mode"]
    print(
        "cuda_submit_phase_mode: "
        f"{mode['baseline']} -> {mode['candidate']}"
    )
    phase = result["cuda_submit_phase_us"]
    print(
        "cuda_submit_phase_us: "
        f"{phase['baseline']} -> {phase['candidate']}"
    )
    applied = result["cuda_submit_wait_applied_pct"]
    print(
        "cuda_submit_wait_applied_pct: "
        f"{format_value(applied['baseline'], '%')} -> "
        f"{format_value(applied['candidate'], '%')}"
    )
    target = result["cuda_submit_target_phase_active_mean_us"]
    print(
        "cuda_submit_target_phase_active_mean_us: "
        f"{format_value(target['baseline'])} -> "
        f"{format_value(target['candidate'])}"
    )
    exploring = result["cuda_submit_adaptive_exploring_active_pct"]
    print(
        "cuda_submit_adaptive_exploring_active_pct: "
        f"{format_value(exploring['baseline'], '%')} -> "
        f"{format_value(exploring['candidate'], '%')}"
    )
    rate = result["vision_active_hz"]
    print(
        "vision_active_hz: "
        f"{format_value(rate['baseline'])} -> {format_value(rate['candidate'])} "
        f"({format_value(rate['change_pct'], '%')})"
    )
    accumulated = result["accumulated_active_gt1_pct"]
    print(
        "accumulated_active_gt1_pct: "
        f"{format_value(accumulated['baseline'], '%')} -> "
        f"{format_value(accumulated['candidate'], '%')} "
        f"({format_value(accumulated['change_pct'], '%')})"
    )
    print("latency metric                 mean before/after      window P95 before/after")
    for name, value in result["latency_ms"].items():
        mean_text = (
            f"{value['baseline_mean']:.3f}/{value['candidate_mean']:.3f} "
            f"({format_value(value['mean_change_pct'], '%')})"
        )
        p95_text = (
            f"{value['baseline_window_p95_median']:.3f}/"
            f"{value['candidate_window_p95_median']:.3f} "
            f"({format_value(value['p95_change_pct'], '%')})"
        )
        print(f"{name:<30} {mean_text:<25} {p95_text}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare active windows from runtime.performance JSONL logs."
    )
    parser.add_argument("--baseline", nargs="+", required=True, help="Baseline files or globs")
    parser.add_argument("--candidate", nargs="+", required=True, help="Candidate files or globs")
    parser.add_argument("--min-aiming-ratio", type=float, default=0.50)
    parser.add_argument("--output-json", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    baseline_paths = expand_paths(args.baseline)
    candidate_paths = expand_paths(args.candidate)
    if not baseline_paths or not candidate_paths:
        raise SystemExit("baseline and candidate must each resolve to at least one file")
    baseline_records = load_records(baseline_paths, args.min_aiming_ratio)
    candidate_records = load_records(candidate_paths, args.min_aiming_ratio)
    if not baseline_records or not candidate_records:
        raise SystemExit("no active windows passed --min-aiming-ratio")
    payload = {
        "baseline_files": [str(path) for path in baseline_paths],
        "candidate_files": [str(path) for path in candidate_paths],
        "min_aiming_ratio": args.min_aiming_ratio,
        "baseline": summarize(baseline_records),
        "candidate": summarize(candidate_records),
    }
    payload["comparison"] = comparison(payload["baseline"], payload["candidate"])
    print_report(payload["comparison"])
    if args.output_json is not None:
        args.output_json.parent.mkdir(parents=True, exist_ok=True)
        args.output_json.write_text(
            json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
