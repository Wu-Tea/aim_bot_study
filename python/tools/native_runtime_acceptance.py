#!/usr/bin/env python3
"""Score native-runtime acceptance evidence without turning missing data into success."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import platform
from pathlib import Path
from typing import Any


SECTION_KEYS = {
    "A": "configuration",
    "B": "vision",
    "C": "telemetry",
    "D": "scheduler",
    "E": "color",
    "F": "non_regression",
    "G": "pascal",
}


def percentile(values: list[float], quantile: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    rank = max(1, math.ceil(quantile * len(ordered)))
    return ordered[rank - 1]


def median_run(runs: list[dict[str, Any]], metric: str) -> dict[str, Any]:
    if not runs:
        raise ValueError("at least one run is required")
    return sorted(runs, key=lambda run: run[metric])[(len(runs) - 1) // 2]


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def score_acceptance(evidence: dict[str, Any]) -> dict[str, Any]:
    sections: dict[str, Any] = {}
    for letter, key in SECTION_KEYS.items():
        item = evidence.get(key)
        if item is None:
            sections[letter] = {"status": "UNVERIFIED", "reason": "evidence missing"}
        elif item.get("passed") is True:
            sections[letter] = {"status": "PASS", "reason": item.get("reason", "all recorded gates passed")}
        elif item.get("passed") is False:
            sections[letter] = {"status": "FAIL", "reason": item.get("reason", "one or more hard gates failed")}
        else:
            sections[letter] = {"status": "UNVERIFIED", "reason": item.get("reason", "evidence incomplete")}
    modern = [sections[letter]["status"] for letter in "ABCDEF"]
    if "FAIL" in modern:
        release = "FAIL"
    elif all(status == "PASS" for status in modern):
        release = "PASS"
    else:
        release = "UNVERIFIED"
    pascal_support = sections["G"]["status"] if release != "FAIL" else "UNVERIFIED"
    return {
        "schema_version": 1,
        "sections": sections,
        "release_decision": release,
        "gtx1060_support": pascal_support,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--engine", type=Path)
    args = parser.parse_args()
    evidence = json.loads(args.evidence.read_text(encoding="utf-8"))
    report = score_acceptance(evidence)
    report["host"] = {"system": platform.system(), "release": platform.release()}
    if args.binary and args.binary.exists():
        report["binary_sha256"] = sha256_file(args.binary)
    if args.engine and args.engine.exists():
        report["engine_sha256"] = sha256_file(args.engine)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True), encoding="utf-8")
    return 1 if report["release_decision"] == "FAIL" else 0


if __name__ == "__main__":
    raise SystemExit(main())
