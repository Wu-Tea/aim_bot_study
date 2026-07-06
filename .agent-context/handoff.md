# Agent Handoff

Last updated: 2026-07-06T18:19:00+08:00
Updated by: Codex
Active scope: Native C++ COD/FPS gamepad runtime, target selection, ADS/bodylock authority, tracker/controller feel, recoil isolation, native vision performance.
Staleness: stale after a runtime-entry change, detector/model baseline change, major native controller/selector behavior change, or live evidence that current native feel/perf regressed.

## Current Objective

Fix live wrong-target lock and ADS authority issues in the native C++ runtime while preserving the current controller feel baseline.

The main live issue is multi-target selection: the user may intend a close side-running target, but selector/ADS can prefer a farther or more front-facing target. Current direction is to make user input inform target selection and ADS strong-snap eligibility before considering vision image cropping.

## Current State

- Workspace: `D:\work\AI\yolo-study-001`
- Branch: `dev`, with multiple unpushed local commits at last check.
- Recent baseline commits:
  - `5c5cb3d Score real selector intent in aimlab benchmark`
  - `d725a7f Wire user aim intent into native vision selection`
  - `acf99ab Add native aimlab benchmark executable`
  - `94742c2 Add target validity scoring for vision selection`
  - `328a670 Baseline target validity and pose benchmarks`
  - `579446c Improve ADS bodylock slide tracking`
- Default live gamepad runtime is full native C++:
  - `scripts\launch\gamepad_start.bat`
  - `GAMEPAD_RUNTIME=native`
  - `native\vision_native\build\Release\cod_native_runtime.exe --config config.toml --perf-log`
- Python gamepad/vision paths are fallback, debug, tools, training/export, or comparison paths, not the default live hot path.
- Mouse and `kbm_to_gamepad` still use Python-side hosts unless explicitly changed.
- Historical native timing evidence from 2026-06-07 showed typical `[Vision][CPP]` GPU timing around `6-10ms` and vision age around `8-12ms`; investigate native timing before assuming Python/native handoff bottlenecks.
- Recent video/live review conclusion: obvious assist systems that treat every detection as strong authority look too visible and can overshoot or hold wrong targets. This project should keep ADS/bodylock authority separated and evidence-gated.
- Native AimLab benchmark now has `cod_native_aimlab_benchmark` plus real selector-backed near-side-vs-far-front scenarios:
  - no intent: `final=0`, `wrong_ads=119`, `fight=119`
  - perfect lower-left intent: `final=100`, `wrong_ads=0`, `fight=0`
  - established manual clean/slow/noisy intent: `final=100`, `wrong_ads=0`, `fight=0`
  - late slow manual intent: `final=0`, `wrong_ads=67`, `fight=59`
- Live native runtime now builds `UserAimIntent` from physical right stick and passes it through `VisionEngine` into `VisionTargetSelector`; L3 also counts as aiming.
- Native runtime perf logging now reports measured loop FPS from actual tick elapsed time instead of a hard-coded `1000`.
- Native aim perf JSON now includes explicit `vision_age_ms` and `output_age_ms` fields alongside legacy `age_ms`/`out_age_ms`.
- ADS resume now clears target/tracker state observed before the last ADS release, so the first resumed ADS frame waits for fresh vision instead of pulling an old target still inside TTL.

## Current Design Direction

- Pass live user right-stick intent into native vision selection.
- AimLab now includes a deterministic `ManualInputModel` so benchmarks can simulate human-like manual input instead of only perfect intent:
  - reaction delay / slow input ramp
  - direction noise
  - overshoot and short reverse correction
  - low-strength or uncertain frames
  - stable intent derived from a short smoothed manual-input window
- Split broad vision detection from narrower ADS strong-snap eligibility.
- Use cue/live/validity evidence as authority gating, not only score bonus, especially for corpse-lock avoidance.
- Keep base vision crop broad for now; do not hard-crop vision from user input as the first fix.
- Consider user-input-guided soft ROI later as a performance/selection optimization with fallback full ROI.
- Preserve bodylock and ADS as different policies:
  - ADS should avoid large overshoot and wrong strong snaps.
  - Bodylock can tolerate some overshoot for moving close targets and should avoid sticky stalls.

## Known Live Problems

- ADS snap consumes the selected strong target; it does not independently correct a wrong target choice.
- Multi-target cases can prefer a target that is more selector-friendly instead of matching user intent.
- Corpse/dead-target locking still needs stronger cue/validity authority handling.
- Tracker short memory is necessary for sliding, jumping, arc movement, and brief occlusion, but can become harmful if it overpowers live evidence or user correction.
- Most AimLab default scenarios still use fallback perfect scoring; expand them one by one before treating aggregate score as representative.
- Current near-side selector benchmark has both established manual profiles and a late slow profile. Treat the established profiles as proof that userInput can steer selector pickup; treat `near_side_vs_far_front_manual_slow_late` as evidence that late intent still leaves a sticky wrong-target risk.

## Verification Rules

- For native controller/runtime behavior edits, add or update focused native unit tests in the same change.
- For tracker/controller/recoil boundary changes, run:
  - `scripts\verify\native_pipeline_contract.bat`
- For selector changes, run focused native selector tests and benchmark/self-test where relevant.
- Recoil remains final feed-forward playback and must not consume target dx/dy, tracker state, target freshness, or controller correction errors.
- Recoil feel contract:
  - uncalibrated profile Y output uses per-sample profile delta scaled by `profile_velocity_reference_ms`, not cumulative Y from fire start.
  - fallback feedback is the old constant feed-forward down-pull; live config was last recorded at `feedback_amount = 0.30`.
  - do not add timed/pulsed fallback shaping unless a focused native test proves the old linear fallback remains available.

## Open Background Follow-Ups

- Continue TensorRT/smaller-engine A/B tests only if GPU timing is again the limiting factor.
- Live-validate recoil feel after tracker/controller/recoil boundary changes.

## Do Not Do Without New Evidence

- Do not assume Python/native handoff is the live-gamepad bottleneck.
- Do not make Python gamepad changes expecting default native runtime behavior to change.
- Do not remove the Python fallback; it remains useful for comparison, tools, tests, and recovery.
- Do not give cue-only, weak-only, or predicted-only targets fire authority.
- Do not hard-crop vision by user input before intent-aware selection and ADS authority gating are benchmarked.
- Do not smooth final gamepad output after recoil unless live evidence shows tuned recoil/manual feel can tolerate it.
- Do not reintroduce controller-to-recoil target feedback without focused native contract tests.
- Do not revert unrelated user or generated worktree changes.
- For training/data jobs, avoid heavy writes to `C:` and avoid RAM-backed modes unless explicitly approved.

## Related Context

- Session index: `.agent-context/session-log.md`
- Full historical archive: `.agent-context/session-log-full.md`
- Recent fusion/canvas detail archive: `.agent-context/archive/2026-06-25-fusion-canvas.md`
- Current proposed decision: `.agent-context/decisions/DEC-2026-07-06-001-intent-aware-selection-before-vision-cropping.md`
- Current benchmark spec: `docs/superpowers/specs/2026-07-06-native-aimlab-userinput-vision-benchmark-design.md`
- Native runtime docs: `docs/project/NATIVE_CPP_RUNTIME.md`
- Controller docs: `docs/project/GAMEPAD_OVERVIEW.md`, `docs/project/CONTROLLER_OVERVIEW.md`
