# Single-target bodylock handoff and telemetry design

## Purpose

This work addresses the live defect visible around seconds 3–4 of `Call of Duty  Black Ops 7 2026.07.13 - 11.42.57.13.DVR.mp4`: one human target is visible, the user pushes toward it, but bodylock resists strongly enough to leave the reticle stalled beside the target.

The work must establish a deterministic failing benchmark before changing controller behavior. It must also distinguish production target association from telemetry-only identity, expose every controller stage that can suppress manual input, and determine whether the collected data is suitable for a basic user profile and ADS transition model.

## Confirmed evidence

In the aligned live window, manual X reached approximately `-0.35` while AI X reached approximately `+0.38`, leaving pre-recoil X near `+0.03`. The obstruction therefore exists before recoil. During the same interval, the telemetry identity changed `335 -> 336 -> 337`, `has_target` toggled, and the controller moved between `manual`, `ads_snap`, and `body_lock`.

The video shows only one visible human target. Multiple telemetry IDs must not be interpreted as simultaneous targets. Until production association IDs are logged separately, the identity churn is evidence of an unstable observation identity, not proof that the production tracker created multiple targets.

## Scope and order

The implementation is divided into four ordered gates:

1. Reproduce and quantify the defect without changing controller behavior.
2. Add missing identity and controller-stage evidence, then repeat the live/benchmark analysis.
3. Fix the confirmed source of resistance with explicit authority and manual-takeover rules.
4. Validate user-profile and ADS-transition model readiness without enabling automatic live adaptation.

No controller tuning is accepted before Gate 1 produces a failing baseline.

## Gate 1: deterministic benchmarks

### Primary scenario: single visible target with association churn

Add a native benchmark scenario named `single_visible_target_identity_churn_manual_takeover`.

The scene contains exactly one valid human target. It progresses through:

1. No live target after the previous engagement.
2. One new target appearing from partial occlusion.
3. Body-box geometry changing as the target becomes visible.
4. One or two latest-only frame gaps or weak observations.
5. The user committing a sustained stick input toward the visible target.
6. A temporary identity/authority disturbance matching the live sequence.
7. Stable reacquisition of the same logical target.

The manual input ramp and opposing assist should reproduce the observed range: manual X grows from about `-0.14` to `-0.38`, while stale or unstable bodylock may attempt `+0.12` to `+0.43` in the current implementation.

### Required controls

The benchmark must include:

- a stable single-target control with the same user input;
- a single-target partial-occlusion case with no user takeover intent;
- a true two-target switch case, kept separate from the reference-video reproduction;
- a manual-only baseline for measuring extra switch latency introduced by assistance.

### Metrics

Measure before recoil:

- `manual_direction_preservation_ratio`: integral of output projected onto manual intent divided by integral of requested manual input;
- `manual_reversal_frames`: frames where manual and pre-recoil output have a negative dot product;
- `manual_stall_ms`: time with manual magnitude at least `0.25` and projected output at most `0.05`;
- `manual_takeover_latency_ms`: time until projected output reaches at least 50% of manual input for three consecutive samples;
- `old_target_resistance_integral`: opposing AI output after switch intent is established;
- `logical_identity_recreations`: new logical identities while the scene contains one target;
- `target_reacquire_latency_ms`;
- `controller_mode_transitions` during the 500 ms handoff window;
- target/authority toggles during the handoff.

The current build is expected to fail clearly. Initial post-fix acceptance thresholds are:

- preservation ratio at least `0.75`, with a target of `0.90`;
- no continuous reversal longer than `20 ms`;
- cumulative stall at most `40 ms` and no single stall longer than `20 ms`;
- takeover latency p95 at most `60 ms`;
- no logical identity recreation for a continuously associated single target;
- no strong bodylock authority during ambiguous identity evidence;
- at most the two intentional mode transitions `body_lock -> manual takeover -> body_lock`.

Thresholds must be compared against the stable single-target and manual-only controls. A fix that merely disables bodylock fails the control scenarios.

## Gate 2: evidence completeness

### Identity provenance

Log separate fields for:

- detector box count;
- selector candidate count and selected-candidate key;
- production association/tracker ID and its quality/source;
- telemetry observation identity ID;
- controller-consumed target ID;
- live, projected, ambiguous, weak, and explicit-switch evidence;
- body-box geometry and selected aim point.

This separation determines whether churn originates in detection, selector, production association, telemetry identity, or controller consumption.

### Controller stages

Persist the existing component snapshots at the configured telemetry sampling rate:

- manual;
- post-AI;
- AI delta;
- post-dynamics and dynamics delta;
- post-near-target-brake and brake delta;
- post-carry-brake and carry-brake delta;
- pre-recoil;
- recoil delta;
- final.

Also log bodylock confidence, manual arbitration classification, manual-takeover state, carry-brake reason, and whether output limiting was active. These fields must be type-specific and must not restore the previous JSON bloat.

## Gate 3: behavioral correction

The precise implementation is selected only after Gate 1 and Gate 2 identify the responsible stage. The intended policy boundary is nevertheless fixed:

- uncertain or rapidly changing identity evidence cannot retain strong bodylock authority;
- sustained, directional manual intent must gain takeover authority within a bounded time;
- small tracking input must not be treated as noise merely because it is orthogonal to the previous AI plan;
- target-following assistance and target-switch intent must be distinguished using time, direction, target evidence, and candidate geometry rather than a single stick threshold;
- carry braking must use relative target motion and may not turn same-direction cooperative tracking into a stall;
- reacquiring the same logical target after a brief gap should restore continuity without reviving resistance toward a departed target.

Each behavior change requires a focused failing unit or benchmark test before implementation. Stable bodylock tracking, short harmless stick noise, occlusion continuity, low-target recovery, and recoil boundaries remain regression gates.

## Gate 4: data-model readiness

### User profile

The current logs can support an offline, confidence-scored baseline profile containing stick center/drift, input magnitude distributions, ramp rate, reversal timing, micro-correction behavior, takeover commitment time, and manual/AI conflict response.

The first model runs in shadow mode only. It may emit predictions and suggested parameters but must not alter live controller output. Automatic adaptation requires stable context keys, held-out validation, minimum sample counts, bounded parameter ranges, and rollback to static defaults.

Required additions before reliable personalization are the controller-stage fields above, production identity provenance, candidate context, weapon/optic/FOV/sensitivity keys, and explicit separation of target-following from target-switch episodes.

### ADS transition model

The current recording contains 193 ADS transition events, 38 valid and complete conditional events, and no calibration-clean events. This proves collection now functions, but it is not yet sufficient for a tracker calibration. Per-event scales contain large outliers and only about 15 events are approximately free of manual, AI, and recoil contamination.

The model should fit robust conditional mappings:

`ads_dx = scale_x * hipfire_dx + offset_x`

`ads_dy = scale_y * hipfire_dy + offset_y`

Group by weapon, optic, FOV/resolution, and a target-size/distance proxy. Record ADS press time, settled time, transition duration, hipfire/ADS box geometry, and contamination integrals. Do not publish a tracker calibration until either 50 clean events or 150 high-quality conditional events exist, cross-validated residual p95 is at most 5 px, scale confidence-interval width is at most 0.03, offset confidence-interval width is at most 2 px, and independent recordings agree within 5%.

## Verification

Before completion:

1. Run the new benchmark on the pre-fix behavior and retain the failing artifact.
2. Demonstrate which identity and controller stage produces the resistance.
3. Run focused unit tests for the selected fix.
4. Run all native controller, target, telemetry, benchmark-metrics, and recoil-contract tests.
5. Run `scripts/verify/native_pipeline_contract.bat`.
6. Run the primary scenario and all control scenarios, comparing artifacts before and after.
7. Use a new live recording only as final confirmation, not as the first proof of correctness.

## Non-goals

- Do not train or replace YOLO as part of this change.
- Do not weaken bodylock globally to make the defect metric disappear.
- Do not enable live automatic user adaptation.
- Do not write ADS calibration values into tracker configuration before the evidence thresholds pass.
- Do not treat telemetry observation IDs as production tracker IDs.
