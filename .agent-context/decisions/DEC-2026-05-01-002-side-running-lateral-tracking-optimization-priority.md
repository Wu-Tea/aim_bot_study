# DEC-2026-05-01-002: Prioritize Narrow Lateral-Tracking Improvements Before Heavy MOT Additions

Status: superseded
Date: 2026-05-01
Confirmed by: superseded after rollback
Related sessions:
- 2026-05-01T13:33:34+08:00
Related files:
- D:/Downloads/deep-research-report (5).md
- native/vision_native/src/body_state_tracker.cpp
- native/vision_native/src/ego_motion.cpp
- native/vision_native/src/vision_engine.cpp
- native/vision_native/src/target_selector.cpp
- tests/test_native_vision_body_state_bridge.py
- tests/test_native_vision_targeting_bridge.py
Supersedes: none
Superseded by: DEC-2026-05-01-003-revert-default-native-runtime-to-708c253

## Context

The current native runtime already matches the article's preferred high-level architecture for sideways-running targets:

- ego-motion compensation runs before candidate selection
- `VisionTargetSelector` still owns person-level arbitration
- `BodyStateTracker` performs selected-target-only torso-local continuation
- regression coverage already exists for large pan continuity, partial occlusion, short hold, reacquire transitions, and pan-stop overshoot

The article review on 2026-05-01 focused on lateral motion, short occlusion, and near-neighbor misattachment. It argued that the biggest gains for this class of failure do not come from importing a full BoT-SORT or ReID-style MOT stack, but from tightening the local torso tracker and reacquire behavior around rapid sideways motion.

The review found that the local implementation is directionally aligned, but still has a few likely weak points for side-running enemies:

- patch tracking in `BodyStateTracker` is currently unmasked in both observed and unobserved paths
- reacquire behavior decays residual velocity conservatively and does not have an explicit mini-ORU-style reset when observation quality returns
- recovery constants look tuned for general stability, but may be tight for fast lateral motion with partial occlusion
- the ego branch currently fits full affine, while the article suggests lower-DOF models may be more stable in HUD-heavy FPS captures

## Decision

If the project chooses to optimize sideways-running enemy handling, prioritize a narrow continuation slice in this order:

1. add masked torso-local patch tracking
2. add explicit reacquire-time residual-velocity reset behavior
3. retune recovery window and residual-velocity limits for lateral movement
4. only then consider narrowing ego-motion from full affine toward similarity or partial affine
5. keep any color cue or low-score detection follow-up as secondary refinements

Do not treat full BoT-SORT, ReID, dense optical flow, or SLAM-style additions as the default next step for this problem.

This proposal was recorded against the later native hotpath/cue experiment branch. After the user chose to roll the default runtime back to the pre-hotpath native baseline, it is no longer the active default-runtime direction and is therefore superseded for now.

## Reasons

- It matches the article's strongest practical argument: side-running failures are usually local-continuity and reacquire problems, not proof that the hot path needs a full multi-object tracking stack.
- It builds directly on the current accepted native architecture instead of reopening major scope.
- It targets the places where the local code appears most exposed: unmasked torso patches, conservative post-loss velocity handling, and potentially tight lateral recovery windows.
- It gives a focused path for verification with existing body-state and selector bridge tests.

## Rejected Alternatives

- Import full BoT-SORT or ReID into the hot path first: rejected because the article and current architecture both point to a lighter local-fix path with better cost-benefit.
- Treat detector quality alone as the main solution for sideways-running targets: rejected because the article emphasizes short-occlusion continuity and local-anchor stability, not only first-frame detection quality.
- Retune everything at once, including cue policy and heavy ego-model changes: rejected because it would blur which change actually improved lateral tracking.

## Evidence

- `D:/Downloads/deep-research-report (5).md` recommends camera-motion compensation plus selected-target torso-local tracking, and specifically calls out masked patch tracking, OC-SORT-like short-loss update logic, and narrow local cues as higher-value than a full MOT stack for FPS hot paths.
- `native/vision_native/src/body_state_tracker.cpp` currently calls patch matching without a mask in both observed and missing-target flows.
- `native/vision_native/src/body_state_tracker.cpp` also decays residual velocity during missing-target handling, which may underfit rapid lateral reacquire events.
- `native/vision_native/src/ego_motion.cpp` currently fits and exports a full affine warp, which the article treats as potentially more flexible than needed for this problem.
- `tests/test_native_vision_body_state_bridge.py` and `tests/test_native_vision_targeting_bridge.py` already encode several continuity and pan-related regressions that make this slice measurable.

## Consequences

- No runtime behavior changes were accepted from this record; it now survives only as a deferred note from the rolled-back experiment branch.
- If adopted, the first implementation step should likely stay inside `BodyStateTracker` and focused tests before touching larger selector or controller contracts.
- Live validation should specifically watch sideways sprinting, short occlusion behind cover, neighbor confusion during lateral cross, and pan-stop overshoot after reacquire.

## Review Triggers

- Revisit if live tests show sideways-running targets are already stable enough that birth-path work remains the clearly dominant priority.
- Revisit if masked patching plus reacquire reset does not materially improve lateral continuity.
- Revisit if a lower-DOF ego model improves pan-stop stability but harms general camera-motion compensation.
