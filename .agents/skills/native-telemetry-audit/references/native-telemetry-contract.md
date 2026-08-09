# Native telemetry evidence contract

## Contents

1. Evidence hierarchy
2. Manifest fields
3. Identity and integrity gates
4. Clock and join rules
5. Cohort and metric rules
6. A/B comparability
7. Report contract

## 1. Evidence hierarchy

Use the narrowest source that can answer the question:

| Question | Primary source | Important boundary |
| --- | --- | --- |
| Throughput, latency, queue pressure | `runtime.performance` JSONL | Keep detailed telemetry disabled for matched normal-play A/B |
| Controller or lifecycle causality | detailed telemetry JSONL | Bound the capture and require schema/provenance joins |
| Runtime/config/model identity | `session.json`, `session_metadata` | Executable SHA-256 is the live runtime identity |
| GPU utilization and board power | matched NVIDIA/HWiNFO CSV | Not wall power; sampling can miss transients |
| Visible smoothness or motion | video plus analysis manifest | Does not identify a controller branch without a time bridge |
| Subjective behavior with logging off | dated user-confirmed note | Do not merge as a telemetry-on numerical cohort |

## 2. Manifest fields

Use manifest schema `1`.

```json
{
  "schema_version": 1,
  "audit_id": "2026-08-08-example",
  "question": "Does the candidate improve active Vision cadence without worse tail latency?",
  "cohorts": [
    {
      "name": "baseline",
      "role": "baseline",
      "runtime": {
        "executable_sha256": "<64 lowercase hex>",
        "git_commit": "<commit or unknown>",
        "config_hash": "<64 lowercase hex or unknown>",
        "engine_hash": "<64 lowercase hex or unknown>",
        "telemetry_schema": 13,
        "logging_mode": "performance_summary",
        "hardware_profile": "shared-4070s",
        "game_refresh_hz": 180
      },
      "required_record_types": ["runtime_perf_summary"],
      "files": [
        {
          "id": "baseline-summary",
          "kind": "performance_jsonl",
          "path": "relative/or/absolute/input.jsonl",
          "expected_schema_version": 1
        }
      ]
    }
  ],
  "coverage_rules": [
    {
      "name": "active-windows",
      "record_types": ["runtime_perf_summary"],
      "field": "aiming_ratio",
      "min_ratio": 1.0,
      "min_records": 1
    }
  ],
  "comparison": {
    "required_equal": [
      "logging_mode",
      "hardware_profile",
      "game_refresh_hz",
      "telemetry_schema"
    ],
    "allowed_differences": ["executable_sha256", "git_commit", "config_hash"]
  },
  "limits": {"max_malformed_rows": 0, "min_parsed_rows_per_jsonl": 1}
}
```

Allowed file kinds are `session_manifest`, `telemetry_jsonl`, `performance_jsonl`, `nvidia_csv`, `video`, `notes`, `binary`, `config`, and `other`. Use stable file IDs; the preflight output omits input paths.

Allowed logging modes are `detailed`, `performance_summary`, `disabled`, and `mixed`. Treat `mixed` as non-comparable unless the audit explicitly segments it.

## 3. Identity and integrity gates

- Require the full executable SHA-256 for a live-runtime claim. A Git commit alone is insufficient when a validated binary predates a protection commit.
- Compare `session.json` and telemetry `session_metadata` against the declared executable, config, engine, commit, and schema.
- Hash every input artifact in chunks and report size plus SHA-256.
- Count blank, parsed, malformed, and non-object JSONL records. Do not discard malformed rows silently.
- Stop with `BLOCKED` for a missing file, hash mismatch, contradicted identity, unexpected schema, duplicate file ID, or violated comparison invariant.
- Use `INSUFFICIENT_EVIDENCE` for missing required record types, unknown required identity, or inadequate declared coverage.
- Treat an active/unclean session state as a limitation on duration claims, not automatic invalidation of all timestamped rows.

## 4. Clock and join rules

- Join controller samples with `tick_id` or `sample_seq` only when the producing schema defines the key for both sides.
- Join Vision provenance with source frame, source observation, persistent target, physical ADS epoch, target acquisition, and controller tick identities as required by the question.
- Keep selector target generation distinct from detector/tracker IDs.
- Use calibrated `source_present_steady_ns` only when `source_present_steady_available` is true and calibration uncertainty is acceptable.
- Do not subtract raw DXGI QPC from steady-clock timestamps.
- Do not use nearest row or line number as identity.
- Report duplicate, out-of-order, unmatched, and excluded records explicitly.

## 5. Cohort and metric rules

- Freeze the cohort definition before reading the candidate headline.
- Keep active and idle Vision accumulation pressure separate.
- Segment ADS and BodyLock, fresh and non-fresh, target generation, target count, manual input, recoil/firing, and refresh rate when they affect the hypothesis.
- For pure-AI evidence, prove right-stick manual input is zero in the declared interval; do not infer it from absent comments.
- Report denominators and coverage next to all rates and percentiles.
- Use one documented percentile convention. The project commonly reports P50/P95 and sometimes P99/max.
- Preserve unavailable data as unavailable. Zero is a real value, not a missing-data marker.

## 6. A/B comparability

Require equality for all declared invariants. Usually include scenario, logging mode, telemetry schema or explicit schema mapping, hardware profile, game refresh rate, model/engine, capture/tensor shape, and background workload.

List the intended changed variable. More than one uncontrolled difference makes causal attribution insufficient even when both cohorts are individually valid.

Never compare a telemetry-heavy run with a performance-summary or telemetry-disabled run as if logging conditions matched.

## 7. Report contract

Return `PASS`, `BLOCKED`, or `INSUFFICIENT_EVIDENCE` for audit validity. Separately state whether the candidate passed the product metric gate.

Every numerical claim must carry:

- cohort and segment;
- source artifact ID and hash;
- eligible and excluded counts;
- join or field coverage;
- unit and percentile convention;
- baseline/candidate invariant status when comparative.

Separate repository evidence, user-confirmed observations, inference, and unknowns. End with the smallest additional evidence needed, not a speculative production fix.
