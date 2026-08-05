# Subagent Brief: W3 Live Ego-Motion Quality Gate

Created: 2026-08-03T18:59:21+08:00
Expires: after the W3 audit is accepted/rejected or the telemetry/runtime baseline changes
Owner session: primary Codex task `019fb9a8-e636-7ee0-9a2a-b88a0c3c519e`

## Mission

Audit the existing W3 latest-only background ego-motion shadow against live
session `runs/native_perf/sessions/20260802T181639Z_53916_1` and produce a
reproducible quality gate for entering W4 response identification. This mission
is evidence collection and analysis only; it must not promote ego motion,
PendingMotion, rollout, or any memory result into actuation.

## Role

explorer and light skeptic

## Required Context

- The protected production baseline is source commit `5d2f3be`, installed
  executable SHA-256
  `872F6FFFD1598C64ABC34E0D551F4C112B40FEAC630844C6CB0B0FEE50558C38`.
- W0-W2 are implemented/tested. W3 is latest-only shadow telemetry and has no
  output authority. W4 real ViGEm-to-background response identification is not
  complete. W5 CausalMotionLedger/Causal Remaining v2 is not implemented.
- The session covers approximately 29 minutes 57 seconds and ended with a stale
  `state=active` plus one truncated JSONL line. Its executable/config identity
  remains usable.
- Naive row counts are unsafe: `controller_sample` contains semantically
  identical duplicate telemetry rows and some file-order reordering. De-duplicate
  by stable event/sample identity before cohort statistics. Actual
  `delivered_control_sample.applied_at_ns` timestamps were unique in the primary
  audit; verify rather than assume.
- Latest live evidence says AI magnitude is sufficient and target-loss carry is
  short. Do not reopen global gain or occlusion tuning in this mission.
- Most active samples used strong left-stick movement, so movement-versus-static
  causal claims may be underpowered. Report cohort sizes and confounding.

## Relevant Decisions

- `.agent-context/decisions/DEC-2026-08-03-001-protect-live-baseline-defer-w5.md`
- `.agent-context/decisions/DEC-2026-08-03-002-target-count-aware-manual-exit-authority.md`
- `.agent-context/decisions/DEC-2026-08-02-002-predictive-manual-ai-control-envelope.md`
- `.agent-context/decisions/DEC-2026-07-29-002-tracker-remaining-work-contract.md`

## Files To Read

1. `.agent-context/handoff.md`
2. `docs/project/CURRENT_STATE.md`
3. `docs/project/LIVE_ACCEPTED_RUNTIME_20260803.md`
4. `native/vision_native/src/ego_motion_observer.cpp`
5. `native/vision_native/include/vision_native/ego_motion_observer.h`
6. `native/runtime_app/runtime_loop.cpp` only around ego-shadow production,
   source timestamps, causal journal publication and controller consumption
7. `native/runtime_app/telemetry_schema.h`
8. `native/runtime_app/telemetry_collectors.*`
9. `native/runtime_app/runtime_telemetry.cpp` only for relevant serializers
10. `runs/native_perf/sessions/20260802T181639Z_53916_1/session.json` and its
    JSONL shards through a streaming parser

## Files Not To Read Unless Needed

- Historical video-review artifacts and personal video directories
- Far-target/head-peek micro-aim benchmarks
- Dynamic ROI work
- Recoil/OCR/profile code
- Archived pre-August control history
- Selector/fuser implementation beyond fields required to interpret a logged
  cohort

## Constraints

- Preserve every existing dirty/untracked change. No reset, checkout, clean,
  broad formatting or unrelated edits.
- Do not edit production C++ or config in this mission. A small reproducible
  analysis script plus JSON/Markdown report may be added under
  `artifacts/benchmarks/causal-aiming-state-20260802/w3-live-audit-20260803/`.
- Do not build, deploy, overwrite, launch or stop the user runtime. Do not stage
  or commit. Do not edit `.agent-context`.
- Parse the 1.4 GB telemetry in streaming form. Do not create a second raw copy
  or check raw records into artifacts.
- Use explicit IDs and clock-domain contracts. If ego frames cannot be joined
  to delivered/controller events without guessing, return
  `INSUFFICIENT_EVIDENCE` for that portion and name the missing key/clock.
- Separate logger duplication/reordering from actual control delivery. Do not
  infer duplicate ViGEm output from duplicate diagnostic rows.
- Treat W3 quality, W4 feasibility and production control authority as three
  different claims. An exploratory correlation is not W4 completion.
- Do not change AI/BodyLock/ADS gain, manual arbitration, target ownership,
  activation radius or the 135/220 ms ADS lifecycle.

## Expected Output

Create a concise reproducible audit containing all of the following:

1. **Provenance and parser integrity**
   - session/build/config/engine/schema identities;
   - shard coverage, invalid-line count and time span;
   - raw versus de-duplicated counts for ego, controller and delivered samples;
   - ordering and join policy, including maximum accepted join age.
2. **W3 cost and availability**
   - available/valid rate and invalid-reason counts;
   - compute time P50/P95/P99/max;
   - cadence, gaps, repeated sequence/frame IDs and age/staleness distribution;
   - evidence of backlog or latest-only violations.
3. **W3 signal quality**
   - confidence, valid-background ratio, residual and inlier/sample-count
     distributions;
   - X/Y displacement distribution, zero rate and configured-bound saturation
     rate (explicitly inspect the apparent `+/-8 px` boundary);
   - coordinate/sign invariant checks between background and camera displacement;
   - split-half and time-slice stability.
4. **Required cohorts**
   - no-target/manual, ADS Snap and BodyLock;
   - observed versus briefly unobserved target;
   - left-stick idle/move/strong and right-stick idle/micro/mid/strong;
   - near/far only if a repository-defined, frame-aligned definition exists.
     Otherwise identify the missing contract instead of inventing a threshold.
5. **W4 feasibility probe, still shadow-only**
   - determine whether delivered `final_right` can be joined causally to later
     background/camera displacement in a common clock domain;
   - if valid, report exploratory cross-correlation peak/sign and stability by
     ADS/BodyLock and split half, including sample counts and uncertainty;
   - if invalid, list the exact telemetry fields or source-present contract W4
     needs. Do not label an exploratory peak as a calibrated response curve.
6. **Decision packet**
   - one verdict: `PASS_TO_W4`, `BLOCKED_W3`, or `INSUFFICIENT_EVIDENCE`;
   - blocker list in severity order;
   - minimum next work package, affected files and RED fixtures, without
     implementing it;
   - exact commands, script hash and generated artifact paths.

Provisional review thresholds, to be reported rather than silently tuned:

- no evidence of non-latest backlog or repeated control consumption;
- W3 compute P95 below 25% of a 10 ms control-analysis budget and no pathological
  tail that threatens capture/inference cadence;
- at least 90% usable W3 observations in active-target cohorts, unless invalid
  frames are explicitly explained by scene evidence;
- displacement-bound saturation low enough that response magnitude remains
  identifiable; saturation above 5% is an automatic review blocker;
- for a W4 feasibility pass, correlation sign must be physically consistent and
  peak delay stable within 5 ms across sufficiently populated split halves.

If repository configuration or established tests define stricter semantics,
report both and apply the stricter gate. These numerical thresholds are primary
review criteria, not user-confirmed product requirements.

## Non-goals

- Implementing W4 response identification or response curves
- Implementing or activating W5 short-term memory
- Changing controller output, target ownership or manual/AI arbitration
- Solving the deferred head-peek/approximately 3% micro-adjustment requirement
- Tuning the current protected runtime or producing an installable candidate
