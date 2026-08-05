# Subagent Brief: W3 Observable Envelope and W4 Clock Contract

Created: 2026-08-03T19:53:00+08:00
Expires: after the source/test checkpoint is accepted or the protected runtime baseline changes
Owner session: primary Codex task `019fb9a8-e636-7ee0-9a2a-b88a0c3c519e`

## Mission

Implement one shadow-only development package that makes the next live session
capable of doing two jobs: re-run the W3 ego-motion quality gate without the
current `+/-8 px` measurement ceiling, and provide an explicit common-clock
contract for a later W4 ViGEm-output-to-camera-response analysis. The package
must not affect aiming output or replace the protected runtime.

Stop after source changes, deterministic tests, Release build verification and
a review packet. Do not deploy the candidate and do not perform W4 correlation
against the old session.

## Role

implementer with test-first evidence; stop at reviewer checkpoint

## Required Context

- Protected rollback baseline: source commit `5d2f3be`; installed executable
  SHA-256
  `872F6FFFD1598C64ABC34E0D551F4C112B40FEAC630844C6CB0B0FEE50558C38`.
- The accepted runtime remains user-owned and must not be overwritten, stopped,
  launched or reconfigured by this package.
- W3 is shadow-only. Its current live audit found fast compute but an apparent
  hard search boundary at `+/-8 px`; the final accepted audit report is the
  authority for exact counts and caveats.
- W4 is blocked because ego displacement describes the interval
  `[previous_present, current_present]`, but persisted delivered output uses
  steady-clock `applied_at_ns` and the current ego payload has no calibrated
  present timestamp in that clock domain. `current_result_ns` is result-ready
  time and must never be substituted for effect time.
- `EgoMotionShadowResult` already carries previous/current capture-result fields
  internally, but the persisted telemetry payload omits the capture fields.
- The observer and runtime paths use single/latest mailboxes. Missing frames and
  mailbox replacement must be counted explicitly; no queue may be introduced.
- Current realized camera-response ownership is already embedded in the
  tracker/coordinator projection. Future W5 work must replace that owner at the
  audited boundary, not stack another realized-motion correction after it.

## Relevant Decisions and Evidence

1. `.agent-context/decisions/DEC-2026-08-03-001-protect-live-baseline-defer-w5.md`
2. `.agent-context/decisions/DEC-2026-08-03-002-target-count-aware-manual-exit-authority.md`
3. `docs/archive/control-history/CAUSAL_RESPONSE_EGO_MOTION_OWNERSHIP_AUDIT.md`
4. `artifacts/benchmarks/causal-aiming-state-20260802/w3-live-audit-20260803/W3_LIVE_AUDIT_20260803.md`
5. `artifacts/benchmarks/causal-aiming-state-20260802/w3-live-audit-20260803/W3_LIVE_AUDIT_20260803.json`

## Files To Inspect First

1. `.agent-context/handoff.md`
2. `docs/project/CURRENT_STATE.md`
3. `native/vision_native/include/vision_native/dxgi_capture.h`
4. `native/vision_native/src/dxgi_capture.cpp`
5. `native/vision_native/include/vision_native/ego_motion_observer.h`
6. `native/vision_native/src/ego_motion_observer.cpp`
7. `native/vision_native/src/ego_motion_observer_tests.cpp`
8. `native/vision_native/include/vision_native/types.h`
9. `native/vision_native/src/vision_engine.cpp` only around capture metadata and
   ego staging/submission/result collection
10. `native/runtime_app/runtime_loop.cpp` only around ego telemetry publication
11. `native/runtime_app/telemetry_schema.h`
12. `native/runtime_app/telemetry_collectors.h/.cpp`
13. `native/runtime_app/runtime_telemetry.cpp` and its relevant tests

## Constraints

- Preserve all current dirty/untracked changes. No reset, checkout, clean,
  destructive rewrite, broad formatting or unrelated cleanup.
- Do not modify controller output, fuser, selector, tracker/coordinator control
  semantics, ADS 135/220 ms lifecycle, BodyLock policy, recoil, OCR, profile,
  activation radius, gain or manual/AI arbitration.
- Do not implement W5 memory, PendingMotion actuation or W6 ownership.
- Keep all new behavior shadow-only and output-bit-stable when telemetry/shadow
  observation is disabled or enabled.
- Do not add an unbounded queue, wait in the controller loop, or make telemetry
  a control dependency.
- Do not lower `min_confidence`, `max_residual_px` or other validity gates merely
  to raise valid rate. Add diagnostics before proposing threshold changes.
- A build is allowed for verification. No deploy, runtime overwrite, launch,
  stage or commit.

## Workstream A: W3 Measurable Displacement Envelope

1. Add RED fixtures for translations beyond the current boundary: positive,
   negative and diagonal shifts in the `9..16 px` range; retain all existing
   `0..8 px`, foreground-mask, noise, duplicate/out-of-order and latest-only
   fixtures.
2. Benchmark at least the current `radius=8` reference and the simplest larger
   envelope candidates. Prefer the least complex implementation that:
   - correctly identifies at least `+/-12 px` in both axes;
   - does not regress ordinary `0..8 px` accuracy/sign;
   - keeps Release compute P95 below the existing `2.5 ms` review budget on the
     retained deterministic benchmark;
   - does not silently return a boundary-clipped displacement as fully valid.
3. An exhaustive radius increase is acceptable if it meets the cost gate. Use a
   bounded coarse-to-fine search only if the simple implementation fails the
   cost or accuracy gate. Retain variant timings and the selection reason.
4. Publish explicit search-envelope diagnostics in the shadow result and
   telemetry: configured radius/envelope, boundary-hit count/rate and a clear
   invalid/limited reason when the estimate remains unidentifiable at the
   boundary.
5. Preserve the fixed-capacity design and deterministic estimator. Do not turn
   the observer into a controller or predictor.

## Workstream B: W4 Common-Clock Contract

1. Establish a same-code-point QPC/steady calibration sample. Bound sampling
   uncertainty (for example by bracketing one clock read with the other) and
   carry an explicit validity/domain contract.
2. Map DXGI `LastPresentTime` QPC into a calibrated steady-clock
   `source_present_steady_ns` without mixing it with acquire/copy/result latency.
   Use overflow-safe signed delta math and retain raw QPC/frequency plus the
   calibration identity/uncertainty for audit.
3. Carry previous/current present-steady and capture-copy-complete timestamps
   through the ego frame, stored frame, shadow result, runtime input, payload and
   serializer. Do not infer them later from `current_result_ns`.
4. Bump the relevant record/payload schema deliberately and update serializer
   tests. Older log parsing must fail or degrade explicitly rather than silently
   treating a missing timestamp as zero-time evidence.
5. Add RED fixtures proving:
   - known QPC/steady offset and drift map with the expected sign and bounded
     error;
   - an output applied after `current_present_steady_ns` but before
     `current_result_ns` is ineligible as a cause of that present interval;
   - a later present interval may admit the same output only when its boundaries
     and response delay allow it;
   - invalid calibration never enables a causal join.
6. This package provides timestamps and fixtures only. It must not estimate or
   activate a production response curve.

## Workstream C: Latest-Only Accounting

Add bounded monotonic diagnostics sufficient to distinguish:

- frames submitted;
- a pending frame replaced before processing;
- pairs processed;
- an unread result replaced by a newer result;
- duplicate/out-of-order frames rejected;
- result age when taken/published.

Counters must be lifecycle-aware, race-safe, non-blocking and covered by a
worker-delay fixture. They diagnose drop/backpressure; they must not create a
queue or retry old motion.

## Required Verification

1. Focused RED-to-GREEN tests for clock mapping, present-interval causality,
   enlarged translation envelope, search-bound reporting and latest-only
   replacement counters.
2. Relevant telemetry serializer/collector tests, including absent/invalid clock
   fields and schema identity.
3. Full Release CTest suite with exact pass/fail count.
4. Retained Release microbenchmark comparing current and selected search
   variants, including P50/P95/P99/max, accuracy and boundary rate.
5. Output-invariance evidence: enabling the new shadow fields cannot change the
   final controller output sequence/hash in an existing deterministic fixture.
6. `git diff --check`, changed-file inventory and explicit confirmation that the
   installed runtime hash remains unchanged.

## Acceptance Gate

Return one of:

- `READY_FOR_LIVE_W3_W4_CAPTURE`: every deterministic/schema/performance gate
  passes; candidate is built but not deployed.
- `BLOCKED_W3_ENVELOPE`: displacement accuracy/cost/boundary semantics fail.
- `BLOCKED_W4_CLOCK`: direct present-steady contract or causal RED fixtures fail.
- `BLOCKED_REGRESSION`: full suite or output-invariance evidence fails.

The reviewer will separately decide whether to install a candidate for a live
capture. Do not combine a deterministic pass with a claim that W3 live quality
or W4 response identification is already complete.

## Expected Review Packet

- concise architecture/data-flow description and clock-domain table;
- changed files and why each changed;
- failing RED evidence followed by passing commands/output;
- variant benchmark artifact and selection rationale;
- schema/version changes and example JSON record;
- full test/build commands and exact results;
- source commit/worktree state, built executable SHA-256 and installed runtime
  SHA-256 (read-only comparison only);
- remaining risks and the exact next live capture fields/cohorts required.

## Non-Goals

- Deploying or replacing the current runtime
- Performing W4 correlation on the old log
- Implementing the 150-200 ms W5 ledger or changing actuation
- Implementing single/multi-target ownership or head-peek micro-aim
- Tuning AI/manual authority, ADS/BodyLock gain, target loss or occlusion carry
