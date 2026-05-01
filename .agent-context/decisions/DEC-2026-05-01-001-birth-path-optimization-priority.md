# DEC-2026-05-01-001: Prioritize Birth-Path Optimization Before More Continuation Complexity

Status: superseded
Date: 2026-05-01
Confirmed by: superseded after rollback
Related sessions:
- 2026-05-01T12:52:40+08:00
Related files:
- D:/Downloads/deep-research-report (4).md
- native/vision_native/src/vision_engine.cpp
- native/vision_native/src/target_selector.cpp
- native/vision_native/src/body_state_tracker.cpp
- native/vision_native/src/center_cue_refiner.cpp
- vision/native_runner.py
Supersedes: none
Superseded by: DEC-2026-05-01-003-revert-default-native-runtime-to-708c253

## Context

The current native COD22 pipeline already has the accepted detector + local-body-state + yellow-cue mixed architecture:

- `WarmScan / ActiveTrack` cadence split
- TensorRT person detection
- `VisionTargetSelector` candidate arbitration
- `BodyStateTracker` continuation authority
- `CenterCueRefiner` post-body-state cue refinement
- controller-facing damping-only output

An article review on 2026-05-01 compared that implementation against a broader real-time perception argument: keep the detector in charge of the first valid geometric observation, let the tracker improve continuation rather than own birth, and treat the auxiliary cue as an accelerator or refiner rather than a blocking peer.

The review found that the local implementation is broadly aligned with that architecture, but still retains conservative birth-path gates that likely dominate perceived first-lock latency:

- selector pickup still requires two confirmation frames before first lock
- cue-seeded selection can hard-veto otherwise valid person candidates
- `yellow_cue` fallback results can still propagate as generic `has_target` outputs through the Python native runner

## Decision

Before adding more continuation complexity or more aggressive cue-driven logic, prioritize a narrow birth-path optimization slice with this order:

1. restore a detector-owned provisional fast path for the first valid person observation
2. demote cue alignment from hard veto to soft bonus outside weak / reacquire handling
3. explicitly separate provisional `yellow_cue` visibility from controller-trusted output tiers

This proposal was recorded against the later native hotpath/cue experiment branch. After the user chose to roll the default runtime back to the pre-hotpath native baseline, it is no longer the active default-runtime direction and is therefore superseded for now.

## Reasons

- It targets the most likely remaining source of perceived first-lock hesitation without discarding the native continuation gains already implemented.
- It matches the article's strongest systems recommendation: separate birth logic from continuation logic instead of asking the tracker or cue to share first-output authority.
- It resolves a likely semantic drift between the accepted COD22 mixed-acquisition intent and the current runtime contract, where `yellow_cue` may still flow downstream as a generic target.
- It is narrower and easier to verify than another round of continuation-feature growth.

## Rejected Alternatives

- Continue only live validation with no birth-path review: rejected because the code comparison already exposed concrete conservative gates that can explain sluggish first lock.
- Strengthen cue authority first: rejected because the current issue appears to be gating and tiering, not lack of cue influence.
- Add more continuation or recovery machinery first: rejected because continuation quality is already comparatively mature in the current native stack.

## Evidence

- `D:/Downloads/deep-research-report (4).md` argues for detector-owned first output, non-blocking tracker attach, and auxiliary cues that accelerate rather than delay first lock.
- `native/vision_native/src/target_selector.cpp` still uses `kPickupConfirmFrames=2` and cue-alignment selection paths that can return no person candidate when cue alignment fails.
- `native/vision_native/src/vision_engine.cpp` still converts cue-only situations into `yellow_cue` fallback targets.
- `vision/native_runner.py` currently treats any native `has_target` result as controller-facing while aiming.

## Consequences

- No runtime behavior changes were accepted from this record; it now survives only as a deferred note from the rolled-back experiment branch.
- If adopted, the next implementation slice should add focused tests for provisional person pickup, cue soft-bonus behavior, and controller gating of provisional cue outputs.
- Live validation should compare first-lock feel and false-positive risk before treating this direction as accepted.

## Review Triggers

- Revisit if live validation shows no meaningful first-lock hesitation in the current build.
- Revisit if provisional detector-first output creates unacceptable false positives or target switching.
- Revisit if the user explicitly prefers the current more conservative confirmation behavior.
