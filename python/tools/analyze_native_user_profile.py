"""Build a read-only, quantitative controller profile from native JSONL telemetry."""

from __future__ import annotations

import argparse
import json
import math
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


def _quantile(values: list[float], fraction: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    weight = position - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def analyze_user_profile(paths: Iterable[Path]) -> dict:
    samples = conflicts = takeover = preserved = directional = 0
    magnitudes: list[float] = []
    bodylock_samples = 0
    for row in _rows(paths):
        if row.get("type") != "controller_sample":
            continue
        samples += 1
        mx, my = float(row.get("manual_x", 0.0)), float(row.get("manual_y", 0.0))
        ax, ay = float(row.get("ai_x", 0.0)), float(row.get("ai_y", 0.0))
        px, py = float(row.get("pre_recoil_x", 0.0)), float(row.get("pre_recoil_y", 0.0))
        manual_mag = math.hypot(mx, my)
        ai_mag = math.hypot(ax, ay)
        if manual_mag >= 0.05:
            magnitudes.append(manual_mag)
            directional += 1
            if mx * px + my * py > 0.0:
                preserved += 1
        if manual_mag >= 0.10 and ai_mag >= 0.05 and mx * ax + my * ay < 0.0:
            conflicts += 1
        if row.get("manual_takeover_active") is True:
            takeover += 1
        if row.get("aim_mode") == "body_lock":
            bodylock_samples += 1

    reasons = []
    if samples < 10_000:
        reasons.append("controller_samples<10000")
    if len(magnitudes) < 1_000:
        reasons.append("active_manual_samples<1000")
    return {
        "schema": "cod_user_profile_v1",
        "controller_samples": samples,
        "active_manual_samples": len(magnitudes),
        "manual_magnitude": {
            "p50": round(_quantile(magnitudes, 0.50), 6),
            "p90": round(_quantile(magnitudes, 0.90), 6),
            "p99": round(_quantile(magnitudes, 0.99), 6),
        },
        "manual_ai_conflict_samples": conflicts,
        "manual_takeover_samples": takeover,
        "bodylock_samples": bodylock_samples,
        "direction_preservation_ratio": round(preserved / directional, 6) if directional else 1.0,
        "shadow_only": True,
        "ready_for_live_adaptation": False,
        "readiness_reasons": reasons + ["live_adaptation_requires_offline_validation"],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = analyze_user_profile(args.logs)
    encoded = json.dumps(report, ensure_ascii=False, indent=2)
    if args.output:
        args.output.write_text(encoded + "\n", encoding="utf-8")
    else:
        print(encoded)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
