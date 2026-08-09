#!/usr/bin/env python3
"""Initialize and validate deterministic incident-to-regression contracts."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import sys
from pathlib import Path
from typing import Any, Iterable


PASS = "PASS"
INCOMPLETE = "INCOMPLETE"
BLOCKED = "BLOCKED"
EXIT_CODES = {PASS: 0, INCOMPLETE: 2, BLOCKED: 3}
SEVERITY_ORDER = {"blocked": 0, "incomplete": 1}
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
COMMIT_RE = re.compile(r"^[0-9a-f]{7,64}$")
ID_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_-]*$")
FIXTURE_KINDS = {"unit", "native_benchmark", "replay", "integration", "live_ab"}
SOURCE_KINDS = {
    "user_confirmed",
    "telemetry",
    "video",
    "code",
    "benchmark",
    "replay",
    "hardware",
    "document",
}
OPERATORS = {"<", "<=", ">", ">=", "==", "!="}
REQUIRED_COVARIATES = (
    "freshness",
    "target_generation",
    "target_count",
    "right_stick_manual",
    "left_stick_manual",
    "recoil_firing",
    "controller_mode",
    "refresh_rate_hz",
    "logging_mode",
)


def canonical_bytes(value: Any) -> bytes:
    return json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")


def payload_hash(value: Any) -> str:
    return hashlib.sha256(canonical_bytes(value)).hexdigest()


def hash_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def issue(
    issues: list[dict[str, Any]], severity: str, code: str, detail: str
) -> None:
    issues.append({"severity": severity, "code": code, "detail": detail})


def issue_sort_key(item: dict[str, Any]) -> tuple[Any, ...]:
    return (
        SEVERITY_ORDER.get(str(item.get("severity")), 99),
        str(item.get("code", "")),
        str(item.get("detail", "")),
    )


def status_from_issues(issues: Iterable[dict[str, Any]]) -> str:
    severities = {str(item.get("severity")) for item in issues}
    if "blocked" in severities:
        return BLOCKED
    if "incomplete" in severities:
        return INCOMPLETE
    return PASS


def normalize(value: Any) -> str | None:
    if not isinstance(value, str):
        return None
    normalized = value.strip().lower()
    return normalized or None


def is_placeholder(value: Any) -> bool:
    return value is None or (
        isinstance(value, str)
        and (
            not value.strip()
            or value.strip().lower() in {"unknown", "unavailable", "n/a"}
            or value.strip().upper().startswith("REPLACE")
        )
    )


def valid_command(value: Any) -> bool:
    return (
        isinstance(value, list)
        and bool(value)
        and all(isinstance(part, str) and bool(part.strip()) for part in value)
    )


def valid_id(value: Any) -> bool:
    return isinstance(value, str) and ID_RE.fullmatch(value) is not None


def scalar(value: Any) -> bool:
    return isinstance(value, (str, int, float, bool)) and not (
        isinstance(value, float) and not math.isfinite(value)
    )


def init_template(incident_id: str) -> dict[str, Any]:
    return {
        "schema_version": 1,
        "incident_id": incident_id,
        "title": "REPLACE: concise incident title",
        "symptom": "REPLACE: externally measurable failure without presumed cause",
        "evidence": {
            "facts": [
                {
                    "id": "F1",
                    "claim": "REPLACE: directly supported fact",
                    "source": {
                        "kind": "telemetry",
                        "artifact_id": "REPLACE-artifact-id",
                        "locator": "REPLACE: row range, timestamp, line, or dated statement",
                    },
                }
            ],
            "inferences": [
                {
                    "id": "I1",
                    "claim": "REPLACE: bounded inference",
                    "basis": ["F1"],
                }
            ],
            "unknowns": [
                {"id": "U1", "question": "REPLACE: unresolved causal question"}
            ],
        },
        "known_bad": {
            "runtime_sha256": "unknown",
            "source_commit": "unknown",
            "audit_artifact_id": "REPLACE-audit-artifact-id",
            "audit_artifact_sha256": "unknown",
        },
        "fixture": {
            "kind": "native_benchmark",
            "source_files": ["REPLACE/native_fixture.cpp"],
            "build_target": "REPLACE-build-target",
            "run_command": ["REPLACE/path/to/fixture.exe", "--output", "known-bad.json"],
            "trigger_assertions": ["REPLACE: prove the incident path executed"],
            "covariates": {
                "freshness": "REPLACE",
                "target_generation": "REPLACE",
                "target_count": "REPLACE",
                "right_stick_manual": "REPLACE",
                "left_stick_manual": "REPLACE",
                "recoil_firing": "REPLACE",
                "controller_mode": "REPLACE",
                "refresh_rate_hz": "REPLACE",
                "logging_mode": "REPLACE",
            },
        },
        "oracles": [
            {
                "id": "O1",
                "metric": "REPLACE_metric",
                "operator": "<=",
                "threshold": 0.0,
                "unit": "REPLACE_unit",
                "evidence_fact_ids": ["F1"],
            }
        ],
        "counterfactuals": [
            {
                "id": "C1",
                "change": "REPLACE: remove or invert the trigger",
                "expected": "REPLACE: observable outcome must change",
            }
        ],
        "red_proof": None,
        "green_proof": None,
        "verification": None,
    }


def validate_sha(
    value: Any,
    label: str,
    issues: list[dict[str, Any]],
    *,
    required: bool,
) -> str | None:
    normalized = normalize(value)
    if is_placeholder(value):
        if required:
            issue(issues, "incomplete", f"{label}_missing", f"{label} is required")
        return None
    if normalized is None or SHA256_RE.fullmatch(normalized) is None:
        issue(
            issues,
            "blocked",
            f"{label}_invalid",
            f"{label} must be a 64-character lowercase SHA-256",
        )
        return None
    return normalized


def verify_artifact(
    reference: Any,
    label: str,
    base_dir: Path,
    issues: list[dict[str, Any]],
    verified: list[dict[str, Any]],
    artifact_ids: set[str],
    *,
    required: bool,
) -> dict[str, Any] | None:
    if not isinstance(reference, dict):
        if required:
            issue(
                issues,
                "incomplete",
                f"{label}_artifact_missing",
                f"{label} artifact reference is required",
            )
        return None
    artifact_id = reference.get("id")
    raw_path = reference.get("path")
    expected = validate_sha(
        reference.get("sha256"), f"{label}_artifact_sha256", issues, required=required
    )
    if not valid_id(artifact_id):
        issue(
            issues,
            "blocked",
            f"{label}_artifact_id_invalid",
            f"{label} artifact needs a stable identifier",
        )
        return None
    if artifact_id in artifact_ids:
        issue(
            issues,
            "blocked",
            "artifact_id_duplicate",
            f"artifact id {artifact_id} is duplicated",
        )
    artifact_ids.add(artifact_id)
    if not isinstance(raw_path, str) or not raw_path.strip():
        issue(
            issues,
            "incomplete" if required else "blocked",
            f"{label}_artifact_path_missing",
            f"{label} artifact path is required",
        )
        return None
    candidate = Path(raw_path)
    path = candidate if candidate.is_absolute() else base_dir / candidate
    if not path.is_file():
        issue(
            issues,
            "incomplete",
            f"{label}_artifact_unavailable",
            f"{label} artifact does not exist",
        )
        return None
    try:
        actual = hash_file(path)
    except OSError as exc:
        issue(
            issues,
            "blocked",
            f"{label}_artifact_unreadable",
            f"{label} artifact could not be read: {exc.__class__.__name__}",
        )
        return None
    if expected is not None and expected != actual:
        issue(
            issues,
            "blocked",
            f"{label}_artifact_hash_mismatch",
            f"{label} artifact hash does not match content",
        )
    item = {
        "id": artifact_id,
        "sha256": actual,
        "size_bytes": path.stat().st_size,
    }
    verified.append(item)
    return item


def validate_evidence(
    evidence: Any, issues: list[dict[str, Any]]
) -> tuple[set[str], dict[str, int]]:
    if not isinstance(evidence, dict):
        issue(
            issues,
            "blocked",
            "evidence_not_object",
            "evidence must be an object",
        )
        return set(), {"facts": 0, "inferences": 0, "unknowns": 0}
    facts = evidence.get("facts")
    inferences = evidence.get("inferences")
    unknowns = evidence.get("unknowns")
    fact_ids: set[str] = set()
    all_ids: set[str] = set()
    if not isinstance(facts, list) or not facts:
        issue(
            issues,
            "incomplete",
            "facts_missing",
            "at least one evidence fact is required",
        )
        facts = []
    for index, fact in enumerate(facts):
        if not isinstance(fact, dict):
            issue(
                issues,
                "blocked",
                "fact_not_object",
                f"fact {index} must be an object",
            )
            continue
        fact_id = fact.get("id")
        if not valid_id(fact_id) or fact_id in all_ids:
            issue(
                issues,
                "blocked",
                "fact_id_invalid",
                f"fact {index} needs a unique identifier",
            )
            continue
        all_ids.add(fact_id)
        fact_ids.add(fact_id)
        if is_placeholder(fact.get("claim")):
            issue(
                issues,
                "incomplete",
                "fact_claim_missing",
                f"fact {fact_id} needs a supported claim",
            )
        source = fact.get("source")
        if not isinstance(source, dict) or source.get("kind") not in SOURCE_KINDS:
            issue(
                issues,
                "blocked",
                "fact_source_invalid",
                f"fact {fact_id} needs a valid source kind",
            )
            continue
        if is_placeholder(source.get("artifact_id")) or is_placeholder(
            source.get("locator")
        ):
            issue(
                issues,
                "incomplete",
                "fact_source_locator_missing",
                f"fact {fact_id} needs an artifact ID and locator",
            )

    if not isinstance(inferences, list):
        issue(
            issues,
            "blocked",
            "inferences_not_array",
            "evidence.inferences must be an array",
        )
        inferences = []
    for index, inference in enumerate(inferences):
        if not isinstance(inference, dict):
            issue(
                issues,
                "blocked",
                "inference_not_object",
                f"inference {index} must be an object",
            )
            continue
        inference_id = inference.get("id")
        if not valid_id(inference_id) or inference_id in all_ids:
            issue(
                issues,
                "blocked",
                "inference_id_invalid",
                f"inference {index} needs a unique identifier",
            )
            continue
        all_ids.add(inference_id)
        if is_placeholder(inference.get("claim")):
            issue(
                issues,
                "incomplete",
                "inference_claim_missing",
                f"inference {inference_id} needs a bounded claim",
            )
        basis = inference.get("basis")
        if not isinstance(basis, list) or not basis:
            issue(
                issues,
                "incomplete",
                "inference_basis_missing",
                f"inference {inference_id} needs fact IDs",
            )
        elif any(value not in fact_ids for value in basis):
            issue(
                issues,
                "blocked",
                "inference_basis_invalid",
                f"inference {inference_id} references a non-fact ID",
            )

    if not isinstance(unknowns, list):
        issue(
            issues,
            "blocked",
            "unknowns_not_array",
            "evidence.unknowns must be an explicit array",
        )
        unknowns = []
    for index, unknown in enumerate(unknowns):
        if not isinstance(unknown, dict):
            issue(
                issues,
                "blocked",
                "unknown_not_object",
                f"unknown {index} must be an object",
            )
            continue
        unknown_id = unknown.get("id")
        if not valid_id(unknown_id) or unknown_id in all_ids:
            issue(
                issues,
                "blocked",
                "unknown_id_invalid",
                f"unknown {index} needs a unique identifier",
            )
            continue
        all_ids.add(unknown_id)
        if is_placeholder(unknown.get("question")):
            issue(
                issues,
                "incomplete",
                "unknown_question_missing",
                f"unknown {unknown_id} needs a question",
            )
    return fact_ids, {
        "facts": len(facts),
        "inferences": len(inferences),
        "unknowns": len(unknowns),
    }


def validate_known_bad(
    known_bad: Any, issues: list[dict[str, Any]]
) -> dict[str, Any]:
    if not isinstance(known_bad, dict):
        issue(
            issues,
            "blocked",
            "known_bad_not_object",
            "known_bad must be an object",
        )
        return {"runtime_sha256": None, "source_commit": None}
    runtime_sha = validate_sha(
        known_bad.get("runtime_sha256"),
        "known_bad_runtime_sha256",
        issues,
        required=True,
    )
    commit = normalize(known_bad.get("source_commit"))
    if is_placeholder(known_bad.get("source_commit")):
        issue(
            issues,
            "incomplete",
            "known_bad_source_commit_missing",
            "known_bad source commit is required",
        )
        commit = None
    elif commit is None or COMMIT_RE.fullmatch(commit) is None:
        issue(
            issues,
            "blocked",
            "known_bad_source_commit_invalid",
            "known_bad source commit must be hexadecimal",
        )
        commit = None
    audit_id = known_bad.get("audit_artifact_id")
    audit_sha = known_bad.get("audit_artifact_sha256")
    if not is_placeholder(audit_id) or not is_placeholder(audit_sha):
        if is_placeholder(audit_id):
            issue(
                issues,
                "incomplete",
                "audit_artifact_id_missing",
                "audit artifact ID is required when an audit hash is supplied",
            )
        validate_sha(
            audit_sha,
            "audit_artifact_sha256",
            issues,
            required=True,
        )
    return {"runtime_sha256": runtime_sha, "source_commit": commit}


def validate_fixture(
    fixture: Any,
    base_dir: Path,
    stage: str,
    issues: list[dict[str, Any]],
) -> dict[str, Any]:
    if not isinstance(fixture, dict):
        issue(
            issues,
            "blocked",
            "fixture_not_object",
            "fixture must be an object",
        )
        return {"kind": None, "source_files": 0, "trigger_assertions": 0}
    kind = fixture.get("kind")
    if kind not in FIXTURE_KINDS:
        issue(
            issues,
            "blocked",
            "fixture_kind_invalid",
            f"fixture kind must be one of {sorted(FIXTURE_KINDS)}",
        )
    source_files = fixture.get("source_files")
    if not isinstance(source_files, list) or not source_files or not all(
        isinstance(value, str) and value.strip() for value in source_files
    ):
        issue(
            issues,
            "incomplete",
            "fixture_source_files_missing",
            "fixture source_files must be a non-empty array",
        )
        source_files = []
    elif stage in {"red", "complete"}:
        for index, raw_path in enumerate(source_files):
            candidate = Path(raw_path)
            path = candidate if candidate.is_absolute() else base_dir / candidate
            if not path.is_file():
                issue(
                    issues,
                    "incomplete",
                    "fixture_source_file_unavailable",
                    f"fixture source file {index} does not exist",
                )
    if is_placeholder(fixture.get("build_target")):
        issue(
            issues,
            "incomplete",
            "fixture_build_target_missing",
            "fixture build_target is required",
        )
    if not valid_command(fixture.get("run_command")) or any(
        is_placeholder(part) for part in fixture.get("run_command", [])
    ):
        issue(
            issues,
            "incomplete",
            "fixture_run_command_missing",
            "fixture run_command must be an argv array without placeholders",
        )
    triggers = fixture.get("trigger_assertions")
    if not isinstance(triggers, list) or not triggers or any(
        is_placeholder(value) for value in triggers
    ):
        issue(
            issues,
            "incomplete",
            "fixture_trigger_assertions_missing",
            "at least one concrete trigger assertion is required",
        )
        triggers = []
    covariates = fixture.get("covariates")
    if not isinstance(covariates, dict):
        issue(
            issues,
            "blocked",
            "fixture_covariates_not_object",
            "fixture covariates must be an object",
        )
        covariates = {}
    for name in REQUIRED_COVARIATES:
        if name not in covariates or is_placeholder(covariates.get(name)):
            issue(
                issues,
                "incomplete",
                "fixture_covariate_missing",
                f"fixture covariate {name} must be explicit",
            )
    return {
        "kind": kind,
        "source_files": len(source_files),
        "trigger_assertions": len(triggers),
        "covariates": sorted(covariates),
    }


def validate_oracles(
    raw_oracles: Any, fact_ids: set[str], issues: list[dict[str, Any]]
) -> dict[str, dict[str, Any]]:
    if not isinstance(raw_oracles, list) or not raw_oracles:
        issue(
            issues,
            "incomplete",
            "oracles_missing",
            "at least one incident oracle is required",
        )
        return {}
    oracles: dict[str, dict[str, Any]] = {}
    for index, oracle in enumerate(raw_oracles):
        if not isinstance(oracle, dict):
            issue(
                issues,
                "blocked",
                "oracle_not_object",
                f"oracle {index} must be an object",
            )
            continue
        oracle_id = oracle.get("id")
        if not valid_id(oracle_id) or oracle_id in oracles:
            issue(
                issues,
                "blocked",
                "oracle_id_invalid",
                f"oracle {index} needs a unique identifier",
            )
            continue
        metric = oracle.get("metric")
        operator = oracle.get("operator")
        threshold = oracle.get("threshold")
        unit = oracle.get("unit")
        evidence_ids = oracle.get("evidence_fact_ids")
        if is_placeholder(metric) or is_placeholder(unit):
            issue(
                issues,
                "incomplete",
                "oracle_definition_missing",
                f"oracle {oracle_id} needs metric and unit",
            )
        if operator not in OPERATORS:
            issue(
                issues,
                "blocked",
                "oracle_operator_invalid",
                f"oracle {oracle_id} operator is invalid",
            )
        if not scalar(threshold):
            issue(
                issues,
                "blocked",
                "oracle_threshold_invalid",
                f"oracle {oracle_id} threshold must be a finite scalar",
            )
        elif operator in {"<", "<=", ">", ">="} and not isinstance(
            threshold, (int, float)
        ):
            issue(
                issues,
                "blocked",
                "oracle_threshold_not_numeric",
                f"oracle {oracle_id} inequality threshold must be numeric",
            )
        if not isinstance(evidence_ids, list) or not evidence_ids:
            issue(
                issues,
                "incomplete",
                "oracle_evidence_missing",
                f"oracle {oracle_id} needs evidence fact IDs",
            )
        elif any(value not in fact_ids for value in evidence_ids):
            issue(
                issues,
                "blocked",
                "oracle_evidence_invalid",
                f"oracle {oracle_id} references a non-fact ID",
            )
        oracles[oracle_id] = oracle
    return oracles


def validate_counterfactuals(
    values: Any, issues: list[dict[str, Any]]
) -> int:
    if not isinstance(values, list) or not values:
        issue(
            issues,
            "incomplete",
            "counterfactuals_missing",
            "at least one counterfactual or negative control is required",
        )
        return 0
    seen: set[str] = set()
    for index, item in enumerate(values):
        if not isinstance(item, dict):
            issue(
                issues,
                "blocked",
                "counterfactual_not_object",
                f"counterfactual {index} must be an object",
            )
            continue
        item_id = item.get("id")
        if not valid_id(item_id) or item_id in seen:
            issue(
                issues,
                "blocked",
                "counterfactual_id_invalid",
                f"counterfactual {index} needs a unique identifier",
            )
            continue
        seen.add(item_id)
        if is_placeholder(item.get("change")) or is_placeholder(item.get("expected")):
            issue(
                issues,
                "incomplete",
                "counterfactual_definition_missing",
                f"counterfactual {item_id} needs change and expected result",
            )
    return len(values)


def oracle_result(oracle: dict[str, Any], observed: Any) -> bool | None:
    operator = oracle.get("operator")
    threshold = oracle.get("threshold")
    if not scalar(observed) or not scalar(threshold):
        return None
    try:
        if operator == "<":
            return bool(observed < threshold)
        if operator == "<=":
            return bool(observed <= threshold)
        if operator == ">":
            return bool(observed > threshold)
        if operator == ">=":
            return bool(observed >= threshold)
        if operator == "==":
            return bool(observed == threshold)
        if operator == "!=":
            return bool(observed != threshold)
    except TypeError:
        return None
    return None


def validate_proof_oracles(
    values: Any,
    oracles: dict[str, dict[str, Any]],
    issues: list[dict[str, Any]],
    *,
    expected_pass: bool,
    require_all: bool,
    label: str,
) -> list[str]:
    if not isinstance(values, list) or not values:
        issue(
            issues,
            "incomplete",
            f"{label}_oracle_results_missing",
            f"{label} must include measured oracle results",
        )
        return []
    seen: set[str] = set()
    for index, item in enumerate(values):
        if not isinstance(item, dict):
            issue(
                issues,
                "blocked",
                f"{label}_oracle_result_not_object",
                f"{label} oracle result {index} must be an object",
            )
            continue
        oracle_id = item.get("oracle_id")
        if oracle_id not in oracles or oracle_id in seen:
            issue(
                issues,
                "blocked",
                f"{label}_oracle_id_invalid",
                f"{label} oracle result {index} has an unknown or duplicate ID",
            )
            continue
        seen.add(oracle_id)
        result = oracle_result(oracles[oracle_id], item.get("observed"))
        if result is None:
            issue(
                issues,
                "blocked",
                f"{label}_oracle_observed_invalid",
                f"{label} oracle {oracle_id} has an invalid observed value",
            )
        elif result != expected_pass:
            expected_text = "pass" if expected_pass else "fail"
            issue(
                issues,
                "blocked",
                f"{label}_oracle_outcome_contradiction",
                f"{label} oracle {oracle_id} does not {expected_text} its declared threshold",
            )
    if require_all and seen != set(oracles):
        issue(
            issues,
            "incomplete",
            f"{label}_oracles_incomplete",
            f"{label} must report every declared oracle",
        )
    return sorted(seen)


def validate_red(
    proof: Any,
    known_bad_sha: str | None,
    oracles: dict[str, dict[str, Any]],
    base_dir: Path,
    issues: list[dict[str, Any]],
    verified: list[dict[str, Any]],
    artifact_ids: set[str],
) -> dict[str, Any]:
    if not isinstance(proof, dict):
        issue(
            issues,
            "incomplete",
            "red_proof_missing",
            "known-bad RED proof is required",
        )
        return {"verified": False, "failed_oracles": []}
    runtime_sha = validate_sha(
        proof.get("runtime_sha256"), "red_runtime_sha256", issues, required=True
    )
    if known_bad_sha is not None and runtime_sha is not None and runtime_sha != known_bad_sha:
        issue(
            issues,
            "blocked",
            "red_runtime_identity_mismatch",
            "RED proof runtime does not match known_bad runtime",
        )
    if not valid_command(proof.get("command")):
        issue(
            issues,
            "incomplete",
            "red_command_missing",
            "RED proof command must be an argv array",
        )
    exit_code = proof.get("exit_code")
    if not isinstance(exit_code, int):
        issue(
            issues,
            "incomplete",
            "red_exit_code_missing",
            "RED proof exit_code is required",
        )
    elif exit_code == 0:
        issue(
            issues,
            "blocked",
            "red_exit_code_zero",
            "known-bad fixture must return a non-zero exit code",
        )
    artifact = verify_artifact(
        proof.get("report"),
        "red_report",
        base_dir,
        issues,
        verified,
        artifact_ids,
        required=True,
    )
    failed = validate_proof_oracles(
        proof.get("failed_oracles"),
        oracles,
        issues,
        expected_pass=False,
        require_all=False,
        label="red",
    )
    return {"verified": artifact is not None, "failed_oracles": failed}


def validate_green(
    proof: Any,
    known_bad_sha: str | None,
    oracles: dict[str, dict[str, Any]],
    base_dir: Path,
    issues: list[dict[str, Any]],
    verified: list[dict[str, Any]],
    artifact_ids: set[str],
) -> tuple[dict[str, Any], str | None]:
    if not isinstance(proof, dict):
        issue(
            issues,
            "incomplete",
            "green_proof_missing",
            "candidate GREEN proof is required for complete stage",
        )
        return {"verified": False, "passed_oracles": []}, None
    runtime_sha = validate_sha(
        proof.get("runtime_sha256"), "green_runtime_sha256", issues, required=True
    )
    if known_bad_sha is not None and runtime_sha is not None and runtime_sha == known_bad_sha:
        issue(
            issues,
            "blocked",
            "green_runtime_not_distinct",
            "candidate runtime SHA-256 must differ from known-bad runtime",
        )
    if not valid_command(proof.get("command")):
        issue(
            issues,
            "incomplete",
            "green_command_missing",
            "GREEN proof command must be an argv array",
        )
    exit_code = proof.get("exit_code")
    if not isinstance(exit_code, int):
        issue(
            issues,
            "incomplete",
            "green_exit_code_missing",
            "GREEN proof exit_code is required",
        )
    elif exit_code != 0:
        issue(
            issues,
            "blocked",
            "green_exit_code_nonzero",
            "candidate fixture must return exit code zero",
        )
    artifact = verify_artifact(
        proof.get("report"),
        "green_report",
        base_dir,
        issues,
        verified,
        artifact_ids,
        required=True,
    )
    passed = validate_proof_oracles(
        proof.get("passed_oracles"),
        oracles,
        issues,
        expected_pass=True,
        require_all=True,
        label="green",
    )
    return {"verified": artifact is not None, "passed_oracles": passed}, runtime_sha


def validate_verification(
    verification: Any,
    known_bad_sha: str | None,
    candidate_sha: str | None,
    base_dir: Path,
    issues: list[dict[str, Any]],
    verified: list[dict[str, Any]],
    artifact_ids: set[str],
) -> dict[str, Any]:
    if not isinstance(verification, dict):
        issue(
            issues,
            "incomplete",
            "verification_missing",
            "complete stage requires verification",
        )
        return {"focused_tests": 0, "diff_reviewed": False}
    focused = verification.get("focused_tests")
    if not isinstance(focused, list) or not focused:
        issue(
            issues,
            "incomplete",
            "focused_tests_missing",
            "at least one focused test result is required",
        )
        focused = []
    for index, item in enumerate(focused):
        if not isinstance(item, dict) or not valid_command(item.get("command")):
            issue(
                issues,
                "blocked",
                "focused_test_invalid",
                f"focused test {index} needs an argv command",
            )
        elif item.get("exit_code") != 0:
            issue(
                issues,
                "blocked",
                "focused_test_failed",
                f"focused test {index} did not pass",
            )
    comparison = verification.get("ab_comparison")
    if not isinstance(comparison, dict):
        issue(
            issues,
            "incomplete",
            "ab_comparison_missing",
            "matched A/B comparison record is required",
        )
    else:
        baseline = normalize(comparison.get("baseline_runtime_sha256"))
        candidate = normalize(comparison.get("candidate_runtime_sha256"))
        if baseline != known_bad_sha or candidate != candidate_sha:
            issue(
                issues,
                "blocked",
                "ab_runtime_identity_mismatch",
                "A/B runtime identities do not match RED and GREEN proofs",
            )
        if comparison.get("invariants_checked") is not True:
            issue(
                issues,
                "incomplete",
                "ab_invariants_unchecked",
                "A/B comparison invariants must be explicitly checked",
            )
    diff_reviewed = verification.get("diff_reviewed") is True
    if not diff_reviewed:
        issue(
            issues,
            "incomplete",
            "diff_review_missing",
            "full diff review must be recorded",
        )
    verify_artifact(
        verification.get("artifact_manifest"),
        "artifact_manifest",
        base_dir,
        issues,
        verified,
        artifact_ids,
        required=True,
    )
    return {"focused_tests": len(focused), "diff_reviewed": diff_reviewed}


def run_check(manifest_path: Path, stage: str) -> dict[str, Any]:
    issues: list[dict[str, Any]] = []
    verified: list[dict[str, Any]] = []
    artifact_ids: set[str] = set()
    try:
        raw = json.loads(manifest_path.read_text(encoding="utf-8"))
        if not isinstance(raw, dict):
            raise ValueError("top-level JSON value is not an object")
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as exc:
        report = {
            "schema_version": 1,
            "incident_id": "unknown",
            "stage": stage,
            "status": BLOCKED,
            "checks": {},
            "verified_artifacts": [],
            "issues": [
                {
                    "severity": "blocked",
                    "code": "manifest_parse_failed",
                    "detail": f"manifest parsing failed: {exc.__class__.__name__}",
                }
            ],
        }
        report["artifact_sha256"] = payload_hash(report)
        return report

    if raw.get("schema_version") != 1:
        issue(
            issues,
            "blocked",
            "manifest_schema_invalid",
            "manifest schema_version must be 1",
        )
    incident_id = raw.get("incident_id")
    if not isinstance(incident_id, str) or not incident_id.strip():
        issue(
            issues,
            "blocked",
            "incident_id_invalid",
            "incident_id must be a non-empty string",
        )
        incident_id = "unknown"
    for field in ("title", "symptom"):
        if is_placeholder(raw.get(field)):
            issue(
                issues,
                "incomplete",
                f"{field}_missing",
                f"{field} must be explicit",
            )

    fact_ids, evidence_counts = validate_evidence(raw.get("evidence"), issues)
    known_bad = validate_known_bad(raw.get("known_bad"), issues)
    fixture = validate_fixture(raw.get("fixture"), manifest_path.parent, stage, issues)
    oracles = validate_oracles(raw.get("oracles"), fact_ids, issues)
    counterfactual_count = validate_counterfactuals(
        raw.get("counterfactuals"), issues
    )

    red_report = {"verified": False, "failed_oracles": []}
    green_report = {"verified": False, "passed_oracles": []}
    verification_report = {"focused_tests": 0, "diff_reviewed": False}
    candidate_sha: str | None = None
    if stage in {"red", "complete"}:
        red_report = validate_red(
            raw.get("red_proof"),
            known_bad["runtime_sha256"],
            oracles,
            manifest_path.parent,
            issues,
            verified,
            artifact_ids,
        )
    if stage == "complete":
        green_report, candidate_sha = validate_green(
            raw.get("green_proof"),
            known_bad["runtime_sha256"],
            oracles,
            manifest_path.parent,
            issues,
            verified,
            artifact_ids,
        )
        verification_report = validate_verification(
            raw.get("verification"),
            known_bad["runtime_sha256"],
            candidate_sha,
            manifest_path.parent,
            issues,
            verified,
            artifact_ids,
        )

    issues.sort(key=issue_sort_key)
    report = {
        "schema_version": 1,
        "incident_id": incident_id,
        "stage": stage,
        "status": status_from_issues(issues),
        "checks": {
            "evidence": evidence_counts,
            "fixture": fixture,
            "oracles": len(oracles),
            "counterfactuals": counterfactual_count,
            "red": red_report,
            "green": green_report,
            "verification": verification_report,
        },
        "verified_artifacts": sorted(verified, key=lambda item: item["id"]),
        "issues": issues,
    }
    report["artifact_sha256"] = payload_hash(report)
    return report


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2) + "\n",
        encoding="utf-8",
    )


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Initialize or validate an incident-to-regression contract."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)
    init_parser = subparsers.add_parser("init", help="write a contract template")
    init_parser.add_argument("--output", type=Path, required=True)
    init_parser.add_argument("--incident-id", required=True)
    check_parser = subparsers.add_parser("check", help="validate a contract")
    check_parser.add_argument("--manifest", type=Path, required=True)
    check_parser.add_argument(
        "--stage", choices=("draft", "red", "complete"), required=True
    )
    check_parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if args.command == "init":
        if args.output.exists():
            print("refusing to overwrite an existing manifest", file=sys.stderr)
            return 3
        write_json(args.output, init_template(args.incident_id))
        print(f"created regression contract template: {args.output}")
        return 0
    report = run_check(args.manifest, args.stage)
    write_json(args.output, report)
    print(
        f"{report['status']} artifact_sha256={report['artifact_sha256']} "
        f"issues={len(report['issues'])}"
    )
    return EXIT_CODES[report["status"]]


if __name__ == "__main__":
    raise SystemExit(main())
