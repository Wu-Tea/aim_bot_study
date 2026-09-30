"""Compare frozen per-scenario traces; missing cases/coverage fail acceptance."""
import argparse
import csv
import hashlib
import json
from pathlib import Path


def load(root):
    cases = {}
    for cohort in ("development", "validation"):
        path = root / f"state_machine_replay_{cohort}" / "traces.csv"
        with path.open(newline="", encoding="utf-8") as source:
            for row in csv.DictReader(source):
                key = tuple(row[k] for k in ("seed", "hz", "duration_ms", "scenario"))
                if key in cases:
                    raise ValueError(f"duplicate scenario: {key}")
                cases[key] = row
    if len(cases) != 192:
        raise ValueError(f"expected 192 scenarios, got {len(cases)}")
    for column in ("ads", "bodylock", "cue", "waiting"):
        if sum(int(row[column]) for row in cases.values()) <= 0:
            raise ValueError(f"missing trigger: {column}")
    return cases


def compare(baseline, candidate):
    return [dict(case=key, baseline=baseline.get(key), candidate=candidate.get(key))
            for key in sorted(baseline.keys() | candidate.keys())
            if baseline.get(key) != candidate.get(key)]


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    baseline, candidate = load(args.baseline), load(args.candidate)
    differences = compare(baseline, candidate)
    # Prove the comparator detects an output divergence with covariates fixed.
    control = {k: dict(v) for k, v in candidate.items()}
    key = next(iter(control))
    control[key]["hash"] += "-deliberately-corrupted"
    detected = bool(compare(candidate, control))
    fixture = Path(__file__).resolve().parents[3] / "native/controller_native/state_machine_replay_tests.cpp"
    report = dict(cases=len(candidate), ticks=sum(int(r["ticks"]) for r in candidate.values()),
                  different_cases=len(differences), differences=differences,
                  negative_control_detected=detected,
                  fixture_sha256=hashlib.sha256(fixture.read_bytes()).hexdigest())
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: v for k, v in report.items() if k != "differences"}))
    return 0 if not differences and detected else 1


if __name__ == "__main__":
    raise SystemExit(main())
