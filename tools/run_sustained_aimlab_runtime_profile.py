#!/usr/bin/env python3
"""Run the native sustained AimLab benchmark with audited runtime covariates.

The profile supplies observation timing, target geometry/error samples, and
target-relative manual input.  The target trajectory and camera plant remain
synthetic and therefore must be selected and attributed explicitly.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Sequence


PROFILE_SCHEMA = "sustained_aimlab_runtime_profile_v2"
LEGACY_PROFILE_SCHEMA = "sustained_aimlab_runtime_profile_v1"
HEX_DIGITS = frozenset("0123456789abcdef")
EXECUTABLE_NAME = "cod_native_sustained_aimlab_benchmark.exe"
ADS_MANUAL_MODES = frozenset({"ads", "ads_acquire", "acquisition"})
BODYLOCK_MANUAL_MODES = frozenset({"body_lock", "bodylock"})


def _is_sha256(value: object) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 64
        and all(character in HEX_DIGITS for character in value.lower())
    )


def _integer(value: object, label: str, lower: int, upper: int) -> int:
    if (
        isinstance(value, bool)
        or not isinstance(value, int)
        or value < lower
        or value > upper
    ):
        raise ValueError(f"{label} must be an integer in [{lower}, {upper}]")
    return value


def _number(value: object, label: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{label} must be a finite number")
    result = float(value)
    if not math.isfinite(result):
        raise ValueError(f"{label} must be a finite number")
    return result


def _array(value: object, label: str) -> list:
    if not isinstance(value, list) or not value:
        raise ValueError(f"{label} must be a non-empty array")
    return value


def _object(value: object, label: str) -> dict:
    if not isinstance(value, dict):
        raise ValueError(f"{label} must be an object")
    return value


def _format_number(value: float) -> str:
    return format(value, ".12g")


def _profile_payload_sha256(profile: dict) -> str:
    payload = dict(profile)
    payload.pop("profile_payload_sha256", None)
    canonical = json.dumps(
        payload, ensure_ascii=True, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    return hashlib.sha256(canonical).hexdigest()


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _sha256_file_with_context(path: Path, context: str) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    if context:
        digest.update(b"\0")
        digest.update(context.encode("utf-8"))
    return digest.hexdigest()


def validate_config_relationship(
    encoded: dict[str, object],
    relationship: str,
    calculated_provenance_sha256: str | None,
) -> None:
    if relationship not in {"matched", "counterfactual"}:
        raise ValueError("config relationship must be matched or counterfactual")
    if relationship == "matched" and (
        not _is_sha256(calculated_provenance_sha256)
        or calculated_provenance_sha256.lower() != encoded["source_config_sha256"]
    ):
        raise ValueError(
            "matched config relationship requires the source contextual config SHA-256"
        )


def validate_and_encode_profile(profile_path: Path) -> dict[str, object]:
    """Validate one extracted profile and encode its native CLI patterns."""
    try:
        profile = json.loads(Path(profile_path).read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(f"failed to read runtime profile: {error}") from error
    profile = _object(profile, "runtime profile")
    profile_schema = profile.get("schema")
    if profile_schema not in {PROFILE_SCHEMA, LEGACY_PROFILE_SCHEMA}:
        raise ValueError("unsupported runtime profile schema")

    declared_sha = profile.get("profile_payload_sha256")
    if not _is_sha256(declared_sha):
        raise ValueError("runtime profile has no payload SHA-256")
    calculated_sha = _profile_payload_sha256(profile)
    if declared_sha.lower() != calculated_sha:
        raise ValueError("runtime profile payload SHA-256 mismatch")

    source = _object(profile.get("source"), "runtime profile source")
    audit_id = source.get("audit_id")
    session_id = source.get("telemetry_session_id")
    if not isinstance(audit_id, str) or not audit_id:
        raise ValueError("runtime profile has no audit ID")
    if not isinstance(session_id, str) or not session_id:
        raise ValueError("runtime profile has no telemetry session ID")
    if source.get("audit_status") != "PASS":
        raise ValueError("runtime benchmark requires a PASS audit source")
    audit_sha = source.get("audit_artifact_sha256")
    if not _is_sha256(audit_sha):
        raise ValueError("runtime profile has no audit artifact SHA-256")
    for field in ("runtime_sha256", "config_sha256", "engine_sha256"):
        if not _is_sha256(source.get(field)):
            raise ValueError(f"runtime profile source {field} is invalid")
    if source.get("logging_mode") != "detailed":
        raise ValueError("runtime benchmark requires detailed telemetry")
    source_files = _array(source.get("files"), "runtime profile source files")
    for index, raw_file in enumerate(source_files):
        source_file = _object(raw_file, f"runtime profile source file {index}")
        if set(source_file) != {"id", "sha256", "size_bytes"}:
            raise ValueError("runtime profile source files must not contain paths")
        if not isinstance(source_file.get("id"), str) or not source_file["id"]:
            raise ValueError("runtime profile source file has no ID")
        if not _is_sha256(source_file.get("sha256")):
            raise ValueError("runtime profile source file has no SHA-256")
        _integer(
            source_file.get("size_bytes"),
            f"runtime profile source file {index} size",
            1,
            sys.maxsize,
        )

    plant = _object(profile.get("plant"), "runtime profile plant")
    if (
        plant.get("status") != "unavailable"
        or plant.get("camera_response_px_per_stick_second") is not None
        or plant.get("slowdown_curve") is not None
    ):
        raise ValueError(
            "runtime profile must not claim a plant that telemetry cannot identify"
        )
    semantics = _object(profile.get("semantics"), "runtime profile semantics")
    if semantics.get("not_exact_replay") is not True:
        raise ValueError("runtime profile must retain its not-exact-replay boundary")
    manual_signal = "filtered_right_stick_legacy"
    if profile_schema == PROFILE_SCHEMA:
        if semantics.get("manual_signal") != "physical_right_stick":
            raise ValueError("v2 runtime profile must replay the physical right stick")
        input_habits = _object(
            profile.get("input_habits"), "runtime profile input habits"
        )
        if input_habits.get("manual_signal") != "physical_right_stick":
            raise ValueError("runtime input habits use the wrong manual signal")
        active_fraction = _number(
            input_habits.get("active_sample_fraction"),
            "runtime input active sample fraction",
        )
        if active_fraction < 0.0 or active_fraction > 1.0:
            raise ValueError("runtime input active sample fraction is outside [0, 1]")
        _integer(
            input_habits.get("sample_count"),
            "runtime input habit sample count",
            1,
            sys.maxsize,
        )
        manual_signal = "physical_right_stick"

    observations = _array(
        profile.get("observation_pattern"), "runtime observation pattern"
    )
    encoded_observations: list[str] = []
    for index, raw_sample in enumerate(observations):
        sample = _object(raw_sample, f"observation sample {index}")
        delivery = _integer(
            sample.get("delivery_interval_ms"),
            f"observation sample {index} delivery interval",
            1,
            100,
        )
        capture_age = _integer(
            sample.get("capture_age_ms"),
            f"observation sample {index} capture age",
            0,
            100,
        )
        _integer(
            sample.get("result_delay_ms"),
            f"observation sample {index} result delay",
            0,
            100,
        )
        _integer(
            sample.get("consume_delay_ms"),
            f"observation sample {index} consume delay",
            0,
            100,
        )
        encoded_observations.append(f"{delivery}:{capture_age}")

    manual_segments = _array(
        profile.get("manual_segments"), "runtime manual segments"
    )
    encoded_segments: list[str] = []
    manual_sample_count = 0
    for segment_index, raw_segment in enumerate(manual_segments):
        segment = _object(raw_segment, f"manual segment {segment_index}")
        encoded_mode = ""
        start_delay = 0
        if profile_schema == PROFILE_SCHEMA:
            if segment.get("category") not in {"idle", "helpful", "opposing", "mixed"}:
                raise ValueError(f"manual segment {segment_index} has an invalid category")
            aim_mode = segment.get("aim_mode")
            if aim_mode in ADS_MANUAL_MODES:
                encoded_mode = "ads@"
            elif aim_mode in BODYLOCK_MANUAL_MODES:
                encoded_mode = "bodylock@"
            else:
                raise ValueError(f"manual segment {segment_index} has no supported aim mode")
            start_delay = _integer(
                segment.get("start_delay_ms"),
                f"manual segment {segment_index} start delay",
                0,
                1000,
            )
        samples = _array(
            segment.get("samples"), f"manual segment {segment_index} samples"
        )
        encoded_samples: list[str] = []
        remaining_delay = start_delay
        while remaining_delay > 0:
            duration = min(100, remaining_delay)
            encoded_samples.append(f"{duration}:0:0")
            manual_sample_count += 1
            remaining_delay -= duration
        for sample_index, raw_sample in enumerate(samples):
            sample = _object(
                raw_sample,
                f"manual segment {segment_index} sample {sample_index}",
            )
            duration = _integer(
                sample.get("duration_ms"),
                f"manual segment {segment_index} sample {sample_index} duration",
                1,
                100,
            )
            radial = _number(
                sample.get("radial"),
                f"manual segment {segment_index} sample {sample_index} radial",
            )
            tangential = _number(
                sample.get("tangential"),
                f"manual segment {segment_index} sample {sample_index} tangential",
            )
            if math.hypot(radial, tangential) > 1.01:
                raise ValueError("runtime manual sample magnitude exceeds 1.01")
            encoded_samples.append(
                f"{duration}:{_format_number(radial)}:{_format_number(tangential)}"
            )
            manual_sample_count += 1
        encoded_segments.append(encoded_mode + ";".join(encoded_samples))

    targets = _array(profile.get("target_samples"), "runtime target samples")
    encoded_targets: list[str] = []
    for index, raw_sample in enumerate(targets):
        sample = _object(raw_sample, f"target sample {index}")
        error = sample.get("initial_error_px")
        size = sample.get("body_size_px")
        if not isinstance(error, list) or len(error) != 2:
            raise ValueError(f"target sample {index} error must have two values")
        if not isinstance(size, list) or len(size) != 2:
            raise ValueError(f"target sample {index} size must have two values")
        error_x = _number(error[0], f"target sample {index} error x")
        error_y = _number(error[1], f"target sample {index} error y")
        width = _number(size[0], f"target sample {index} width")
        height = _number(size[1], f"target sample {index} height")
        # Stable detector geometry can extend slightly beyond the viewport;
        # retain it while bounding corrupted or wrong-coordinate samples.
        if abs(error_x) > 1280.0 or abs(error_y) > 1024.0:
            raise ValueError(f"target sample {index} error is outside the simulator")
        if width <= 0.0 or width > 1280.0 or height <= 0.0 or height > 1024.0:
            raise ValueError(f"target sample {index} size is outside the simulator")
        encoded_targets.append(
            ":".join(
                _format_number(value)
                for value in (error_x, error_y, width, height)
            )
        )

    coverage = _object(profile.get("coverage"), "runtime profile coverage")
    if coverage.get("malformed_rows") != 0:
        raise ValueError("runtime profile coverage must retain zero malformed rows")
    _integer(coverage.get("parsed_rows"), "runtime profile parsed rows", 1, sys.maxsize)
    observation_coverage = _object(
        coverage.get("observation"), "runtime observation coverage"
    )
    manual_coverage = _object(coverage.get("manual"), "runtime manual coverage")
    controller_interval = _object(
        manual_coverage.get("duration_ms"),
        "runtime controller sample interval coverage",
    )
    source_controller_interval_p50_ms = _number(
        controller_interval.get("p50"),
        "runtime controller sample interval p50",
    )
    source_controller_interval_p95_ms = _number(
        controller_interval.get("p95"),
        "runtime controller sample interval p95",
    )
    if (
        source_controller_interval_p50_ms <= 0.0
        or source_controller_interval_p95_ms < source_controller_interval_p50_ms
    ):
        raise ValueError("runtime controller sample interval coverage is invalid")
    expected_counts = (
        (observation_coverage.get("selected_pairs"), len(observations), "observation"),
        (
            observation_coverage.get("selected_target_samples"),
            len(targets),
            "target",
        ),
        (manual_coverage.get("selected_segments"), len(manual_segments), "manual"),
    )
    for declared, actual, label in expected_counts:
        if declared != actual:
            raise ValueError(f"runtime profile {label} coverage count mismatch")

    return {
        "profile_id": f"{audit_id}:{session_id}",
        "profile_sha256": declared_sha.lower(),
        "audit_sha256": audit_sha.lower(),
        "source_runtime_sha256": source["runtime_sha256"].lower(),
        "source_config_sha256": source["config_sha256"].lower(),
        "source_engine_sha256": source["engine_sha256"].lower(),
        "manual_signal": manual_signal,
        "observation_pattern": ",".join(encoded_observations),
        "manual_pattern": "|".join(encoded_segments),
        "target_pattern": ";".join(encoded_targets),
        "observation_count": len(observations),
        "manual_segment_count": len(manual_segments),
        "manual_sample_count": manual_sample_count,
        "target_count": len(targets),
        "source_controller_interval_p50_ms": (
            source_controller_interval_p50_ms
        ),
        "source_controller_interval_p95_ms": (
            source_controller_interval_p95_ms
        ),
        "source_controller_tick_hz_estimate": (
            1000.0 / source_controller_interval_p50_ms
        ),
    }


def transform_runtime_observation_pattern(
    encoded: dict[str, object],
    *,
    vision_hz: float | None,
    vision_age_scale: float,
) -> dict[str, object]:
    """Scale an audited timing shape without claiming it is an exact replay.

    Delivery intervals use cumulative rounding so the requested average rate is
    retained without flattening the extracted short/long cadence pattern.
    Capture ages are scaled independently to model a different Vision pipeline
    latency while retaining the logged distribution shape.
    """
    if not math.isfinite(vision_age_scale) or vision_age_scale <= 0.0:
        raise ValueError("vision-age-scale must be finite and positive")
    raw_pattern = encoded.get("observation_pattern")
    if not isinstance(raw_pattern, str) or not raw_pattern:
        raise ValueError("encoded runtime profile has no observation pattern")

    samples: list[tuple[int, int]] = []
    try:
        for token in raw_pattern.split(","):
            delivery_text, age_text = token.split(":", 1)
            samples.append((int(delivery_text), int(age_text)))
    except (TypeError, ValueError) as error:
        raise ValueError("encoded runtime observation pattern is invalid") from error
    if not samples or any(delivery <= 0 or age < 0 for delivery, age in samples):
        raise ValueError("encoded runtime observation pattern is invalid")

    source_mean_interval_ms = sum(delivery for delivery, _ in samples) / len(samples)
    source_vision_hz = 1000.0 / source_mean_interval_ms
    if vision_hz is None:
        requested_vision_hz = source_vision_hz
        cadence_scale = 1.0
    else:
        if not math.isfinite(vision_hz) or vision_hz <= 0.0 or vision_hz > 1000.0:
            raise ValueError("vision-hz must be finite and in (0, 1000]")
        requested_vision_hz = float(vision_hz)
        cadence_scale = (1000.0 / requested_vision_hz) / source_mean_interval_ms

    transformed_samples: list[tuple[int, int]] = []
    desired_total = 0.0
    emitted_total = 0
    for delivery, capture_age in samples:
        desired_total += delivery * cadence_scale
        rounded_total = max(emitted_total + 1, math.floor(desired_total + 0.5))
        transformed_delivery = rounded_total - emitted_total
        emitted_total = rounded_total
        transformed_age = math.floor(capture_age * vision_age_scale + 0.5)
        if transformed_delivery > 100:
            raise ValueError(
                "scaled Vision cadence contains an interval above 100 ms; "
                "choose a higher Vision rate for this runtime shape"
            )
        if transformed_age > 100:
            raise ValueError(
                "scaled Vision capture age exceeds the 100 ms simulator bound"
            )
        transformed_samples.append((transformed_delivery, transformed_age))

    realized_mean_interval_ms = (
        sum(delivery for delivery, _ in transformed_samples) /
        len(transformed_samples)
    )
    transformed = dict(encoded)
    transformed.update(
        {
            "source_observation_pattern": raw_pattern,
            "observation_pattern": ",".join(
                f"{delivery}:{capture_age}"
                for delivery, capture_age in transformed_samples
            ),
            "vision_mode": (
                "audited_runtime_profile"
                if vision_hz is None and math.isclose(
                    vision_age_scale, 1.0, rel_tol=0.0, abs_tol=1e-12
                )
                else "scaled_runtime_profile"
            ),
            "source_vision_hz": source_vision_hz,
            "requested_vision_hz": requested_vision_hz,
            "realized_vision_hz": 1000.0 / realized_mean_interval_ms,
            "vision_age_scale": vision_age_scale,
        }
    )
    return transformed


def _find_cmake(build_dir: Path) -> Path:
    cache = build_dir / "CMakeCache.txt"
    if cache.is_file():
        for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
            prefix = "CMAKE_COMMAND:INTERNAL="
            if line.startswith(prefix):
                candidate = Path(line[len(prefix) :])
                if candidate.is_file():
                    return candidate
    discovered = shutil.which("cmake")
    if discovered:
        return Path(discovered)
    raise ValueError(f"CMake was not found for configured build directory {build_dir}")


def _git_identity(root: Path) -> tuple[str, bool]:
    revision = subprocess.run(
        ["git", "-C", str(root), "rev-parse", "HEAD"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    status = subprocess.run(
        ["git", "-C", str(root), "status", "--porcelain"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    return revision, bool(status.strip())


def _verify_report(
    path: Path,
    encoded: dict[str, object],
    plant_source: str,
    config_relationship: str,
    config_file_sha256: str,
    executable_sha256: str,
    base_camera_response: float,
    sensitivity_multiplier: float,
    slowdown_edge: float,
    slowdown_center: float,
    controller_tick_hz: int,
    target_motion_preset: str,
    pov_motion_preset: str,
) -> None:
    try:
        report = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(f"failed to read benchmark report: {error}") from error
    runtime = _object(report.get("runtime_profile"), "benchmark runtime profile")
    expected = {
        "id": encoded["profile_id"],
        "profile_payload_sha256": encoded["profile_sha256"],
        "audit_artifact_sha256": encoded["audit_sha256"],
        "source_runtime_sha256": encoded["source_runtime_sha256"],
        "source_config_sha256": encoded["source_config_sha256"],
        "source_engine_sha256": encoded["source_engine_sha256"],
        "config_relationship": config_relationship,
        "benchmark_config_file_sha256": config_file_sha256,
        "benchmark_executable_sha256": executable_sha256,
        "observation_samples": encoded["observation_count"],
        "manual_segments": encoded["manual_segment_count"],
        "manual_samples": encoded["manual_sample_count"],
        "target_samples": encoded["target_count"],
        "not_exact_replay": True,
    }
    for key, value in expected.items():
        if runtime.get(key) != value:
            raise ValueError(f"benchmark report lost runtime profile field {key}")
    simulator = _object(report.get("simulator"), "benchmark simulator")
    if (
        simulator.get("plant_tick_ms") != 1
        or simulator.get("controller_tick_hz") != controller_tick_hz
        or simulator.get("controller_tick_ms") != 1000 // controller_tick_hz
    ):
        raise ValueError("benchmark report changed controller/plant clocks")
    controller = _object(
        simulator.get("controller"), "benchmark simulator controller clock"
    )
    expected_controller = {
        "mode": "fixed_requested",
        "output_hold": "zero_order_hold",
        "source_interval_p50_ms": encoded[
            "source_controller_interval_p50_ms"
        ],
        "source_interval_p95_ms": encoded[
            "source_controller_interval_p95_ms"
        ],
        "source_hz_estimate": encoded["source_controller_tick_hz_estimate"],
        "requested_hz": float(controller_tick_hz),
        "tick_ms": float(1000 // controller_tick_hz),
    }
    for key, expected_value in expected_controller.items():
        actual_value = controller.get(key)
        if isinstance(expected_value, str):
            if actual_value != expected_value:
                raise ValueError(f"benchmark report changed controller field {key}")
        elif not math.isclose(
            _number(actual_value, f"benchmark controller {key}"),
            float(expected_value),
            rel_tol=1e-10,
            abs_tol=1e-10,
        ):
            raise ValueError(f"benchmark report changed controller field {key}")
    proposal = _object(
        simulator.get("ai_proposal"), "benchmark simulator AI proposal clock"
    )
    expected_proposal = {
        "scope": "controller_pipeline",
        "mode": "lockstep",
        "requested_hz": controller_tick_hz,
        "update_stage": "assist_solver_and_dynamics",
        "dynamics_hz": controller_tick_hz,
        "hold": "none",
        "target_plan_and_safety_hz": controller_tick_hz,
    }
    for key, expected_value in expected_proposal.items():
        if proposal.get(key) != expected_value:
            raise ValueError(f"benchmark report changed AI proposal field {key}")
    for field in (
        "manual_sample_hz",
        "final_arbitration_hz",
        "recoil_hz",
        "output_hz",
    ):
        if simulator.get(field) != controller_tick_hz:
            raise ValueError(f"benchmark report changed split-clock field {field}")
    vision = _object(simulator.get("vision"), "benchmark simulator Vision")
    if not math.isclose(
        _number(
            simulator.get("vision_hz_requested"),
            "benchmark requested Vision rate",
        ),
        float(encoded["requested_vision_hz"]),
        rel_tol=1e-10,
        abs_tol=1e-10,
    ):
        raise ValueError("benchmark report changed requested Vision rate")
    expected_vision = {
        "mode": encoded["vision_mode"],
        "controller_delivery": "latest_only_latched",
        "source_hz": encoded["source_vision_hz"],
        "requested_hz": encoded["requested_vision_hz"],
        "realized_hz": encoded["realized_vision_hz"],
        "capture_age_scale": encoded["vision_age_scale"],
    }
    for key, expected_value in expected_vision.items():
        actual_value = vision.get(key)
        if isinstance(expected_value, str):
            if actual_value != expected_value:
                raise ValueError(f"benchmark report changed Vision field {key}")
        elif not math.isclose(
            _number(actual_value, f"benchmark Vision {key}"),
            float(expected_value),
            rel_tol=1e-10,
            abs_tol=1e-10,
        ):
            raise ValueError(f"benchmark report changed Vision field {key}")
    plant = _object(simulator.get("plant"), "benchmark simulator plant")
    if plant.get("source") != plant_source or plant.get("telemetry_status") != "unavailable":
        raise ValueError("benchmark report lost explicit synthetic plant attribution")
    expected_plant = {
        "base_camera_response_px_per_stick_second": base_camera_response,
        "sensitivity_multiplier": sensitivity_multiplier,
        "camera_response_px_per_stick_second": (
            base_camera_response * sensitivity_multiplier
        ),
        "slowdown_edge_multiplier": slowdown_edge,
        "slowdown_center_multiplier": slowdown_center,
    }
    for key, expected_value in expected_plant.items():
        actual_value = _number(plant.get(key), f"benchmark plant {key}")
        if not math.isclose(
            actual_value,
            expected_value,
            rel_tol=1e-10,
            abs_tol=1e-10,
        ):
            raise ValueError(f"benchmark report changed plant field {key}")
    if (
        simulator.get("target_trajectory_source") != "algorithmic_preset"
        or simulator.get("target_motion_preset") != target_motion_preset
        or simulator.get("target_motion") != (
            "moving" if target_motion_preset == "seeded-legacy" else "stationary"
        )
        or simulator.get("pov_motion_preset") != pov_motion_preset
    ):
        raise ValueError("benchmark report lost algorithmic motion preset attribution")
    if (
        simulator.get("observation_source") != encoded["vision_mode"]
        or simulator.get("manual_source") != "audited_runtime_profile"
    ):
        raise ValueError("benchmark report lost runtime covariate attribution")
    runs = _array(report.get("runs"), "benchmark runs")
    expected_pov_modes = {
        "off": ("off", "off"),
        "seeded-strafe": ("full-reversal", "off"),
        "seeded-vertical": ("off", "random"),
        "seeded-combined": ("full-reversal", "random"),
    }
    expected_left_strafe, expected_vertical_motion = expected_pov_modes[
        pov_motion_preset
    ]
    duration_ms = _integer(
        simulator.get("duration_ms"), "benchmark duration", 1, sys.maxsize
    )
    expected_controller_updates = (
        duration_ms + (1000 // controller_tick_hz) - 1
    ) // (1000 // controller_tick_hz)
    if any(
        not isinstance(run, dict)
        or run.get("profile") != "runtime"
        or run.get("left_strafe") != expected_left_strafe
        or run.get("vertical_motion") != expected_vertical_motion
        or run.get("controller_updates") != expected_controller_updates
        for run in runs
    ):
        raise ValueError("benchmark report contains a mismatched runtime run")


def main(argv: Sequence[str] | None = None) -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", required=True, type=Path)
    parser.add_argument(
        "--config", type=Path, default=root / "config.native.example.toml"
    )
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--duration-ms", type=int, default=60_000)
    parser.add_argument("--seed", action="append", type=int)
    parser.add_argument("--cohort", choices=("ads", "bodylock", "both"), default="both")
    target_group = parser.add_mutually_exclusive_group(required=True)
    target_group.add_argument(
        "--target-motion", choices=("moving", "stationary")
    )
    target_group.add_argument(
        "--target-motion-preset", choices=("stationary", "seeded-legacy")
    )
    parser.add_argument(
        "--pov-motion-preset",
        choices=("off", "seeded-strafe", "seeded-vertical", "seeded-combined"),
        default="off",
    )
    parser.add_argument(
        "--controller-tick-hz",
        type=int,
        required=True,
        help="Exact integer-ms controller cadence (for example 1000/500/250/200 Hz).",
    )
    parser.add_argument(
        "--vision-hz",
        type=float,
        help="Scale the extracted delivery-interval shape to this mean rate.",
    )
    parser.add_argument(
        "--vision-age-scale",
        type=float,
        default=1.0,
        help="Multiply each extracted capture-to-controller age.",
    )
    parser.add_argument(
        "--camera-response-px-per-stick-second", required=True, type=float
    )
    parser.add_argument(
        "--sensitivity-multiplier",
        type=float,
        default=1.0,
        help="Scale only the synthetic camera plant; logged manual input is unchanged.",
    )
    parser.add_argument("--slowdown-edge", required=True, type=float)
    parser.add_argument("--slowdown-center", required=True, type=float)
    parser.add_argument(
        "--plant-source",
        required=True,
        choices=("measured", "inferred", "assumption"),
    )
    parser.add_argument(
        "--config-relationship",
        required=True,
        choices=("matched", "counterfactual"),
        help="Whether the current controller config matches the logged source config.",
    )
    parser.add_argument(
        "--config-provenance-context",
        help=(
            "Exact context appended by runtime provenance hashing. Required for "
            "'matched'; for example profile=;auto_fire=0;capture_fps=200."
        ),
    )
    parser.add_argument(
        "--build-dir", type=Path, default=root / "native" / "vision_native" / "build"
    )
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args(argv)

    try:
        profile_path = args.profile.resolve(strict=True)
        config_path = args.config.resolve(strict=True)
        build_dir = args.build_dir.resolve(strict=True)
        output_path = args.output.resolve()
        if output_path.exists():
            raise ValueError(f"refusing to overwrite benchmark report: {output_path}")
        if args.duration_ms <= 0:
            raise ValueError("duration-ms must be positive")
        if (
            args.controller_tick_hz <= 0
            or args.controller_tick_hz > 1000
            or 1000 % args.controller_tick_hz != 0
        ):
            raise ValueError(
                "controller-tick-hz must be a positive divisor of 1000"
            )
        for label, value in (
            ("camera response", args.camera_response_px_per_stick_second),
            ("sensitivity multiplier", args.sensitivity_multiplier),
            ("Vision age scale", args.vision_age_scale),
            ("slowdown edge", args.slowdown_edge),
            ("slowdown center", args.slowdown_center),
        ):
            if not math.isfinite(value) or value <= 0.0:
                raise ValueError(f"{label} must be finite and positive")

        encoded = transform_runtime_observation_pattern(
            validate_and_encode_profile(profile_path),
            vision_hz=args.vision_hz,
            vision_age_scale=args.vision_age_scale,
        )
        target_motion_preset = args.target_motion_preset or (
            "seeded-legacy" if args.target_motion == "moving" else "stationary"
        )
        calculated_provenance_sha256 = None
        if args.config_relationship == "matched":
            if args.config_provenance_context is None:
                raise ValueError(
                    "matched config relationship requires config-provenance-context"
                )
            calculated_provenance_sha256 = _sha256_file_with_context(
                config_path, args.config_provenance_context
            )
        validate_config_relationship(
            encoded, args.config_relationship, calculated_provenance_sha256
        )
        if not args.skip_build:
            cmake = _find_cmake(build_dir)
            subprocess.run(
                [
                    str(cmake),
                    "--build",
                    str(build_dir),
                    "--config",
                    "Release",
                    "--target",
                    "cod_native_sustained_aimlab_benchmark",
                ],
                check=True,
            )
        executable = build_dir / "Release" / EXECUTABLE_NAME
        if not executable.is_file():
            raise ValueError(f"benchmark executable is missing: {executable}")

        revision, dirty = _git_identity(root)
        config_file_sha256 = _sha256_file(config_path)
        executable_sha256 = _sha256_file(executable)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        seeds = args.seed or [2026082501, 2026082502, 2026082503]
        command = [
            str(executable),
            "--config",
            str(config_path),
            "--output",
            str(output_path),
            "--revision",
            revision,
            "--duration-ms",
            str(args.duration_ms),
            "--profile",
            "runtime",
            "--cohort",
            args.cohort,
            "--target-motion-preset",
            target_motion_preset,
            "--pov-motion-preset",
            args.pov_motion_preset,
            "--controller-tick-hz",
            str(args.controller_tick_hz),
            "--runtime-profile-id",
            str(encoded["profile_id"]),
            "--runtime-profile-sha256",
            str(encoded["profile_sha256"]),
            "--runtime-audit-sha256",
            str(encoded["audit_sha256"]),
            "--runtime-source-runtime-sha256",
            str(encoded["source_runtime_sha256"]),
            "--runtime-source-config-sha256",
            str(encoded["source_config_sha256"]),
            "--runtime-source-engine-sha256",
            str(encoded["source_engine_sha256"]),
            "--config-relationship",
            args.config_relationship,
            "--benchmark-config-file-sha256",
            config_file_sha256,
            "--benchmark-executable-sha256",
            executable_sha256,
            "--runtime-vision-mode",
            str(encoded["vision_mode"]),
            "--runtime-source-vision-hz",
            _format_number(float(encoded["source_vision_hz"])),
            "--runtime-requested-vision-hz",
            _format_number(float(encoded["requested_vision_hz"])),
            "--runtime-realized-vision-hz",
            _format_number(float(encoded["realized_vision_hz"])),
            "--runtime-vision-age-scale",
            _format_number(float(encoded["vision_age_scale"])),
            "--runtime-source-controller-interval-p50-ms",
            _format_number(
                float(encoded["source_controller_interval_p50_ms"])
            ),
            "--runtime-source-controller-interval-p95-ms",
            _format_number(
                float(encoded["source_controller_interval_p95_ms"])
            ),
            "--observation-pattern-ms",
            str(encoded["observation_pattern"]),
            "--manual-pattern",
            str(encoded["manual_pattern"]),
            "--target-sample-pattern",
            str(encoded["target_pattern"]),
            "--camera-response-px-per-stick-second",
            _format_number(args.camera_response_px_per_stick_second),
            "--sensitivity-multiplier",
            _format_number(args.sensitivity_multiplier),
            "--slowdown-edge",
            _format_number(args.slowdown_edge),
            "--slowdown-center",
            _format_number(args.slowdown_center),
            "--plant-source",
            args.plant_source,
        ]
        for seed in seeds:
            if seed < 0 or seed > 0xFFFFFFFF:
                raise ValueError("seed must fit an unsigned 32-bit integer")
            command.extend(("--seed", str(seed)))
        if dirty:
            command.append("--dirty")
        subprocess.run(command, check=True)
        if _sha256_file(config_path) != config_file_sha256:
            raise ValueError("benchmark config changed while the run was in progress")
        if _sha256_file(executable) != executable_sha256:
            raise ValueError("benchmark executable changed while the run was in progress")
        _verify_report(
            output_path,
            encoded,
            args.plant_source,
            args.config_relationship,
            config_file_sha256,
            executable_sha256,
            args.camera_response_px_per_stick_second,
            args.sensitivity_multiplier,
            args.slowdown_edge,
            args.slowdown_center,
            args.controller_tick_hz,
            target_motion_preset,
            args.pov_motion_preset,
        )
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"runtime benchmark failed: {error}", file=sys.stderr)
        return 2

    print(
        "runtime benchmark written to "
        f"{output_path} (profile={encoded['profile_sha256']}, "
        f"config_relationship={args.config_relationship}, "
        f"plant_source={args.plant_source}, "
        f"vision={encoded['realized_vision_hz']:.3f}Hz, "
        f"controller={args.controller_tick_hz}Hz, "
        "ai_proposal=lockstep, "
        f"sensitivity={args.sensitivity_multiplier:g}, "
        f"target_preset={target_motion_preset}, "
        f"pov_preset={args.pov_motion_preset})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
