"""Apply the existing protected-metric policy to every frozen matrix pair."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    args = parser.parse_args()
    before = json.loads((args.baseline / "identity.json").read_text())
    after = json.loads((args.candidate / "identity.json").read_text())
    for key in ["config_sha256", "matrix", "plant_source"]:
        if before[key] != after[key]:
            raise SystemExit(f"INVALID / NON-COMPARABLE: {key}")
    for directory, identity in [(args.baseline, before), (args.candidate, after)]:
        execution = json.loads((directory / "execution.json").read_text())
        expected = {case["id"] for case in identity["matrix"]}
        if len(execution) != len(expected) or {case["id"] for case in execution} != expected:
            raise SystemExit("INVALID / NON-COMPARABLE: incomplete execution")
        for case in execution:
            report = directory / (case["id"] + ".json")
            if case["exit_code"] != 0 or not report.is_file() or hashlib.sha256(
                    report.read_bytes()).hexdigest() != case["report_sha256"]:
                raise SystemExit(f"INVALID / NON-COMPARABLE: execution/hash {report}")

    jobs = [{"id": case["id"],
             "baseline": str((args.baseline / (case["id"] + ".json")).resolve()),
             "candidate": str((args.candidate / (case["id"] + ".json")).resolve())}
            for case in before["matrix"]]
    job_path = args.candidate / "comparison-jobs.json"
    job_path.write_text(json.dumps(jobs, indent=2), encoding="utf-8")
    comparator = Path(__file__).resolve().parents[3] / "scripts/verify/compare_oscillation_matrix.ps1"
    command = ["pwsh", "-NoProfile", "-File", str(comparator),
               "-Jobs", str(job_path.resolve())]
    started = time.perf_counter()
    batch = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if batch.returncode:
        raise SystemExit(f"INVALID / NON-COMPARABLE: comparator host failed: {batch.stderr}")
    results = json.loads(batch.stdout)
    if [r["id"] for r in results] != [r["id"] for r in jobs]:
        raise SystemExit("INVALID / NON-COMPARABLE: incomplete comparison")
    for result in results:
        name = result["id"]
        log = result.pop("log")
        (args.candidate / (name + ".comparison.log")).write_text(log, encoding="utf-8")
        result["regressions"] = [s for s in log.splitlines() if "[PROTECTED-REGRESSION]" in s]
    failed = sum(r["exit_code"] != 0 for r in results)
    report = {"status": "FAILED CONSTRAINTS" if failed else "EXPLORATORY / MISSING COVERAGE",
              "matrix_constraints_pass": failed == 0,
              "note": "Matrix comparison only. Product and incident gates must also pass before BENCHMARK-ELIGIBLE.",
              "pairs": len(results), "failed_pairs": failed, "results": results,
              "seconds": time.perf_counter() - started, "host_command": command}
    (args.candidate / "comparison.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(report["status"], "failed_pairs=", failed)
    return int(failed != 0)


if __name__ == "__main__":
    raise SystemExit(main())
