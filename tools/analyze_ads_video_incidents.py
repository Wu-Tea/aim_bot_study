#!/usr/bin/env python3
"""Bounded ADS incident extraction from native detailed telemetry.

The caller supplies already-derived steady-clock event anchors.  The script
does not guess a wall/steady conversion and never joins by row proximity: ADS
rows join controller rows by tick_id and committed observations by the exact
source frame/observation identity published in the ADS trace.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


def parse_event(value: str) -> tuple[str, int]:
    name, separator, steady_ns = value.partition("=")
    if not separator or not name or not steady_ns:
        raise argparse.ArgumentTypeError("event must be NAME=STEADY_NS")
    try:
        parsed = int(steady_ns)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("STEADY_NS must be an integer") from exc
    if parsed <= 0:
        raise argparse.ArgumentTypeError("STEADY_NS must be positive")
    return name, parsed


def magnitude(value: Any) -> float:
    if not isinstance(value, list) or len(value) < 2:
        return 0.0
    return math.hypot(float(value[0]), float(value[1]))


def record_time_ns(record: dict[str, Any]) -> int:
    value = int(record.get("controller_consume_ns", 0) or 0)
    if value > 0:
        return value
    value = int(record.get("output_sent_ns", 0) or 0)
    if value > 0:
        return value
    observation = record.get("observation")
    if isinstance(observation, dict):
        return int(observation.get("controller_consume_ns", 0) or 0)
    return 0


def nearest_event(
    timestamp_ns: int,
    events: dict[str, int],
    window_ns: int,
) -> str | None:
    if timestamp_ns <= 0:
        return None
    name, distance = min(
        ((name, abs(timestamp_ns - anchor)) for name, anchor in events.items()),
        key=lambda item: item[1],
    )
    return name if distance <= window_ns else None


def vector_sign_reversals(rows: list[dict[str, Any]], field: str, axis: int) -> int:
    previous = 0
    reversals = 0
    for row in rows:
        value = row.get(field)
        if not isinstance(value, list) or len(value) <= axis:
            continue
        component = float(value[axis])
        sign = 1 if component > 0.02 else -1 if component < -0.02 else 0
        if sign == 0:
            continue
        if previous != 0 and sign != previous:
            reversals += 1
        previous = sign
    return reversals


def summarize_controller(record: dict[str, Any] | None) -> dict[str, Any] | None:
    if record is None:
        return None
    return {
        "tick_id": record.get("tick_id"),
        "aim_mode": record.get("aim_mode"),
        "left_trigger": record.get("left_trigger"),
        "right_trigger": record.get("right_trigger"),
        "firing": record.get("final_fire_button"),
        "manual": [record.get("manual_x"), record.get("manual_y")],
        "filtered_manual": [
            record.get("filtered_manual_x"),
            record.get("filtered_manual_y"),
        ],
        "requested_assist": [
            record.get("requested_assist_x"),
            record.get("requested_assist_y"),
        ],
        "shaped_assist": [
            record.get("shaped_assist_x"),
            record.get("shaped_assist_y"),
        ],
        "target_final": record.get("target_final"),
        "final": [record.get("final_x"), record.get("final_y")],
        "selected_track_id": record.get("selected_track_id"),
        "selected_observation_id": record.get("selected_observation_id"),
        "target_error_px": record.get("target_error_px"),
    }


def summarize_observation(record: dict[str, Any] | None) -> dict[str, Any] | None:
    if record is None:
        return None
    observation = record.get("observation", {})
    return {
        "persistent_target_id": observation.get("persistent_target_id"),
        "source_frame_id": observation.get("source_frame_id"),
        "source_observation_id": observation.get("source_observation_id"),
        "stable_error": observation.get("stable_error"),
        "stable_body_size": observation.get("stable_body_size"),
        "normalized_size": observation.get("normalized_size"),
        "reliability": observation.get("reliability"),
        "eligible_candidate_count": observation.get("eligible_candidate_count"),
        "viewport_sequence": observation.get("viewport_sequence"),
        "viewport_offset": observation.get("viewport_offset"),
        "fresh_observed": observation.get("fresh_observed"),
        "strong_observation": observation.get("strong_observation"),
    }


def summarize_ads_row(
    record: dict[str, Any],
    event_anchor_ns: int,
    controller_by_tick: dict[int, dict[str, Any]],
    observation_by_source: dict[tuple[int, int], dict[str, Any]],
) -> dict[str, Any]:
    source_frame_id = int(record.get("source_frame_id", 0) or 0)
    selected_source_id = int(record.get("selected_source_id", 0) or 0)
    tick_id = int(record.get("tick_id", 0) or 0)
    error = record.get("raw_error", [0.0, 0.0])
    activation_radius = float(record.get("effective_activation_radius_px", 0.0) or 0.0)
    error_radius = magnitude(error)
    return {
        "delta_ms": round((record_time_ns(record) - event_anchor_ns) / 1.0e6, 3),
        "tick_id": tick_id,
        "source_frame_id": source_frame_id,
        "selected_source_id": selected_source_id,
        "plan_admitted": bool(record.get("plan_admitted", False)),
        "acquisition_active": bool(record.get("acquisition_active", False)),
        "acquisition_state": record.get("acquisition_state"),
        "decision_reason": record.get("decision_reason"),
        "source_decision_outcome": record.get("source_decision_outcome"),
        "source_decision_reason": record.get("source_decision_reason"),
        "candidate_count": record.get("candidate_count"),
        "raw_error": error,
        "error_radius_px": round(error_radius, 6),
        "target_size": record.get("target_size"),
        "effective_activation_radius_px": activation_radius,
        "outside_effective_radius": error_radius > activation_radius,
        "requested_ai": record.get("requested_ai"),
        "shaped_ai": record.get("shaped_ai"),
        "fused_output": record.get("fused_output"),
        "post_output": record.get("post_output"),
        "controller": summarize_controller(controller_by_tick.get(tick_id)),
        "observation": summarize_observation(
            observation_by_source.get((source_frame_id, selected_source_id))
        ),
    }


def summarize_group(
    name: str,
    key: tuple[int, int, int],
    rows: list[dict[str, Any]],
    event_anchor_ns: int,
    controller_by_tick: dict[int, dict[str, Any]],
    observation_by_source: dict[tuple[int, int], dict[str, Any]],
) -> dict[str, Any]:
    rows.sort(key=record_time_ns)
    summarized_rows = [
        summarize_ads_row(
            row,
            event_anchor_ns,
            controller_by_tick,
            observation_by_source,
        )
        for row in rows
    ]
    first_admitted = next(
        (row for row in summarized_rows if row["plan_admitted"]), None
    )
    return {
        "event": name,
        "physical_ads_epoch": key[0],
        "target_acquisition_id": key[1],
        "selector_target_generation": key[2],
        "row_count": len(rows),
        "active_row_count": sum(bool(row.get("acquisition_active")) for row in rows),
        "admitted_row_count": sum(bool(row.get("plan_admitted")) for row in rows),
        "first_delta_ms": summarized_rows[0]["delta_ms"],
        "last_delta_ms": summarized_rows[-1]["delta_ms"],
        "candidate_count_range": [
            min(int(row.get("candidate_count", 0) or 0) for row in rows),
            max(int(row.get("candidate_count", 0) or 0) for row in rows),
        ],
        "maximum_error_radius_px": max(
            row["error_radius_px"] for row in summarized_rows
        ),
        "maximum_requested_ai_magnitude": max(
            magnitude(row.get("requested_ai")) for row in rows
        ),
        "maximum_fused_output_magnitude": max(
            magnitude(row.get("fused_output")) for row in rows
        ),
        "fused_output_x_sign_reversals": vector_sign_reversals(
            rows, "fused_output", 0
        ),
        "fused_output_y_sign_reversals": vector_sign_reversals(
            rows, "fused_output", 1
        ),
        "decision_reasons": dict(Counter(row.get("decision_reason") for row in rows)),
        "first_admitted": first_admitted,
        "rows": summarized_rows,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--telemetry", type=Path, required=True)
    parser.add_argument("--event", action="append", type=parse_event, required=True)
    parser.add_argument("--window-seconds", type=float, default=9.0)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    events = dict(args.event)
    if len(events) != len(args.event):
        raise SystemExit("event names must be unique")
    window_ns = int(args.window_seconds * 1.0e9)

    sha256 = hashlib.sha256()
    integrity = {
        "bytes": 0,
        "total_lines": 0,
        "blank_lines": 0,
        "parsed_objects": 0,
        "malformed_lines": 0,
        "non_object_lines": 0,
    }
    record_types: Counter[str] = Counter()
    telemetry_schemas: Counter[int] = Counter()
    session_metadata: list[dict[str, Any]] = []
    ads_by_event: dict[str, list[dict[str, Any]]] = defaultdict(list)
    controller_by_tick: dict[int, dict[str, Any]] = {}
    observation_by_source: dict[tuple[int, int], dict[str, Any]] = {}

    with args.telemetry.open("rb") as stream:
        for raw_line in stream:
            sha256.update(raw_line)
            integrity["bytes"] += len(raw_line)
            integrity["total_lines"] += 1
            if not raw_line.strip():
                integrity["blank_lines"] += 1
                continue
            try:
                record = json.loads(raw_line)
            except (UnicodeDecodeError, json.JSONDecodeError):
                integrity["malformed_lines"] += 1
                continue
            if not isinstance(record, dict):
                integrity["non_object_lines"] += 1
                continue
            integrity["parsed_objects"] += 1
            record_type = str(record.get("type", "unknown"))
            record_types[record_type] += 1
            schema = record.get("schema_version")
            if isinstance(schema, int):
                telemetry_schemas[schema] += 1
            if record_type == "session_metadata":
                session_metadata.append(record)
                continue
            timestamp_ns = record_time_ns(record)
            event_name = nearest_event(timestamp_ns, events, window_ns)
            if event_name is None:
                continue
            if record_type == "ads_acquisition_trace":
                ads_by_event[event_name].append(record)
            elif record_type == "controller_sample":
                controller_by_tick[int(record.get("tick_id", 0) or 0)] = record
            elif record_type == "committed_capture_observation":
                observation = record.get("observation", {})
                if isinstance(observation, dict):
                    key = (
                        int(observation.get("source_frame_id", 0) or 0),
                        int(observation.get("source_observation_id", 0) or 0),
                    )
                    observation_by_source[key] = record

    groups: list[dict[str, Any]] = []
    for event_name, rows in ads_by_event.items():
        grouped: dict[tuple[int, int, int], list[dict[str, Any]]] = defaultdict(list)
        for row in rows:
            key = (
                int(row.get("physical_ads_epoch", 0) or 0),
                int(row.get("target_acquisition_id", 0) or 0),
                int(row.get("selector_target_generation", 0) or 0),
            )
            grouped[key].append(row)
        for key, group_rows in grouped.items():
            if not any(
                bool(row.get("acquisition_active"))
                or bool(row.get("plan_admitted"))
                for row in group_rows
            ):
                continue
            groups.append(
                summarize_group(
                    event_name,
                    key,
                    group_rows,
                    events[event_name],
                    controller_by_tick,
                    observation_by_source,
                )
            )
    groups.sort(key=lambda group: (group["event"], group["first_delta_ms"]))

    output = {
        "schema_version": 1,
        "analysis": "ads_video_incident_windows",
        "source": {
            "artifact_id": "telemetry-session-20260820T174549Z-0",
            "size_bytes": integrity["bytes"],
            "sha256": sha256.hexdigest(),
        },
        "integrity": integrity,
        "record_types": dict(sorted(record_types.items())),
        "telemetry_schemas": {
            str(key): value for key, value in sorted(telemetry_schemas.items())
        },
        "session_metadata": session_metadata,
        "events": [
            {"name": name, "estimated_steady_ns": steady_ns}
            for name, steady_ns in events.items()
        ],
        "window_seconds": args.window_seconds,
        "join_contract": {
            "controller": "ads.tick_id == controller.tick_id",
            "observation": (
                "ads.(source_frame_id, selected_source_id) == "
                "observation.(source_frame_id, source_observation_id)"
            ),
        },
        "groups": groups,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(output, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
