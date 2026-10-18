"""Per-case numerical comparison, not a release/acceptance score."""
import csv
import json
from pathlib import Path
from statistics import mean

ROOT = Path(__file__).resolve().parents[2] / "runs/sustaining-motion-20261018"
DEVELOPMENT = {2026101853, 32452909}
VALIDATION = {49979731, 104395409}


def rows(version, suite, pattern, seeds):
    result = []
    for seed in sorted(seeds):
        for path in (ROOT / version / suite / str(seed)).glob(pattern):
            result.extend(csv.DictReader(path.open(encoding="utf-8")))
    return result


summary = {}
comparisons = []
for label, seeds in (("development", DEVELOPMENT), ("independent", VALIDATION)):
    groups = []
    for suite, pattern, metric in (
        ("noise", "seed-*.csv", "mean_late_error"),
        ("noise", "focused-*.csv", "late_mean"),
        ("chain", "seed-*.csv", "mean_late_error"),
        ("focused", "weak-focused-*.csv", "late_mean"),
        ("focused", "ideal-*.csv", "late_mean"),
    ):
        before = rows("before-v3" if suite == "focused" else "before", suite, pattern, seeds)
        after = rows("after-v3" if suite == "focused" else "after", suite, pattern, seeds)
        key_fields = ("seed", "case", "duration_ms") if metric == "mean_late_error" else (
            ("seed", "kind", "nonlinear", "duration") if "focused" in pattern else
            ("seed", "curve", "case", "duration"))
        key = lambda row: tuple(row[k] for k in key_fields)
        old = {key(r): r for r in before}
        assert len(old) == len(before)
        assert len(after) == len(before) and {key(r) for r in after} == set(old), (label, suite, pattern)
        partition = {}
        for r in after:
            b = old[key(r)]
            kind = r.get("kind", "over_budget" if r["case"] == "11" else "feasible") if "kind" not in r else r["kind"]
            evidence = r.get("enemy_evidence", r.get("nonlinear", r.get("curve")))
            group = (kind, evidence)
            partition.setdefault(group, []).append((b, r))
            comparisons.append(dict(phase=label, suite=suite, file=pattern, key="/".join(key(r)),
                                    kind=kind, evidence=evidence, before=float(b[metric]),
                                    after=float(r[metric]), delta=float(r[metric])-float(b[metric])))
        for (kind, evidence), pairs in partition.items():
            groups.append(dict(suite=suite, file=pattern, kind=kind, evidence=evidence,
                               count=len(pairs), before=mean(float(b[metric]) for b,r in pairs),
                               after=mean(float(r[metric]) for b,r in pairs),
                               increased=sum(float(r[metric])>float(b[metric])+1e-4 for b,r in pairs)))
    summary[label] = groups

(ROOT / "comparison.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
with (ROOT / "comparison.csv").open("w", newline="", encoding="utf-8") as file:
    writer = csv.DictWriter(file, fieldnames=list(comparisons[0]))
    writer.writeheader()
    writer.writerows(comparisons)
print(json.dumps(summary, indent=2))
