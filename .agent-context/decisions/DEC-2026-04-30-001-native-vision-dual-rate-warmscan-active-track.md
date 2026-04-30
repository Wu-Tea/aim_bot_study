# DEC-2026-04-30-001: Native vision dual-rate WarmScan and ActiveTrack rollout

Status: accepted
Date: 2026-04-30
Confirmed by: explicit user-provided rollout plan plus explicit request to sync project-local context to that plan
Related sessions:
- 2026-04-30T16:13:20+08:00
Related files:
- `native/vision_native/include/vision_native/vision_engine.h`
- `native/vision_native/src/vision_engine.cpp`
- `native/vision_native/include/vision_native/body_state_tracker.h`
- `native/vision_native/src/body_state_tracker.cpp`
- `native/vision_native/include/vision_native/target_selector.h`
- `native/vision_native/src/target_selector.cpp`
- `native/vision_native/src/aim_enhancement.cpp`
- `native/vision_native/src/vision_native_module.cpp`
- `vision/native_runner.py`
- `docs/project/NATIVE_VISION.md`
- `gamepad_start.bat`
- `gamepad_native_debug.bat`
Supersedes: none
Superseded by: none

## Context

The earlier native body-state v1 work already added ego-aware selector continuity, torso-anchor output, and richer debug metadata, but the runtime still needed a clearer separation between low-frequency whole-frame recognition and high-frequency local update behavior. The new rollout also needed to avoid pre-aim wrong-target lock by using non-aim time only for scene prewarm rather than controller output.

## Decision

Split the native vision engine into three internal modes: `Idle`, `WarmScan`, and `ActiveTrack`.

Map `!is_aiming -> WarmScan` and `is_aiming -> ActiveTrack`, keep `NativeVisionEngine.poll_once()` and the Python controller contract unchanged, and make the first-pass rollout behave as follows:

- `WarmScan` only runs low-frequency whole-ROI scans at `50ms`, updates a `WarmScanSnapshot`, caches up to three ranked candidates as keyframes, and does not output controller targets, auto-fire, or active-target continuity
- `ActiveTrack` may prime from a fresh prewarm candidate (`<= 60ms`) but must force a real active scan before treating any target as a confirmed controller output
- high-frequency local updates between scans must flow through `BodyStateTracker` entrypoints (`prime_from_keyframe`, `update_interframe`, `update_scan_miss`), and true low-frequency scan misses alone advance hold / reacquire / drop budgets
- first pass keeps the current detector outputs and centered full ROI capture; torso box and torso patch continue to be derived from the existing person box
- first pass keeps only near-target damping in the controller-facing enhancement path; lead prediction and catchup boost stay disabled for this rollout comparison

## Reasons

- Separating coarse recognition from local tracking reduces repeated whole-frame YOLO work while keeping aim-stage updates frequent.
- Non-aim prewarm shortens aim-entry confirmation latency without committing to the wrong target before ADS.
- Reusing the existing detector/body-box output keeps the rollout scoped and lets the team validate cadence changes before changing model or capture contracts.
- Damping-only controller output isolates the benefit of the dual-rate split from earlier lead/catchup instability.

## Rejected Alternatives

- Keep the old binary aim/not-aim behavior that resets the native engine when leaving aim: rejected because it throws away usable prewarm context and blurs low-frequency versus high-frequency responsibilities.
- Let non-aim prewarm drive controller targets or auto-fire: rejected because it can lock the wrong person before the user fully commits to aim.
- Expand v1 scope to new detector outputs, dynamic local capture windows, or a new native background thread: rejected because the immediate goal is validating cadence and module boundaries with minimum additional surface area.
- Keep lead prediction and catchup boost active during the first dual-rate validation round: rejected because the rollout needs a cleaner comparison focused on tracking cadence and target stability.

## Evidence

- The user provided a detailed rollout plan specifying `Idle`, `WarmScan`, `ActiveTrack`, prewarm behavior, scan cadences, and damping-only first-pass scope.
- The current workspace already contains matching implementation pieces in `vision_engine.cpp`, `body_state_tracker.cpp`, `target_selector.cpp`, `aim_enhancement.cpp`, and `vision/native_runner.py`.
- Focused tests in the current workspace already verify WarmScan no-output behavior, debug contract fields, ego-warp continuity, scan-miss-budget behavior, pan-stop hold guardrails, and lower-screen muzzle-flash masking.

## Consequences

- Future handoffs and docs should describe the native runtime as a dual-rate `WarmScan` / `ActiveTrack` engine rather than a single aim-only loop.
- The Python bridge continues to consume only `target_x/target_y` plus debug metadata; controller interface shape stays unchanged.
- Verification must distinguish between scan frames and interframe local updates, and must only consume miss budgets on true scan misses.
- Startup scripts and docs should align on the intended manual acceptance default of `VISION_CAPTURE_FPS=240` where this rollout expects it, or explicitly document any exception.

## Review Triggers

- Revisit if the runtime starts using dynamic local capture windows, new model outputs, or a native background thread.
- Revisit if prewarm ever begins emitting controller targets before the first confirmed active scan.
- Revisit if lead/catchup are reintroduced to the controller-facing native enhancement path.
- Revisit if live validation shows that the current `BodyStateTracker` split is still too mixed to support reliable recovery or wrong-target protection.
