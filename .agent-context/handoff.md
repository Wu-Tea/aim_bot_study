# Agent Handoff

Last updated: 2026-05-30T17:43:18+08:00
Updated by: Codex
Active scope: COD/FPS native targeting and gamepad aim assist version checkpoint.
Staleness: stale after a new detector/model baseline, another targeting/controller contract change, or live evidence that weak/cue continuation feels sticky or overpowered.

## Current Objective

Checkpoint the live-tested native YOLO single-target stability upgrade: short visual loss, firing/weapon occlusion, and side-running should be smoother while weak, cue-only, or predicted targets still cannot become fire authority.

## Current State

- Branch: `dev` in `D:\work\AI\yolo-study-001`.
- Baseline before this version: `a196f92 Improve gamepad body-lock lateral hold`.
- This handoff is part of the commit that checkpoints the targeting/controller upgrade after user live testing.
- The system uses native YOLO/TensorRT person detection, native target selection, Python runner handoff, and gamepad controller-side projection.
- User-provided report `D:/Downloads/deep-research-report (14).md` and subagent synthesis led to the accepted direction in `decisions/DEC-2026-05-29-001-single-target-weak-association-authority-gating.md`.
- User live feedback on 2026-05-30: the change feels very strong, possibly "a bit too strong"; acceptable to checkpoint as a version.

## Implemented In This Version

- Added tiered native/Python target authority fields:
  - `target_tier`
  - `aim_authority`
  - `fire_authority`
  - `association_stage`
  - `target_confidence`
- Added runner/controller fail-closed gates:
  - only fire-authorized strong observed targets may pass auto-fire
  - `aim_authority=false` targets clear controller target state instead of entering assist
- Added native active-only low-score association:
  - native live decode keeps boxes down to `0.20` for selector continuation
  - low-score boxes cannot birth, switch, or fire
  - near active low-score matches output as `associated_weak`
- Preserved yellow cue as auxiliary continuation:
  - `cue_hold` can keep a shifted active target visible briefly
  - `cue_hold` has aim authority but no fire authority
- Added source-aware controller projection and auto-fire settling:
  - weak/cue/predicted targets do not become fire-authorized
  - single-shot auto-fire waits for an aim-ready/settled signal
  - ADS snap got time-to-go and opposing-manual handling improvements
- Made gamepad controller source-aware:
  - weak/low-score targets do not refresh projection velocity
  - weak association cannot trigger ADS snap
  - weak/cue body-lock uses lighter force scales
  - predicted/no-authority targets stay manual
- Adjusted default body-lock/selector aim point from head-biased `0.38` to chest-biased `0.43` after live feedback that `0.50` was too low.
- Added config knobs:
  - `[gamepad.ai_aim].weak_target_body_lock_force_scale`
  - `[gamepad.ai_aim].cue_hold_body_lock_force_scale`

## How The Idea Was Found

- The starting observation was that high-FPS detection alone did not fix practical misses; failures clustered around short occlusion, firing effects, side-running posture, and controller timing.
- `D:/Downloads/deep-research-report (14).md` reframed the work toward detector-led short-horizon continuity rather than full MOT/ReID.
- Three subagents split the problem into native selector, gamepad motion/projection, and controller contract/safety. All converged on active-only weak association plus explicit target authority.
- A GitHub/open-source scan was used as a sanity check: most public FPS YOLO projects stop at detection/box selection, while this project already needed deeper controller-facing semantics.
- The final implementation came from combining local evidence with the research: keep detector as birth/fire authority, let low-score/cue evidence continue the active target briefly, and make runner/controller fail closed by source/tier.

## Verification Evidence

- Native build passed:
  - `powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1`
- Targeted combined regression passed:
  - `py -3 -B -m unittest tests.test_native_vision_targeting_bridge tests.test_native_vision_runner tests.test_vision_runner tests.gamepad.test_gamepad_auto_fire_plugin tests.gamepad.test_gamepad_controller_host tests.gamepad.test_gamepad_target_tracker tests.gamepad.test_gamepad_ai_aim_plugin tests.mouse.test_mouse_controller_host tests.test_native_vision_scaffold tests.test_config_loader -v`
  - Result: `222` tests OK.
- Related targeting/controller/config regression passed:
  - `py -3 -B -m unittest tests.test_native_vision_targeting_bridge tests.test_vision_targeting tests.test_vision_occlusion_compensation tests.gamepad.test_gamepad_ai_aim_plugin tests.test_config_loader -v`
  - Result: `128` tests OK.
- Python compile check passed for modified modules/tests.
- `git diff --check` passed; only LF/CRLF normalization warnings were printed.
- User live smoke tested and approved checkpointing this version.

## Recommended Next Actions

1. Treat this version as the current gameplay baseline after commit.
2. If live feel is too strong or sticky, tune conservatively:
   - `weak_target_body_lock_force_scale`
   - `cue_hold_body_lock_force_scale`
   - native weak association gates around confidence/distance
   - `body_lock_upper_body_ratio` around `0.41` to `0.44`
3. Longer next step: add replay/benchmark logs for source/tier, weak gate counts, cue age/score, fire request vs gate result, and controller final output.

## Do Not Do Without New Evidence

- Do not introduce full MOT, ReID, SLAM, dense optical flow, or long prediction-only target authority as the next step.
- Do not allow cue-only, weak-only, or predicted-only targets to birth a controller-trusted target or trigger auto-fire.
- Do not lower the detector threshold globally and feed all low-score boxes through normal birth/switch selection.
- Do not move the controller hot path to C++ before timing logs show Python/native communication is the bottleneck.
- Do not revert unrelated user or generated worktree changes.
- For training/data jobs, avoid heavy writes to `C:` and avoid RAM-backed modes unless the user explicitly approves.

## Related Decisions

- `decisions/DEC-2026-05-01-005-use-yellow-cue-as-short-continuation-hold.md`
- `decisions/DEC-2026-05-01-006-prioritize-native-hotpath-copy-reduction-over-full-controller-cpp-rewrite.md`
- `decisions/DEC-2026-05-05-001-add-external-yellow-cue-input-and-sidecar-fallback.md`
- `decisions/DEC-2026-05-05-002-scope-active-vision-work-to-native.md`
- `decisions/DEC-2026-05-29-001-single-target-weak-association-authority-gating.md`
