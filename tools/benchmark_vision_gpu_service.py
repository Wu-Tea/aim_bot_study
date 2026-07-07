from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
import json
from pathlib import Path
import math
from typing import Any, Iterable, Sequence


Window = tuple[float, float]


@dataclass(frozen=True, slots=True)
class StrategyConfig:
    name: str
    active_hz: float
    idle_hz: float = 0.0
    repeat_on_no_update: bool = False
    prewarm: bool = False
    steady_gpu_total_ms: float = 6.5
    cold_gpu_total_ms: float = 55.0
    cold_after_ms: float = 300.0
    output_wait_ms: float = 2.0
    preprocess_ms: float = 0.08


DEFAULT_STRATEGIES: dict[str, StrategyConfig] = {
    "current_sync_poll": StrategyConfig(
        name="current_sync_poll",
        active_hz=50.0,
        cold_gpu_total_ms=55.0,
        output_wait_ms=7.0,
        preprocess_ms=0.45,
    ),
    "warmup_only": StrategyConfig(
        name="warmup_only",
        active_hz=50.0,
        prewarm=True,
        cold_gpu_total_ms=25.0,
        output_wait_ms=6.0,
        preprocess_ms=0.35,
    ),
    "idle_low_rate_keepwarm": StrategyConfig(
        name="idle_low_rate_keepwarm",
        active_hz=70.0,
        idle_hz=15.0,
        cold_gpu_total_ms=18.0,
        output_wait_ms=4.0,
        preprocess_ms=0.18,
    ),
    "repeat_last_keepwarm": StrategyConfig(
        name="repeat_last_keepwarm",
        active_hz=80.0,
        idle_hz=15.0,
        repeat_on_no_update=True,
        cold_gpu_total_ms=14.0,
        output_wait_ms=3.5,
        preprocess_ms=0.16,
    ),
    "independent_worker": StrategyConfig(
        name="independent_worker",
        active_hz=100.0,
        cold_gpu_total_ms=45.0,
        output_wait_ms=1.2,
        preprocess_ms=0.10,
    ),
    "worker_keepwarm": StrategyConfig(
        name="worker_keepwarm",
        active_hz=100.0,
        idle_hz=20.0,
        repeat_on_no_update=True,
        cold_gpu_total_ms=14.0,
        output_wait_ms=1.0,
        preprocess_ms=0.08,
    ),
    "always_full_rate": StrategyConfig(
        name="always_full_rate",
        active_hz=100.0,
        idle_hz=100.0,
        repeat_on_no_update=True,
        cold_gpu_total_ms=10.0,
        output_wait_ms=1.0,
        preprocess_ms=0.08,
    ),
}


def _in_windows(value_ms: float, windows: Sequence[Window]) -> bool:
    return any(start_ms <= value_ms < end_ms for start_ms, end_ms in windows)


def _window_duration_ms(windows: Sequence[Window]) -> float:
    return sum(max(0.0, end_ms - start_ms) for start_ms, end_ms in windows)


def _period_ms(hz: float) -> float:
    return 1000.0 / hz if hz > 0.0 else float("inf")


def _should_infer(
    now_ms: float,
    last_infer_ms: float | None,
    period_ms: float,
) -> bool:
    if period_ms == float("inf"):
        return False
    if last_infer_ms is None:
        return True
    return now_ms - last_infer_ms >= period_ms - 1e-6


def _gpu_total_ms(config: StrategyConfig, now_ms: float, last_infer_ms: float | None) -> float:
    if config.prewarm and last_infer_ms is None:
        return config.steady_gpu_total_ms
    if last_infer_ms is None or now_ms - last_infer_ms >= config.cold_after_ms:
        return config.cold_gpu_total_ms
    return config.steady_gpu_total_ms


def simulate_strategy(
    config: StrategyConfig,
    *,
    duration_ms: float,
    controller_hz: float,
    active_windows: Sequence[Window],
    no_update_windows: Sequence[Window],
) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    tick_ms = _period_ms(controller_hz)
    tick = 0
    now_ms = 0.0
    frame_id = 0
    last_gpu_infer_ms: float | None = None
    next_service_ms: float | None = None
    latest_snapshot: dict[str, Any] | None = None
    consumed_sequence = 0
    sequence = 0

    while now_ms <= duration_ms + 1e-6:
        active = _in_windows(now_ms, active_windows)
        next_service_ms = _next_service_due_ms(
            now_ms=now_ms,
            next_service_ms=next_service_ms,
            active_windows=active_windows,
            duration_ms=duration_ms,
            config=config,
        )
        while next_service_ms is not None and next_service_ms <= now_ms + 1e-6:
            service_active = _in_windows(next_service_ms, active_windows)
            source_available = not _in_windows(next_service_ms, no_update_windows)
            service_hz = config.active_hz if service_active else config.idle_hz
            service_period_ms = _period_ms(service_hz)
            can_repeat = config.repeat_on_no_update and latest_snapshot is not None
            if service_hz > 0.0 and (source_available or can_repeat):
                reused_source = not source_available
                gpu_total_ms = (
                    0.0
                    if reused_source
                    else _gpu_total_ms(config, next_service_ms, last_gpu_infer_ms)
                )
                if not reused_source:
                    last_gpu_infer_ms = next_service_ms
                frame_id += 1
                sequence += 1
                latest_snapshot = {
                    "frame_updated": True,
                    "frame_id": frame_id,
                    "service_sequence": sequence,
                    "source": "synthetic_repeat" if reused_source else "synthetic",
                    "source_state": "repeat_last" if reused_source else "fresh",
                    "freshness": "reused" if reused_source else "fresh",
                    "preprocess_mode": "old_bgra_copy" if not reused_source else "none",
                    "preprocess_ms": 0.0 if reused_source else config.preprocess_ms,
                    "infer_ms": max(0.0, gpu_total_ms - config.preprocess_ms),
                    "gpu_total_ms": gpu_total_ms,
                    "gpu_work_start_ms": next_service_ms,
                    "output_wait_ms": config.output_wait_ms,
                    "age_ms": gpu_total_ms + config.output_wait_ms + max(0.0, now_ms - next_service_ms),
                    "vision_age_ms": gpu_total_ms + config.output_wait_ms + max(0.0, now_ms - next_service_ms),
                    "consume_ms": gpu_total_ms + config.output_wait_ms,
                    "output_age_ms": gpu_total_ms + config.output_wait_ms + max(0.0, now_ms - next_service_ms),
                    "ctrl_loop_ms": tick_ms + min(gpu_total_ms, 50.0) * 0.01,
                }
            if service_period_ms == float("inf"):
                next_service_ms = None
            else:
                next_service_ms = round(next_service_ms + service_period_ms, 6)

        row = _empty_row(tick=tick, now_ms=now_ms, active=active)
        if latest_snapshot is not None and latest_snapshot["service_sequence"] != consumed_sequence:
            consumed_sequence = int(latest_snapshot["service_sequence"])
            row.update(latest_snapshot)
        rows.append(row)
        tick += 1
        now_ms = round(tick * tick_ms, 6)
    return rows


def _next_service_due_ms(
    *,
    now_ms: float,
    next_service_ms: float | None,
    active_windows: Sequence[Window],
    duration_ms: float,
    config: StrategyConfig,
) -> float | None:
    if next_service_ms is not None:
        return next_service_ms
    if _in_windows(now_ms, active_windows):
        return now_ms
    if config.idle_hz > 0.0:
        return now_ms
    future_active_starts = [start_ms for start_ms, _ in active_windows if start_ms >= now_ms - 1e-6]
    if future_active_starts:
        return min(future_active_starts)
    if duration_ms >= now_ms and _in_windows(duration_ms, active_windows):
        return duration_ms
    return None


def _empty_row(*, tick: int, now_ms: float, active: bool) -> dict[str, Any]:
    return {
        "tick": tick,
        "relative_ms": now_ms,
        "aiming": active,
        "frame_updated": False,
        "frame_id": 0,
        "target": False,
        "tier": "none",
        "source": "none",
        "stage": "none",
        "aim_authority": False,
        "fire_authority": False,
        "confidence": 0.0,
        "dx": 0.0,
        "dy": 0.0,
        "capture_ms": 0.0,
        "copy_ms": 0.0,
        "capture_transfer_ms": 0.0,
        "cuda_map_ms": 0.0,
        "preprocess_mode": "none",
        "preprocess_ms": 0.0,
        "infer_ms": 0.0,
        "gpu_total_ms": 0.0,
        "output_wait_ms": 0.0,
        "decode_ms": 0.0,
        "selector_ms": 0.0,
        "enhance_ms": 0.0,
        "post_ms": 0.0,
        "age_ms": 0.0,
        "vision_age_ms": 0.0,
        "boxes_seen": 0,
        "consume_ms": 0.0,
        "out_age_ms": 0.0,
        "output_age_ms": 0.0,
        "ctrl_loop_ms": 0.0,
        "ctrl_pipeline_ms": 0.0,
        "vigem_update_ms": 0.0,
    }


def _percentile(values: list[float], percentile: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, round((len(ordered) - 1) * percentile / 100.0)))
    return ordered[index]


def _updated_rows(rows: Iterable[dict[str, Any]]) -> list[dict[str, Any]]:
    return [row for row in rows if row.get("frame_updated") and row.get("frame_id", 0) > 0]


def _intervals_ms(rows: list[dict[str, Any]]) -> list[float]:
    intervals: list[float] = []
    previous_ms: float | None = None
    for row in rows:
        now_ms = float(row["relative_ms"])
        if previous_ms is not None:
            intervals.append(now_ms - previous_ms)
        previous_ms = now_ms
    return intervals


def _activation_rows(rows: list[dict[str, Any]], active_windows: Sequence[Window]) -> list[dict[str, Any]]:
    activations: list[dict[str, Any]] = []
    for start_ms, end_ms in active_windows:
        for row in rows:
            now_ms = float(row["relative_ms"])
            if start_ms <= now_ms < end_ms and row.get("frame_updated"):
                activations.append(row)
                break
    return activations


def _gpu_work_ms(row: dict[str, Any]) -> float:
    if row.get("freshness") == "reused" or row.get("source_state") == "repeat_last":
        return 0.0
    return float(row.get("gpu_total_ms", 0.0) or 0.0)


def _overlap_ms(a_start: float, a_end: float, b_start: float, b_end: float) -> float:
    return max(0.0, min(a_end, b_end) - max(a_start, b_start))


def _sum_gpu_work_ms(rows: list[dict[str, Any]], windows: Sequence[Window]) -> float:
    total = 0.0
    for row in _updated_rows(rows):
        start_ms = float(row.get("gpu_work_start_ms", row["relative_ms"]))
        work_ms = _gpu_work_ms(row)
        if work_ms <= 0.0:
            continue
        end_ms = start_ms + work_ms
        for window_start, window_end in windows:
            total += _overlap_ms(start_ms, end_ms, window_start, window_end)
    return total


def _bucket_windows(windows: Sequence[Window], bucket_ms: float) -> list[Window]:
    buckets: list[Window] = []
    for start_ms, end_ms in windows:
        cursor = start_ms
        while cursor < end_ms - 1e-6:
            bucket_end = min(end_ms, cursor + bucket_ms)
            buckets.append((cursor, bucket_end))
            cursor = bucket_end
    return buckets


def _population_stdev(values: Sequence[float]) -> float:
    if not values:
        return 0.0
    mean = sum(values) / len(values)
    return math.sqrt(sum((value - mean) ** 2 for value in values) / len(values))


def _occupancy_stability(
    rows: list[dict[str, Any]],
    windows: Sequence[Window],
    *,
    bucket_ms: float,
) -> dict[str, Any]:
    bucket_values: list[float] = []
    for start_ms, end_ms in _bucket_windows(windows, bucket_ms):
        duration_ms = max(0.0, end_ms - start_ms)
        if duration_ms <= 0.0:
            continue
        work_ms = _sum_gpu_work_ms(rows, ((start_ms, end_ms),))
        bucket_values.append(work_ms * 100.0 / duration_ms)

    mean = sum(bucket_values) / len(bucket_values) if bucket_values else 0.0
    stdev = _population_stdev(bucket_values)
    return {
        "bucket_ms": bucket_ms,
        "bucket_count": len(bucket_values),
        "mean_pct": mean,
        "min_pct": min(bucket_values) if bucket_values else 0.0,
        "max_pct": max(bucket_values) if bucket_values else 0.0,
        "p05_pct": _percentile(bucket_values, 5),
        "p50_pct": _percentile(bucket_values, 50),
        "p95_pct": _percentile(bucket_values, 95),
        "stdev_pct_points": stdev,
        "cv": (stdev / mean) if mean > 1e-6 else 0.0,
        "zero_or_near_zero_buckets": sum(1 for value in bucket_values if value < 1.0),
    }


def _occupancy_summary(
    rows: list[dict[str, Any]],
    *,
    duration_ms: float,
    active_windows: Sequence[Window],
    bucket_ms: float = 500.0,
) -> dict[str, Any]:
    active_duration_ms = _window_duration_ms(active_windows)
    idle_duration_ms = max(0.0, duration_ms - active_duration_ms)
    all_windows = ((0.0, duration_ms),)
    idle_windows = _idle_windows(duration_ms, active_windows)
    total_work_ms = _sum_gpu_work_ms(rows, all_windows)
    active_work_ms = _sum_gpu_work_ms(rows, active_windows)
    idle_work_ms = _sum_gpu_work_ms(rows, idle_windows)
    return {
        "estimated_gpu_work_ms": {
            "total": total_work_ms,
            "active": active_work_ms,
            "idle": idle_work_ms,
        },
        "estimated_gpu_occupancy_pct": {
            "overall": (total_work_ms * 100.0 / duration_ms) if duration_ms else 0.0,
            "active": (active_work_ms * 100.0 / active_duration_ms) if active_duration_ms else 0.0,
            "idle": (idle_work_ms * 100.0 / idle_duration_ms) if idle_duration_ms else 0.0,
        },
        "gpu_occupancy_stability": {
            "overall": _occupancy_stability(rows, all_windows, bucket_ms=bucket_ms),
            "active": _occupancy_stability(rows, active_windows, bucket_ms=bucket_ms),
            "idle": _occupancy_stability(rows, idle_windows, bucket_ms=bucket_ms),
        },
    }


def _efficiency_summary(summary: dict[str, Any]) -> dict[str, float]:
    occupancy = summary["estimated_gpu_occupancy_pct"]
    overall = float(occupancy["overall"])
    active = float(occupancy["active"])
    snapshot_fps = float(summary["active_snapshot_fps"])
    fresh_fps = float(summary["active_fresh_source_fps"])
    return {
        "active_snapshot_fps_per_overall_gpu_pct": snapshot_fps / overall if overall > 1e-6 else 0.0,
        "active_fresh_fps_per_overall_gpu_pct": fresh_fps / overall if overall > 1e-6 else 0.0,
        "active_snapshot_fps_per_active_gpu_pct": snapshot_fps / active if active > 1e-6 else 0.0,
        "active_fresh_fps_per_active_gpu_pct": fresh_fps / active if active > 1e-6 else 0.0,
    }


def _idle_windows(duration_ms: float, active_windows: Sequence[Window]) -> list[Window]:
    merged = sorted((max(0.0, start), min(duration_ms, end)) for start, end in active_windows)
    idle: list[Window] = []
    cursor = 0.0
    for start_ms, end_ms in merged:
        if end_ms <= cursor:
            continue
        if start_ms > cursor:
            idle.append((cursor, start_ms))
        cursor = max(cursor, end_ms)
    if cursor < duration_ms:
        idle.append((cursor, duration_ms))
    return idle


def summarize_rows(
    rows: list[dict[str, Any]],
    active_windows: Sequence[Window],
    *,
    duration_ms: float | None = None,
) -> dict[str, Any]:
    if duration_ms is None:
        duration_ms = max((float(row["relative_ms"]) for row in rows), default=0.0)
    updated = _updated_rows(rows)
    active_updated = [row for row in updated if _in_windows(float(row["relative_ms"]), active_windows)]
    active_fresh_source = [row for row in active_updated if row.get("freshness") == "fresh"]
    active_reused_source = [row for row in active_updated if row.get("freshness") == "reused"]
    intervals = _intervals_ms(updated)
    active_duration_ms = _window_duration_ms(active_windows)
    gaps = [value for value in intervals if value >= 100.0]
    activation_rows = _activation_rows(rows, active_windows)
    activation_gpu = [float(row["gpu_total_ms"]) for row in activation_rows]
    gpu_values = [float(row["gpu_total_ms"]) for row in updated]
    age_values = [float(row["vision_age_ms"]) for row in updated]
    summary = {
        "rows": len(rows),
        "updated_rows": len(updated),
        "active_updated_rows": len(active_updated),
        "active_snapshot_fps": (len(active_updated) * 1000.0 / active_duration_ms) if active_duration_ms else 0.0,
        "active_fresh_source_fps": (len(active_fresh_source) * 1000.0 / active_duration_ms) if active_duration_ms else 0.0,
        "active_reused_source_rows": len(active_reused_source),
        "long_gap_ms": {
            "count": len(gaps),
            "max": max(gaps) if gaps else 0.0,
            "p95": _percentile(gaps, 95),
        },
        "activation_gpu_total_ms": {
            "count": len(activation_gpu),
            "max": max(activation_gpu) if activation_gpu else 0.0,
            "p95": _percentile(activation_gpu, 95),
        },
        "gpu_total_ms": {
            "p50": _percentile(gpu_values, 50),
            "p95": _percentile(gpu_values, 95),
            "max": max(gpu_values) if gpu_values else 0.0,
        },
        "vision_age_ms": {
            "p50": _percentile(age_values, 50),
            "p95": _percentile(age_values, 95),
            "max": max(age_values) if age_values else 0.0,
        },
        "reused_source_rows": sum(1 for row in updated if row.get("freshness") == "reused"),
    }
    summary.update(
        _occupancy_summary(
            rows,
            duration_ms=duration_ms,
            active_windows=active_windows,
        )
    )
    summary["gpu_efficiency"] = _efficiency_summary(summary)
    return summary


def run_benchmark(
    *,
    duration_ms: float = 8000.0,
    controller_hz: float = 100.0,
    active_windows: Sequence[Window] = ((1500.0, 3500.0), (5000.0, 7000.0)),
    no_update_windows: Sequence[Window] = ((2500.0, 2900.0),),
    strategies: Sequence[str] = tuple(DEFAULT_STRATEGIES.keys()),
) -> dict[str, Any]:
    output: dict[str, Any] = {
        "duration_ms": duration_ms,
        "controller_hz": controller_hz,
        "active_windows": [list(window) for window in active_windows],
        "no_update_windows": [list(window) for window in no_update_windows],
        "strategies": {},
    }
    for strategy_name in strategies:
        config = DEFAULT_STRATEGIES[strategy_name]
        rows = simulate_strategy(
            config,
            duration_ms=duration_ms,
            controller_hz=controller_hz,
            active_windows=active_windows,
            no_update_windows=no_update_windows,
        )
        output["strategies"][strategy_name] = {
            "config": asdict(config),
            "summary": summarize_rows(rows, active_windows, duration_ms=duration_ms),
            "rows": rows,
        }
    return output


def write_strategy_jsonl(rows: Iterable[dict[str, Any]], path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        for row in rows:
            handle.write(json.dumps(row, sort_keys=True, separators=(",", ":")))
            handle.write("\n")


def write_benchmark_outputs(result: dict[str, Any], output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    summary = {
        key: value
        for key, value in result.items()
        if key != "strategies"
    }
    summary["strategies"] = {
        name: {"config": data["config"], "summary": data["summary"]}
        for name, data in result["strategies"].items()
    }
    (output_dir / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True),
        encoding="utf-8",
    )
    for name, data in result["strategies"].items():
        write_strategy_jsonl(data["rows"], output_dir / f"{name}.jsonl")


def print_summary(result: dict[str, Any]) -> None:
    for name, data in result["strategies"].items():
        summary = data["summary"]
        print(
            "{name}: active_snapshot_fps={snapshot_fps:.2f} active_fresh_source_fps={fresh_fps:.2f} "
            "long_gap_max_ms={gap:.1f} activation_gpu_max_ms={activation:.1f} "
            "gpu_p95_ms={gpu_p95:.1f} age_p95_ms={age_p95:.1f} reused_rows={reused}".format(
                name=name,
                snapshot_fps=summary["active_snapshot_fps"],
                fresh_fps=summary["active_fresh_source_fps"],
                gap=summary["long_gap_ms"]["max"],
                activation=summary["activation_gpu_total_ms"]["max"],
                gpu_p95=summary["gpu_total_ms"]["p95"],
                age_p95=summary["vision_age_ms"]["p95"],
                reused=summary["reused_source_rows"],
            )
        )
        occupancy = summary["estimated_gpu_occupancy_pct"]
        stability = summary["gpu_occupancy_stability"]["active"]
        print(
            "  estimated_gpu_occupancy: overall={overall:.1f}% active={active:.1f}% idle={idle:.1f}% "
            "active_bucket_stdev={stdev:.1f}pp active_bucket_cv={cv:.2f} "
            "snapshot_fps_per_gpu_pct={eff:.2f}".format(
                overall=occupancy["overall"],
                active=occupancy["active"],
                idle=occupancy["idle"],
                stdev=stability["stdev_pct_points"],
                cv=stability["cv"],
                eff=summary["gpu_efficiency"]["active_snapshot_fps_per_overall_gpu_pct"],
            )
        )


def _parse_windows(values: list[str]) -> list[Window]:
    windows: list[Window] = []
    for value in values:
        start, end = value.split(":", 1)
        windows.append((float(start), float(end)))
    return windows


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run a synthetic GPU-service stability benchmark for native vision strategies."
    )
    parser.add_argument("--duration-ms", type=float, default=8000.0)
    parser.add_argument("--controller-hz", type=float, default=100.0)
    parser.add_argument(
        "--active-window",
        action="append",
        default=[],
        help="Active interval as start_ms:end_ms. Can be passed multiple times.",
    )
    parser.add_argument(
        "--no-update-window",
        action="append",
        default=[],
        help="No-source-update interval as start_ms:end_ms. Can be passed multiple times.",
    )
    parser.add_argument(
        "--strategy",
        action="append",
        choices=sorted(DEFAULT_STRATEGIES),
        default=[],
        help="Strategy to include. Defaults to every strategy.",
    )
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()

    result = run_benchmark(
        duration_ms=args.duration_ms,
        controller_hz=args.controller_hz,
        active_windows=_parse_windows(args.active_window) or ((1500.0, 3500.0), (5000.0, 7000.0)),
        no_update_windows=_parse_windows(args.no_update_window) or ((2500.0, 2900.0),),
        strategies=tuple(args.strategy) or tuple(DEFAULT_STRATEGIES.keys()),
    )
    print_summary(result)
    if args.output_dir:
        write_benchmark_outputs(result, args.output_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
