#!/usr/bin/env python3
"""Initialize and fail-closed preflight native runtime evidence manifests."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import re
import sys
from collections import Counter
from pathlib import Path
from typing import Any, Iterable


PASS = "PASS"
INSUFFICIENT = "INSUFFICIENT_EVIDENCE"
BLOCKED = "BLOCKED"

EXIT_CODES = {PASS: 0, INSUFFICIENT: 2, BLOCKED: 3}
SEVERITY_ORDER = {"blocked": 0, "insufficient": 1}
JSONL_KINDS = {"telemetry_jsonl", "performance_jsonl"}
FILE_KINDS = {
    "session_manifest",
    "telemetry_jsonl",
    "performance_jsonl",
    "nvidia_csv",
    "video",
    "notes",
    "binary",
    "config",
    "other",
}
LOGGING_MODES = {"detailed", "performance_summary", "disabled", "mixed"}
ROLES = {"single", "baseline", "candidate", "control"}
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
COMMIT_RE = re.compile(r"^[0-9a-f]{7,64}$")
IDENTITY_FIELDS = (
    "executable_sha256",
    "config_hash",
    "engine_hash",
    "git_commit",
)
RECORD_IDENTITY_ALIASES = {
    "executable_sha256": ("executable_sha256",),
    "config_hash": ("config_hash",),
    "engine_hash": ("engine_hash",),
    "git_commit": ("git_commit", "build_commit"),
}


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
    issues: list[dict[str, Any]],
    severity: str,
    code: str,
    detail: str,
    *,
    cohort: str | None = None,
    file_id: str | None = None,
) -> None:
    item: dict[str, Any] = {
        "severity": severity,
        "code": code,
        "detail": detail,
    }
    if cohort is not None:
        item["cohort"] = cohort
    if file_id is not None:
        item["file_id"] = file_id
    issues.append(item)


def issue_sort_key(item: dict[str, Any]) -> tuple[Any, ...]:
    return (
        SEVERITY_ORDER.get(str(item.get("severity")), 99),
        str(item.get("code", "")),
        str(item.get("cohort", "")),
        str(item.get("file_id", "")),
        str(item.get("detail", "")),
    )


def status_from_issues(issues: Iterable[dict[str, Any]]) -> str:
    severities = {str(item.get("severity")) for item in issues}
    if "blocked" in severities:
        return BLOCKED
    if "insufficient" in severities:
        return INSUFFICIENT
    return PASS


def normalize_identity(value: Any) -> str | None:
    if not isinstance(value, str):
        return None
    normalized = value.strip().lower()
    return normalized or None


def is_unknown(value: Any) -> bool:
    return value is None or (
        isinstance(value, str)
        and value.strip().lower() in {"", "unknown", "unavailable", "n/a"}
    )


def deep_get(record: dict[str, Any], dotted: str) -> tuple[bool, Any]:
    current: Any = record
    for part in dotted.split("."):
        if not isinstance(current, dict) or part not in current:
            return False, None
        current = current[part]
    return True, current


def usable_coverage_value(value: Any, require_nonzero: bool) -> bool:
    if value is None:
        return False
    if isinstance(value, float) and not math.isfinite(value):
        return False
    if isinstance(value, str) and not value.strip():
        return False
    if require_nonzero and value in (0, 0.0, False, "0"):
        return False
    return True


def extract_identity(record: dict[str, Any], observed: dict[str, set[str]]) -> None:
    for canonical, aliases in RECORD_IDENTITY_ALIASES.items():
        for alias in aliases:
            value = normalize_identity(record.get(alias))
            if value is not None:
                observed[canonical].add(value)
                break


def parse_json_object(path: Path) -> dict[str, Any]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError("top-level JSON value is not an object")
    return data


def init_template(audit_id: str) -> dict[str, Any]:
    return {
        "schema_version": 1,
        "audit_id": audit_id,
        "question": "REPLACE: state one falsifiable audit question",
        "cohorts": [
            {
                "name": "observed-run",
                "role": "single",
                "runtime": {
                    "executable_sha256": "unknown",
                    "git_commit": "unknown",
                    "config_hash": "unknown",
                    "engine_hash": "unknown",
                    "telemetry_schema": 15,
                    "logging_mode": "detailed",
                    "hardware_profile": "unknown",
                    "game_refresh_hz": "unknown",
                },
                "required_record_types": ["session_metadata"],
                "files": [
                    {
                        "id": "session-manifest",
                        "kind": "session_manifest",
                        "path": "REPLACE/session.json",
                    },
                    {
                        "id": "telemetry-0",
                        "kind": "telemetry_jsonl",
                        "path": "REPLACE/native_runtime_telemetry_0.jsonl",
                        "expected_schema_version": 15,
                    },
                ],
            }
        ],
        "coverage_rules": [
            {
                "name": "session-metadata-identity",
                "record_types": ["session_metadata"],
                "field": "executable_sha256",
                "min_ratio": 1.0,
                "min_records": 1,
            }
        ],
        "comparison": {
            "required_equal": [
                "logging_mode",
                "hardware_profile",
                "game_refresh_hz",
                "telemetry_schema",
            ],
            "allowed_differences": [
                "executable_sha256",
                "git_commit",
                "config_hash",
            ],
        },
        "limits": {
            "max_malformed_rows": 0,
            "min_parsed_rows_per_jsonl": 1,
        },
    }


def validate_runtime(
    runtime: Any, issues: list[dict[str, Any]], cohort_name: str
) -> dict[str, Any]:
    if not isinstance(runtime, dict):
        issue(
            issues,
            "blocked",
            "runtime_not_object",
            "runtime must be an object",
            cohort=cohort_name,
        )
        return {}

    sha = normalize_identity(runtime.get("executable_sha256"))
    if is_unknown(sha):
        issue(
            issues,
            "insufficient",
            "runtime_sha_unknown",
            "full executable SHA-256 is required",
            cohort=cohort_name,
        )
    elif sha is None or SHA256_RE.fullmatch(sha) is None:
        issue(
            issues,
            "blocked",
            "runtime_sha_invalid",
            "executable_sha256 must be 64 lowercase hexadecimal characters",
            cohort=cohort_name,
        )

    for field in ("config_hash", "engine_hash"):
        value = normalize_identity(runtime.get(field))
        if not is_unknown(value) and (value is None or SHA256_RE.fullmatch(value) is None):
            issue(
                issues,
                "blocked",
                f"{field}_invalid",
                f"{field} must be unknown or a 64-character SHA-256",
                cohort=cohort_name,
            )

    commit = normalize_identity(runtime.get("git_commit"))
    if not is_unknown(commit) and (commit is None or COMMIT_RE.fullmatch(commit) is None):
        issue(
            issues,
            "blocked",
            "git_commit_invalid",
            "git_commit must be unknown or a hexadecimal commit identity",
            cohort=cohort_name,
        )

    telemetry_schema = runtime.get("telemetry_schema")
    if not isinstance(telemetry_schema, int) or telemetry_schema < 1:
        issue(
            issues,
            "insufficient",
            "telemetry_schema_missing",
            "telemetry_schema must be a positive integer",
            cohort=cohort_name,
        )

    logging_mode = runtime.get("logging_mode")
    if logging_mode not in LOGGING_MODES:
        issue(
            issues,
            "blocked",
            "logging_mode_invalid",
            f"logging_mode must be one of {sorted(LOGGING_MODES)}",
            cohort=cohort_name,
        )
    elif logging_mode == "mixed":
        issue(
            issues,
            "insufficient",
            "logging_mode_mixed",
            "mixed logging must be segmented before comparison",
            cohort=cohort_name,
        )

    for field in ("hardware_profile", "game_refresh_hz"):
        if is_unknown(runtime.get(field)):
            issue(
                issues,
                "insufficient",
                f"{field}_unknown",
                f"{field} is required for matched runtime evidence",
                cohort=cohort_name,
            )
    return runtime


def validate_coverage_rules(
    raw_rules: Any, issues: list[dict[str, Any]]
) -> list[dict[str, Any]]:
    if raw_rules is None:
        return []
    if not isinstance(raw_rules, list):
        issue(
            issues,
            "blocked",
            "coverage_rules_not_array",
            "coverage_rules must be an array",
        )
        return []
    rules: list[dict[str, Any]] = []
    names: set[str] = set()
    for index, raw in enumerate(raw_rules):
        if not isinstance(raw, dict):
            issue(
                issues,
                "blocked",
                "coverage_rule_not_object",
                f"coverage rule {index} must be an object",
            )
            continue
        name = raw.get("name")
        field = raw.get("field")
        record_types = raw.get("record_types", [])
        min_ratio = raw.get("min_ratio", 1.0)
        min_records = raw.get("min_records", 1)
        if not isinstance(name, str) or not name.strip() or name in names:
            issue(
                issues,
                "blocked",
                "coverage_rule_name_invalid",
                f"coverage rule {index} needs a unique non-empty name",
            )
            continue
        names.add(name)
        if not isinstance(field, str) or not field.strip():
            issue(
                issues,
                "blocked",
                "coverage_rule_field_invalid",
                f"coverage rule {name} needs a dotted field",
            )
            continue
        if not isinstance(record_types, list) or not all(
            isinstance(value, str) and value for value in record_types
        ):
            issue(
                issues,
                "blocked",
                "coverage_rule_types_invalid",
                f"coverage rule {name} record_types must be strings",
            )
            continue
        if (
            not isinstance(min_ratio, (int, float))
            or not math.isfinite(float(min_ratio))
            or not 0.0 <= float(min_ratio) <= 1.0
        ):
            issue(
                issues,
                "blocked",
                "coverage_rule_ratio_invalid",
                f"coverage rule {name} min_ratio must be between 0 and 1",
            )
            continue
        if not isinstance(min_records, int) or min_records < 1:
            issue(
                issues,
                "blocked",
                "coverage_rule_min_records_invalid",
                f"coverage rule {name} min_records must be a positive integer",
            )
            continue
        rules.append(
            {
                "name": name,
                "field": field,
                "record_types": sorted(set(record_types)),
                "min_ratio": float(min_ratio),
                "min_records": min_records,
                "require_nonzero": bool(raw.get("require_nonzero", False)),
            }
        )
    return rules


def analyze_jsonl(
    path: Path,
    rules: list[dict[str, Any]],
    counters: dict[str, dict[str, int]],
) -> tuple[dict[str, Any], dict[str, set[str]]]:
    record_types: Counter[str] = Counter()
    schemas: Counter[str] = Counter()
    observed = {field: set() for field in IDENTITY_FIELDS}
    lines_total = 0
    blank_rows = 0
    parsed_rows = 0
    malformed_rows = 0
    non_object_rows = 0
    incomplete_rows = 0
    malformed_line_numbers: list[int] = []

    with path.open("r", encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, start=1):
            lines_total += 1
            stripped = line.strip()
            if not stripped:
                blank_rows += 1
                continue
            try:
                record = json.loads(stripped)
            except (json.JSONDecodeError, UnicodeDecodeError):
                malformed_rows += 1
                if len(malformed_line_numbers) < 20:
                    malformed_line_numbers.append(line_number)
                continue
            if not isinstance(record, dict):
                non_object_rows += 1
                continue
            parsed_rows += 1
            record_type = str(record.get("type", "<missing>"))
            record_types[record_type] += 1
            if "schema_version" in record:
                schemas[str(record.get("schema_version"))] += 1
            if record.get("complete") is False:
                incomplete_rows += 1
            extract_identity(record, observed)
            for rule in rules:
                if rule["record_types"] and record_type not in rule["record_types"]:
                    continue
                counter = counters[rule["name"]]
                counter["eligible"] += 1
                present, value = deep_get(record, rule["field"])
                if present and usable_coverage_value(value, rule["require_nonzero"]):
                    counter["covered"] += 1

    return (
        {
            "lines_total": lines_total,
            "blank_rows": blank_rows,
            "parsed_rows": parsed_rows,
            "malformed_rows": malformed_rows,
            "non_object_rows": non_object_rows,
            "incomplete_rows": incomplete_rows,
            "malformed_line_numbers": malformed_line_numbers,
            "record_types": dict(sorted(record_types.items())),
            "schema_versions": dict(sorted(schemas.items())),
        },
        observed,
    )


def analyze_csv(path: Path) -> dict[str, Any]:
    rows = 0
    columns = 0
    with path.open("r", encoding="utf-8-sig", newline="") as stream:
        reader = csv.reader(stream)
        for index, row in enumerate(reader):
            if index == 0:
                columns = len(row)
            else:
                rows += 1
    return {"data_rows": rows, "columns": columns}


def merge_observed(
    target: dict[str, set[str]], source: dict[str, set[str]]
) -> None:
    for field in IDENTITY_FIELDS:
        target[field].update(source[field])


def check_expected_identity(
    runtime: dict[str, Any],
    observed: dict[str, set[str]],
    issues: list[dict[str, Any]],
    cohort_name: str,
) -> None:
    for field in IDENTITY_FIELDS:
        expected = normalize_identity(runtime.get(field))
        values = observed[field]
        if len(values) > 1:
            issue(
                issues,
                "blocked",
                "observed_identity_mixed",
                f"observed more than one {field}: {sorted(values)}",
                cohort=cohort_name,
            )
        if is_unknown(expected):
            continue
        if values and expected not in values:
            issue(
                issues,
                "blocked",
                "observed_identity_mismatch",
                f"declared {field} does not match observed identity",
                cohort=cohort_name,
            )
    expected_sha = normalize_identity(runtime.get("executable_sha256"))
    if not is_unknown(expected_sha) and not observed["executable_sha256"]:
        issue(
            issues,
            "insufficient",
            "runtime_sha_unverified",
            "no session metadata, session manifest, or executable artifact verifies the declared runtime SHA-256",
            cohort=cohort_name,
        )


def check_file(
    entry: Any,
    manifest_dir: Path,
    runtime: dict[str, Any],
    rules: list[dict[str, Any]],
    coverage_counters: dict[str, dict[str, int]],
    issues: list[dict[str, Any]],
    cohort_name: str,
) -> tuple[dict[str, Any] | None, dict[str, set[str]]]:
    observed = {field: set() for field in IDENTITY_FIELDS}
    if not isinstance(entry, dict):
        issue(
            issues,
            "blocked",
            "file_entry_not_object",
            "every file entry must be an object",
            cohort=cohort_name,
        )
        return None, observed
    file_id = entry.get("id")
    kind = entry.get("kind")
    raw_path = entry.get("path")
    if not isinstance(file_id, str) or not file_id.strip():
        issue(
            issues,
            "blocked",
            "file_id_invalid",
            "every file entry needs a non-empty stable id",
            cohort=cohort_name,
        )
        return None, observed
    if kind not in FILE_KINDS:
        issue(
            issues,
            "blocked",
            "file_kind_invalid",
            f"kind must be one of {sorted(FILE_KINDS)}",
            cohort=cohort_name,
            file_id=file_id,
        )
        return None, observed
    if not isinstance(raw_path, str) or not raw_path.strip():
        issue(
            issues,
            "blocked",
            "file_path_invalid",
            "file path must be a non-empty string",
            cohort=cohort_name,
            file_id=file_id,
        )
        return None, observed
    candidate = Path(raw_path)
    path = candidate if candidate.is_absolute() else manifest_dir / candidate
    if not path.is_file():
        issue(
            issues,
            "blocked",
            "file_missing",
            "declared source file does not exist",
            cohort=cohort_name,
            file_id=file_id,
        )
        return {"id": file_id, "kind": kind, "available": False}, observed

    try:
        digest = hash_file(path)
    except OSError as exc:
        issue(
            issues,
            "blocked",
            "file_unreadable",
            f"source file could not be read: {exc.__class__.__name__}",
            cohort=cohort_name,
            file_id=file_id,
        )
        return {"id": file_id, "kind": kind, "available": False}, observed

    summary: dict[str, Any] = {
        "id": file_id,
        "kind": kind,
        "available": True,
        "size_bytes": path.stat().st_size,
        "sha256": digest,
    }
    expected_digest = normalize_identity(entry.get("sha256"))
    if expected_digest is not None:
        if SHA256_RE.fullmatch(expected_digest) is None:
            issue(
                issues,
                "blocked",
                "declared_file_hash_invalid",
                "declared file sha256 must be 64 lowercase hexadecimal characters",
                cohort=cohort_name,
                file_id=file_id,
            )
        elif expected_digest != digest:
            issue(
                issues,
                "blocked",
                "file_hash_mismatch",
                "declared file sha256 does not match content",
                cohort=cohort_name,
                file_id=file_id,
            )

    try:
        if kind == "session_manifest":
            data = parse_json_object(path)
            extract_identity(data, observed)
            summary["session"] = {
                key: data[key]
                for key in ("schema_version", "session_id", "state")
                if key in data
            }
            expected_schema = entry.get("expected_schema_version")
            if expected_schema is not None and data.get("schema_version") != expected_schema:
                issue(
                    issues,
                    "blocked",
                    "file_schema_mismatch",
                    "session manifest schema does not match expected_schema_version",
                    cohort=cohort_name,
                    file_id=file_id,
                )
        elif kind in JSONL_KINDS:
            jsonl, jsonl_observed = analyze_jsonl(
                path, rules, coverage_counters
            )
            summary["jsonl"] = jsonl
            merge_observed(observed, jsonl_observed)
            expected_schema = entry.get("expected_schema_version")
            if expected_schema is None and kind == "telemetry_jsonl":
                expected_schema = runtime.get("telemetry_schema")
            if expected_schema is not None:
                unexpected = sorted(
                    key
                    for key in jsonl["schema_versions"]
                    if key != str(expected_schema)
                )
                if unexpected:
                    issue(
                        issues,
                        "blocked",
                        "file_schema_mismatch",
                        f"observed unexpected schema versions {unexpected}",
                        cohort=cohort_name,
                        file_id=file_id,
                    )
        elif kind == "nvidia_csv":
            summary["csv"] = analyze_csv(path)
        elif kind == "binary" and entry.get("identity_role") == "executable":
            observed["executable_sha256"].add(digest)
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError, csv.Error) as exc:
        issue(
            issues,
            "blocked",
            "file_parse_failed",
            f"source parsing failed: {exc.__class__.__name__}",
            cohort=cohort_name,
            file_id=file_id,
        )
    return summary, observed


def check_comparison(
    cohorts: list[dict[str, Any]],
    comparison: Any,
    issues: list[dict[str, Any]],
) -> dict[str, Any]:
    if comparison is None:
        comparison = {}
    if not isinstance(comparison, dict):
        issue(
            issues,
            "blocked",
            "comparison_not_object",
            "comparison must be an object",
        )
        return {"evaluated": False}
    required_equal = comparison.get("required_equal", [])
    allowed = comparison.get("allowed_differences", [])
    if not isinstance(required_equal, list) or not all(
        isinstance(value, str) and value for value in required_equal
    ):
        issue(
            issues,
            "blocked",
            "comparison_required_equal_invalid",
            "comparison.required_equal must be an array of field names",
        )
        required_equal = []
    if not isinstance(allowed, list) or not all(
        isinstance(value, str) and value for value in allowed
    ):
        issue(
            issues,
            "blocked",
            "comparison_allowed_invalid",
            "comparison.allowed_differences must be an array of field names",
        )
        allowed = []
    overlap = sorted(set(required_equal) & set(allowed))
    if overlap:
        issue(
            issues,
            "blocked",
            "comparison_rule_conflict",
            f"fields cannot be both required equal and allowed different: {overlap}",
        )

    baselines = [item for item in cohorts if item.get("role") == "baseline"]
    candidates = [item for item in cohorts if item.get("role") == "candidate"]
    if not baselines and not candidates:
        return {
            "evaluated": False,
            "required_equal": sorted(set(required_equal)),
            "allowed_differences": sorted(set(allowed)),
        }
    if len(baselines) != 1 or len(candidates) != 1:
        issue(
            issues,
            "blocked",
            "comparison_pair_invalid",
            "a comparison requires exactly one baseline and one candidate cohort",
        )
        return {"evaluated": False}

    baseline = baselines[0]
    candidate = candidates[0]
    checks: list[dict[str, Any]] = []
    for field in sorted(set(required_equal)):
        before = baseline.get("runtime", {}).get(field)
        after = candidate.get("runtime", {}).get(field)
        equal = before == after and not is_unknown(before) and not is_unknown(after)
        checks.append({"field": field, "equal": equal})
        if is_unknown(before) or is_unknown(after):
            issue(
                issues,
                "insufficient",
                "comparison_identity_unknown",
                f"required comparison field {field} is unknown",
            )
        elif before != after:
            issue(
                issues,
                "blocked",
                "comparison_invariant_mismatch",
                f"required comparison field {field} differs",
            )
    return {
        "evaluated": True,
        "baseline": baseline.get("name"),
        "candidate": candidate.get("name"),
        "required_equal": checks,
        "allowed_differences": sorted(set(allowed)),
    }


def run_check(manifest_path: Path) -> dict[str, Any]:
    issues: list[dict[str, Any]] = []
    try:
        manifest = parse_json_object(manifest_path)
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as exc:
        report = {
            "schema_version": 1,
            "audit_id": "unknown",
            "question": "unavailable",
            "status": BLOCKED,
            "cohorts": [],
            "comparison": {"evaluated": False},
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

    audit_id = manifest.get("audit_id")
    question = manifest.get("question")
    if manifest.get("schema_version") != 1:
        issue(
            issues,
            "blocked",
            "manifest_schema_invalid",
            "manifest schema_version must be 1",
        )
    if not isinstance(audit_id, str) or not audit_id.strip():
        issue(
            issues,
            "blocked",
            "audit_id_invalid",
            "audit_id must be a non-empty string",
        )
        audit_id = "unknown"
    if not isinstance(question, str) or not question.strip():
        issue(
            issues,
            "blocked",
            "question_invalid",
            "question must be a non-empty falsifiable statement",
        )
        question = "unavailable"

    rules = validate_coverage_rules(manifest.get("coverage_rules"), issues)
    limits = manifest.get("limits", {})
    if not isinstance(limits, dict):
        issue(
            issues,
            "blocked",
            "limits_not_object",
            "limits must be an object",
        )
        limits = {}
    max_malformed = limits.get("max_malformed_rows", 0)
    min_parsed = limits.get("min_parsed_rows_per_jsonl", 1)
    if not isinstance(max_malformed, int) or max_malformed < 0:
        issue(
            issues,
            "blocked",
            "max_malformed_invalid",
            "max_malformed_rows must be a non-negative integer",
        )
        max_malformed = 0
    if not isinstance(min_parsed, int) or min_parsed < 1:
        issue(
            issues,
            "blocked",
            "min_parsed_invalid",
            "min_parsed_rows_per_jsonl must be a positive integer",
        )
        min_parsed = 1

    raw_cohorts = manifest.get("cohorts")
    cohort_reports: list[dict[str, Any]] = []
    cohort_names: set[str] = set()
    global_file_ids: set[str] = set()
    if not isinstance(raw_cohorts, list) or not raw_cohorts:
        issue(
            issues,
            "blocked",
            "cohorts_invalid",
            "cohorts must be a non-empty array",
        )
        raw_cohorts = []

    for index, raw_cohort in enumerate(raw_cohorts):
        if not isinstance(raw_cohort, dict):
            issue(
                issues,
                "blocked",
                "cohort_not_object",
                f"cohort {index} must be an object",
            )
            continue
        cohort_name = raw_cohort.get("name")
        if (
            not isinstance(cohort_name, str)
            or not cohort_name.strip()
            or cohort_name in cohort_names
        ):
            issue(
                issues,
                "blocked",
                "cohort_name_invalid",
                f"cohort {index} needs a unique non-empty name",
            )
            cohort_name = f"invalid-{index}"
        cohort_names.add(cohort_name)
        role = raw_cohort.get("role")
        if role not in ROLES:
            issue(
                issues,
                "blocked",
                "cohort_role_invalid",
                f"role must be one of {sorted(ROLES)}",
                cohort=cohort_name,
            )
            role = "single"
        runtime = validate_runtime(raw_cohort.get("runtime"), issues, cohort_name)
        files = raw_cohort.get("files")
        if not isinstance(files, list) or not files:
            issue(
                issues,
                "blocked",
                "cohort_files_invalid",
                "files must be a non-empty array",
                cohort=cohort_name,
            )
            files = []

        coverage_counters = {
            rule["name"]: {"eligible": 0, "covered": 0} for rule in rules
        }
        observed = {field: set() for field in IDENTITY_FIELDS}
        file_reports: list[dict[str, Any]] = []
        record_types: Counter[str] = Counter()
        malformed_rows = 0
        for entry in files:
            if isinstance(entry, dict) and isinstance(entry.get("id"), str):
                file_id = entry["id"]
                if file_id in global_file_ids:
                    issue(
                        issues,
                        "blocked",
                        "file_id_duplicate",
                        "file IDs must be unique across the audit",
                        cohort=cohort_name,
                        file_id=file_id,
                    )
                global_file_ids.add(file_id)
            file_report, file_observed = check_file(
                entry,
                manifest_path.parent,
                runtime,
                rules,
                coverage_counters,
                issues,
                cohort_name,
            )
            if file_report is None:
                continue
            file_reports.append(file_report)
            merge_observed(observed, file_observed)
            jsonl = file_report.get("jsonl")
            if isinstance(jsonl, dict):
                record_types.update(jsonl.get("record_types", {}))
                malformed_rows += int(jsonl.get("malformed_rows", 0))
                if int(jsonl.get("parsed_rows", 0)) < min_parsed:
                    issue(
                        issues,
                        "insufficient",
                        "jsonl_rows_insufficient",
                        f"parsed rows are below the declared minimum {min_parsed}",
                        cohort=cohort_name,
                        file_id=str(file_report.get("id")),
                    )
        if malformed_rows > max_malformed:
            issue(
                issues,
                "blocked",
                "malformed_rows_exceeded",
                f"malformed rows {malformed_rows} exceed limit {max_malformed}",
                cohort=cohort_name,
            )

        required_types = raw_cohort.get("required_record_types", [])
        if not isinstance(required_types, list) or not all(
            isinstance(value, str) and value for value in required_types
        ):
            issue(
                issues,
                "blocked",
                "required_record_types_invalid",
                "required_record_types must be an array of strings",
                cohort=cohort_name,
            )
            required_types = []
        for required in sorted(set(required_types)):
            if record_types.get(required, 0) == 0:
                issue(
                    issues,
                    "insufficient",
                    "required_record_type_missing",
                    f"required record type {required} is absent",
                    cohort=cohort_name,
                )

        coverage_report: list[dict[str, Any]] = []
        for rule in rules:
            counts = coverage_counters[rule["name"]]
            eligible = counts["eligible"]
            covered = counts["covered"]
            ratio = covered / eligible if eligible else 0.0
            passed = covered >= rule["min_records"] and ratio >= rule["min_ratio"]
            coverage_report.append(
                {
                    "name": rule["name"],
                    "eligible": eligible,
                    "covered": covered,
                    "ratio": ratio,
                    "min_ratio": rule["min_ratio"],
                    "min_records": rule["min_records"],
                    "passed": passed,
                }
            )
            if not passed:
                issue(
                    issues,
                    "insufficient",
                    "coverage_rule_failed",
                    f"coverage rule {rule['name']} failed",
                    cohort=cohort_name,
                )

        check_expected_identity(runtime, observed, issues, cohort_name)
        cohort_reports.append(
            {
                "name": cohort_name,
                "role": role,
                "runtime": runtime,
                "files": sorted(file_reports, key=lambda item: str(item.get("id", ""))),
                "record_types": dict(sorted(record_types.items())),
                "coverage": sorted(coverage_report, key=lambda item: item["name"]),
                "observed_identity": {
                    field: sorted(values)
                    for field, values in observed.items()
                    if values
                },
            }
        )

    comparison_report = check_comparison(
        cohort_reports, manifest.get("comparison"), issues
    )
    issues.sort(key=issue_sort_key)
    report = {
        "schema_version": 1,
        "audit_id": audit_id,
        "question": question,
        "status": status_from_issues(issues),
        "cohorts": sorted(cohort_reports, key=lambda item: item["name"]),
        "comparison": comparison_report,
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
        description="Initialize or preflight a native telemetry evidence manifest."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)
    init_parser = subparsers.add_parser("init", help="write a manifest template")
    init_parser.add_argument("--output", type=Path, required=True)
    init_parser.add_argument("--audit-id", required=True)
    check_parser = subparsers.add_parser("check", help="stream and verify evidence")
    check_parser.add_argument("--manifest", type=Path, required=True)
    check_parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if args.command == "init":
        if args.output.exists():
            print("refusing to overwrite an existing manifest", file=sys.stderr)
            return 3
        write_json(args.output, init_template(args.audit_id))
        print(f"created manifest template: {args.output}")
        return 0

    report = run_check(args.manifest)
    write_json(args.output, report)
    print(
        f"{report['status']} artifact_sha256={report['artifact_sha256']} "
        f"issues={len(report['issues'])}"
    )
    return EXIT_CODES[report["status"]]


if __name__ == "__main__":
    raise SystemExit(main())
