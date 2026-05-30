# Decision: Prioritize Single-Target Weak Association And Authority Gating

Status: accepted
Date: 2026-05-29
Confirmed by: User asked to consolidate the subagent discussion and prepare to proceed after reviewing the recommendation.
Related sessions: 2026-05-29 targeting research and subagent review.
Related files:
- `native/vision_native/src/target_selector.cpp`
- `native/vision_native/src/tensorrt_engine.cpp`
- `native/vision_native/include/vision_native/types.h`
- `vision/native_runner.py`
- `controllers/base_controller.py`
- `controllers/gamepad/target_tracker.py`
- `controllers/gamepad/ai_aim.py`
- `D:/Downloads/deep-research-report (14).md`
Supersedes: None
Superseded by: None

## Context

The current native targeting baseline uses detector-led person boxes and a single active target selector. The user observed that practical aim quality still suffers when the target is briefly occluded by firing, weapon effects, side-running posture, or other short visual gaps. The system already has yellow-dot cue support and controller-side projection, but `has_target` and `target_source` do not fully separate a visible hypothesis from controller/fire authority.

The research report recommends a detector-led, short-horizon, single-target approach with low-score association and no prediction-only firing authority. Three subagents reviewed the native selector, controller projection, and output contract. They converged on the same direction: improve same-target continuation first, but make the controller trust boundary explicit before widening the target path.

## Decision

Implement the next targeting work as detector-led single-target weak association with explicit authority gating.

The planned architecture is:

- Strong detector observations remain the only normal birth and firing authority.
- Low-score detections may continue the current active target only after strict spatial/size/direction/cue gating.
- Yellow cue remains auxiliary same-target evidence and short hold support.
- Cue-only, weak-only, and predicted-only targets cannot birth, switch, or fire.
- Runner and controller must fail closed for auto-fire when target authority is not strong observed.
- Controller behavior should become source-aware before introducing more complex prediction.

## Reasons

- The user's live failure mode is short target loss and reacquire quality, not lack of full multi-target identity management.
- Active-only low-score association directly addresses detector score drops during muzzle flash, partial body, and side-running frames.
- Explicit authority fields prevent continuity improvements from accidentally becoming stronger aim or fire authority.
- The existing controller projection already covers part of the timing/prediction problem; improving source semantics is lower risk than adding Kalman first.
- Keeping the work native-focused matches the current project scope and avoids reintroducing the earlier ROI/lifecycle overreach.

## Rejected Alternatives

- Full MOT, ReID, DeepSORT/StrongSORT, or global identity management: rejected for now because the application is a controller-facing single-target system.
- Immediate production Kalman/alpha-beta filter: deferred until weak association, authority fields, and logs exist.
- Dense optical flow or SLAM-style camera compensation in the hot path: deferred until logs prove controller-output compensation is insufficient.
- Cue-only target birth or cue-only firing: rejected because the cue is a useful auxiliary signal but not reliable geometry authority.
- Lowering the global detector confidence threshold and feeding all boxes into the normal selector: rejected because it would increase false locks and switch risk.
- Moving the controller hot path to C++ first: deferred until timing logs show Python/native communication is the bottleneck.

## Evidence

- Native selector currently has strong and tracking thresholds but TensorRT decode filters boxes before selector-level second-pass association can use low-score detections.
- Runner currently forwards any `has_target` result as a controller target, while controller-side body-lock freshness checks do not fully gate on target source.
- Existing cue hold clears auto-fire in native, but aim/control authority is still not cleanly separated across layers.
- Subagent reviews independently recommended active-only weak association, source-aware controller behavior, and fail-closed fire gating.
- The report explicitly favors detector-led observation-centric single-target tracking over full MOT/ReID for this class of system.
- Later GitHub/open-source comparison reinforced that most public FPS YOLO projects stop at detection plus simple selection/smoothing; this project's remaining gains were more likely in source authority, short continuation, and controller handoff semantics.
- User live-tested the implemented version on 2026-05-30 and reported it felt very strong, possibly too strong, but worth checkpointing as a version.

## Implementation Outcome

The accepted design was implemented as a native/controller version checkpoint:

- Native result contract gained `target_tier`, `aim_authority`, `fire_authority`, `association_stage`, and `target_confidence`.
- Native live decode keeps low-score boxes available for selector-only continuation, while strong detector observations remain the only normal birth/fire authority.
- `associated_weak` and `cue_hold` can preserve same-target continuity but cannot auto-fire.
- Python runner, base controller, gamepad controller, and mouse controller now fail closed when target authority is insufficient.
- Gamepad body-lock and projection became source-aware, with reduced weak/cue force and no weak ADS snap.
- Auto-fire gained an aim-readiness settling gate for single-shot timing.
- Default body-lock/native selector aim point was tuned from the older head-biased `0.38` to chest-biased `0.43`; `0.50` was tried and rejected as too low.

Verification for the checkpoint included native build, 222 broader native/controller tests, 128 related targeting/controller/config tests, Python compile checks, `git diff --check`, and user live smoke feedback.

## Consequences

- Public/native result contracts and Python runner mapping will need additive fields or equivalent compatibility handling.
- Native bridge and controller tests should be updated from `has_target + target_source` assumptions toward tier/authority semantics.
- Logging and benchmarks should bucket metrics by source/tier to prove weak/cue continuation improves reacquire without increasing wrong relock or fire risk.
- Live improvements should be evaluated against the latest gamepad body-lock baseline, not the older ROI/lifecycle experiment.

## Review Triggers

- Weak association increases wrong target snap/relock rate.
- Any weak/cue/predicted target produces actual auto-fire.
- Live tests show strong detector observations are being delayed or suppressed by the new contract.
- Timing logs show Python/native handoff, not detection/association/controller tuning, is the dominant remaining bottleneck.
- A future detector/model change makes low-score continuation unnecessary or unsafe.
