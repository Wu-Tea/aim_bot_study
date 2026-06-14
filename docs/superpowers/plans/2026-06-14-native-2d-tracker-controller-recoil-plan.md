# Native 2D Tracker / Controller / Recoil Refactor TODO

Date: 2026-06-14
Status: draft; local executor analysis reviewed and folded in
Scope: native C++ gamepad runtime, target tracking, controller arbitration, recoil interaction

## Overview

The live gamepad path is now the native C++ runtime. Vision produces screen-space target observations, the controller turns those observations into right-stick output, and recoil logic also contributes to final output. The current `NativeGamepadTargetTracker` is useful as a small projection helper, but it now sits inside controller code and has started to mix responsibilities that should become explicit contracts.

This plan records the TODO for a staged refactor toward:

```text
vision -> tracker -> controller -> recoil/output
```

The goal is not a large rewrite. The goal is to make target memory, ego-motion compensation, ADS/bodylock authority, and recoil interaction testable without creating more cross-coupled controller code.

For the maximum architecture and long-horizon wave plan, see `docs/superpowers/plans/2026-06-14-native-max-refactor-architecture-plan.md`. This file stays focused on the shorter TODO path and immediate implementation order.

## Current Context

- Default live path is native C++.
- Current tracker files:
  - `native/controller_native/target_tracker.h`
  - `native/controller_native/target_tracker.cpp`
- Current controller integration includes:
  - `native/controller_native/ai_aim.cpp`
  - `native/controller_native/native_gamepad_controller.cpp`
  - `native/controller_native/aim_assist_dynamics.cpp`
- Current research/reference package:
  - `native/fps_2d_tracker_package/`
- Recent live-feel issue:
  - long-lived or over-authoritative projection can feel like distant target drag or sluggish pull
  - recoil compensation must not silently pollute tracker ego-motion without a calibrated visual recoil model
- Current tuned feel:
  - user reported the current feel is acceptable on 2026-06-14
  - create a git checkpoint before starting behavior-changing refactor work
- New optimization idea:
  - if a target keeps moving in one direction, increase follow speed and optionally predict the next tick
  - this must be gated by fresh, strong, consistent observations so stale projection does not create drag
- Current local fix keeps tracker motion based on pre-recoil motion output. This is a pragmatic boundary until recoil visual displacement is modeled separately.
- `NativeAiAim` also has its own bodylock motion estimate. That is a separate coupling point from `NativeGamepadTargetTracker`, and it must be handled explicitly before making bodylock more aggressive.

## Hard Constraints

- Every C++ behavior change needs focused native unit tests in the same change.
- Predicted/coasting targets may assist aim only under explicit low-authority rules.
- Predicted/coasting targets must never grant fire authority.
- Tracker changes should be benchmarkable from logs or synthetic replay before becoming the default.
- Preserve Python fallback as comparison/reference, but do not assume Python is in the default live gamepad hot path.
- Do not fold recoil, controller, and tracker into one hidden feedback loop.

## Target Module Responsibilities

### Vision

Vision owns capture, ROI transform, inference, detection boxes, confidence, tier/class, and source timestamps.

Vision should not own:

- controller gain
- manual input arbitration
- recoil compensation
- long-lived target identity
- fire authority

### Tracker

Tracker owns short-term 2D screen-space memory.

Tracker should consume:

- vision frames at capture time
- body boxes and aim offsets
- detection confidence and tier/class
- mode state such as ADS/zoom
- controller/camera motion samples through a clear ego-motion contract
- optional recoil visual displacement, when calibrated

Tracker should produce:

- selected track snapshot
- projected aim error
- confidence/uncertainty
- assist authority
- fire authority
- debug state for benchmark logs

### Controller

Controller owns live input arbitration and output intent.

Controller should consume tracker snapshots and decide:

- ADS snap strength
- bodylock strength
- manual input arbitration
- dynamic curve shaping
- output clamping/saturation

Controller should not own:

- detector association
- long-lived track identity
- recoil visual modeling

### Recoil

Recoil has two separate concepts:

1. `recoil compensation`: a controller command added to right-stick output.
2. `recoil visual displacement`: the camera/screen impulse caused by firing, if the game actually moves the target projection.

Until the visual displacement model is calibrated, tracker ego-motion must not blindly treat anti-recoil compensation as ordinary target/camera motion. The current pre-recoil tracker feed is an explicit temporary boundary, not the final theory.

## Phase 0: Analysis And Guardrails

Status: in progress

- [x] Record this refactor as a dedicated TODO plan.
- [x] Ask the local executor to analyze `native/fps_2d_tracker_package/` against the current native runtime.
- [x] Review the local executor output before turning this into implementation work.
- [ ] Decide whether the first implementation step should be a type/contract wrapper or a physical module move.
- [ ] Add a short `.agent-context` SyncSet after review, if the user confirms context update.

Acceptance criteria:

- There is one durable plan for tracker/controller/recoil refactor work.
- The plan identifies tests required before code changes.
- The plan distinguishes current pragmatic recoil handling from the future recoil visual model.

Local executor findings folded into this plan:

- The current strongest coupling point is `native_gamepad_controller.cpp`: vision observations feed the tracker, tracker projection is injected back into the frame as if it were vision, and tracker motion is recorded from the pre-recoil output snapshot.
- `target_tracker.cpp` and `native_gamepad_controller.cpp` duplicate target-tier string classification.
- `ai_aim.cpp` contains separate bodylock motion tracking, so tracker migration cannot assume there is only one velocity estimate in the system.
- The reference package is useful, but the full `fps::TargetTracker` is too large and behavior-changing for the first step.
- The first PR-sized step should be behavior-preserving contract types plus an adapter, not a module move or Kalman import.

## Phase 0.5: Contract Types And Adapter, No Behavior Change

Outcome: create a small seam for future tracker backends while leaving live controller behavior unchanged.

Implementation TODO:

- [ ] Add a new native tracker contract header near the current tracker code.
- [ ] Prefer `NativeTracker*` names to avoid confusion with the `fps::` reference package:
  - `NativeTrackerControlSample`
  - `NativeTrackerQuery`
  - `NativeTrackerSnapshot`
  - `NativeTrackerSnapshotSource`
  - `NativeTrackerAssistAuthority`
  - `NativeTrackerFireAuthority`
- [ ] Add an adapter around `NativeGamepadTargetTracker`.
- [ ] Map adapter `pushControlSample(...)` to the existing `record_output(...)`.
- [ ] Map adapter `query(...)` to the existing `project(...)`.
- [ ] Fill source and authority fields in the adapter without changing controller behavior.
- [ ] Keep `NativeGamepadController` using the existing tracker directly until adapter tests pass.
- [ ] Compare the adapter type shape against `native/fps_2d_tracker_package/include/fps_tracker/types.hpp` before committing names.

Tests:

- [ ] adapter output matches direct `NativeGamepadTargetTracker::project(...)`
- [ ] strong observation maps to `AimObserved`
- [ ] weak/cue observation maps to at most `AimCoast`
- [ ] weak/cue/projected observation has no fire authority
- [ ] projection expiry returns absent source
- [ ] empty observation clears the adapter snapshot

Acceptance criteria:

- No live-feel config changes.
- No controller pipeline order changes.
- Existing controller behavior tests pass.
- New adapter tests are deterministic and do not rely on real sleeps.

## Phase 1: Stabilize The Tracker Contract Without Changing Feel

Outcome: the existing behavior is wrapped in a clearer contract before larger movement.

Implementation TODO:

- [ ] Expand the Phase 0.5 contract into full tracker IO structs, either beside the current tracker or in a new header:
  - `NativeTrackerVisionFrame`
  - `NativeTrackerDetection`
  - `NativeTrackerControlSample`
  - `NativeTrackerQuery`
  - `NativeTrackerSnapshot`
- [ ] Carry explicit timestamps:
  - `capture_time`
  - `ready_time`
  - `control_time`
  - `output_apply_time`
- [ ] Preserve the current `NativeGamepadTargetTracker` behavior behind an adapter.
- [ ] Record whether a snapshot is observed, projected, weak/continuity, or absent.
- [ ] Keep projected age bounded by `target_projection_max_age_ms`.
- [ ] Add debug fields that can be logged later without changing control output.
- [ ] Add a fake-clock or explicit-time test path for tracker tests so projection expiry tests do not rely on `sleep_for`.

Tests:

- [ ] current strong observation projects within `target_projection_max_age_ms`
- [ ] projection expires after `target_projection_max_age_ms`
- [ ] weak/continuity observation decays velocity and does not create stronger authority
- [ ] empty/no-target observation clears or suppresses projection as intended
- [ ] existing controller tests still pass

Verification:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1
native\vision_native\build\Release\cod_native_runtime.exe --config config.toml --once
```

## Phase 2: Split Tracker From Controller-Private State

Outcome: tracker code becomes a boundary module that controller consumes, not controller internals.

Implementation TODO:

- [ ] Create one of these shapes after code review:
  - `native/tracking_native/` as a new module, or
  - `native/controller_native/tracking/` as a lower-risk interim location
- [ ] Move or wrap:
  - `target_tracker.h`
  - `target_tracker.cpp`
- [ ] Keep old include paths working temporarily if the move touches many files.
- [ ] Add a CMake target or source grouping that makes tracker tests easy to run.
- [ ] Avoid migrating Kalman/association in this phase unless the contract is already stable.

Tests:

- [ ] all existing native controller behavior tests pass after the move
- [ ] a minimal tracker-only test target can instantiate and query the tracker
- [ ] no live-feel tuning values change in this phase

## Phase 3: Formalize Ego Motion And Recoil Boundaries

Outcome: tracker receives camera/ego-motion information through explicit samples, and recoil is no longer an implicit side effect.

Implementation TODO:

- [ ] Replace raw `record_output(right_x, right_y, dt)` usage with a structured sample:
  - manual stick component
  - assist stick component
  - recoil compensation component
  - final emitted stick
  - clamped/saturated final stick
  - sample time and dt
- [ ] Make the default ego-motion component explicit: current default is pre-recoil motion, matching the latest tested behavior.
- [ ] Add a config/contract flag for which component currently feeds tracker ego-motion.
- [ ] Keep current tested behavior as default: tracker uses the motion component that excludes anti-recoil compensation until a recoil visual model is calibrated.
- [ ] Add an experimental path that uses final emitted stick plus an explicit `RecoilVisualModel`.
- [ ] Add a disabled-by-default `RecoilVisualModel` interface or stub so the future model has a clean home.
- [ ] Log residuals needed to decide whether recoil visual displacement exists for a given weapon/game mode.

Tests:

- [ ] anti-recoil compensation alone does not move tracker projection in the default mode
- [ ] manual/assist camera motion still moves tracker projection
- [ ] final-stick experimental mode can be enabled in an isolated unit test
- [ ] saturated/clamped samples are the only samples accepted by final-stick mode
- [ ] fire authority remains observed-only during recoil windows

## Phase 4: Authority Model

Outcome: controller can use stronger aim correction without accidentally granting unsafe fire authority.

Implementation TODO:

- [ ] Add explicit authority values:
  - `AssistAuthority::None`
  - `AssistAuthority::AimObserved`
  - `AssistAuthority::AimCoast`
  - `FireAuthority::None`
  - `FireAuthority::ObservedOnly`
- [ ] Map current target tiers into authority once, inside tracker code.
- [ ] Let ADS/bodylock read assist authority rather than interpreting target tier strings directly.
- [ ] Keep auto-fire and fire gate reading fire authority only.
- [ ] Add debug logging for authority transitions.

Tests:

- [ ] observed strong target can produce `AimObserved`
- [ ] weak/cue/projected target can at most produce `AimCoast`
- [ ] missed/coasting/projected target always has `FireAuthority::None`
- [ ] no backing detection means no fire authority
- [ ] a newer vision miss clears fire authority even if projection still exists

## Phase 5: Evaluate The Research Package For Incremental Import

Outcome: use `native/fps_2d_tracker_package/` as reference material without dropping in a large unreviewed subsystem.

Candidate imports:

- `types.hpp`: useful as a target shape, but likely needs naming adapted to existing native runtime.
- `projection_model.*`: useful after ROI/ADS mode fields are stable.
- `ego_motion_buffer.*`: useful after Phase 3 clarifies recoil/default motion components.
- `kalman_cv2d.*`: useful for a second backend after adapter tests exist.
- `association.*`: useful only after detections, boxes, and confidence are logged frame-by-frame.
- `recoil_visual_model.*`: useful as an experimental disabled path, not a default requirement.

Implementation TODO:

- [ ] Build the package independently and preserve its invariant tests.
- [ ] Compare package data types with current native runtime structures.
- [ ] Decide how to handle C++ standard differences. The package is C++20-oriented, while the main native project should not be silently upgraded unless that is intentional.
- [ ] Decide copy-vs-adapt-vs-reference for each component.
- [ ] If importing code, keep package logic behind a feature flag such as `tracker_backend`.
- [ ] Start with tracker-only tests and synthetic replay, not live default behavior.

Tests:

- [ ] package `tracker_invariants` still passes if integrated
- [ ] synthetic replay still reports zero predicted-only fire violations
- [ ] current default tracker backend remains unchanged until A/B data exists

## Phase 6: Benchmarks And Logs

Outcome: changes can be evaluated with repeatable logs instead of only live feel.

Implementation TODO:

- [ ] Extend aim-only perf logs with tracker fields:
  - track id
  - snapshot source
  - assist authority
  - fire authority
  - projection age
  - observed capture age
  - velocity estimate
  - velocity consistency
  - lead/prediction horizon
  - ego-motion sample age
  - recoil/default motion mode
- [ ] Add synthetic replay cases:
  - static target with manual stick sweep
  - target strafe
  - short detector dropout
  - ADS transition
  - recoil window
  - two-target crossing
- [ ] Add A/B modes:
  - detector-only
  - current projection helper
  - imported Kalman backend
  - imported Kalman backend plus ego-motion buffer
- [ ] Track metrics:
  - p50/p95/p99 aim error
  - wrong-lock duration
  - reacquire time
  - ID switches
  - predicted-only fire violations
  - CPU time per tick
  - allocations per tick if practical

Verification:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1
native\vision_native\build\Release\cod_native_runtime.exe --config config.toml --perf-log --once
```

## Phase 7: Controller Feel Rollout

Outcome: bodylock can become more aggressive without stale tracker state making it feel heavy or wrong.

Implementation TODO:

- [ ] Gate aggressive bodylock by assist authority and freshness.
- [ ] Audit `NativeAiAim::observe_body_lock_motion` and `motion_velocity_x_/y_` before moving velocity prediction into tracker.
- [ ] Add sustained-motion follow assist:
  - detect same-direction movement across multiple fresh strong observations
  - require low velocity-angle variance before increasing follow gain
  - optionally lead by one controller tick or a short capped horizon
  - reset the boost on direction reversal, high innovation, target switch, ADS transition, or manual override
- [ ] Make ADS snap use observed/projected source differently:
  - observed target: higher correction allowed
  - short projection: limited correction
  - weak/coast: low correction or reacquire prior only
- [ ] Keep manual input arbitration based on user intent and target authority.
- [ ] Add dynamic curve straightening only at the controller layer; do not hide it inside tracker state.
- [ ] Add short max-age defaults for projected state before raising force/gain.
- [ ] Add a separate cap for ADS snap strength on projected targets so stale projection cannot produce a high-force snap.

Tests:

- [ ] bodylock gain falls back when projection age exceeds threshold
- [ ] sustained one-direction target motion increases follow gain only after enough consistent observations
- [ ] next-tick lead is capped and disabled for weak/coast/projected-only targets
- [ ] direction reversal clears the follow-speed boost within one tick
- [ ] manual input can still override when the user clearly moves away
- [ ] aggressive correction can recover wrong initial aim on fresh observed targets
- [ ] ADS does not over-pull upward after manual/recoil mixing cases covered by tests

## Phase 8: Cleanup And Documentation

Outcome: once a new tracker backend is proven, remove transitional coupling.

Implementation TODO:

- [ ] Remove old string-tier authority checks from controller code.
- [ ] Replace duplicate target-tier string classification in `target_tracker.cpp` and `native_gamepad_controller.cpp` with one shared authority/classification helper.
- [ ] Remove temporary include wrappers after callers migrate.
- [ ] Document the tracker/controller/recoil contract in `docs/project/NATIVE_CPP_RUNTIME.md`.
- [ ] Update `.agent-context/handoff.md` with final accepted architecture and default backend.
- [ ] Keep a short rollback note for returning to the current projection helper.

## Local Executor Analysis Questions

Ask the local executor to answer these before implementation:

- Which parts of `native/fps_2d_tracker_package/` can be imported with the least CMake disruption?
- Does the package assume final stick ego-motion in a way that conflicts with the current pre-recoil tracker feed?
- What minimal adapter types would let the existing tracker and package tracker share tests?
- Where are the current controller/tracker/recoil coupling points?
- What should be the first behavior-preserving PR-sized change?
- Which native unit tests should exist before changing live feel?
