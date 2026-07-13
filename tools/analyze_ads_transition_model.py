"""Estimate a gated hipfire-to-ADS geometry model from native JSONL telemetry."""

from __future__ import annotations

import argparse
import json
import statistics
from pathlib import Path
from typing import Iterable


def _rows(paths: Iterable[Path]):
    for path in paths:
        with Path(path).open("r", encoding="utf-8") as source:
            for line in source:
                try:
                    yield json.loads(line)
                except (json.JSONDecodeError, UnicodeDecodeError):
                    continue


def _summary(rows: list[dict], field: str) -> dict:
    values = [float(row[field]) for row in rows if field in row]
    if not values:
        return {"median": 0.0, "mad": 0.0, "min": 0.0, "max": 0.0}
    median = statistics.median(values)
    mad = statistics.median(abs(value - median) for value in values)
    return {"median": round(median, 6), "mad": round(mad, 6),
            "min": round(min(values), 6), "max": round(max(values), 6)}


def analyze_ads_transitions(paths: Iterable[Path]) -> dict:
    transitions = [row for row in _rows(paths) if row.get("type") == "ads_transition"]
    valid = [row for row in transitions if row.get("valid") is True and row.get("complete") is True]
    clean = [row for row in valid if row.get("calibration_class") == "calibration_clean"]
    conditional = [row for row in valid if row.get("calibration_class") == "conditional_model"]
    reasons = []
    if len(clean) < 50:
        reasons.append("clean_samples<50")
    if len(valid) < 150:
        reasons.append("valid_complete_samples<150")
    fields = ("scale_x", "scale_y", "offset_x", "offset_y", "settle_confidence")
    return {
        "schema": "cod_ads_transition_model_v1",
        "transition_events": len(transitions),
        "valid_complete_samples": len(valid),
        "clean_samples": len(clean),
        "conditional_samples": len(conditional),
        "clean_model": {field: _summary(clean, field) for field in fields},
        "conditional_diagnostics": {field: _summary(conditional, field) for field in fields},
        "ready_for_tracker_calibration": not reasons,
        "readiness_reasons": reasons,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = analyze_ads_transitions(args.logs)
    encoded = json.dumps(report, ensure_ascii=False, indent=2)
    if args.output:
        args.output.write_text(encoded + "\n", encoding="utf-8")
    else:
        print(encoded)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
