# Agent Handoff

Last updated: 2026-10-12
Active scope: record and commit this conversation's DSE directional notifications and old Tk GUI removal; preserve unrelated concurrent controller work.

## Current state

- React/WebView2 is the only desktop GUI, opened by `启动助手.vbs` through the thin `desktop_app.gui` entry and `web_host.py`. The user authorized deleting the old Tk window/widgets/tools, superseding October 4 Tk retention. Python configuration/curves/process/IPC remain; native control/Vision/build remain Python-free.
- DSE USB direction notifications are implemented: 参数调校 → 目标与识别 → 人物震动提醒, default off, strength 24%, adjustable 0–100%. The user accepted left/both/right pulse feel; automatic live-game notification behavior remains unverified. Details: [DSE and GUI decision](decisions/DEC-2026-10-12-001-dse-haptics-and-webview-gui.md).
- Controller notes below describe the October 6 checkpoint; concurrent controller changes in the worktree are outside this conversation's commit and are not revalidated by this record.
- GUI pages and previews redesigned; manual output compensation is in parameters with disclosure navigation. Numeric parameter catalog generates native parsing/defaults/validation/reload rules and drives GUI controls. Output caps use percentages in GUI, normalized values in configuration; response times are independent milliseconds. AI deadzone defaults to 3%, per axis, without rescaling manual or recoil input. Recoil no longer has a hidden 14% floor.
- Canonical gamepad may bridge a same-target observation gap for at most the user-approved 32 ms, using capture age and delivered-camera history. Invalid evidence/manual exit/expiry revoke it; gap prediction never becomes a fresh observation or fire authority. Mouse/legacy behavior remains separate.
- Windows Vision deadline waits use an interruptible high-resolution timer. COD sustained-ADS capture/inference increased from approximately 66–69 FPS to 166–167 FPS in the recorded before/after sessions (configured target 180); this is a measured improvement, not an exact target-rate guarantee.
- Current approach curve continuously tapers position correction at the actual target point, with a nominal 25 ms braking budget. Pursuit minimum cannot override braking. `arrival_radius_px` is now the smooth final-approach scale and still the ADS completion radius; GUI label is 近点收尾半径. Geometry preview semantics are `range-response-v3`.
- Target velocity uses up to five aligned capture intervals and at most 25 ms of displacement history instead of a median of three instantaneous derivatives. No optical flow, 10-frame/2-frame extrapolator or additional return-to-zero delay was added.

## Verification and open limits

- October 12 feature checks: frontend/native Release builds, 79 desktop tests and four native Feature suites pass; actual WebView2 notification settings persist and close protection passes. Default/minimum layouts inspected. DSE production worker playback/disable/shutdown passed; full gameplay acceptance remains open. Local evidence: `output/dse-capability-probe-20261012/` (excluded from commit).
- Historical October 6 checkpoint: Release build, 50/50 CTest groups, 111/111 desktop tests and whitespace checks passed. Evidence: `output/point-curve-tuning-20261006/`.
- Point simulation: 192 scenarios / 1,344,000 steps, two independent seeds, short/long durations. Of 96 paired configured-floor cases, 90 reduce mean absolute error; six clean stationary cases have larger residual error but remain inside their configured arrival radii. Representative moving-target mean error: 8.13 → 0.62 px. This is a simplified plant, not live Apex acceptance.
- Latest Apex sessions without detailed telemetry cannot establish current live control behavior. Remaining gaps: actual nonlinear game response, recoil/manual arbitration, cue/source transitions, and small AI commands removed by configured deadzone.
- An intermittent truncated native config dump was not causally reproduced; end-marker validation is boundary hardening, not a proven fix for its origin.

## Resume and boundaries

- Read `docs/project/CONFIGURATION_AND_CHECKS.md`, the October 6 decision and `output/README.md`; earlier snapshots are historical. Prior handoff: `archive/handoff-2026-10-04-before-20261006-checkpoint.md`.
- Preserve user profiles/models/configuration; do not restore retired Python runtime paths, SHA manifests or benchmark acceptance gates. Preserve raw manual passthrough, 15–30% authority, selection/identity/handover and complete controller lockstep.
- Next evidence needed is live Apex feedback with detailed telemetry on the rebuilt runtime. Simulations are not a release verdict.

## Context review

SyncSet `sync-20261012-002` self-review: accept_draft. The user's latest request explicitly authorizes recording and committing this conversation's decisions and changes. User-accepted pulse feel is distinguished from implementation choices and unverified live-game behavior. No secrets, personal configuration, raw logs, subagent briefs or external writes included. Existing session history retained; log compaction remains a separate suggested task.
