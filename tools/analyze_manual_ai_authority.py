#!/usr/bin/env python3
"""Measure manual/AI authority behavior from schema-14 controller samples."""

from __future__ import annotations

import argparse
import json
import math
from collections import Counter
from pathlib import Path
from typing import Any


MODES = ("ads_snap", "body_lock")
AXES = (("x", 0), ("y", 1))
MATERIAL_AI = 1.0e-4
HARMFUL_LOSS = 0.02
MAX_CONTIGUOUS_GAP_NS = 12_000_000


def percentile(values: list[float], fraction: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    index = max(0, min(len(ordered) - 1, math.ceil(len(ordered) * fraction) - 1))
    return ordered[index]


def distribution(values: list[float]) -> dict[str, float | int | None]:
    if not values:
        return {"n": 0, "mean": None, "p50": None, "p95": None, "p99": None, "max": None}
    return {
        "n": len(values),
        "mean": sum(values) / len(values),
        "p50": percentile(values, 0.50),
        "p95": percentile(values, 0.95),
        "p99": percentile(values, 0.99),
        "max": max(values),
    }


def vector_norm(x: float, y: float) -> float:
    return math.hypot(x, y)


def active_target(row: dict[str, Any]) -> bool:
    return (
        bool(row.get("has_target"))
        and bool(row.get("aim_authority"))
        and row.get("aim_mode") in MODES
    )


def axis_values(row: dict[str, Any], axis: str, index: int) -> tuple[float, float, float, float, bool]:
    target_final = row.get("target_final") or [0.0, 0.0]
    return (
        float(row.get(f"physical_{axis}", 0.0)),
        float(row.get(f"filtered_manual_{axis}", 0.0)),
        float(row.get(f"ai_{axis}", 0.0)),
        float(target_final[index]),
        bool(row.get(f"manual_correction_{axis}")),
    )


def production_yield(row: dict[str, Any], axis: str, index: int) -> bool:
    physical, _, ai, target_final, correction = axis_values(row, axis, index)
    return (
        correction
        and abs(ai) > MATERIAL_AI
        and physical * ai < 0.0
        and abs(target_final - physical) <= 1.0e-4
    )


def harmful_yield(row: dict[str, Any], axis: str, index: int) -> bool:
    if not production_yield(row, axis, index):
        return False
    physical, _, ai, _, _ = axis_values(row, axis, index)
    return abs(ai) - abs(physical) >= HARMFUL_LOSS


def load_controller_rows(paths: list[Path]) -> tuple[list[dict[str, Any]], dict[str, int]]:
    rows: list[dict[str, Any]] = []
    totals = {"total_lines": 0, "parsed_rows": 0, "malformed_rows": 0, "controller_rows": 0}
    for path in paths:
        with path.open("r", encoding="utf-8") as stream:
            for line in stream:
                totals["total_lines"] += 1
                try:
                    record = json.loads(line)
                except json.JSONDecodeError:
                    totals["malformed_rows"] += 1
                    continue
                totals["parsed_rows"] += 1
                if record.get("type") == "controller_sample":
                    rows.append(record)
    rows.sort(key=lambda row: int(row.get("sample_ns", 0)))
    totals["controller_rows"] = len(rows)
    return rows, totals


def same_continuity(previous: dict[str, Any], current: dict[str, Any]) -> bool:
    return (
        active_target(previous)
        and active_target(current)
        and current.get("aim_mode") == previous.get("aim_mode")
        and current.get("controller_target_track_id") == previous.get("controller_target_track_id")
        and int(current.get("sample_ns", 0)) - int(previous.get("sample_ns", 0))
        <= MAX_CONTIGUOUS_GAP_NS
    )


def analyze_mode(rows: list[dict[str, Any]], mode: str) -> dict[str, Any]:
    eligible = [row for row in rows if active_target(row) and row.get("aim_mode") == mode]
    manual_correction_samples = 0
    any_yield_samples = 0
    any_harmful_samples = 0
    yield_overall_aligned = 0
    harmful_axis_filtered: list[float] = []
    harmful_axis_loss: list[float] = []
    harmful_axis_ai: list[float] = []
    harmful_axis_overall_aligned = 0
    raw_filtered_direction_mismatches = 0
    pure_ai_norms: list[float] = []
    pure_ai_errors: list[float] = []

    for row in eligible:
        correction = bool(row.get("manual_correction_x")) or bool(row.get("manual_correction_y"))
        if correction:
            manual_correction_samples += 1
        filtered_x = float(row.get("filtered_manual_x", 0.0))
        filtered_y = float(row.get("filtered_manual_y", 0.0))
        ai_x = float(row.get("ai_x", 0.0))
        ai_y = float(row.get("ai_y", 0.0))
        vector_aligned = filtered_x * ai_x + filtered_y * ai_y >= 0.0
        yielded = False
        harmed = False
        for axis, index in AXES:
            physical, filtered, ai, target_final, _ = axis_values(row, axis, index)
            if production_yield(row, axis, index):
                yielded = True
                if filtered * ai >= 0.0:
                    raw_filtered_direction_mismatches += 1
            if harmful_yield(row, axis, index):
                harmed = True
                harmful_axis_filtered.append(abs(filtered))
                harmful_axis_ai.append(abs(ai))
                harmful_axis_loss.append(abs(ai) - abs(target_final))
                if vector_aligned:
                    harmful_axis_overall_aligned += 1
        if yielded:
            any_yield_samples += 1
            if vector_aligned:
                yield_overall_aligned += 1
        if harmed:
            any_harmful_samples += 1
        pure_ai = (
            not correction
            and abs(filtered_x) <= 1.0e-9
            and abs(filtered_y) <= 1.0e-9
        )
        if pure_ai:
            pure_ai_norms.append(vector_norm(ai_x, ai_y))
            pure_ai_errors.append(float(row.get("target_error_px", 0.0)))

    return {
        "active_owned_samples": len(eligible),
        "manual_correction_samples": manual_correction_samples,
        "manual_correction_rate": manual_correction_samples / len(eligible) if eligible else None,
        "any_yield_samples": any_yield_samples,
        "any_yield_rate": any_yield_samples / len(eligible) if eligible else None,
        "yield_samples_with_nonnegative_vector_dot": yield_overall_aligned,
        "harmful_yield_samples": any_harmful_samples,
        "harmful_yield_rate": any_harmful_samples / len(eligible) if eligible else None,
        "harmful_axes": len(harmful_axis_filtered),
        "harmful_axes_with_nonnegative_vector_dot": harmful_axis_overall_aligned,
        "harmful_filtered_manual_abs": distribution(harmful_axis_filtered),
        "harmful_ai_abs": distribution(harmful_axis_ai),
        "harmful_ai_magnitude_loss": distribution(harmful_axis_loss),
        "harmful_filtered_le_0_05": sum(value <= 0.05 for value in harmful_axis_filtered),
        "harmful_filtered_le_0_08": sum(value <= 0.08 for value in harmful_axis_filtered),
        "harmful_filtered_le_0_12": sum(value <= 0.12 for value in harmful_axis_filtered),
        "raw_filtered_direction_mismatch_axes": raw_filtered_direction_mismatches,
        "pure_ai_norm": distribution(pure_ai_norms),
        "pure_ai_error_px": distribution(pure_ai_errors),
    }


def correction_onsets(rows: list[dict[str, Any]]) -> dict[str, Any]:
    values: dict[str, list[tuple[float, float, float]]] = {mode: [] for mode in MODES}
    for previous, current in zip(rows, rows[1:]):
        if not same_continuity(previous, current):
            continue
        mode = str(current.get("aim_mode"))
        for axis, index in AXES:
            physical, filtered, ai, target_final, correction = axis_values(current, axis, index)
            prior_correction = bool(previous.get(f"manual_correction_{axis}"))
            if (
                correction
                and not prior_correction
                and abs(ai) > MATERIAL_AI
                and physical * ai < 0.0
                and abs(target_final - physical) <= 1.0e-4
            ):
                values[mode].append((abs(filtered), abs(ai), max(0.0, abs(ai) - abs(target_final))))
    result: dict[str, Any] = {}
    for mode, samples in values.items():
        filtered = [sample[0] for sample in samples]
        result[mode] = {
            "n": len(samples),
            "filtered_manual_abs": distribution(filtered),
            "ai_abs": distribution([sample[1] for sample in samples]),
            "same_tick_ai_magnitude_loss": distribution([sample[2] for sample in samples]),
            "filtered_le_0_05": sum(value <= 0.05 for value in filtered),
            "filtered_le_0_08": sum(value <= 0.08 for value in filtered),
        }
    return result


def harmful_runs(rows: list[dict[str, Any]]) -> dict[str, Any]:
    runs: list[list[dict[str, Any]]] = []
    current: list[dict[str, Any]] = []
    for row in rows:
        harmful = active_target(row) and any(harmful_yield(row, axis, index) for axis, index in AXES)
        if harmful:
            if current and not same_continuity(current[-1], row):
                runs.append(current)
                current = []
            current.append(row)
        elif current:
            runs.append(current)
            current = []
    if current:
        runs.append(current)

    durations: list[float] = []
    for run in runs:
        gaps = [
            (int(run[index]["sample_ns"]) - int(run[index - 1]["sample_ns"])) / 1.0e6
            for index in range(1, len(run))
        ]
        tail = percentile(gaps, 0.50) if gaps else 4.0
        durations.append((int(run[-1]["sample_ns"]) - int(run[0]["sample_ns"])) / 1.0e6 + float(tail))
    return {
        "runs": len(runs),
        "duration_ms": distribution(durations),
        "ge_20_ms": sum(value >= 20.0 for value in durations),
        "ge_50_ms": sum(value >= 50.0 for value in durations),
        "ge_100_ms": sum(value >= 100.0 for value in durations),
    }


def pure_ai_error_bins(rows: list[dict[str, Any]]) -> dict[str, Any]:
    bins = ((0.0, 5.0), (5.0, 10.0), (10.0, 20.0), (20.0, 40.0), (40.0, 80.0), (80.0, math.inf))
    result: dict[str, Any] = {}
    for mode in MODES:
        mode_result: dict[str, Any] = {}
        for lower, upper in bins:
            values: list[float] = []
            for row in rows:
                if not active_target(row) or row.get("aim_mode") != mode:
                    continue
                if row.get("manual_correction_x") or row.get("manual_correction_y"):
                    continue
                if abs(float(row.get("filtered_manual_x", 0.0))) > 1.0e-9 or abs(float(row.get("filtered_manual_y", 0.0))) > 1.0e-9:
                    continue
                error = float(row.get("target_error_px", 0.0))
                if lower <= error < upper:
                    values.append(vector_norm(float(row.get("ai_x", 0.0)), float(row.get("ai_y", 0.0))))
            label = f"{lower:g}-{upper:g}" if math.isfinite(upper) else f"{lower:g}-inf"
            mode_result[label] = distribution(values)
        result[mode] = mode_result
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", action="append", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    rows, integrity = load_controller_rows(args.input)
    report = {
        "schema_version": 1,
        "analysis": "manual_ai_authority",
        "percentile_convention": "nearest-rank",
        "material_ai_threshold": MATERIAL_AI,
        "harmful_loss_threshold": HARMFUL_LOSS,
        "integrity": integrity,
        "aim_mode_counts": dict(Counter(str(row.get("aim_mode")) for row in rows)),
        "by_mode": {mode: analyze_mode(rows, mode) for mode in MODES},
        "correction_onsets": correction_onsets(rows),
        "harmful_runs": harmful_runs(rows),
        "pure_ai_by_error_bin": pure_ai_error_bins(rows),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({"status": "PASS", "controller_rows": len(rows), "output": str(args.output)}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
