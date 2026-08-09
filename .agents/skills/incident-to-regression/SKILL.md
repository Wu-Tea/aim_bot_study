---
name: incident-to-regression
description: Convert native gameplay incidents, video symptoms, telemetry findings, and controller defects into reproducible RED fixtures, replay benchmarks, and explicit regression gates. Use when a real-world failure must become a known-bad proof with fixed covariates, trigger coverage, non-vacuous oracles, counterfactual controls, runtime/artifact hashes, and focused verification. Do not use to guess an unproven cause or directly change production behavior; use native-telemetry-audit first when the incident signature is not established.
---

# Incident to Regression

## Objective

Translate one observed incident into the smallest truthful regression package that fails on the known-bad runtime for the observed reason. Stop at a reviewable RED owner checkpoint; do not implement the production fix inside this skill.

Read `references/regression-contract.md` before choosing the fixture surface or writing the manifest.

## Establish the incident boundary

1. Read `.agent-context/handoff.md` and the decision governing the affected owner.
2. Read the source audit/report and verify its artifact hash when available.
3. Separate:

   - facts supported by telemetry, video, code, benchmark output, or an explicit dated user statement;
   - inferences and their supporting fact IDs;
   - unknowns that the regression must not silently assume.

4. State the visible or externally measurable failure without naming a presumed implementation cause.
5. Record the full known-bad executable SHA-256. A branch name or Git commit is not a runtime identity.

If the trigger cannot yet be isolated from the source evidence, stop and use `$native-telemetry-audit` rather than inventing a fixture.

## Choose the smallest faithful surface

- Use a unit test for a local invariant with no lifecycle or timing sequence.
- Use a native benchmark fixture for controller state transitions, freshness gaps, target lifecycle, input mixes, recoil, or bounded timing.
- Use replay when exact event provenance, a longer sequence, or recorded identities matter.
- Use integration or live A/B only when an offline surface cannot express the incident, and document why.

Reuse the repository's existing controller benchmark adapters, replay reader/metrics, JSON artifact conventions, CMake targets, and focused test patterns. Do not create a second controller owner or a test-only algorithm that bypasses production behavior.

## Create the regression contract

Initialize a manifest:

```powershell
python .agents/skills/incident-to-regression/scripts/regression_contract.py init `
  --output <regression-manifest.json> --incident-id <incident-id>
```

Fill every required covariate explicitly:

- fresh/non-fresh observation schedule;
- target generation and replacement/loss boundaries;
- credible target count;
- right-stick manual input, including proof of `M=0` when claimed;
- left-stick motion;
- recoil and firing state;
- ADS, BodyLock, or other controller mode;
- refresh/tick cadence;
- logging mode.

Use `not_applicable` only when the fixture demonstrably cannot exercise that dimension. Do not use defaults as evidence.

Define oracles in symptom language. For example, score same-direction speed loss and rapid rebound when that is the incident; do not replace them with overshoot or sign-flip assertions merely because those are easier to test.

## Prove the fixture is non-vacuous

Require all of the following:

1. At least one trigger assertion proving the incident path actually executed.
2. At least one quantitative or categorical oracle tied to evidence fact IDs.
3. At least one counterfactual or negative control that changes the trigger and changes the expected result.
4. Coverage of every relevant axis, lifecycle boundary, and input condition from the incident.
5. A report artifact containing measured values, not only `passed=true` or "the command ran."

Reject tests that can pass because the fixture never creates an observer pair, the target is never admitted, a field remains at its default, a condition is tautological, or only one axis is exercised when the defect is vector-valued.

## Establish RED on the known-bad runtime

Run the focused fixture against the exact known-bad runtime/source state. The regression must fail because at least one declared oracle fails while every trigger assertion passes.

Record:

- argv-form build/test commands;
- non-zero exit code;
- known-bad executable SHA-256;
- fixture report path and SHA-256;
- observed value for each failed oracle;
- source files and build target;
- exclusions and unverified live-only behavior.

Validate the RED contract:

```powershell
python .agents/skills/incident-to-regression/scripts/regression_contract.py check `
  --manifest <regression-manifest.json> --stage red `
  --output <regression-contract-report.json>
```

Interpret exit codes as `0=PASS`, `2=INCOMPLETE`, and `3=BLOCKED`. Do not change an oracle after seeing known-bad output unless new evidence shows the original oracle was wrong; record that correction.

## Stop at the owner checkpoint

Before production work, hand the user a compact RED package containing:

1. incident symptom and evidence classification;
2. known-bad runtime and source identity;
3. fixture surface and covariate matrix;
4. trigger coverage and counterfactuals;
5. oracle definitions and known-bad observed values;
6. exact reproduction command;
7. fixture/report hashes;
8. files added or changed;
9. remaining fidelity gaps;
10. explicit statement that production behavior is unchanged.

Only proceed to a fix when the user request separately authorizes implementation. Preserve the RED artifact and run it unchanged against the candidate.

## Verify after an authorized fix

After another workflow implements a fix, add a distinct green proof and validate with `--stage complete`. Require the same fixture/oracles, a different candidate runtime SHA-256, exit code zero, matched A/B invariants, focused tests, full diff review, and a hashed artifact manifest.

Do not call a package complete because a broad suite is green while the incident fixture was skipped or weakened.

## Resources

- `scripts/regression_contract.py`: deterministic manifest initializer and draft/RED/complete contract checker.
- `references/regression-contract.md`: fixture selection, manifest schema, authenticity checks, and project-specific covariates.
