# Incident-to-regression contract

## Contents

1. Required package
2. Fixture selection
3. Manifest example
4. Authenticity rules
5. RED and complete gates
6. Project-specific examples

## 1. Required package

Create a regression package with four independently reviewable layers:

- evidence: facts, inferences, unknowns, source artifact IDs, and locators;
- fixture: the smallest production-faithful state sequence plus explicit covariates;
- oracle: measurable failure thresholds tied to evidence facts;
- proof: hashed known-bad output, and later a distinct hashed candidate output.

Do not include raw personal telemetry or video in ordinary commits. Reference stable artifact IDs and SHA-256 values.

## 2. Fixture selection

| Surface | Use when | Reject when |
| --- | --- | --- |
| Unit test | One local invariant fully expresses the incident | Timing, lifecycle, or owner interaction is essential |
| Native benchmark | A bounded controller/selector/observer sequence expresses it | The benchmark bypasses production classes or hides trigger coverage |
| Replay | Exact recorded provenance or a long event sequence matters | A smaller synthetic fixture is equally faithful |
| Integration/live A/B | Offline execution cannot reproduce game behavior | It is being used only because the offline contract was not designed |

## 3. Manifest example

Use schema `1`.

```json
{
  "schema_version": 1,
  "incident_id": "fresh-clamp-rebound-m0",
  "title": "Pure-AI output loses speed and rebounds across a freshness gap",
  "symptom": "Final same-direction camera speed drops materially and recovers within two frames.",
  "evidence": {
    "facts": [
      {
        "id": "F1",
        "claim": "The source clip used no right-stick input.",
        "source": {
          "kind": "user_confirmed",
          "artifact_id": "clip-2026-08-07",
          "locator": "2026-08-07 statement"
        }
      }
    ],
    "inferences": [
      {
        "id": "I1",
        "claim": "The pulse is compatible with a fresh/non-fresh continuity defect.",
        "basis": ["F1"]
      }
    ],
    "unknowns": [
      {"id": "U1", "question": "Which controller tick produced each video pulse?"}
    ]
  },
  "known_bad": {
    "runtime_sha256": "<64 lowercase hex>",
    "source_commit": "<commit>",
    "audit_artifact_id": "audit-report",
    "audit_artifact_sha256": "<64 lowercase hex>"
  },
  "fixture": {
    "kind": "native_benchmark",
    "source_files": ["native/controller_native/example_fixture.cpp"],
    "build_target": "cod_native_example_benchmark",
    "run_command": ["path/to/benchmark.exe", "--output", "known-bad.json"],
    "trigger_assertions": [
      "at least one fresh clamp is followed by a non-fresh tick within 25 ms"
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
      "logging_mode": "disabled"
    }
  },
  "oracles": [
    {
      "id": "O1",
      "metric": "same_direction_speed_loss_pct",
      "operator": "<=",
      "threshold": 20.0,
      "unit": "percent",
      "evidence_fact_ids": ["F1"]
    }
  ],
  "counterfactuals": [
    {
      "id": "C1",
      "change": "remove the non-fresh gap",
      "expected": "the rebound trigger count becomes zero"
    }
  ],
  "red_proof": {
    "runtime_sha256": "<same known-bad SHA-256>",
    "command": ["path/to/benchmark.exe", "--output", "known-bad.json"],
    "exit_code": 1,
    "report": {
      "id": "known-bad-report",
      "path": "known-bad.json",
      "sha256": "<64 lowercase hex>"
    },
    "failed_oracles": [{"oracle_id": "O1", "observed": 41.0}]
  },
  "green_proof": null,
  "verification": null
}
```

Commands are argv arrays, not shell strings. The validator records file IDs and hashes without copying absolute paths into its deterministic output.

## 4. Authenticity rules

- Prove the trigger occurred; a failed result without trigger coverage is not the incident.
- Tie each oracle to one or more fact IDs.
- Use symptom metrics, not implementation fields that merely restate the proposed fix.
- Include at least one counterfactual that would expose a tautology or default-value pass.
- Make every relevant covariate explicit. `not_applicable` requires a structural reason.
- Exercise both axes for vector defects unless evidence limits the incident to one axis.
- Require a measured report artifact; stdout or exit code alone is insufficient.
- Keep the known-bad and candidate runtime identities distinct and fully hashed.
- Never update thresholds from candidate output merely to make GREEN.

## 5. RED and complete gates

`draft` requires evidence classification, fixture contract, covariates, oracles, and counterfactuals.

`red` additionally requires:

- existing fixture source files;
- build target and argv command;
- full known-bad runtime SHA-256;
- non-zero exit code;
- verified report hash;
- at least one failed declared oracle with an observed value.

`complete` additionally requires:

- a different candidate runtime SHA-256;
- the identical fixture and oracle contract returning zero;
- hashed candidate report;
- focused tests with zero exit codes;
- matched A/B identity/invariant record;
- full diff review and hashed artifact manifest.

## 6. Project-specific examples

- For fresh/non-fresh continuity, score clamp/rebound incidence, same-direction speed loss, recovery horizon, maximum tick delta, and reversal count. Keep target generation constant in the primary fixture and add lifecycle changes as boundaries.
- For pure-AI motion, assert right-stick manual input is zero. Do not use a mixed-input fixture as the only proof.
- For BodyLock or ADS, declare the mode and acquisition/lifecycle state; a fixture that never admits a target is vacuous.
- For W3/W4/W5 work, distinguish asynchronous observer availability, clock calibration, and shadow versus actuation authority.
- For selector incidents, use credible candidate count and target generation, not raw detector-box count alone.
- For recoil incidents, declare firing state and both fixed feed-forward and profile playback configuration.
