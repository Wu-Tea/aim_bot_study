#!/usr/bin/env python3
"""Mine per-operation-class feature distributions from native telemetry.

This is the §4.5 template-mining half of the operation-pattern model (see
docs/project/ASSIST_PERCEPTION_QUANTIFICATION_DESIGN_20260813.md).

For every controller_sample in a session it reconstructs the classifier input
from telemetry fields, runs a faithful mirror of the C++ OperationIntentClassifier
(native/controller_native/operation_intent.cpp), and accumulates per-class
feature distributions. The emitted template JSON is the input to the future
online "context -> expected template -> compare" matcher.

Reconstruction notes (schema-17 sessions lack intent fields):
  * firing is joined from delivered_control_sample.control.firing by sample_seq
    (fallback: auto_fire_active || final_fire_button);
  * right_purpose is inferred from manual_authority_mode;
  * right_phase is Sustained when |filtered| > 0, else Neutral.
Schema-18 sessions (after the operation_class telemetry addition) carry the
ground-truth label directly and need no reconstruction.

Usage:
  python tools/mine_operation_templates.py SESSION_DIR [--out OUT.json]
"""

from __future__ import annotations

import argparse
import json
import math
import statistics
import sys
from collections import defaultdict
from pathlib import Path

# --- Faithful mirror of native/controller_native/operation_intent.cpp -------

MATERIAL_FLOOR = 0.15
ACQUIRE_FLICK_MIN_MAGNITUDE = 0.40
RECOIL_PULL_MIN_DOWNWARD_Y = 0.05
FOLLOW_TRACK_MAX_MAGNITUDE = 0.12
CORRECT_TRACK_MAX_MAGNITUDE = 0.45
LEAD_TRACK_MIN_VELOCITY_PX_PER_SEC = 110.0
LEAD_TRACK_MIN_ERROR_ALONG_MOTION_PX = 6.0
DIRECTION_CONSISTENCY_ALPHA = 0.08

CLASSES = [
    "no_gesture",
    "acquire_flick",
    "recoil_pull",
    "lead_track",
    "follow_track",
    "correct_track",
    "handover_intent",
    "unreliable",
]


class ClassifierMirror:
    """Byte-for-byte logic mirror of OperationIntentClassifier::classify."""

    def __init__(self):
        self.was_firing = False
        self.last_manual_x = 0.0
        self.last_manual_y = 0.0
        self.direction_consistency = 0.5

    def reset(self):
        self.__init__()

    def classify(self, inp):
        magnitude = math.hypot(inp["fx"], inp["fy"])
        fire_started = inp["firing"] and not self.was_firing
        self.was_firing = inp["firing"]

        if magnitude > MATERIAL_FLOOR:
            prev_mag = math.hypot(self.last_manual_x, self.last_manual_y)
            if prev_mag > MATERIAL_FLOOR:
                dot = (self.last_manual_x * inp["fx"] + self.last_manual_y * inp["fy"]) / (
                    magnitude * prev_mag
                )
                if dot > 0.5:
                    self.direction_consistency += DIRECTION_CONSISTENCY_ALPHA * (
                        1.0 - self.direction_consistency
                    )
                elif dot < -0.5:
                    self.direction_consistency *= 0.5
            self.last_manual_x = inp["fx"]
            self.last_manual_y = inp["fy"]

        downward = max(0.0, -inp["fy"])

        out = {"class": "no_gesture", "confidence": 0.0, "trust": 0.5, "pull": 0.0, "onset": False}

        if inp["purpose"] == "handover":
            out["class"] = "handover_intent"
            out["confidence"] = inp["confidence"]
        elif inp["firing"] and downward > RECOIL_PULL_MIN_DOWNWARD_Y:
            out["class"] = "recoil_pull"
            out["pull"] = min(1.0, downward)
            out["onset"] = fire_started
            out["confidence"] = 0.5 + 0.5 * min(1.0, max(0.0, inp["confidence"]))
        elif (
            inp["purpose"] == "acquire"
            and inp["phase"] in ("onset", "sustained")
            and magnitude > ACQUIRE_FLICK_MIN_MAGNITUDE
        ):
            out["class"] = "acquire_flick"
            out["confidence"] = inp["confidence"]
        elif (
            inp["target_owned"]
            and inp["velocity"] > LEAD_TRACK_MIN_VELOCITY_PX_PER_SEC
            and inp["error_along_motion"] > LEAD_TRACK_MIN_ERROR_ALONG_MOTION_PX
            and magnitude < FOLLOW_TRACK_MAX_MAGNITUDE
        ):
            out["class"] = "lead_track"
            out["confidence"] = 0.7
        elif inp["target_owned"] and magnitude < FOLLOW_TRACK_MAX_MAGNITUDE:
            out["class"] = "follow_track"
            out["confidence"] = 0.8
        elif (
            inp["target_owned"]
            and inp["manual_correction"]
            and magnitude > MATERIAL_FLOOR
            and magnitude < CORRECT_TRACK_MAX_MAGNITUDE
        ):
            out["class"] = "correct_track"
            out["confidence"] = inp["confidence"]
        elif (inp["target_owned"] or inp["firing"]) and magnitude > MATERIAL_FLOOR:
            out["class"] = "unreliable"
            out["confidence"] = 1.0

        if out["class"] == "unreliable":
            out["trust"] = 0.10
        elif out["class"] == "no_gesture":
            out["trust"] = 0.50
        else:
            out["trust"] = min(
                0.97,
                max(
                    0.0,
                    0.72
                    + 0.15 * min(1.0, max(0.0, inp["confidence"]))
                    + 0.10 * self.direction_consistency,
                ),
            )
        return out


# --- Telemetry reading ------------------------------------------------------

def find_jsonl(session_dir: Path):
    candidates = sorted(session_dir.glob("*.jsonl"))
    return candidates


def join_firing_by_seq(records):
    """delivered_control_sample -> {sample_seq: firing}."""
    firing = {}
    for rec in records:
        if rec.get("type") != "delivered_control_sample":
            continue
        control = rec.get("control", {})
        seq = control.get("sample_seq", rec.get("sample_seq"))
        if isinstance(seq, int):
            firing[seq] = bool(control.get("firing"))
    return firing


def infer_purpose(mode: str) -> str:
    if "handover" in mode:
        return "handover"
    if "acquisition_gesture" in mode or "multi_target_selector_handover" in mode:
        return "acquire"
    return "correct"


def make_input(sample: dict, firing_map: dict) -> dict | None:
    seq = sample.get("sample_seq")
    firing = firing_map.get(seq, sample.get("auto_fire_active") or sample.get("final_fire_button"))
    fx = sample.get("filtered_manual_x", 0.0) or 0.0
    fy = sample.get("filtered_manual_y", 0.0) or 0.0
    mode = sample.get("manual_authority_mode", "no_target_passthrough")
    error_x = sample.get("control_error_x", 0.0) or 0.0
    error_y = sample.get("control_error_y", 0.0) or 0.0
    er_x = sample.get("bodylock_error_rate_x", 0.0) or 0.0
    er_y = sample.get("bodylock_error_rate_y", 0.0) or 0.0
    speed = math.hypot(er_x, er_y)
    error_along = 0.0
    if speed > 1.0:
        error_along = (error_x * er_x + error_y * er_y) / speed
    target_owned = bool(
        sample.get("selected_track_id") or sample.get("current_observed_target_present")
    )
    return {
        "firing": bool(firing),
        "fx": fx,
        "fy": fy,
        "confidence": sample.get("manual_confidence", 0.0) or 0.0,
        "purpose": infer_purpose(mode),
        "phase": "sustained" if math.hypot(fx, fy) > 0.0 else "neutral",
        "target_owned": target_owned,
        "manual_correction": bool(
            sample.get("manual_correction_x") or sample.get("manual_correction_y")
        ),
        "error_px": math.hypot(error_x, error_y),
        "error_along_motion": error_along,
        "velocity": speed,
    }


# --- Statistics -------------------------------------------------------------

def summarize(values):
    if not values:
        return None
    sorted_v = sorted(values)
    n = len(sorted_v)
    def pct(p):
        idx = min(n - 1, int(round(p / 100.0 * (n - 1))))
        return sorted_v[idx]
    return {
        "count": n,
        "mean": statistics.fmean(sorted_v),
        "std": statistics.pstdev(sorted_v) if n > 1 else 0.0,
        "min": sorted_v[0],
        "p50": pct(50),
        "p90": pct(90),
        "max": sorted_v[-1],
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("session_dir", type=Path)
    parser.add_argument("--out", type=Path, default=None)
    parser.add_argument("--limit", type=int, default=0, help="cap samples (dev)")
    args = parser.parse_args()

    files = find_jsonl(args.session_dir)
    if not files:
        print(f"no jsonl in {args.session_dir}", file=sys.stderr)
        return 1

    records = []
    for path in files:
        with open(path, encoding="utf-8") as fh:
            for line in fh:
                line = line.strip()
                if not line:
                    continue
                try:
                    records.append(json.loads(line))
                except json.JSONDecodeError:
                    continue

    firing_map = join_firing_by_seq(records)
    classifier = ClassifierMirror()
    per_class = {c: defaultdict(list) for c in CLASSES}
    sample_count = 0
    context = defaultdict(int)
    for rec in records:
        if rec.get("type") != "controller_sample":
            continue
        inp = make_input(rec, firing_map)
        if inp is None:
            continue
        out = classifier.classify(inp)
        cls = out["class"]
        per_class[cls]["magnitude"].append(math.hypot(inp["fx"], inp["fy"]))
        per_class[cls]["error_px"].append(inp["error_px"])
        per_class[cls]["velocity"].append(inp["velocity"])
        per_class[cls]["pull"].append(out["pull"])
        per_class[cls]["trust"].append(out["trust"])
        if inp["firing"]:
            context["firing"] += 1
        if inp["target_owned"]:
            context["target_owned"] += 1
        sample_count += 1
        if args.limit and sample_count >= args.limit:
            break

    if sample_count == 0:
        print("no controller_sample records mined", file=sys.stderr)
        return 1

    classes_out = {}
    for cls in CLASSES:
        buckets = per_class[cls]
        n = len(buckets["magnitude"])
        classes_out[cls] = {
            "count": n,
            "share": n / sample_count,
            "features": {
                key: summarize(buckets[key]) for key in ("magnitude", "error_px", "velocity", "pull", "trust")
            },
        }

    template = {
        "template_schema": "operation_template_v1",
        "source_session": args.session_dir.name,
        "reconstructed": True,
        "notes": "Inputs reconstructed from schema-17 telemetry; firing joined from "
        "delivered_control_sample, purpose inferred from manual_authority_mode. "
        "Schema-18 sessions carry ground-truth operation_class.",
        "samples_total": sample_count,
        "context": {
            "firing_share": context["firing"] / sample_count,
            "target_owned_share": context["target_owned"] / sample_count,
        },
        "classes": classes_out,
    }

    out_path = args.out
    if out_path is None:
        out_path = (
            Path("artifacts/operation_templates")
            / f"operation_template_{args.session_dir.name}.json"
        )
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(template, indent=2, sort_keys=True), encoding="utf-8")

    print(f"mined {sample_count} controller_sample records -> {out_path}")
    for cls in CLASSES:
        entry = classes_out[cls]
        if entry["count"] == 0:
            continue
        f = entry["features"]
        print(
            f"  {cls:16s} n={entry['count']:6d} share={entry['share']:6.3f} "
            f"mag(p50)={f['magnitude']['p50']:5.2f} err(p50)={f['error_px']['p50']:6.1f}px "
            f"vel(p90)={f['velocity']['p90']:7.1f} pull(p50)={f['pull']['p50']:5.2f} "
            f"trust(p50)={f['trust']['p50']:4.2f}"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
