#!/usr/bin/env python3
"""Extract a sanitized AimLab runtime profile from audited native telemetry.

The output intentionally contains only benchmark covariates and stable artifact
identities.  It is not an exact gameplay replay: current telemetry cannot
separate target motion, camera motion, detector geometry, and the game plant.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence


PROFILE_SCHEMA = "sustained_aimlab_runtime_profile_v2"
HEX_64 = frozenset("0123456789abcdef")
SUPPORTED_AIM_MODES = frozenset(
    {"ads", "ads_acquire", "acquisition", "body_lock", "bodylock"}
)


@dataclass(frozen=True)
class SelectedFile:
    file_id: str
    source: Path
    sha256: str
    size_bytes: int
    parsed_rows: int
    record_types: dict[str, int]
    expected_schema: int


@dataclass(frozen=True)
class Observation:
    key: tuple[int, int]
    source_observation_id: int
    captured_ns: int
    result_ns: int
    consume_ns: int
    error_x: float
    error_y: float
    body_width: float
    body_height: float


@dataclass(frozen=True)
class ManualPoint:
    key: tuple[int, str]
    sample_ns: int
    raw_radial: float
    raw_tangential: float
    filtered_radial: float
    filtered_tangential: float
    operation_class: str


def _load_object(path: Path, label: str) -> dict:
    try:
        value = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(f"failed to read {label}: {error}") from error
    if not isinstance(value, dict):
        raise ValueError(f"{label} must be a JSON object")
    return value


def _is_sha256(value: object) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 64
        and all(character in HEX_64 for character in value.lower())
    )


def _finite_number(value: object) -> float | None:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    converted = float(value)
    return converted if math.isfinite(converted) else None


def _positive_int(value: object) -> int | None:
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        return None
    return value


def _vector2(value: object) -> tuple[float, float] | None:
    if not isinstance(value, list) or len(value) != 2:
        return None
    first = _finite_number(value[0])
    second = _finite_number(value[1])
    if first is None or second is None:
        return None
    return first, second


def _round_ms(nanoseconds: int) -> int:
    return int(math.floor(nanoseconds / 1_000_000.0 + 0.5))


def _quantile(values: Sequence[float], fraction: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lower = int(math.floor(position))
    upper = min(lower + 1, len(ordered) - 1)
    weight = position - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def _summary(values: Sequence[float]) -> dict:
    if not values:
        return {"count": 0, "p50": None, "p95": None, "p99": None, "max": None}
    return {
        "count": len(values),
        "p50": round(float(_quantile(values, 0.50)), 6),
        "p95": round(float(_quantile(values, 0.95)), 6),
        "p99": round(float(_quantile(values, 0.99)), 6),
        "max": round(max(values), 6),
    }


def _even_sample(values: Sequence, limit: int) -> list:
    if limit <= 0:
        raise ValueError("sample limits must be positive")
    if len(values) <= limit:
        return list(values)
    if limit == 1:
        return [values[len(values) // 2]]
    indexes = [
        int(math.floor(index * (len(values) - 1) / (limit - 1) + 0.5))
        for index in range(limit)
    ]
    return [values[index] for index in indexes]


def _cohort_by_name(document: dict, name: str, label: str) -> dict:
    cohorts = document.get("cohorts")
    if not isinstance(cohorts, list):
        raise ValueError(f"{label} has no cohorts")
    matches = [item for item in cohorts if isinstance(item, dict) and item.get("name") == name]
    if len(matches) != 1:
        raise ValueError(f"{label} must contain exactly one cohort named {name!r}")
    return matches[0]


def _resolve_source(raw: object, manifest_path: Path) -> Path:
    if not isinstance(raw, str) or not raw:
        raise ValueError("telemetry file has no source location")
    source = Path(raw)
    if source.is_absolute():
        return source
    working_copy = Path.cwd() / source
    if working_copy.exists():
        return working_copy
    return manifest_path.parent / source


def _select_files(
    manifest_path: Path,
    manifest: dict,
    intake: dict,
    file_ids: Sequence[str] | None,
) -> tuple[dict, list[SelectedFile]]:
    if intake.get("status") != "PASS":
        raise ValueError("runtime profiles require a PASS audit intake")
    if manifest.get("schema_version") != 1 or intake.get("schema_version") != 1:
        raise ValueError("unsupported audit schema")
    audit_id = manifest.get("audit_id")
    if not isinstance(audit_id, str) or intake.get("audit_id") != audit_id:
        raise ValueError("audit manifest/intake identity mismatch")
    audit_sha = intake.get("artifact_sha256")
    if not _is_sha256(audit_sha):
        raise ValueError("audit intake has no canonical artifact SHA-256")

    manifest_cohorts = manifest.get("cohorts")
    if not isinstance(manifest_cohorts, list) or len(manifest_cohorts) != 1:
        raise ValueError("runtime profile extraction requires one declared cohort")
    manifest_cohort = manifest_cohorts[0]
    if not isinstance(manifest_cohort, dict) or not isinstance(manifest_cohort.get("name"), str):
        raise ValueError("invalid manifest cohort")
    cohort_name = manifest_cohort["name"]
    intake_cohort = _cohort_by_name(intake, cohort_name, "audit intake")

    runtime = manifest_cohort.get("runtime")
    if not isinstance(runtime, dict):
        raise ValueError("manifest cohort has no runtime identity")
    for identity in ("executable_sha256", "config_hash", "engine_hash"):
        if not _is_sha256(runtime.get(identity)):
            raise ValueError(f"runtime {identity} is unavailable")
    if runtime.get("logging_mode") != "detailed":
        raise ValueError("runtime profile extraction requires detailed telemetry")
    telemetry_schema = runtime.get("telemetry_schema")
    if not isinstance(telemetry_schema, int) or telemetry_schema <= 0:
        raise ValueError("runtime telemetry schema is unavailable")

    declared = {
        item.get("id"): item
        for item in manifest_cohort.get("files", [])
        if isinstance(item, dict) and isinstance(item.get("id"), str)
    }
    checked = {
        item.get("id"): item
        for item in intake_cohort.get("files", [])
        if isinstance(item, dict) and isinstance(item.get("id"), str)
    }
    requested = list(file_ids or [])
    if not requested:
        requested = [
            file_id
            for file_id, item in declared.items()
            if item.get("kind") == "telemetry_jsonl"
            and checked.get(file_id, {}).get("jsonl", {}).get("malformed_rows") == 0
        ]
    if not requested or len(set(requested)) != len(requested):
        raise ValueError("select at least one unique telemetry file ID")

    selected: list[SelectedFile] = []
    for file_id in requested:
        source_item = declared.get(file_id)
        checked_item = checked.get(file_id)
        if source_item is None or checked_item is None:
            raise ValueError(f"telemetry file ID {file_id!r} is not present in both audit inputs")
        if source_item.get("kind") != "telemetry_jsonl" or checked_item.get("kind") != "telemetry_jsonl":
            raise ValueError(f"file {file_id!r} is not detailed telemetry JSONL")
        jsonl = checked_item.get("jsonl")
        if not isinstance(jsonl, dict) or checked_item.get("available") is not True:
            raise ValueError(f"file {file_id!r} did not pass availability preflight")
        if jsonl.get("malformed_rows") != 0:
            raise ValueError(f"file {file_id!r} contains malformed rows")
        parsed_rows = jsonl.get("parsed_rows")
        if not isinstance(parsed_rows, int) or parsed_rows <= 0:
            raise ValueError(f"file {file_id!r} has no parsed rows")
        sha256 = checked_item.get("sha256")
        size_bytes = checked_item.get("size_bytes")
        if not _is_sha256(sha256) or not isinstance(size_bytes, int) or size_bytes <= 0:
            raise ValueError(f"file {file_id!r} has incomplete integrity evidence")
        record_types = jsonl.get("record_types")
        if not isinstance(record_types, dict):
            raise ValueError(f"file {file_id!r} has no record inventory")
        expected_schema = source_item.get("expected_schema_version", telemetry_schema)
        if expected_schema != telemetry_schema:
            raise ValueError(f"file {file_id!r} schema contradicts runtime identity")
        selected.append(
            SelectedFile(
                file_id=file_id,
                source=_resolve_source(source_item.get("path"), manifest_path),
                sha256=sha256.lower(),
                size_bytes=size_bytes,
                parsed_rows=parsed_rows,
                record_types={str(key): int(value) for key, value in record_types.items()},
                expected_schema=expected_schema,
            )
        )
    return runtime, selected


def _observation_from(row: dict, exclusions: Counter) -> Observation | None:
    value = row.get("observation")
    if not isinstance(value, dict):
        exclusions["observation_not_object"] += 1
        return None
    if row.get("vision_sample_quality") != "normal":
        exclusions["observation_quality_not_normal"] += 1
        return None
    if value.get("fresh_observed") is not True or value.get("stable_coordinates_valid") is not True:
        exclusions["observation_not_fresh_stable"] += 1
        return None
    target_id = _positive_int(value.get("persistent_target_id"))
    source_observation_id = _positive_int(value.get("source_observation_id"))
    captured_ns = _positive_int(value.get("captured_at_ns"))
    result_ns = _positive_int(value.get("result_at_ns"))
    consume_ns = _positive_int(value.get("controller_consume_ns"))
    error = _vector2(value.get("stable_error"))
    body = _vector2(value.get("stable_body_size"))
    if None in (target_id, source_observation_id, captured_ns, result_ns, consume_ns) or error is None or body is None:
        exclusions["observation_required_field_missing"] += 1
        return None
    if not (captured_ns <= result_ns <= consume_ns):
        exclusions["observation_clock_order_invalid"] += 1
        return None
    if body[0] <= 0.0 or body[1] <= 0.0:
        exclusions["observation_body_size_invalid"] += 1
        return None
    ads_epoch = value.get("ads_epoch", 0)
    if not isinstance(ads_epoch, int) or ads_epoch < 0:
        exclusions["observation_ads_epoch_invalid"] += 1
        return None
    return Observation(
        key=(target_id, ads_epoch),
        source_observation_id=source_observation_id,
        captured_ns=captured_ns,
        result_ns=result_ns,
        consume_ns=consume_ns,
        error_x=error[0],
        error_y=error[1],
        body_width=body[0],
        body_height=body[1],
    )


def _manual_from(row: dict, exclusions: Counter) -> ManualPoint | None:
    if row.get("physical_connected") is not True:
        exclusions["manual_physical_disconnected"] += 1
        return None
    if row.get("output_delivered") is not True:
        exclusions["manual_output_not_delivered"] += 1
        return None
    if row.get("current_observed_target_present") is not True:
        exclusions["manual_no_current_observation"] += 1
        return None
    target_id = _positive_int(row.get("selected_track_id"))
    sample_ns = _positive_int(row.get("sample_ns"))
    aim_mode = row.get("aim_mode")
    if target_id is None or sample_ns is None or aim_mode not in SUPPORTED_AIM_MODES:
        exclusions["manual_no_supported_target_mode"] += 1
        return None
    raw_x = _finite_number(row.get("manual_x"))
    raw_y = _finite_number(row.get("manual_y"))
    filtered_x = _finite_number(row.get("filtered_manual_x"))
    filtered_y = _finite_number(row.get("filtered_manual_y"))
    error_x = _finite_number(row.get("control_error_x"))
    error_y = _finite_number(row.get("control_error_y"))
    if None in (raw_x, raw_y, filtered_x, filtered_y, error_x, error_y):
        exclusions["manual_required_field_missing"] += 1
        return None
    if math.hypot(raw_x, raw_y) > 1.01 or math.hypot(filtered_x, filtered_y) > 1.01:
        exclusions["manual_out_of_range"] += 1
        return None
    control_x = error_x
    control_y = -error_y
    error_magnitude = math.hypot(control_x, control_y)
    if error_magnitude < 1.0e-6:
        exclusions["manual_zero_error_basis"] += 1
        return None
    helpful_x = control_x / error_magnitude
    helpful_y = control_y / error_magnitude
    tangent_x = -helpful_y
    tangent_y = helpful_x
    raw_radial = raw_x * helpful_x + raw_y * helpful_y
    raw_tangential = raw_x * tangent_x + raw_y * tangent_y
    filtered_radial = filtered_x * helpful_x + filtered_y * helpful_y
    filtered_tangential = filtered_x * tangent_x + filtered_y * tangent_y
    operation_class = row.get("operation_class")
    if not isinstance(operation_class, str) or not operation_class:
        operation_class = "unknown"
    return ManualPoint(
        key=(target_id, str(aim_mode)),
        sample_ns=sample_ns,
        raw_radial=raw_radial,
        raw_tangential=raw_tangential,
        filtered_radial=filtered_radial,
        filtered_tangential=filtered_tangential,
        operation_class=operation_class,
    )


def _read_telemetry(
    selected: Sequence[SelectedFile],
    runtime: dict,
) -> tuple[list[Observation], list[ManualPoint], dict]:
    observations: list[Observation] = []
    manual_points: list[ManualPoint] = []
    counts: Counter = Counter()
    exclusions: Counter = Counter()
    metadata_records = 0
    session_ids: set[str] = set()

    for item in selected:
        if not item.source.is_file():
            raise ValueError(f"audited input {item.file_id!r} is no longer available")
        digest = hashlib.sha256()
        size_bytes = 0
        parsed_rows = 0
        file_counts: Counter = Counter()
        try:
            source = item.source.open("rb")
        except OSError as error:
            raise ValueError(f"failed to open audited input {item.file_id!r}: {error}") from error
        with source:
            for line_number, encoded in enumerate(source, start=1):
                digest.update(encoded)
                size_bytes += len(encoded)
                if not encoded.strip():
                    raise ValueError(f"audited input {item.file_id!r} contains an unexpected blank row")
                try:
                    row = json.loads(encoded)
                except (UnicodeDecodeError, json.JSONDecodeError) as error:
                    raise ValueError(
                        f"audited input {item.file_id!r} changed at row {line_number}: {error}"
                    ) from error
                if not isinstance(row, dict):
                    raise ValueError(f"audited input {item.file_id!r} row {line_number} is not an object")
                parsed_rows += 1
                record_type = row.get("type")
                if not isinstance(record_type, str):
                    raise ValueError(f"audited input {item.file_id!r} row {line_number} has no type")
                schema_version = row.get("schema_version")
                if schema_version != item.expected_schema:
                    raise ValueError(
                        f"audited input {item.file_id!r} row {line_number} changed schema"
                    )
                counts[record_type] += 1
                file_counts[record_type] += 1

                if record_type == "session_metadata":
                    metadata_records += 1
                    session_id = row.get("session_id")
                    if isinstance(session_id, str) and session_id:
                        session_ids.add(session_id)
                    identity_pairs = (
                        ("executable_sha256", "executable_sha256"),
                        ("config_hash", "config_hash"),
                        ("engine_hash", "engine_hash"),
                        ("build_commit", "git_commit"),
                    )
                    for record_key, runtime_key in identity_pairs:
                        if row.get(record_key) != runtime.get(runtime_key):
                            raise ValueError(
                                f"session metadata {record_key} contradicts audit runtime identity"
                            )
                elif record_type == "committed_capture_observation":
                    observation = _observation_from(row, exclusions)
                    if observation is not None:
                        observations.append(observation)
                elif record_type == "controller_sample":
                    point = _manual_from(row, exclusions)
                    if point is not None:
                        manual_points.append(point)

        if digest.hexdigest() != item.sha256 or size_bytes != item.size_bytes:
            raise ValueError(f"audited input {item.file_id!r} no longer matches its SHA-256/size")
        if parsed_rows != item.parsed_rows:
            raise ValueError(f"audited input {item.file_id!r} row count changed after preflight")
        if dict(file_counts) != item.record_types:
            raise ValueError(f"audited input {item.file_id!r} record inventory changed after preflight")

    if metadata_records != len(selected) or len(session_ids) != 1:
        raise ValueError("selected telemetry shards do not share one complete session identity")
    required_counts = {
        "committed_capture_observation": len(observations),
        "controller_sample": len(manual_points),
    }
    for record_type, eligible in required_counts.items():
        if counts[record_type] <= 0 or eligible <= 0:
            raise ValueError(f"selected telemetry has no eligible {record_type} records")
    return observations, manual_points, {
        "record_types": dict(sorted(counts.items())),
        "exclusions": dict(sorted(exclusions.items())),
        "session_id": next(iter(session_ids)),
    }


def _build_observation_pattern(
    observations: Sequence[Observation],
    max_samples: int,
    max_gap_ms: int,
) -> tuple[list[dict], list[dict], dict]:
    grouped: dict[tuple[int, int], list[Observation]] = defaultdict(list)
    for observation in observations:
        grouped[observation.key].append(observation)

    pattern: list[dict] = []
    target_samples: list[dict] = []
    exclusions: Counter = Counter()
    seen_target_samples: set[tuple[int, int]] = set()
    for key, group in grouped.items():
        ordered = sorted(group, key=lambda value: (value.consume_ns, value.captured_ns))
        unique: list[Observation] = []
        seen_observation_ids: set[int] = set()
        for current in ordered:
            if current.source_observation_id in seen_observation_ids:
                exclusions["duplicate_source_observation"] += 1
                continue
            seen_observation_ids.add(current.source_observation_id)
            unique.append(current)
        if not unique:
            continue
        if key not in seen_target_samples:
            first = unique[0]
            target_samples.append(
                {
                    "initial_error_px": [round(first.error_x, 6), round(first.error_y, 6)],
                    "body_size_px": [round(first.body_width, 6), round(first.body_height, 6)],
                }
            )
            seen_target_samples.add(key)
        previous = unique[0]
        for current in unique[1:]:
            interval_ms = _round_ms(current.consume_ns - previous.consume_ns)
            capture_age_ms = _round_ms(current.consume_ns - current.captured_ns)
            result_delay_ms = _round_ms(current.result_ns - current.captured_ns)
            consume_delay_ms = _round_ms(current.consume_ns - current.result_ns)
            if interval_ms <= 0 or interval_ms > max_gap_ms:
                exclusions["delivery_interval_outside_profile"] += 1
            elif (
                capture_age_ms < 0
                or capture_age_ms > 100
                or result_delay_ms < 0
                or consume_delay_ms < 0
            ):
                exclusions["observation_latency_outside_profile"] += 1
            else:
                pattern.append(
                    {
                        "delivery_interval_ms": interval_ms,
                        "capture_age_ms": capture_age_ms,
                        "result_delay_ms": result_delay_ms,
                        "consume_delay_ms": consume_delay_ms,
                    }
                )
            previous = current
    if not pattern:
        raise ValueError("no same-target observation timing pairs survived profile gates")
    sampled_pattern = _even_sample(pattern, max_samples)
    sampled_targets = _even_sample(target_samples, min(64, max_samples))
    statistics = {
        "eligible_pairs": len(pattern),
        "selected_pairs": len(sampled_pattern),
        "target_episodes": len(target_samples),
        "selected_target_samples": len(sampled_targets),
        "exclusions": dict(sorted(exclusions.items())),
        "delivery_interval_ms": _summary(
            [float(value["delivery_interval_ms"]) for value in pattern]
        ),
        "capture_age_ms": _summary(
            [float(value["capture_age_ms"]) for value in pattern]
        ),
        "result_delay_ms": _summary(
            [float(value["result_delay_ms"]) for value in pattern]
        ),
        "consume_delay_ms": _summary(
            [float(value["consume_delay_ms"]) for value in pattern]
        ),
    }
    return sampled_pattern, sampled_targets, statistics


def _manual_category(samples: Sequence[dict]) -> str:
    active = [
        sample
        for sample in samples
        if math.hypot(sample["radial"], sample["tangential"]) >= 0.05
    ]
    if not active or len(active) / len(samples) < 0.10:
        return "idle"
    helpful = sum(sample["radial"] > 0.02 for sample in active) / len(active)
    opposing = sum(sample["radial"] < -0.02 for sample in active) / len(active)
    if helpful >= 0.70:
        return "helpful"
    if opposing >= 0.70:
        return "opposing"
    return "mixed"


def _habit_window(samples: Sequence[dict], limit: int) -> tuple[list[dict], int]:
    if len(samples) <= limit:
        return list(samples), 0
    active_indexes = [
        index
        for index, sample in enumerate(samples)
        if math.hypot(sample["radial"], sample["tangential"]) >= 0.05
    ]
    if not active_indexes:
        start = (len(samples) - limit) // 2
    else:
        # Preserve the user's lead-in and response onset instead of selecting
        # only the highest-energy window, which erases reaction behavior.
        preroll = max(1, limit // 4)
        start = max(0, active_indexes[0] - preroll)
        start = min(start, len(samples) - limit)
    return list(samples[start : start + limit]), start


def _stratified_manual_selection(
    candidates: Sequence[dict],
    max_segments: int,
) -> list[dict]:
    if max_segments <= 0:
        raise ValueError("maximum manual segments must be positive")
    category_order = ("idle", "helpful", "opposing", "mixed")
    buckets = {
        category: [
            candidate
            for candidate in candidates
            if candidate["category"] == category
        ]
        for category in category_order
    }
    active_categories = [category for category in category_order if buckets[category]]
    limit = min(max_segments, len(candidates))
    allocations = {category: 0 for category in category_order}
    if limit >= len(active_categories):
        for category in active_categories:
            allocations[category] = 1
    else:
        for category in sorted(
            active_categories,
            key=lambda value: (-len(buckets[value]), category_order.index(value)),
        )[:limit]:
            allocations[category] = 1

    while sum(allocations.values()) < limit:
        eligible = [
            category
            for category in active_categories
            if allocations[category] < len(buckets[category])
        ]
        if not eligible:
            break
        category = max(
            eligible,
            key=lambda value: (
                len(buckets[value]) / len(candidates)
                - allocations[value] / limit,
                -category_order.index(value),
            ),
        )
        allocations[category] += 1

    chosen: list[dict] = []
    for category in category_order:
        count = allocations[category]
        if count:
            chosen.extend(_even_sample(buckets[category], count))
    return sorted(chosen, key=lambda value: value["_order"])


def _build_manual_segments(
    points: Sequence[ManualPoint],
    max_samples_per_segment: int,
    max_gap_ms: int,
    max_segments: int,
) -> tuple[list[dict], dict, dict]:
    ordered = sorted(points, key=lambda value: value.sample_ns)
    episode_start_ns: dict[tuple[int, str], int] = {}
    for point in ordered:
        episode_start_ns.setdefault(point.key, point.sample_ns)
    point_segments: list[list[ManualPoint]] = []
    current: list[ManualPoint] = []
    for point in ordered:
        if current:
            gap_ms = (point.sample_ns - current[-1].sample_ns) / 1_000_000.0
            if point.key != current[-1].key or gap_ms <= 0.0 or gap_ms > max_gap_ms:
                if len(current) >= 2:
                    point_segments.append(current)
                current = []
        current.append(point)
    if len(current) >= 2:
        point_segments.append(current)

    candidates: list[dict] = []
    complete_segments: list[list[dict]] = []
    complete_start_delays: list[int] = []
    rejected_gap = 0
    rejected_late_start = 0
    for order, segment in enumerate(point_segments):
        samples: list[dict] = []
        for current_point, next_point in zip(segment, segment[1:]):
            duration_ms = _round_ms(next_point.sample_ns - current_point.sample_ns)
            if duration_ms <= 0 or duration_ms > max_gap_ms:
                rejected_gap += 1
                continue
            samples.append(
                {
                    "duration_ms": duration_ms,
                    "radial": round(current_point.raw_radial, 6),
                    "tangential": round(current_point.raw_tangential, 6),
                    "_filtered_radial": current_point.filtered_radial,
                    "_filtered_tangential": current_point.filtered_tangential,
                    "_operation_class": current_point.operation_class,
                }
            )
        if not samples:
            continue
        segment_start_delay_ms = _round_ms(
            segment[0].sample_ns - episode_start_ns[segment[0].key]
        )
        complete_segments.append(samples)
        complete_start_delays.append(segment_start_delay_ms)
        selected_samples, window_start = _habit_window(
            samples, max_samples_per_segment
        )
        replay_start_delay_ms = segment_start_delay_ms + sum(
            sample["duration_ms"] for sample in samples[:window_start]
        )
        if replay_start_delay_ms > 1000:
            rejected_late_start += 1
            continue
        candidates.append(
            {
                "category": _manual_category(selected_samples),
                "aim_mode": segment[0].key[1],
                "start_delay_ms": replay_start_delay_ms,
                "samples": selected_samples,
                "_order": order,
            }
        )
    if not candidates:
        raise ValueError("no stable target-relative manual segments survived profile gates")

    chosen_internal = _stratified_manual_selection(candidates, max_segments)
    chosen = [
        {
            "category": value["category"],
            "aim_mode": value["aim_mode"],
            "start_delay_ms": value["start_delay_ms"],
            "samples": [
                {
                    "duration_ms": sample["duration_ms"],
                    "radial": sample["radial"],
                    "tangential": sample["tangential"],
                }
                for sample in value["samples"]
            ],
        }
        for value in chosen_internal
    ]

    all_samples = [sample for segment in complete_segments for sample in segment]
    active_samples = [
        sample
        for sample in all_samples
        if math.hypot(sample["radial"], sample["tangential"]) >= 0.05
    ]
    onset_delays: list[float] = []
    onset_from_mode_start: list[float] = []
    release_tails: list[float] = []
    active_durations: list[float] = []
    delta_magnitudes: list[float] = []
    radial_reversals = 0
    total_duration_ms = 0.0
    for segment_start_delay, segment in zip(
        complete_start_delays, complete_segments
    ):
        active_indexes = [
            index
            for index, sample in enumerate(segment)
            if math.hypot(sample["radial"], sample["tangential"]) >= 0.05
        ]
        segment_duration = sum(sample["duration_ms"] for sample in segment)
        total_duration_ms += segment_duration
        if active_indexes:
            first_active = active_indexes[0]
            last_active = active_indexes[-1]
            onset_delays.append(
                float(sum(sample["duration_ms"] for sample in segment[:first_active]))
            )
            onset_from_mode_start.append(
                float(segment_start_delay) + onset_delays[-1]
            )
            release_tails.append(
                float(sum(sample["duration_ms"] for sample in segment[last_active + 1 :]))
            )
            active_durations.append(
                float(
                    sum(
                        sample["duration_ms"]
                        for sample in segment
                        if math.hypot(sample["radial"], sample["tangential"]) >= 0.05
                    )
                )
            )
        previous_sign = 0
        for previous, current in zip(segment, segment[1:]):
            delta_magnitudes.append(
                math.hypot(
                    current["radial"] - previous["radial"],
                    current["tangential"] - previous["tangential"],
                )
            )
        for sample in segment:
            sign = 1 if sample["radial"] >= 0.05 else -1 if sample["radial"] <= -0.05 else 0
            if sign:
                if previous_sign and sign != previous_sign:
                    radial_reversals += 1
                previous_sign = sign

    category_counts = Counter(value["category"] for value in candidates)
    selected_category_counts = Counter(value["category"] for value in chosen_internal)
    operation_counts = Counter(sample["_operation_class"] for sample in all_samples)
    attenuation_ratios = []
    for sample in all_samples:
        raw_magnitude = math.hypot(sample["radial"], sample["tangential"])
        if raw_magnitude >= 0.02:
            attenuation_ratios.append(
                math.hypot(
                    sample["_filtered_radial"], sample["_filtered_tangential"]
                ) / raw_magnitude
            )
    active_count = len(active_samples)
    statistics = {
        "eligible_segments": len(candidates),
        "selected_segments": len(chosen),
        "selected_categories": [value["category"] for value in chosen],
        "eligible_category_counts": dict(sorted(category_counts.items())),
        "selected_category_counts": dict(sorted(selected_category_counts.items())),
        "rejected_sample_gaps": rejected_gap,
        "rejected_start_after_replay_horizon": rejected_late_start,
        "duration_ms": _summary([float(value["duration_ms"]) for value in all_samples]),
        "manual_magnitude": _summary(
            [math.hypot(value["radial"], value["tangential"]) for value in all_samples]
        ),
    }
    habit_summary = {
        "manual_signal": "physical_right_stick",
        "active_threshold": 0.05,
        "sample_count": len(all_samples),
        "active_sample_fraction": round(active_count / len(all_samples), 6),
        "active_direction_fractions": {
            "helpful": round(
                sum(sample["radial"] > 0.02 for sample in active_samples) /
                max(1, active_count),
                6,
            ),
            "opposing": round(
                sum(sample["radial"] < -0.02 for sample in active_samples) /
                max(1, active_count),
                6,
            ),
            "tangential_dominant": round(
                sum(
                    abs(sample["tangential"]) > abs(sample["radial"])
                    for sample in active_samples
                ) / max(1, active_count),
                6,
            ),
        },
        "onset_delay_ms": _summary(onset_delays),
        "onset_delay_from_mode_start_ms": _summary(onset_from_mode_start),
        "replay_segment_start_delay_ms": _summary(
            [float(value["start_delay_ms"]) for value in candidates]
        ),
        "active_duration_ms": _summary(active_durations),
        "release_tail_ms": _summary(release_tails),
        "stick_delta_magnitude": _summary(delta_magnitudes),
        "filter_attenuation_ratio": _summary(attenuation_ratios),
        "radial_reversals": radial_reversals,
        "radial_reversals_per_second": round(
            radial_reversals / max(total_duration_ms / 1000.0, 1.0e-9), 6
        ),
        "operation_class_counts": dict(sorted(operation_counts.items())),
        "eligible_category_counts": dict(sorted(category_counts.items())),
        "selected_category_counts": dict(sorted(selected_category_counts.items())),
    }
    return chosen, statistics, habit_summary


def build_runtime_profile(
    manifest_path: Path,
    intake_path: Path,
    *,
    file_ids: Sequence[str] | None = None,
    max_observation_samples: int = 256,
    max_manual_samples_per_segment: int = 160,
    max_manual_segments: int = 32,
    max_gap_ms: int = 50,
) -> dict:
    """Build one deterministic, sanitized runtime-profile object."""
    manifest_path = Path(manifest_path)
    intake_path = Path(intake_path)
    manifest = _load_object(manifest_path, "audit manifest")
    intake = _load_object(intake_path, "audit intake")
    runtime, selected = _select_files(
        manifest_path, manifest, intake, file_ids
    )
    observations, manual_points, telemetry = _read_telemetry(selected, runtime)
    observation_pattern, target_samples, observation_statistics = (
        _build_observation_pattern(observations, max_observation_samples, max_gap_ms)
    )
    manual_segments, manual_statistics, input_habits = _build_manual_segments(
        manual_points,
        max_manual_samples_per_segment,
        min(max_gap_ms, 20),
        max_manual_segments,
    )

    profile = {
        "schema": PROFILE_SCHEMA,
        "source": {
            "audit_id": manifest["audit_id"],
            "audit_status": intake["status"],
            "audit_artifact_sha256": intake["artifact_sha256"].lower(),
            "telemetry_session_id": telemetry["session_id"],
            "runtime_sha256": runtime["executable_sha256"].lower(),
            "git_commit": runtime.get("git_commit", "unknown"),
            "config_sha256": runtime["config_hash"].lower(),
            "engine_sha256": runtime["engine_hash"].lower(),
            "telemetry_schema": runtime["telemetry_schema"],
            "logging_mode": runtime["logging_mode"],
            "game_refresh_hz": runtime.get("game_refresh_hz", "unknown"),
            "files": [
                {
                    "id": item.file_id,
                    "sha256": item.sha256,
                    "size_bytes": item.size_bytes,
                }
                for item in selected
            ],
        },
        "coverage": {
            "parsed_rows": sum(item.parsed_rows for item in selected),
            "malformed_rows": 0,
            "record_types": telemetry["record_types"],
            "field_exclusions": telemetry["exclusions"],
            "observation": observation_statistics,
            "manual": manual_statistics,
        },
        "observation_pattern": observation_pattern,
        "manual_segments": manual_segments,
        "input_habits": input_habits,
        "target_samples": target_samples,
        "plant": {
            "status": "unavailable",
            "camera_response_px_per_stick_second": None,
            "slowdown_curve": None,
            "reason": (
                "telemetry does not uniquely separate target motion, camera motion, "
                "detector geometry, FOV changes, and game response"
            ),
        },
        "semantics": {
            "observation_pattern": (
                "paired same-target committed-observation delivery interval and capture age"
            ),
            "manual_segments": (
                "raw physical right-stick input projected into helpful radial/tangential "
                "target-error coordinates and passed through the production input filter once"
            ),
            "manual_signal": "physical_right_stick",
            "controller_sample_interval": (
                "duration between consecutive eligible delivered controller samples; "
                "a measured jitter distribution, not a fixed source clock"
            ),
            "target_samples": (
                "first eligible stable error and body size from each target/ADS epoch"
            ),
            "not_exact_replay": True,
        },
    }
    canonical = json.dumps(
        profile, ensure_ascii=True, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    profile["profile_payload_sha256"] = hashlib.sha256(canonical).hexdigest()
    return profile


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--intake", required=True, type=Path)
    parser.add_argument("--file-id", action="append", dest="file_ids")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--max-observation-samples", type=int, default=256)
    parser.add_argument("--max-manual-samples-per-segment", type=int, default=160)
    parser.add_argument("--max-manual-segments", type=int, default=32)
    parser.add_argument("--max-gap-ms", type=int, default=50)
    args = parser.parse_args(argv)
    try:
        profile = build_runtime_profile(
            args.manifest,
            args.intake,
            file_ids=args.file_ids,
            max_observation_samples=args.max_observation_samples,
            max_manual_samples_per_segment=args.max_manual_samples_per_segment,
            max_manual_segments=args.max_manual_segments,
            max_gap_ms=args.max_gap_ms,
        )
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(
            json.dumps(profile, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    except (OSError, ValueError) as error:
        print(f"runtime profile extraction failed: {error}", file=sys.stderr)
        return 2
    print(
        f"runtime profile {profile['profile_payload_sha256']} written to {args.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
