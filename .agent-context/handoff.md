# Agent Handoff

Last updated: 2026-06-04T23:42:57+08:00
Updated by: Codex
Active scope: COD/FPS native targeting, gamepad aim assist feel, and recoil playback smoothing.
Staleness: stale after recoil/aim dynamics implementation, a new detector/model baseline, another targeting/controller contract change, or live evidence that assist/recoil smoothing hurts weapon feel.

## Current Objective

Prepare the next controller-feel iteration on top of the live-tested native YOLO single-target baseline: remove recoil profile curve spikes without changing tuned weapon feel, and make AI aim assist output smoother without delaying user manual input.

## Current State

- Branch: `dev` in `D:\work\AI\yolo-study-001`.
- Baseline before this version: `a196f92 Improve gamepad body-lock lateral hold`.
- Recent commits include `cf0c11c Tune target lock ratio to chest` and `94cdc1a Remove bundled official YOLO artifacts`.
- The system uses native YOLO/TensorRT person detection, native target selection, Python runner handoff, and gamepad controller-side projection.
- User-provided report `D:/Downloads/deep-research-report (14).md` and subagent synthesis led to the accepted direction in `decisions/DEC-2026-05-29-001-single-target-weak-association-authority-gating.md`.
- User live feedback on 2026-05-30: the change feels very strong, possibly "a bit too strong"; acceptable to checkpoint as a version.
- User follow-up on 2026-06-04: some weapons with recoil enabled make the camera feel shaky; recoil parameters were tuned carefully, so do not change overall recoil feel. Accepted next direction is in `decisions/DEC-2026-06-04-001-recoil-despike-and-assist-dynamics.md`.

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
- Adjusted default body-lock/selector aim point from head-biased `0.38` to chest-biased `0.43`, then to `0.40` after live feedback that `0.43` was slightly too low.
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

1. Treat the native YOLO/authority-gated targeting version as the current gameplay baseline.
2. Implement recoil profile despike at profile read/activation time: generate a playback cache that removes obvious local delta spikes without overwriting original recoil records or changing total recoil feel.
3. Implement an `AimAssistDynamicsPlugin` after `AIAimPlugin` and before recoil playback: smooth only `output.right_stick - frame.manual_right_stick`, leaving manual input and recoil output untouched.
4. Longer next step: add replay/benchmark logs for source/tier, weak gate counts, cue age/score, fire request vs gate result, controller final output, and assist/recoil output deltas.

## Do Not Do Without New Evidence

- Do not introduce full MOT, ReID, SLAM, dense optical flow, or long prediction-only target authority as the next step.
- Do not allow cue-only, weak-only, or predicted-only targets to birth a controller-trusted target or trigger auto-fire.
- Do not lower the detector threshold globally and feed all low-score boxes through normal birth/switch selection.
- Do not move the controller hot path to C++ before timing logs show Python/native communication is the bottleneck.
- Do not smooth final gamepad output after recoil; that would also change tuned recoil and manual feel.
- Do not globally low-pass recoil curves unless live evidence shows conservative despike is insufficient.
- Do not revert unrelated user or generated worktree changes.
- For training/data jobs, avoid heavy writes to `C:` and avoid RAM-backed modes unless the user explicitly approves.

## Related Decisions

- `decisions/DEC-2026-05-01-005-use-yellow-cue-as-short-continuation-hold.md`
- `decisions/DEC-2026-05-01-006-prioritize-native-hotpath-copy-reduction-over-full-controller-cpp-rewrite.md`
- `decisions/DEC-2026-05-05-001-add-external-yellow-cue-input-and-sidecar-fallback.md`
- `decisions/DEC-2026-05-05-002-scope-active-vision-work-to-native.md`
- `decisions/DEC-2026-05-29-001-single-target-weak-association-authority-gating.md`
- `decisions/DEC-2026-06-04-001-recoil-despike-and-assist-dynamics.md`
