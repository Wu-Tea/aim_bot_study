#!/usr/bin/env python3
"""Verify the frozen Fusion marker continuity incident fixture."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


def hash_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _visible_run_count(values: list[bool]) -> int:
    return sum(value and (index == 0 or not values[index - 1])
               for index, value in enumerate(values))


def evaluate_fixture(
    fixture: dict[str, Any],
    policy: str,
    *,
    hold_ms_override: float | None = None,
) -> dict[str, Any]:
    if policy not in {"legacy", "candidate"}:
        raise ValueError("policy must be legacy or candidate")

    frames = fixture.get("frames")
    render_contract = fixture.get("render_contract")
    if not isinstance(frames, list) or not frames:
        raise ValueError("fixture frames must be a non-empty array")
    if not isinstance(render_contract, dict):
        raise ValueError("fixture render_contract must be an object")

    render_frames = render_contract.get("render_frames")
    if not isinstance(render_frames, list) or not render_frames:
        raise ValueError("render_contract.render_frames must be non-empty")
    render_frame_set = set(render_frames)
    minimum_visible = int(render_contract.get("minimum_visible_render_frames", 0))
    hold_ms = float(
        render_contract.get("hold_ms", 0)
        if hold_ms_override is None
        else hold_ms_override
    )
    if hold_ms <= 0:
        raise ValueError("hold_ms must be positive")

    last_index = -1
    last_time_s = -1.0
    last_direct_time_s: float | None = None
    latched_generation = 0
    pending_direct_render = False
    direct_run_samples = 0
    previous_direct = False
    previous_generation = 0
    source_visibility: list[bool] = []
    render_visibility: dict[int, bool] = {}
    confirmed_gap_hidden = 0
    confirmed_gap_count = 0

    for raw in frames:
        if not isinstance(raw, dict):
            raise ValueError("each fixture frame must be an object")
        index = int(raw["index"])
        time_s = float(raw["time_s"])
        direct = bool(raw.get("direct", False))
        generation = int(raw.get("generation", 0))
        confirmed = bool(raw.get("enemy_identity_confirmed", False))
        source = str(raw.get("source", "none"))
        has_target = generation > 0 and source in {"observed", "cue_hold"}

        if index <= last_index or time_s <= last_time_s:
            raise ValueError("fixture frames must have increasing index and time")
        last_index = index
        last_time_s = time_s

        if direct:
            if previous_direct and previous_generation == generation:
                direct_run_samples += 1
            else:
                direct_run_samples = 1
            last_direct_time_s = time_s
            latched_generation = generation
            if direct_run_samples >= 2:
                pending_direct_render = True
        elif generation != 0 and latched_generation != 0 and generation != latched_generation:
            last_direct_time_s = None
            latched_generation = 0
            pending_direct_render = False
            direct_run_samples = 0
        else:
            direct_run_samples = 0

        latch_fresh = (
            last_direct_time_s is not None
            and time_s >= last_direct_time_s
            and (time_s - last_direct_time_s) * 1000.0 <= hold_ms + 1e-6
        )
        confirmed_continuation = (
            has_target
            and confirmed
            and generation == latched_generation
            and latch_fresh
        )
        visible = direct or (
            policy == "candidate"
            and (pending_direct_render or confirmed_continuation)
            and generation == latched_generation
            and latch_fresh
        )
        source_visibility.append(visible)

        if source == "cue_hold" and confirmed:
            confirmed_gap_count += 1
            if not visible:
                confirmed_gap_hidden += 1

        if index in render_frame_set:
            render_visibility[index] = visible
            if policy == "candidate" and visible:
                pending_direct_render = False

        previous_direct = direct
        previous_generation = generation

    if set(render_visibility) != render_frame_set:
        raise ValueError("every declared render frame must exist in fixture frames")

    visible_render_frames = sum(render_visibility.values())
    oracles = [
        {
            "id": "O1",
            "metric": "confirmed_gap_hidden_frames",
            "operator": "==",
            "threshold": 0,
            "observed": confirmed_gap_hidden,
            "pass": confirmed_gap_hidden == 0,
        },
        {
            "id": "O2",
            "metric": "visible_render_frames",
            "operator": ">=",
            "threshold": minimum_visible,
            "observed": visible_render_frames,
            "pass": visible_render_frames >= minimum_visible,
        },
    ]
    passed = all(item["pass"] for item in oracles)
    return {
        "schema_version": 1,
        "incident_id": fixture.get("incident_id"),
        "policy": policy,
        "status": "PASS" if passed else "FAIL",
        "trigger": {
            "frame_count": len(frames),
            "confirmed_cue_hold_frames": confirmed_gap_count,
            "render_frames": render_frames,
            "hold_ms": hold_ms,
        },
        "metrics": {
            "visible_source_frames": sum(source_visibility),
            "visible_source_runs": _visible_run_count(source_visibility),
            "confirmed_gap_hidden_frames": confirmed_gap_hidden,
            "visible_render_frames": visible_render_frames,
            "render_visibility": {
                str(index): render_visibility[index] for index in render_frames
            },
        },
        "oracles": oracles,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Verify the frozen MW4 Fusion marker continuity fixture."
    )
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--policy", choices=("legacy", "candidate"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    fixture = json.loads(args.fixture.read_text(encoding="utf-8"))
    report = evaluate_fixture(fixture, args.policy)
    report["fixture_sha256"] = hash_file(args.fixture)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(report, ensure_ascii=False, sort_keys=True, indent=2) + "\n",
        encoding="utf-8",
    )
    print(
        f"{report['status']} policy={args.policy} "
        f"visible_render_frames={report['metrics']['visible_render_frames']} "
        f"confirmed_gap_hidden_frames="
        f"{report['metrics']['confirmed_gap_hidden_frames']}"
    )
    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
