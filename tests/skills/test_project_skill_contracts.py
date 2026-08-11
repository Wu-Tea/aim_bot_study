from __future__ import annotations

import hashlib
import importlib.util
import json
import sys
from pathlib import Path
from types import ModuleType
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]


def load_module(name: str, relative_path: str) -> ModuleType:
    path = REPO_ROOT / relative_path
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    previous = sys.dont_write_bytecode
    sys.dont_write_bytecode = True
    try:
        spec.loader.exec_module(module)
    finally:
        sys.dont_write_bytecode = previous
    return module


def write_json(path: Path, value: Any) -> None:
    path.write_text(json.dumps(value, sort_keys=True) + "\n", encoding="utf-8")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def audit_manifest(tmp_path: Path, telemetry_runtime_sha: str) -> Path:
    runtime_sha = "a" * 64
    config_hash = "b" * 64
    engine_hash = "c" * 64
    commit = "d" * 40
    write_json(
        tmp_path / "session.json",
        {
            "schema_version": 2,
            "session_id": "test-session",
            "state": "closed",
            "executable_sha256": runtime_sha,
            "config_hash": config_hash,
            "engine_hash": engine_hash,
            "git_commit": commit,
        },
    )
    records = [
        {
            "schema_version": 15,
            "type": "session_metadata",
            "executable_sha256": telemetry_runtime_sha,
            "config_hash": config_hash,
            "engine_hash": engine_hash,
            "build_commit": commit,
        },
        {
            "schema_version": 15,
            "type": "controller_sample",
            "tick_id": 11,
            "sample_seq": 7,
        },
    ]
    (tmp_path / "telemetry.jsonl").write_text(
        "".join(json.dumps(record, sort_keys=True) + "\n" for record in records),
        encoding="utf-8",
    )
    manifest = {
        "schema_version": 1,
        "audit_id": "test-audit",
        "question": "Does the declared telemetry preserve its runtime identity?",
        "cohorts": [
            {
                "name": "observed",
                "role": "single",
                "runtime": {
                    "executable_sha256": runtime_sha,
                    "git_commit": commit,
                    "config_hash": config_hash,
                    "engine_hash": engine_hash,
                    "telemetry_schema": 15,
                    "logging_mode": "detailed",
                    "hardware_profile": "test-host",
                    "game_refresh_hz": 180,
                },
                "required_record_types": ["session_metadata", "controller_sample"],
                "files": [
                    {
                        "id": "session-manifest",
                        "kind": "session_manifest",
                        "path": "session.json",
                        "expected_schema_version": 2,
                    },
                    {
                        "id": "telemetry",
                        "kind": "telemetry_jsonl",
                        "path": "telemetry.jsonl",
                        "expected_schema_version": 15,
                    },
                ],
            }
        ],
        "coverage_rules": [
            {
                "name": "controller-tick",
                "record_types": ["controller_sample"],
                "field": "tick_id",
                "min_ratio": 1.0,
                "min_records": 1,
                "require_nonzero": True,
            }
        ],
        "comparison": {"required_equal": [], "allowed_differences": []},
        "limits": {"max_malformed_rows": 0, "min_parsed_rows_per_jsonl": 1},
    }
    path = tmp_path / "audit-manifest.json"
    write_json(path, manifest)
    return path


def regression_manifest(tmp_path: Path, observed: float) -> Path:
    fixture = tmp_path / "fixture.cpp"
    fixture.write_text("int main() { return 1; }\n", encoding="utf-8")
    red_report = tmp_path / "known-bad.json"
    write_json(red_report, {"speed_loss_pct": observed, "passed": False})
    runtime_sha = "a" * 64
    manifest = {
        "schema_version": 1,
        "incident_id": "test-incident",
        "title": "Pure-AI same-direction speed loss",
        "symptom": "Output speed drops and rebounds across a freshness gap.",
        "evidence": {
            "facts": [
                {
                    "id": "F1",
                    "claim": "Right-stick manual input was zero.",
                    "source": {
                        "kind": "user_confirmed",
                        "artifact_id": "source-clip",
                        "locator": "2026-08-07 statement",
                    },
                }
            ],
            "inferences": [],
            "unknowns": [],
        },
        "known_bad": {
            "runtime_sha256": runtime_sha,
            "source_commit": "d" * 40,
            "audit_artifact_id": "source-audit",
            "audit_artifact_sha256": "b" * 64,
        },
        "fixture": {
            "kind": "native_benchmark",
            "source_files": ["fixture.cpp"],
            "build_target": "cod_native_test_incident",
            "run_command": ["fixture.exe", "--output", "known-bad.json"],
            "trigger_assertions": [
                "one fresh clamp is followed by a non-fresh tick within 25 ms"
            ],
            "covariates": {
                "freshness": "fresh -> non-fresh -> fresh",
                "target_generation": "constant",
                "target_count": 1,
                "right_stick_manual": "zero",
                "left_stick_manual": "zero",
                "recoil_firing": "disabled",
                "controller_mode": "ADS",
                "refresh_rate_hz": 180,
                "logging_mode": "disabled",
            },
        },
        "oracles": [
            {
                "id": "O1",
                "metric": "same_direction_speed_loss_pct",
                "operator": "<=",
                "threshold": 20.0,
                "unit": "percent",
                "evidence_fact_ids": ["F1"],
            }
        ],
        "counterfactuals": [
            {
                "id": "C1",
                "change": "remove the non-fresh gap",
                "expected": "the rebound trigger count becomes zero",
            }
        ],
        "red_proof": {
            "runtime_sha256": runtime_sha,
            "command": ["fixture.exe", "--output", "known-bad.json"],
            "exit_code": 1,
            "report": {
                "id": "known-bad-report",
                "path": "known-bad.json",
                "sha256": sha256(red_report),
            },
            "failed_oracles": [{"oracle_id": "O1", "observed": observed}],
        },
        "green_proof": None,
        "verification": None,
    }
    path = tmp_path / "regression-manifest.json"
    write_json(path, manifest)
    return path


def test_native_telemetry_audit_passes_and_is_deterministic(tmp_path: Path) -> None:
    module = load_module(
        "native_telemetry_audit_contract",
        ".agents/skills/native-telemetry-audit/scripts/audit_evidence.py",
    )
    manifest = audit_manifest(tmp_path, "a" * 64)

    first = module.run_check(manifest)
    second = module.run_check(manifest)

    assert first["status"] == "PASS"
    assert first["issues"] == []
    assert first["artifact_sha256"] == second["artifact_sha256"]
    assert first["cohorts"][0]["coverage"][0]["ratio"] == 1.0


def test_native_telemetry_audit_blocks_mixed_runtime_identity(tmp_path: Path) -> None:
    module = load_module(
        "native_telemetry_audit_mismatch",
        ".agents/skills/native-telemetry-audit/scripts/audit_evidence.py",
    )
    manifest = audit_manifest(tmp_path, "e" * 64)

    report = module.run_check(manifest)

    assert report["status"] == "BLOCKED"
    assert any(item["code"] == "observed_identity_mixed" for item in report["issues"])


def test_incident_to_regression_accepts_truthful_red_proof(tmp_path: Path) -> None:
    module = load_module(
        "incident_to_regression_contract",
        ".agents/skills/incident-to-regression/scripts/regression_contract.py",
    )
    manifest = regression_manifest(tmp_path, observed=41.0)

    first = module.run_check(manifest, "red")
    second = module.run_check(manifest, "red")

    assert first["status"] == "PASS"
    assert first["issues"] == []
    assert first["checks"]["red"]["failed_oracles"] == ["O1"]
    assert first["artifact_sha256"] == second["artifact_sha256"]


def test_incident_to_regression_rejects_false_failed_oracle(tmp_path: Path) -> None:
    module = load_module(
        "incident_to_regression_false_failure",
        ".agents/skills/incident-to-regression/scripts/regression_contract.py",
    )
    manifest = regression_manifest(tmp_path, observed=10.0)

    report = module.run_check(manifest, "red")

    assert report["status"] == "BLOCKED"
    assert any(
        item["code"] == "red_oracle_outcome_contradiction"
        for item in report["issues"]
    )
