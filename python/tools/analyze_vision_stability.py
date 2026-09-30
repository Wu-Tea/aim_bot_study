from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
import json
import math
from pathlib import Path
from typing import Any, Iterable


DEFAULT_LONG_GAP_MS = 100.0


@dataclass(slots=True)
class Span:
    rows: int
    duration_ms: float


def _bool(row: dict[str, Any], key: str, default: bool = False) -> bool:
    value = row.get(key, default)
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return value != 0
    if isinstance(value, str):
        return value.strip().lower() in {"1", "true", "yes", "on"}
    return default


def _float(row: dict[str, Any], key: str, default: float = 0.0) -> float:
    value = row.get(key, default)
    if value is None or value == "":
        return default
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def _percentile(values: list[float], percentile: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    position = (len(ordered) - 1) * max(0.0, min(100.0, percentile)) / 100.0
    lower = int(math.floor(position))
    upper = int(math.ceil(position))
    if lower == upper:
        return ordered[lower]
    fraction = position - lower
    return ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


def _rate(count: int, duration_ms: float) -> float:
    if count <= 1 or duration_ms <= 0.0:
        return 0.0
    return count * 1000.0 / duration_ms


def _load_jsonl(path: Path) -> tuple[list[dict[str, Any]], int]:
    rows: list[dict[str, Any]] = []
    invalid_rows = 0
    with path.open("r", encoding="utf-8") as handle:
        for line in handle:
            stripped = line.strip()
            if not stripped:
                continue
            try:
                value = json.loads(stripped)
            except json.JSONDecodeError:
                invalid_rows += 1
                continue
            if isinstance(value, dict):
                rows.append(value)
    return rows, invalid_rows


def _positive_metric_values(rows: Iterable[dict[str, Any]], key: str) -> list[float]:
    values: list[float] = []
    for row in rows:
        value = _float(row, key)
        if value > 0.0:
            values.append(value)
    return values


def _metric_summary(rows: list[dict[str, Any]], metric_keys: list[str]) -> dict[str, dict[str, float]]:
    summary: dict[str, dict[str, float]] = {}
    for key in metric_keys:
        values = _positive_metric_values(rows, key)
        if not values:
            continue
        summary[key] = {
            "count": float(len(values)),
            "avg": sum(values) / len(values),
            "p50": _percentile(values, 50),
            "p90": _percentile(values, 90),
            "p95": _percentile(values, 95),
            "p99": _percentile(values, 99),
            "max": max(values),
        }
    return summary


def _updated_rows(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [
        row
        for row in rows
        if _bool(row, "frame_updated") and int(_float(row, "frame_id")) > 0
    ]


def _unique_frame_rows(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    unique_rows: list[dict[str, Any]] = []
    previous_frame_id: int | None = None
    for row in _updated_rows(rows):
        frame_id = int(_float(row, "frame_id"))
        if frame_id == previous_frame_id:
            continue
        unique_rows.append(row)
        previous_frame_id = frame_id
    return unique_rows


def _intervals_ms(rows: list[dict[str, Any]]) -> list[float]:
    values: list[float] = []
    previous: float | None = None
    for row in rows:
        current = _float(row, "relative_ms")
        if previous is not None:
            values.append(current - previous)
        previous = current
    return values


def _span_rows(rows: list[dict[str, Any]], predicate_key: str, expected: bool) -> list[Span]:
    spans: list[Span] = []
    start_ms: float | None = None
    last_ms = 0.0
    count = 0
    for row in rows:
        matches = _bool(row, predicate_key) is expected
        if matches:
            current = _float(row, "relative_ms")
            if count == 0:
                start_ms = current
            last_ms = current
            count += 1
            continue
        if count > 0 and start_ms is not None:
            spans.append(Span(rows=count, duration_ms=max(0.0, last_ms - start_ms)))
        count = 0
        start_ms = None
    if count > 0 and start_ms is not None:
        spans.append(Span(rows=count, duration_ms=max(0.0, last_ms - start_ms)))
    return spans


def _activation_rows(updated: list[dict[str, Any]], long_gap_ms: float) -> list[dict[str, Any]]:
    activations: list[dict[str, Any]] = []
    previous_ms: float | None = None
    for row in updated:
        current_ms = _float(row, "relative_ms")
        if previous_ms is None or current_ms - previous_ms >= long_gap_ms:
            activations.append(row)
        previous_ms = current_ms
    return activations


def analyze_log(path: Path, long_gap_ms: float) -> dict[str, Any]:
    rows, invalid_rows = _load_jsonl(path)
    first_ms = _float(rows[0], "relative_ms") if rows else 0.0
    last_ms = _float(rows[-1], "relative_ms") if rows else 0.0
    duration_ms = max(0.0, last_ms - first_ms)
    updated = _updated_rows(rows)
    unique_frames = _unique_frame_rows(rows)
    intervals = _intervals_ms(unique_frames)
    active_intervals = [value for value in intervals if 0.0 < value < long_gap_ms]
    long_gaps = [value for value in intervals if value >= long_gap_ms]
    no_update_spans = _span_rows(rows, "frame_updated", False)
    aiming_spans = _span_rows(rows, "aiming", True)
    activations = _activation_rows(unique_frames, long_gap_ms)
    unique_frame_ids = {int(_float(row, "frame_id")) for row in unique_frames}
    preprocess_modes = Counter(str(row.get("preprocess_mode", "")) for row in rows)
    service_freshness = Counter(str(row.get("service_freshness", "none")) for row in rows)
    service_source_state = Counter(str(row.get("service_source_state", "unknown")) for row in rows)
    metrics = [
        "capture_ms",
        "copy_ms",
        "capture_transfer_ms",
        "cuda_map_ms",
        "preprocess_ms",
        "infer_ms",
        "gpu_total_ms",
        "output_wait_ms",
        "decode_ms",
        "selector_ms",
        "post_ms",
        "age_ms",
        "vision_age_ms",
        "output_age_ms",
        "ctrl_loop_ms",
    ]
    activation_metrics = _metric_summary(
        activations,
        ["infer_ms", "gpu_total_ms", "output_wait_ms", "preprocess_ms", "age_ms", "vision_age_ms"],
    )
    metric_source_rows = unique_frames if unique_frames else rows
    return {
        "path": str(path),
        "rows": len(rows),
        "invalid_rows": invalid_rows,
        "duration_ms": duration_ms,
        "row_rate_hz": _rate(len(rows), duration_ms),
        "aiming_rows": sum(1 for row in rows if _bool(row, "aiming")),
        "target_rows": sum(1 for row in rows if _bool(row, "target")),
        "frame_updated_rows": sum(1 for row in rows if _bool(row, "frame_updated")),
        "frame_not_updated_rows": sum(1 for row in rows if not _bool(row, "frame_updated")),
        "updated_frame_rows": len(updated),
        "unique_frame_rows": len(unique_frames),
        "unique_frame_ids": len(unique_frame_ids),
        "unique_frame_rate_hz": _rate(len(unique_frames), duration_ms),
        "updated_row_rate_hz": _rate(len(updated), duration_ms),
        "active_interval_ms": {
            "count": len(active_intervals),
            "avg": (sum(active_intervals) / len(active_intervals)) if active_intervals else 0.0,
            "p50": _percentile(active_intervals, 50),
            "p95": _percentile(active_intervals, 95),
            "p99": _percentile(active_intervals, 99),
            "active_rate_hz_from_avg": (1000.0 / (sum(active_intervals) / len(active_intervals))) if active_intervals else 0.0,
            "active_rate_hz_from_p50": (1000.0 / _percentile(active_intervals, 50)) if active_intervals else 0.0,
        },
        "long_gap_ms": {
            "threshold": long_gap_ms,
            "count": len(long_gaps),
            "max": max(long_gaps) if long_gaps else 0.0,
            "p95": _percentile(long_gaps, 95),
            "top5": sorted(long_gaps, reverse=True)[:5],
        },
        "no_update_spans": {
            "count": len(no_update_spans),
            "max_rows": max((span.rows for span in no_update_spans), default=0),
            "max_duration_ms": max((span.duration_ms for span in no_update_spans), default=0.0),
            "p95_duration_ms": _percentile([span.duration_ms for span in no_update_spans], 95),
        },
        "aiming_spans": {
            "count": len(aiming_spans),
            "max_duration_ms": max((span.duration_ms for span in aiming_spans), default=0.0),
        },
        "activation": {
            "count": len(activations),
            "metrics": activation_metrics,
            "top5_gpu_total_ms": sorted(
                (
                    {
                        "relative_ms": _float(row, "relative_ms"),
                        "frame_id": int(_float(row, "frame_id")),
                        "gpu_total_ms": _float(row, "gpu_total_ms"),
                        "infer_ms": _float(row, "infer_ms"),
                        "age_ms": max(_float(row, "vision_age_ms"), _float(row, "age_ms")),
                    }
                    for row in activations
                ),
                key=lambda item: item["gpu_total_ms"],
                reverse=True,
            )[:5],
        },
        "preprocess_modes": dict(preprocess_modes),
        "service_freshness": dict(service_freshness),
        "service_source_state": dict(service_source_state),
        "metrics": _metric_summary(metric_source_rows, metrics),
    }


def _fmt(value: float, digits: int = 3) -> str:
    return f"{value:.{digits}f}"


def print_report(summary: dict[str, Any]) -> None:
    print(f"== {summary['path']} ==")
    print(
        "rows={rows} invalid={invalid_rows} duration_s={duration} row_rate_hz={row_rate}".format(
            rows=summary["rows"],
            invalid_rows=summary["invalid_rows"],
            duration=_fmt(summary["duration_ms"] / 1000.0),
            row_rate=_fmt(summary["row_rate_hz"]),
        )
    )
    print(
        "aiming_rows={aiming} target_rows={target} frame_updated={updated} no_update_rows={not_updated}".format(
            aiming=summary["aiming_rows"],
            target=summary["target_rows"],
            updated=summary["frame_updated_rows"],
            not_updated=summary["frame_not_updated_rows"],
        )
    )
    print(
        "unique_frame_rows={unique_rows} unique_frame_ids={unique_ids} unique_fps={unique_fps} updated_row_fps={updated_fps}".format(
            unique_rows=summary["unique_frame_rows"],
            unique_ids=summary["unique_frame_ids"],
            unique_fps=_fmt(summary["unique_frame_rate_hz"]),
            updated_fps=_fmt(summary["updated_row_rate_hz"]),
        )
    )
    active = summary["active_interval_ms"]
    print(
        "active_interval_ms count={count} avg={avg} p50={p50} p95={p95} p99={p99} active_hz_avg={hz_avg} active_hz_p50={hz_p50}".format(
            count=active["count"],
            avg=_fmt(active["avg"]),
            p50=_fmt(active["p50"]),
            p95=_fmt(active["p95"]),
            p99=_fmt(active["p99"]),
            hz_avg=_fmt(active["active_rate_hz_from_avg"]),
            hz_p50=_fmt(active["active_rate_hz_from_p50"]),
        )
    )
    gaps = summary["long_gap_ms"]
    print(
        "long_gaps threshold_ms={threshold} count={count} max_ms={max_ms} p95_ms={p95} top5_ms={top5}".format(
            threshold=_fmt(gaps["threshold"], 1),
            count=gaps["count"],
            max_ms=_fmt(gaps["max"]),
            p95=_fmt(gaps["p95"]),
            top5=", ".join(_fmt(value) for value in gaps["top5"]),
        )
    )
    spans = summary["no_update_spans"]
    print(
        "no_update_spans count={count} max_rows={max_rows} max_duration_ms={max_ms} p95_duration_ms={p95}".format(
            count=spans["count"],
            max_rows=spans["max_rows"],
            max_ms=_fmt(spans["max_duration_ms"]),
            p95=_fmt(spans["p95_duration_ms"]),
        )
    )
    activation = summary["activation"]
    print(f"activation_count={activation['count']}")
    for key in ["infer_ms", "gpu_total_ms", "output_wait_ms", "preprocess_ms", "age_ms", "vision_age_ms"]:
        metric = activation["metrics"].get(key)
        if metric:
            print(
                f"  activation_{key}: p50={_fmt(metric['p50'])} p95={_fmt(metric['p95'])} max={_fmt(metric['max'])}"
            )
    if activation["top5_gpu_total_ms"]:
        print("  activation_top_gpu_total:")
        for item in activation["top5_gpu_total_ms"]:
            print(
                "    t={t} frame={frame} gpu={gpu} infer={infer} age={age}".format(
                    t=_fmt(item["relative_ms"]),
                    frame=item["frame_id"],
                    gpu=_fmt(item["gpu_total_ms"]),
                    infer=_fmt(item["infer_ms"]),
                    age=_fmt(item["age_ms"]),
                )
            )
    print(f"preprocess_modes={summary['preprocess_modes']}")
    if summary.get("service_freshness"):
        print(f"service_freshness={summary['service_freshness']}")
    if summary.get("service_source_state"):
        print(f"service_source_state={summary['service_source_state']}")
    for key in ["infer_ms", "gpu_total_ms", "output_wait_ms", "preprocess_ms", "age_ms", "vision_age_ms", "ctrl_loop_ms"]:
        metric = summary["metrics"].get(key)
        if not metric:
            continue
        print(
            "{key}: avg={avg} p50={p50} p95={p95} p99={p99} max={max_value}".format(
                key=key,
                avg=_fmt(metric["avg"]),
                p50=_fmt(metric["p50"]),
                p95=_fmt(metric["p95"]),
                p99=_fmt(metric["p99"]),
                max_value=_fmt(metric["max"]),
            )
        )
    print()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Summarize native vision stability from native_aim_perf JSONL logs."
    )
    parser.add_argument("logs", nargs="+", type=Path)
    parser.add_argument(
        "--long-gap-ms",
        type=float,
        default=DEFAULT_LONG_GAP_MS,
        help="Inference interval threshold treated as an activation/long-gap boundary.",
    )
    parser.add_argument("--json", action="store_true", help="Emit machine-readable JSON.")
    args = parser.parse_args()

    summaries = [analyze_log(path, args.long_gap_ms) for path in args.logs]
    if args.json:
        print(json.dumps(summaries, indent=2, sort_keys=True))
    else:
        for summary in summaries:
            print_report(summary)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
