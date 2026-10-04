"""Frozen production-controller matrix; every report has identical paired inputs.

Synthetic plant values are assumptions. This runner does not infer game AA from
the telemetry and produces measurements without a baseline acceptance decision.
"""
import argparse
import concurrent.futures
import json
from pathlib import Path
import subprocess
import time


def scenarios(holdout=False):
    seeds = [20260926, 8675309] if holdout else [1337, 424242]
    result = []
    for profile in ["pure", "mixed", "scripted", "wrong-then-correct"]:
        for kind in ["standard", "slow", "delayed", "long"]:
            args = ["--duration-ms", "60000" if kind == "long" else "10000",
                    "--profile", profile, "--cohort", "both", "--target-slot-ms", "1575"]
            for seed in seeds:
                args += ["--seed", str(seed)]
            if kind in ["slow", "delayed"]:
                args += ["--slowdown-edge", "0.35", "--slowdown-center", "0.20"]
            if kind == "delayed":
                args += ["--vision-hz", "100", "--vision-result-delay-ms", "8",
                         "--control-response-delay-ms", "9"]
            result.append({"id": f"{profile}_{kind}", "args": args})
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--holdout", action="store_true")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    plan = scenarios(args.holdout)
    identity = {"matrix": plan, "plant_source": "assumption"}
    identity_path = args.output / "identity.json"
    if identity_path.exists():
        raise SystemExit("refusing to overwrite matrix")
    identity_path.write_text(json.dumps(identity, indent=2), encoding="utf-8")

    def run(case):
        output = args.output / (case["id"] + ".json")
        if output.exists():
            raise RuntimeError(f"output exists: {output}")
        command = [str(args.exe.resolve()), "--config", str(args.config.resolve()),
                   "--output", str(output.resolve())] + case["args"]
        started = time.perf_counter()
        completed = subprocess.run(command, capture_output=True, text=True, errors="replace")
        (args.output / (case["id"] + ".log")).write_text(
            completed.stdout + completed.stderr, encoding="utf-8")
        print(case["id"], completed.returncode, flush=True)
        return {"id": case["id"], "command": command, "exit_code": completed.returncode,
                "seconds": time.perf_counter() - started}

    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(run, plan))
    (args.output / "execution.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    return int(any(r["exit_code"] for r in results))


if __name__ == "__main__":
    raise SystemExit(main())
