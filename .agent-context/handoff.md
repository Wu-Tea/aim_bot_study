# Agent Handoff

Last updated: 2026-10-06
Active scope: desktop redesign, canonical parameter semantics, native continuity/performance and point-approach fixes; user requested a complete local workspace commit.

## Current state

- Python/Tk remains the desktop GUI/configuration/curves/process/IPC layer; native control, Vision and builds remain Python-free. The October 4 GUI preservation statement was a merge checkpoint, not a ban on the subsequently authorized redesign.
- GUI pages and previews redesigned; manual output compensation is in parameters with disclosure navigation. Numeric parameter catalog generates native parsing/defaults/validation/reload rules and drives GUI controls. Output caps use percentages in GUI, normalized values in configuration; response times are independent milliseconds. AI deadzone defaults to 3%, per axis, without rescaling manual or recoil input. Recoil no longer has a hidden 14% floor.
- Canonical gamepad may bridge a same-target observation gap for at most the user-approved 32 ms, using capture age and delivered-camera history. Invalid evidence/manual exit/expiry revoke it; gap prediction never becomes a fresh observation or fire authority. Mouse/legacy behavior remains separate.
- Windows Vision deadline waits use an interruptible high-resolution timer. COD sustained-ADS capture/inference increased from approximately 66–69 FPS to 166–167 FPS in the recorded before/after sessions (configured target 180); this is a measured improvement, not an exact target-rate guarantee.
- Current approach curve continuously tapers position correction at the actual target point, with a nominal 25 ms braking budget. Pursuit minimum cannot override braking. `arrival_radius_px` is now the smooth final-approach scale and still the ADS completion radius; GUI label is 近点收尾半径. Geometry preview semantics are `range-response-v3`.
- Target velocity uses up to five aligned capture intervals and at most 25 ms of displacement history instead of a median of three instantaneous derivatives. No optical flow, 10-frame/2-frame extrapolator or additional return-to-zero delay was added.

## Verification and open limits

- Final Release build, 50/50 CTest groups, 111/111 desktop tests and whitespace checks pass. Evidence: `output/point-curve-tuning-20261006/`.
- Point simulation: 192 scenarios / 1,344,000 steps, two independent seeds, short/long durations. Of 96 paired configured-floor cases, 90 reduce mean absolute error; six clean stationary cases have larger residual error but remain inside their configured arrival radii. Representative moving-target mean error: 8.13 → 0.62 px. This is a simplified plant, not live Apex acceptance.
- Latest Apex sessions without detailed telemetry cannot establish current live control behavior. Remaining gaps: actual nonlinear game response, recoil/manual arbitration, cue/source transitions, and small AI commands removed by configured deadzone.
- An intermittent truncated native config dump was not causally reproduced; end-marker validation is boundary hardening, not a proven fix for its origin.

## Resume and boundaries

- Read `docs/project/CONFIGURATION_AND_CHECKS.md`, the October 6 decision and `output/README.md`; earlier snapshots are historical. Prior handoff: `archive/handoff-2026-10-04-before-20261006-checkpoint.md`.
- Preserve user profiles/models/configuration; do not restore retired Python runtime paths, SHA manifests or benchmark acceptance gates. Preserve raw manual passthrough, 15–30% authority, selection/identity/handover and complete controller lockstep.
- Next evidence needed is live Apex feedback with detailed telemetry on the rebuilt runtime. Simulations are not a release verdict.

## Context review

SyncSet self-review: accept_draft. User explicitly authorized recording, organization and committing all workspace changes. Current implementation and observed measurements are distinguished from live acceptance; no secrets, raw personal telemetry or new subagent briefs copied into context. Historical session entries retained.
