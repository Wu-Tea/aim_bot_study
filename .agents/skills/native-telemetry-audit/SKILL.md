---
name: native-telemetry-audit
description: Audit native C++ runtime evidence from telemetry JSONL, runtime.performance JSONL, session manifests, NVIDIA CSV, and optional video or notes. Use when Codex must reconstruct a native session, compare baseline and candidate runs, segment ADS/BodyLock or refresh-rate cohorts, validate runtime/schema/logging/clock identity, calculate coverage and latency/cadence metrics, or issue PASS, BLOCKED, or INSUFFICIENT_EVIDENCE. Do not use to change production control code or to turn an accepted incident into a test fixture; use incident-to-regression for the latter.
---

# Native Telemetry Audit

## Objective

Turn native runtime artifacts into a reproducible evidence report. Establish identity and comparability before calculating metrics, preserve exclusions and uncertainty, and fail closed when the evidence contract is not met.

Treat this skill as read-only unless the user explicitly asks to add an analysis script or documentation. Never start the runtime, change config, collect a new live run, delete logs, or modify production code merely to complete an audit.

## Start from project context

1. Read `.agent-context/handoff.md` when present.
2. Read only the decisions and telemetry documents relevant to the audit question.
3. Locate the session manifest and source artifacts without scanning unrelated raw runs.
4. Keep raw telemetry, video, hardware logs, binaries, and personal paths out of ordinary source commits and `.agent-context/`.

Use `references/native-telemetry-contract.md` for the project-specific source hierarchy, clock rules, join identities, cohort rules, comparability gates, and report schema.

## Choose the evidence mode

- Use `runtime.performance` JSONL for normal throughput, cadence, queue-pressure, and latency A/B work.
- Use detailed runtime telemetry JSONL only for a bounded causal diagnosis requiring controller, target, lifecycle, or per-event joins.
- Use video as independent symptom evidence. Do not infer the responsible controller tick or field without a valid time/provenance bridge.
- Record no-log user observations as user-confirmed runtime evidence. Do not merge them numerically with telemetry-on measurements.
- Treat NVIDIA/HWiNFO CSV as sampled hardware evidence, not wall power or sub-sample transient proof.

## Build and check the evidence manifest

Create one manifest per audit. Give every source a stable `id`; paths are inputs and must not appear in the deterministic report.

```powershell
python .agents/skills/native-telemetry-audit/scripts/audit_evidence.py init `
  --output <audit-manifest.json> --audit-id <audit-id>
```

Fill the manifest with the audit question, cohort identities, runtime SHA-256, schema, config/engine hashes when available, logging mode, hardware profile, refresh rate, source files, required record types, coverage rules, and comparison invariants.

Run the streaming preflight before any metric script:

```powershell
python .agents/skills/native-telemetry-audit/scripts/audit_evidence.py check `
  --manifest <audit-manifest.json> --output <audit-intake.json>
```

The checker hashes files in chunks, parses JSONL one record at a time, inventories record types and schemas, verifies observed identities, scores declared coverage rules, and emits a canonical artifact hash. Do not weaken a gate merely to obtain PASS.

Interpret its exit codes as follows:

- `0`: preflight `PASS`; proceed with the declared analysis.
- `2`: `INSUFFICIENT_EVIDENCE`; report what is missing or under-covered.
- `3`: `BLOCKED`; stop because identity, integrity, or comparability is contradicted.

## Segment before aggregating

Define scenario boundaries before viewing headline metrics. Prefer lifecycle IDs and timestamps over visual clock estimates; document any manual boundary.

At minimum, separate relevant dimensions such as:

- ADS versus BodyLock versus idle;
- active aiming versus idle capture;
- fresh versus non-fresh observations;
- target generation, replacement, loss, and reacquisition;
- single-target versus multi-target evidence;
- manual-right-stick, pure-AI `M=0`, left-stick, recoil/firing, and no-input controls;
- game refresh-rate or config changes;
- detailed telemetry, performance summary, and logging-disabled runs.

Never use one global mean when the workload or logging mode changes inside a session.

## Join and calculate

1. State the authoritative identity for every join.
2. Reject row-index, nearest-line, or uncalibrated cross-clock joins.
3. Report total, parsed, malformed, excluded, eligible, joined, and unjoined counts.
4. Report cohort coverage and join coverage beside each metric.
5. Calculate count, mean when meaningful, P50, P95, P99, and max for latency distributions. Preserve the exact percentile convention.
6. Use rates from elapsed time and eligible event counts; never divide by a span that crosses excluded gaps without saying so.
7. Use existing purpose-built tools after preflight when they match the question:

   - `tools/compare_runtime_perf_summary.py` for matched performance-summary A/B;
   - native replay and benchmark targets for replay metrics;
   - a bounded, checked-in analysis script for a repeated detailed-telemetry question.

Do not silently repair malformed input, reinterpret renamed fields across schemas, or collapse unavailable values to zero.

## Decide the audit outcome

Use exactly one top-level outcome:

- `PASS`: the declared evidence contract is satisfied and every claimed gate passes.
- `BLOCKED`: evidence contradicts runtime identity, schema, logging mode, clock validity, file integrity, or an A/B invariant.
- `INSUFFICIENT_EVIDENCE`: inputs are internally valid but cannot answer the question with the required coverage or provenance.

A metric regression may still be a valid audit result; distinguish an audit that executed correctly from a candidate that failed its product gate.

## Write the report

Lead with the question, outcome, and the most important reason. Then include:

1. source manifest and hashes;
2. runtime/config/engine/schema/logging identity;
3. cohort and exclusion definitions;
4. integrity, record-type, clock, and join coverage;
5. metrics with denominators and percentile convention;
6. matched A/B invariants and allowed differences;
7. facts, user-confirmed observations, inferences, and unknowns in separate sections;
8. reproduction commands and generated artifact hashes;
9. explicit next evidence needed when blocked or insufficient.

Do not claim a root cause from correlation, a summary-only run, a video-only pulse, or a telemetry-heavy versus telemetry-off comparison.

## Hand off a confirmed incident

When the audit establishes a reproducible incident signature, pass the report path, artifact hash, known-bad runtime identity, cohort definition, trigger signature, and unresolved covariates to `$incident-to-regression`. Do not design the production fix inside this skill.

## Resources

- `scripts/audit_evidence.py`: deterministic manifest initializer and streaming evidence preflight.
- `references/native-telemetry-contract.md`: project-specific evidence, join, clock, comparability, and report contract.
