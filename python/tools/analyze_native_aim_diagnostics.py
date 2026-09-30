from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import asdict, dataclass
import json
import math
from pathlib import Path
from typing import Any, Iterable


DEFAULT_LOG_DIR = Path("runs") / "native_perf"
MANUAL_FIGHT_THRESHOLD = 0.22
AI_FIGHT_THRESHOLD = 0.22
FIGHT_DOT_THRESHOLD = -0.05
NEAR_TARGET_PX = 60.0
HIGH_OUTPUT_THRESHOLD = 0.45
STALE_TARGET_MS = 80.0


@dataclass(slots=True)
class AimLogDiagnostics:
    path: str
    rows: int
    invalid_rows: int
    duration_ms: float
    target_rows: int
    aim_authority_rows: int
    fire_authority_rows: int
    manual_ai_fight_rows: int
    manual_final_fight_rows: int
    near_target_rows: int
    near_high_output_rows: int
    stale_target_rows: int
    tracker_projection_rows: int
    projected_high_output_rows: int
    authority_without_fire_rows: int
    ads_snap_rows: int
    body_lock_rows: int
    max_manual_magnitude: float
    max_ai_magnitude: float
    max_final_magnitude: float
    max_target_error_px: float
    p95_target_error_px: float
    p95_final_magnitude: float
    aim_modes: dict[str, int]
    tiers: dict[str, int]
    missing_diagnostic_fields: list[str]
    defect_notes: list[str]


def _float(row: dict[str, Any], key: str, default: float = 0.0) -> float:
    value = row.get(key, default)
    if value is None or value == "":
        return default
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def _bool(row: dict[str, Any], key: str, default: bool = False) -> bool:
    value = row.get(key, default)
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return value != 0
    if isinstance(value, str):
        return value.strip().lower() in {"1", "true", "yes", "on"}
    return default


def _magnitude(row: dict[str, Any], x_key: str, y_key: str) -> float:
    return math.hypot(_float(row, x_key), _float(row, y_key))


def _dot(row: dict[str, Any], lhs_x: str, lhs_y: str, rhs_x: str, rhs_y: str) -> float:
    return _float(row, lhs_x) * _float(row, rhs_x) + _float(row, lhs_y) * _float(row, rhs_y)


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


def _load_jsonl(path: Path, invalid_counter: Counter[str]) -> Iterable[dict[str, Any]]:
    with path.open("r", encoding="utf-8") as handle:
        for line_number, line in enumerate(handle, start=1):
            stripped = line.strip()
            if not stripped:
                continue
            try:
                value = json.loads(stripped)
            except json.JSONDecodeError:
                invalid_counter["invalid_rows"] += 1
                continue
            if isinstance(value, dict):
                yield value


def _manual_magnitude(row: dict[str, Any]) -> float:
    if "diagnostic_manual_magnitude" in row:
        return _float(row, "diagnostic_manual_magnitude")
    if "manual_pre_ai_x" in row or "manual_pre_ai_y" in row:
        return _magnitude(row, "manual_pre_ai_x", "manual_pre_ai_y")
    return _magnitude(row, "manual_x", "manual_y")


def _ai_magnitude(row: dict[str, Any]) -> float:
    if "diagnostic_ai_magnitude" in row:
        return _float(row, "diagnostic_ai_magnitude")
    return _magnitude(row, "ai_aim_x", "ai_aim_y")


def _final_magnitude(row: dict[str, Any]) -> float:
    if "diagnostic_final_magnitude" in row:
        return _float(row, "diagnostic_final_magnitude")
    return _magnitude(row, "final_x", "final_y")


def _target_error_px(row: dict[str, Any]) -> float:
    if "diagnostic_target_error_px" in row:
        return _float(row, "diagnostic_target_error_px")
    if _bool(row, "controller_target") or "controller_dx" in row or "controller_dy" in row:
        return _magnitude(row, "controller_dx", "controller_dy")
    return _magnitude(row, "dx", "dy")


def _has_target(row: dict[str, Any]) -> bool:
    return _bool(row, "controller_target") or _bool(row, "target")


def _manual_ai_fight(row: dict[str, Any], manual_mag: float, ai_mag: float) -> bool:
    if "diagnostic_manual_ai_fight" in row:
        return _bool(row, "diagnostic_manual_ai_fight")
    return (
        manual_mag >= MANUAL_FIGHT_THRESHOLD
        and ai_mag >= AI_FIGHT_THRESHOLD
        and _dot(row, "manual_x", "manual_y", "ai_aim_x", "ai_aim_y") <= FIGHT_DOT_THRESHOLD
    )


def _manual_final_fight(row: dict[str, Any], manual_mag: float, final_mag: float) -> bool:
    if "diagnostic_manual_final_fight" in row:
        return _bool(row, "diagnostic_manual_final_fight")
    return (
        manual_mag >= MANUAL_FIGHT_THRESHOLD
        and final_mag >= AI_FIGHT_THRESHOLD
        and _dot(row, "manual_x", "manual_y", "final_x", "final_y") <= FIGHT_DOT_THRESHOLD
    )


def summarize_aim_log(path: Path) -> AimLogDiagnostics:
    invalid_counter: Counter[str] = Counter()
    rows = list(_load_jsonl(path, invalid_counter))
    required_diagnostic_fields = [
        "diagnostic_manual_magnitude",
        "diagnostic_ai_magnitude",
        "diagnostic_final_magnitude",
        "diagnostic_target_error_px",
        "diagnostic_manual_ai_fight",
        "diagnostic_near_high_output",
        "diagnostic_stale_target",
        "diagnostic_tracker_projection",
    ]
    first_row = rows[0] if rows else {}
    missing_diagnostic_fields = [
        field for field in required_diagnostic_fields if field not in first_row
    ]

    target_errors: list[float] = []
    final_magnitudes: list[float] = []
    manual_magnitudes: list[float] = []
    ai_magnitudes: list[float] = []
    aim_modes: Counter[str] = Counter()
    tiers: Counter[str] = Counter()
    counts = Counter()

    for row in rows:
        manual_mag = _manual_magnitude(row)
        ai_mag = _ai_magnitude(row)
        final_mag = _final_magnitude(row)
        target_error = _target_error_px(row)
        has_target = _has_target(row)
        age_ms = max(_float(row, "vision_age_ms"), _float(row, "age_ms"))
        near_target = (
            _bool(row, "diagnostic_near_target")
            if "diagnostic_near_target" in row
            else has_target and target_error > 0.0 and target_error <= NEAR_TARGET_PX
        )
        high_output = final_mag >= HIGH_OUTPUT_THRESHOLD
        tracker_projection = (
            _bool(row, "diagnostic_tracker_projection")
            if "diagnostic_tracker_projection" in row
            else _bool(row, "controller_projection")
        )

        manual_magnitudes.append(manual_mag)
        ai_magnitudes.append(ai_mag)
        final_magnitudes.append(final_mag)
        if has_target:
            target_errors.append(target_error)
            counts["target_rows"] += 1
        if _bool(row, "controller_aim_authority") or _bool(row, "aim_authority"):
            counts["aim_authority_rows"] += 1
        if _bool(row, "controller_fire_authority") or _bool(row, "fire_authority"):
            counts["fire_authority_rows"] += 1
        if _manual_ai_fight(row, manual_mag, ai_mag):
            counts["manual_ai_fight_rows"] += 1
        if _manual_final_fight(row, manual_mag, final_mag):
            counts["manual_final_fight_rows"] += 1
        if near_target:
            counts["near_target_rows"] += 1
        if (
            _bool(row, "diagnostic_near_high_output")
            if "diagnostic_near_high_output" in row
            else near_target and high_output
        ):
            counts["near_high_output_rows"] += 1
        if (
            _bool(row, "diagnostic_stale_target")
            if "diagnostic_stale_target" in row
            else has_target and age_ms >= STALE_TARGET_MS
        ):
            counts["stale_target_rows"] += 1
        if tracker_projection:
            counts["tracker_projection_rows"] += 1
        if (
            _bool(row, "diagnostic_projected_high_output")
            if "diagnostic_projected_high_output" in row
            else tracker_projection and high_output
        ):
            counts["projected_high_output_rows"] += 1
        if (
            _bool(row, "diagnostic_authority_without_fire")
            if "diagnostic_authority_without_fire" in row
            else (
                (_bool(row, "controller_aim_authority") or _bool(row, "aim_authority"))
                and not (_bool(row, "controller_fire_authority") or _bool(row, "fire_authority"))
            )
        ):
            counts["authority_without_fire_rows"] += 1
        mode = str(row.get("aim_mode") or "unknown")
        aim_modes[mode] += 1
        if mode == "ads_snap":
            counts["ads_snap_rows"] += 1
        if mode == "body_lock":
            counts["body_lock_rows"] += 1
        tiers[str(row.get("controller_tier") or row.get("tier") or "none")] += 1

    duration_ms = 0.0
    if rows:
        duration_ms = max(
            0.0,
            _float(rows[-1], "relative_ms") - _float(rows[0], "relative_ms"),
        )
    defect_notes = _diagnose_log(rows_count=len(rows), counts=counts)

    return AimLogDiagnostics(
        path=str(path),
        rows=len(rows),
        invalid_rows=invalid_counter["invalid_rows"],
        duration_ms=duration_ms,
        target_rows=counts["target_rows"],
        aim_authority_rows=counts["aim_authority_rows"],
        fire_authority_rows=counts["fire_authority_rows"],
        manual_ai_fight_rows=counts["manual_ai_fight_rows"],
        manual_final_fight_rows=counts["manual_final_fight_rows"],
        near_target_rows=counts["near_target_rows"],
        near_high_output_rows=counts["near_high_output_rows"],
        stale_target_rows=counts["stale_target_rows"],
        tracker_projection_rows=counts["tracker_projection_rows"],
        projected_high_output_rows=counts["projected_high_output_rows"],
        authority_without_fire_rows=counts["authority_without_fire_rows"],
        ads_snap_rows=counts["ads_snap_rows"],
        body_lock_rows=counts["body_lock_rows"],
        max_manual_magnitude=max(manual_magnitudes, default=0.0),
        max_ai_magnitude=max(ai_magnitudes, default=0.0),
        max_final_magnitude=max(final_magnitudes, default=0.0),
        max_target_error_px=max(target_errors, default=0.0),
        p95_target_error_px=_percentile(target_errors, 95.0),
        p95_final_magnitude=_percentile(final_magnitudes, 95.0),
        aim_modes=dict(aim_modes),
        tiers=dict(tiers),
        missing_diagnostic_fields=missing_diagnostic_fields,
        defect_notes=defect_notes,
    )


def _diagnose_log(rows_count: int, counts: Counter[str]) -> list[str]:
    notes: list[str] = []
    if rows_count == 0:
        return ["empty aim log; runtime only writes this file while aiming is active"]
    if counts["manual_ai_fight_rows"] > 0:
        notes.append("manual and AI vectors fight on some frames")
    if counts["near_high_output_rows"] > 0:
        notes.append("high final stick appears while already near target")
    if counts["projected_high_output_rows"] > 0:
        notes.append("tracker projection is paired with high final output")
    if counts["stale_target_rows"] > 0:
        notes.append("stale target data remains active")
    if counts["authority_without_fire_rows"] > 0:
        notes.append("aim authority exists without fire authority")
    if not notes:
        notes.append("no bridge diagnostic counters fired")
    return notes


def summarize_benchmark(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        payload = json.load(handle)
    scenarios = payload.get("scenarios", [])
    interesting: list[dict[str, Any]] = []
    for scenario in scenarios:
        item: dict[str, Any] = {"name": scenario.get("name", "unknown")}
        for key in (
            "adversarial_controller",
            "ads_bodylock_near_high",
            "ads_carry_through",
            "aimlab_score",
        ):
            if key in scenario:
                item[key] = scenario[key]
        if len(item) > 1:
            interesting.append(item)
    return {
        "path": str(path),
        "scenario_count": len(scenarios),
        "diagnostic_scenarios": interesting,
    }


def latest_aim_log(log_dir: Path) -> Path:
    files = sorted(
        (item for item in log_dir.glob("native_aim_perf_*.jsonl") if item.stat().st_size > 0),
        key=lambda item: item.stat().st_mtime,
    )
    if not files:
        raise FileNotFoundError(f"No non-empty native_aim_perf_*.jsonl found under {log_dir}")
    return files[-1]


def _print_log_summary(summary: AimLogDiagnostics) -> None:
    print(f"[AimLog] {summary.path}")
    print(
        "  rows={rows} invalid_rows={invalid} duration_ms={duration:.1f} "
        "target={target} ads={ads} body={body}".format(
            rows=summary.rows,
            invalid=summary.invalid_rows,
            duration=summary.duration_ms,
            target=summary.target_rows,
            ads=summary.ads_snap_rows,
            body=summary.body_lock_rows,
        )
    )
    print(
        "  fight={fight} manual_final_fight={manual_final} near_high={near_high} stale={stale} "
        "projected_high={projected_high} authority_without_fire={authority_without_fire}".format(
            fight=summary.manual_ai_fight_rows,
            manual_final=summary.manual_final_fight_rows,
            near_high=summary.near_high_output_rows,
            stale=summary.stale_target_rows,
            projected_high=summary.projected_high_output_rows,
            authority_without_fire=summary.authority_without_fire_rows,
        )
    )
    print(
        "  max_manual={manual:.3f} max_ai={ai:.3f} max_final={final:.3f} "
        "p95_error_px={error:.1f} p95_final={p95_final:.3f}".format(
            manual=summary.max_manual_magnitude,
            ai=summary.max_ai_magnitude,
            final=summary.max_final_magnitude,
            error=summary.p95_target_error_px,
            p95_final=summary.p95_final_magnitude,
        )
    )
    if summary.missing_diagnostic_fields:
        print("  missing_diagnostic_fields=" + ",".join(summary.missing_diagnostic_fields))
    for note in summary.defect_notes:
        print(f"  note: {note}")


def _print_benchmark_summary(summary: dict[str, Any]) -> None:
    print(f"[Benchmark] {summary['path']}")
    print(f"  scenarios={summary['scenario_count']} diagnostic_scenarios={len(summary['diagnostic_scenarios'])}")
    for scenario in summary["diagnostic_scenarios"]:
        name = scenario["name"]
        if "adversarial_controller" in scenario:
            block = scenario["adversarial_controller"]
            print(
                "  {name}: wrong={wrong} fight={fight} invalid={invalid} stale_high={stale} "
                "err={err} recovery={recovery} p95_error={p95:.1f}".format(
                    name=name,
                    wrong=block.get("wrong_target_frames", 0),
                    fight=block.get("user_fight_frames", 0),
                    invalid=block.get("invalid_strong_frames", 0),
                    stale=block.get("stale_high_output_frames", 0),
                    err=block.get("err_target_frames", 0),
                    recovery=block.get("recovery_frames", 0),
                    p95=float(block.get("p95_error_px", 0.0)),
                )
            )
        elif "ads_bodylock_near_high" in scenario:
            block = scenario["ads_bodylock_near_high"]
            print(
                "  {name}: near_high={near_high} brake_gap={brake_gap} chatter={chatter} "
                "p95_turn={turn:.1f}".format(
                    name=name,
                    near_high=block.get("near_high_output_frames", 0),
                    brake_gap=block.get("brake_inactive_frames", 0),
                    chatter=block.get("chatter_events", 0),
                    turn=float(block.get("p95_turn_degrees", 0.0)),
                )
            )
        elif "ads_carry_through" in scenario:
            block = scenario["ads_carry_through"]
            print(
                "  {name}: same_dir={same_dir} near_high={near_high} brake_active={brake} "
                "max_over={over:.1f}".format(
                    name=name,
                    same_dir=block.get("same_direction_accel_frames", 0),
                    near_high=block.get("near_high_output_frames", 0),
                    brake=block.get("brake_active_frames", 0),
                    over=float(block.get("max_overshoot_px", 0.0)),
                )
            )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Summarize native aim runtime logs and benchmark diagnostic counters."
    )
    parser.add_argument("aim_logs", nargs="*", type=Path, help="native_aim_perf_*.jsonl files")
    parser.add_argument("--benchmark", type=Path, help="native gamepad benchmark JSON")
    parser.add_argument("--log-dir", type=Path, default=DEFAULT_LOG_DIR)
    parser.add_argument("--latest", action="store_true", help="summarize the latest native aim log")
    parser.add_argument("--json", action="store_true", help="print machine-readable JSON")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    aim_logs = list(args.aim_logs)
    if args.latest or not aim_logs:
        try:
            aim_logs.append(latest_aim_log(args.log_dir))
        except FileNotFoundError:
            if not args.benchmark:
                raise

    log_summaries = [summarize_aim_log(path) for path in aim_logs]
    benchmark_summary = summarize_benchmark(args.benchmark) if args.benchmark else None

    if args.json:
        print(
            json.dumps(
                {
                    "aim_logs": [asdict(summary) for summary in log_summaries],
                    "benchmark": benchmark_summary,
                },
                indent=2,
                sort_keys=True,
            )
        )
        return 0

    if benchmark_summary is not None:
        _print_benchmark_summary(benchmark_summary)
    for summary in log_summaries:
        _print_log_summary(summary)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
