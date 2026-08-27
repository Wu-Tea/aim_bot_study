import hashlib
import json
from pathlib import Path

import pytest

from tools.extract_sustained_aimlab_runtime_profile import (
    ManualPoint,
    _build_manual_segments,
    build_runtime_profile,
)
from tools.run_sustained_aimlab_runtime_profile import (
    _profile_payload_sha256,
    resolve_camera_response,
    _sha256_file_with_context,
    transform_runtime_observation_pattern,
    validate_and_encode_profile,
    validate_config_relationship,
)


RUNTIME_SHA = "1" * 64
CONFIG_SHA = "2" * 64
ENGINE_SHA = "3" * 64


def _write_jsonl(path: Path, rows: list[dict]) -> tuple[str, int]:
    encoded = "".join(json.dumps(row, sort_keys=True) + "\n" for row in rows)
    payload = encoded.encode("utf-8")
    path.write_bytes(payload)
    return hashlib.sha256(payload).hexdigest(), len(rows)


def _evidence_files(tmp_path: Path, *, status: str = "PASS") -> tuple[Path, Path]:
    telemetry = tmp_path / "telemetry.jsonl"
    rows: list[dict] = [
        {
            "schema_version": 18,
            "type": "session_metadata",
            "session_id": "telemetry-session",
            "build_commit": "a" * 40,
            "config_hash": CONFIG_SHA,
            "engine_hash": ENGINE_SHA,
            "executable_sha256": RUNTIME_SHA,
        }
    ]

    captures_ms = [0, 7, 14, 22]
    for index, capture_ms in enumerate(captures_ms, start=1):
        capture_ns = 1_000_000_000 + capture_ms * 1_000_000
        result_ns = capture_ns + 4_000_000
        consume_ns = capture_ns + 5_000_000
        rows.append(
            {
                "schema_version": 18,
                "type": "committed_capture_observation",
                "vision_sample_quality": "normal",
                "observation": {
                    "source_frame_id": index,
                    "source_observation_id": 100 + index,
                    "persistent_target_id": 9,
                    "captured_at_ns": capture_ns,
                    "result_at_ns": result_ns,
                    "controller_consume_ns": consume_ns,
                    "stable_error": [20.0 - index, -10.0],
                    "stable_body_size": [42.0, 96.0],
                    "eligible_candidate_count": 1,
                    "fresh_observed": True,
                    "strong_observation": True,
                    "stable_coordinates_valid": True,
                    "lifecycle": 1,
                    "mode": 1,
                },
                "provenance": {
                    "build_revision": "a" * 40,
                    "config_sha256": CONFIG_SHA,
                    "engine_sha256": ENGINE_SHA,
                    "executable_sha256": RUNTIME_SHA,
                },
            }
        )

    manual_values = [
        (0.40, 0.00, 0.30, 0.00),
        (0.30, 0.15, 0.20, 0.10),
        (-0.35, 0.00, -0.25, 0.00),
        (0.02, 0.00, 0.00, 0.00),
    ]
    for index, (manual_x, manual_y, filtered_x, filtered_y) in enumerate(manual_values):
        rows.append(
            {
                "schema_version": 18,
                "type": "controller_sample",
                "sample_ns": 2_000_000_000 + index * 4_000_000,
                "physical_connected": True,
                "output_delivered": True,
                "current_observed_target_present": True,
                "selected_track_id": 9,
                "aim_mode": "body_lock",
                "manual_x": manual_x,
                "manual_y": manual_y,
                "filtered_manual_x": filtered_x,
                "filtered_manual_y": filtered_y,
                "operation_class": "target_correction",
                "control_error_x": 20.0,
                "control_error_y": 0.0,
                "bodylock_position_stick_x": 1.0 / 6.0,
                "bodylock_position_stick_y": 0.0,
                "bodylock_lifecycle": "observed",
                "has_target": True,
                "aim_authority": True,
            }
        )

    sha256, parsed_rows = _write_jsonl(telemetry, rows)
    manifest = {
        "schema_version": 1,
        "audit_id": "fixture-audit",
        "question": "fixture",
        "cohorts": [
            {
                "name": "fixture",
                "role": "single",
                "runtime": {
                    "executable_sha256": RUNTIME_SHA,
                    "git_commit": "a" * 40,
                    "config_hash": CONFIG_SHA,
                    "engine_hash": ENGINE_SHA,
                    "telemetry_schema": 18,
                    "logging_mode": "detailed",
                    "hardware_profile": "fixture",
                    "game_refresh_hz": 190,
                },
                "required_record_types": [
                    "session_metadata",
                    "committed_capture_observation",
                    "controller_sample",
                ],
                "files": [
                    {
                        "id": "telemetry-0",
                        "kind": "telemetry_jsonl",
                        "path": str(telemetry),
                        "expected_schema_version": 18,
                    }
                ],
            }
        ],
        "coverage_rules": [],
        "comparison": {"required_equal": [], "allowed_differences": []},
        "limits": {"max_malformed_rows": 0, "min_parsed_rows_per_jsonl": 1},
    }
    intake = {
        "schema_version": 1,
        "audit_id": "fixture-audit",
        "question": "fixture",
        "status": status,
        "artifact_sha256": "4" * 64,
        "issues": [],
        "comparison": {},
        "cohorts": [
            {
                "name": "fixture",
                "files": [
                    {
                        "id": "telemetry-0",
                        "kind": "telemetry_jsonl",
                        "available": True,
                        "sha256": sha256,
                        "size_bytes": telemetry.stat().st_size,
                        "jsonl": {
                            "parsed_rows": parsed_rows,
                            "malformed_rows": 0,
                            "record_types": {
                                "session_metadata": 1,
                                "committed_capture_observation": 4,
                                "controller_sample": 4,
                            },
                            "schema_versions": {"18": parsed_rows},
                        },
                    }
                ],
                "coverage": [],
            }
        ],
    }
    manifest_path = tmp_path / "manifest.json"
    intake_path = tmp_path / "intake.json"
    manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
    intake_path.write_text(json.dumps(intake), encoding="utf-8")
    return manifest_path, intake_path


def test_profile_keeps_paired_runtime_timing_shape_and_physical_relative_manual(tmp_path):
    manifest, intake = _evidence_files(tmp_path)

    profile = build_runtime_profile(
        manifest,
        intake,
        file_ids=["telemetry-0"],
        max_observation_samples=16,
        max_manual_samples_per_segment=16,
    )

    assert profile["schema"] == "sustained_aimlab_runtime_profile_v2"
    assert profile["source"]["audit_status"] == "PASS"
    assert "path" not in json.dumps(profile).lower()
    assert profile["coverage"]["malformed_rows"] == 0
    assert profile["observation_pattern"] == [
        {
            "delivery_interval_ms": 7,
            "capture_age_ms": 5,
            "result_delay_ms": 4,
            "consume_delay_ms": 1,
        },
        {
            "delivery_interval_ms": 7,
            "capture_age_ms": 5,
            "result_delay_ms": 4,
            "consume_delay_ms": 1,
        },
        {
            "delivery_interval_ms": 8,
            "capture_age_ms": 5,
            "result_delay_ms": 4,
            "consume_delay_ms": 1,
        },
    ]
    assert profile["target_samples"][0] == {
        "initial_error_px": [19.0, -10.0],
        "body_size_px": [42.0, 96.0],
    }
    assert profile["manual_segments"][0]["samples"][0] == {
        "duration_ms": 4,
        "radial": 0.4,
        "tangential": 0.0,
    }
    assert profile["manual_segments"][0]["aim_mode"] == "body_lock"
    assert profile["manual_segments"][0]["start_delay_ms"] == 0
    assert profile["semantics"]["manual_signal"] == "physical_right_stick"
    assert profile["input_habits"]["manual_signal"] == "physical_right_stick"
    assert profile["input_habits"]["sample_count"] == 3
    assert profile["input_habits"]["operation_class_counts"] == {
        "target_correction": 3
    }
    assert profile["plant"]["status"] == "unavailable"
    assert profile["controller_response_estimate"]["status"] == "inferred"
    assert profile["controller_response_estimate"]["quantity"] == (
        "controller_used_response_scale_px_per_stick_second"
    )
    assert profile["controller_response_estimate"]["method"] == (
        "bodylock_position_x_term_inverse_v1"
    )
    assert profile["controller_response_estimate"]["distribution"]["count"] == 4
    assert profile["controller_response_estimate"]["distribution"]["p50"] == pytest.approx(
        1500.0
    )
    assert len(profile["profile_payload_sha256"]) == 64


def test_profile_rejects_nonpassing_audit(tmp_path):
    manifest, intake = _evidence_files(tmp_path, status="INSUFFICIENT_EVIDENCE")

    with pytest.raises(ValueError, match="PASS audit"):
        build_runtime_profile(manifest, intake, file_ids=["telemetry-0"])


def test_runtime_runner_revalidates_payload_and_encodes_native_patterns(tmp_path):
    manifest, intake = _evidence_files(tmp_path)
    profile = build_runtime_profile(
        manifest,
        intake,
        file_ids=["telemetry-0"],
        max_observation_samples=16,
        max_manual_samples_per_segment=16,
    )
    profile_path = tmp_path / "runtime-profile.json"
    profile_path.write_text(json.dumps(profile), encoding="utf-8")

    encoded = validate_and_encode_profile(profile_path)

    assert encoded["profile_id"] == "fixture-audit:telemetry-session"
    assert encoded["profile_sha256"] == profile["profile_payload_sha256"]
    assert encoded["audit_sha256"] == "4" * 64
    assert encoded["source_runtime_sha256"] == RUNTIME_SHA
    assert encoded["source_config_sha256"] == CONFIG_SHA
    assert encoded["source_engine_sha256"] == ENGINE_SHA
    assert encoded["manual_signal"] == "physical_right_stick"
    assert encoded["manual_pattern"].startswith("bodylock@")
    assert encoded["observation_pattern"] == "7:5,7:5,8:5"
    assert encoded["target_pattern"] == "19:-10:42:96"
    assert encoded["manual_sample_count"] == 3
    assert encoded["source_controller_interval_p50_ms"] == 4.0
    assert encoded["source_controller_interval_p95_ms"] == 4.0
    assert encoded["source_controller_tick_hz_estimate"] == 250.0
    assert encoded["controller_response_estimate_p50"] == pytest.approx(1500.0)
    assert encoded["controller_response_estimate_p95"] == pytest.approx(1500.0)
    assert encoded["controller_response_estimate_count"] == 4

    response, source, basis = resolve_camera_response(encoded, None, None)
    assert response == pytest.approx(1500.0)
    assert source == "inferred"
    assert basis == "runtime_controller_response_p50"

    with pytest.raises(ValueError, match="also requires --plant-source"):
        resolve_camera_response(encoded, 900.0, None)


def test_existing_v2_profile_without_response_estimate_remains_usable(tmp_path):
    manifest, intake = _evidence_files(tmp_path)
    profile = build_runtime_profile(
        manifest_path=manifest,
        intake_path=intake,
        file_ids=["telemetry-0"],
        max_gap_ms=12.0,
        max_observation_samples=16,
        max_manual_segments=8,
        max_manual_samples_per_segment=16,
    )
    profile.pop("controller_response_estimate")
    profile["profile_payload_sha256"] = _profile_payload_sha256(profile)
    profile_path = tmp_path / "existing-v2-runtime-profile.json"
    profile_path.write_text(json.dumps(profile), encoding="utf-8")

    encoded = validate_and_encode_profile(profile_path)

    assert encoded["controller_response_estimate_p50"] is None
    with pytest.raises(ValueError, match="has no controller response estimate"):
        resolve_camera_response(encoded, None, None)
    assert resolve_camera_response(encoded, 900.0, "assumption") == (
        900.0,
        "assumption",
        "explicit_cli",
    )


def test_manual_library_is_proportional_stratified_and_preserves_rare_habits():
    points: list[ManualPoint] = []
    patterns = (
        [[0.20, 0.20, 0.20]] * 4
        + [[-0.20, -0.20, -0.20]] * 3
        + [[0.20, -0.20, 0.20]] * 2
        + [[0.0, 0.0, 0.0]]
    )
    for target_id, radial_values in enumerate(patterns, start=1):
        base_ns = target_id * 100_000_000
        for index, radial in enumerate(radial_values + [radial_values[-1]]):
            points.append(
                ManualPoint(
                    key=(target_id, "body_lock"),
                    sample_ns=base_ns + index * 4_000_000,
                    raw_radial=radial,
                    raw_tangential=0.0,
                    filtered_radial=radial * 0.8,
                    filtered_tangential=0.0,
                    operation_class="fixture",
                )
            )

    segments, coverage, habits = _build_manual_segments(
        points,
        max_samples_per_segment=16,
        max_gap_ms=20,
        max_segments=5,
    )

    assert len(segments) == 5
    assert coverage["eligible_category_counts"] == {
        "helpful": 4,
        "idle": 1,
        "mixed": 2,
        "opposing": 3,
    }
    assert coverage["selected_category_counts"] == {
        "helpful": 2,
        "idle": 1,
        "mixed": 1,
        "opposing": 1,
    }
    assert habits["sample_count"] == 30
    assert habits["filter_attenuation_ratio"]["p50"] == pytest.approx(0.8)


def test_runtime_runner_encodes_mode_relative_start_delay_as_idle(tmp_path):
    manifest, intake = _evidence_files(tmp_path)
    profile = build_runtime_profile(manifest, intake, file_ids=["telemetry-0"])
    profile["manual_segments"][0]["start_delay_ms"] = 135
    profile["profile_payload_sha256"] = _profile_payload_sha256(profile)
    profile_path = tmp_path / "delayed-runtime-profile.json"
    profile_path.write_text(json.dumps(profile), encoding="utf-8")

    encoded = validate_and_encode_profile(profile_path)

    assert encoded["manual_pattern"].startswith("bodylock@100:0:0;35:0:0;")
    assert encoded["manual_sample_count"] == 5


def test_runtime_runner_keeps_v1_profiles_as_explicit_legacy_input(tmp_path):
    manifest, intake = _evidence_files(tmp_path)
    profile = build_runtime_profile(manifest, intake, file_ids=["telemetry-0"])
    profile["schema"] = "sustained_aimlab_runtime_profile_v1"
    profile.pop("input_habits")
    profile["semantics"].pop("manual_signal")
    profile["profile_payload_sha256"] = _profile_payload_sha256(profile)
    profile_path = tmp_path / "legacy-runtime-profile.json"
    profile_path.write_text(json.dumps(profile), encoding="utf-8")

    encoded = validate_and_encode_profile(profile_path)

    assert encoded["manual_signal"] == "filtered_right_stick_legacy"


def test_runtime_runner_rejects_profile_changed_after_extraction(tmp_path):
    manifest, intake = _evidence_files(tmp_path)
    profile = build_runtime_profile(manifest, intake, file_ids=["telemetry-0"])
    profile["target_samples"][0]["body_size_px"][0] = 123.0
    profile_path = tmp_path / "tampered-runtime-profile.json"
    profile_path.write_text(json.dumps(profile), encoding="utf-8")

    with pytest.raises(ValueError, match="payload SHA-256 mismatch"):
        validate_and_encode_profile(profile_path)


def test_runtime_runner_requires_proof_for_a_matched_config(tmp_path):
    manifest, intake = _evidence_files(tmp_path)
    profile = build_runtime_profile(manifest, intake, file_ids=["telemetry-0"])
    profile_path = tmp_path / "runtime-profile.json"
    profile_path.write_text(json.dumps(profile), encoding="utf-8")
    encoded = validate_and_encode_profile(profile_path)

    with pytest.raises(ValueError, match="source contextual config SHA-256"):
        validate_config_relationship(encoded, "matched", "9" * 64)

    validate_config_relationship(encoded, "matched", CONFIG_SHA)
    validate_config_relationship(encoded, "counterfactual", None)

    config_path = tmp_path / "config.toml"
    config_bytes = b"[vision]\ncapture_fps = 200\n"
    config_path.write_bytes(config_bytes)
    context = "profile=;auto_fire=0;capture_fps=200"
    expected = hashlib.sha256(config_bytes + b"\0" + context.encode()).hexdigest()
    assert _sha256_file_with_context(config_path, context) == expected


def test_runtime_vision_transform_preserves_shape_and_reports_realized_rate(tmp_path):
    manifest, intake = _evidence_files(tmp_path)
    profile = build_runtime_profile(
        manifest,
        intake,
        file_ids=["telemetry-0"],
        max_observation_samples=16,
        max_manual_samples_per_segment=16,
    )
    profile_path = tmp_path / "runtime-profile.json"
    profile_path.write_text(json.dumps(profile), encoding="utf-8")
    encoded = validate_and_encode_profile(profile_path)

    transformed = transform_runtime_observation_pattern(
        encoded,
        vision_hz=100.0,
        vision_age_scale=2.0,
    )

    assert transformed["observation_pattern"] == "10:10,9:10,11:10"
    assert transformed["source_observation_pattern"] == "7:5,7:5,8:5"
    assert transformed["vision_mode"] == "scaled_runtime_profile"
    assert transformed["source_vision_hz"] == pytest.approx(1000.0 / (22.0 / 3.0))
    assert transformed["requested_vision_hz"] == 100.0
    assert transformed["realized_vision_hz"] == pytest.approx(100.0)
    assert transformed["vision_age_scale"] == 2.0


def test_runtime_vision_identity_keeps_extracted_pattern(tmp_path):
    manifest, intake = _evidence_files(tmp_path)
    profile = build_runtime_profile(manifest, intake, file_ids=["telemetry-0"])
    profile_path = tmp_path / "runtime-profile.json"
    profile_path.write_text(json.dumps(profile), encoding="utf-8")
    encoded = validate_and_encode_profile(profile_path)

    transformed = transform_runtime_observation_pattern(
        encoded,
        vision_hz=None,
        vision_age_scale=1.0,
    )

    assert transformed["observation_pattern"] == "7:5,7:5,8:5"
    assert transformed["vision_mode"] == "audited_runtime_profile"
