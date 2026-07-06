# Agent Session Log Index

Last updated: 2026-07-06T15:35:00+08:00
Updated by: Codex
Purpose: quick navigation for project continuity. Full older history is preserved in `session-log-full.md`; detailed recent fusion/canvas notes are archived under `archive/`.

## Reading Order

1. Read `handoff.md` for the current active objective and next action.
2. Read this index for recent milestones.
3. Open `decisions/` for durable architecture or scope decisions.
4. Open archive files only when deeper detail is needed.

## Current Active Thread

- 2026-07-06 - Native AimLab benchmark and live intent wiring landed.
  - Added native data-only benchmark/scorer/executable across commits:
    - `252edda Add native aimlab benchmark scorer`
    - `3e0f35e Add synthetic aimlab benchmark scenarios`
    - `acf99ab Add native aimlab benchmark executable`
  - Added live user intent wiring in `d725a7f Wire user aim intent into native vision selection`:
    - `RuntimeLoop` builds `UserAimIntent` from physical right stick every tick.
    - `VisionEngine` stores and passes the latest intent to `VisionTargetSelector`.
    - Intent metadata is copied back into `VisionResult`.
    - L3/left-thumb now counts as aiming.
  - Added selector-backed AimLab scenario in `5c5cb3d Score real selector intent in aimlab benchmark`.
  - Added fail-closed behavior for unknown AimLab scenario names after this milestone, so typos no longer synthesize perfect passing reports.
  - Current benchmark contrast:
    - `near_side_vs_far_front_no_intent final=0 wrong_ads=119 fight=119 helpful=0`
    - `near_side_vs_far_front_intent final=100 wrong_ads=0 fight=0 helpful=1`
  - Verification run during implementation:
    - `cod_native_aimlab_benchmark_tests` build PASS
    - `cod_native_aimlab_benchmark_tests.exe` PASS
    - `cod_native_aimlab_benchmark` build PASS
    - `cod_native_aimlab_benchmark.exe` PASS
    - `cod_native_target_selector_tests` PASS
    - `cod_native_runtime` build PASS
    - `scripts\verify\native_pipeline_contract.bat` PASS
    - `git diff --check` PASS
  - Remaining benchmark limitation: most default scenarios still use fallback perfect scoring. Expand `multi_target_flick`, `corpse_cue_loss`, and `err_target_recovery` into selector/controller-backed scenarios before trusting aggregate score.

- 2026-07-06 - Intent-aware target selection and ADS authority direction.
  - User reported live multi-target mislock: intended target was a close side-running enemy at lower-left, but ADS snapped to a farther/smaller front-facing target on the right/up.
  - Investigation found the native selector has `UserAimIntent` overloads, but the live `VisionEngine` path has not been confirmed to pass user input into selector.
  - Design spec committed: `docs/superpowers/specs/2026-07-06-native-aimlab-userinput-vision-benchmark-design.md`.
  - Current issue summary:
    - Multi-target selection lacks live user intent.
    - ADS strong snap eligibility is too broad relative to user intent and target evidence.
    - Cue/corpse evidence should gate strong authority more explicitly.
    - Bodylock and ADS need different overshoot/authority policies.
    - Tracker memory is useful for movement/occlusion but can become harmful if it overpowers live evidence or user correction.
  - Preferred next direction:
    - Pass userInput/UserAimIntent into `VisionTargetSelector`.
    - Keep the base vision crop broad.
    - Use intent/evidence gating for ADS strong snap.
    - Defer actual userInput-based image crop to a later soft-ROI stage.
  - Proposed decision: `decisions/DEC-2026-07-06-001-intent-aware-selection-before-vision-cropping.md`.

- 2026-07-06 - Third-party assist/video review.
  - User provided examples showing assist that appears to keep high-strength following inside detection range without good friend/enemy or authority separation.
  - Project takeaway: avoid detection-range == control-range. Keep ADS/bodylock policies separate, keep weak/cue/predicted tiers limited, and test ADS overshoot explicitly so behavior does not look like uncontrolled strong following.

- 2026-07-06 - Recent native selector baseline.
  - Recent commit `94742c2 Add target validity scoring for vision selection` added selector target validity scoring, live/corpse/uncertainty fields, adapter downgrade behavior, and regression tests.
  - Recent baseline commit before it: `328a670 Baseline target validity and pose benchmarks`.
  - Verification at commit time included target selector tests, controller tests, benchmark self-test, native pipeline contract, and `git diff --check`.

- 2026-07-06 - Compaction information-preservation audit.
  - After compacting `handoff.md` and `session-log.md`, old `HEAD` versions were compared against current context files.
  - Restored important startup constraints that were too weak after the first compaction:
    - mouse/`kbm_to_gamepad` Python-side boundary
    - 2026-06-07 native timing evidence
    - recoil per-sample/fallback feel contract
    - native perf/output-age/ADS-resume/TensorRT/recoil live-validation follow-ups
    - Python fallback preservation and training/data write-location caution
  - Fusion/canvas details remain archived at `archive/2026-06-25-fusion-canvas.md`.

## Recent Milestones

- 2026-06-25 - Performance-first fusion canvas execution decision recorded.
  - Decision: `decisions/DEC-2026-06-25-001-performance-first-fusion-canvas.md`.
  - Direction: do not build full audio+visual fusion first and optimize later; build a usable vision-target canvas/publisher skeleton with performance and kill-switch boundaries.
  - First usable target: show native vision target or explicit all-detections debug on fullscreen canvas while preserving native runtime hot-path isolation.
  - Prior audio/visual architecture notes archived at `archive/2026-06-24-audio-visual-fusion-plan.md`.
  - Detailed implementation notes archived at `archive/2026-06-25-fusion-canvas.md`.

- 2026-06-25 - Fusion canvas usability/capture corrections.
  - Default overlay became target-dot only, not all detection boxes.
  - Target marker mapping was corrected to use center plus `VisionResult.dx/dy` restored to capture pixels.
  - Stale target data clears after 250ms; default idle mode is hidden, with optional crosshair mode.
  - Canvas attempts `SetWindowDisplayAffinity(..., WDA_EXCLUDEFROMCAPTURE)` to reduce feedback into Windows capture.
  - Follow-up if interference remains: process-window-aware DXGI ROI alignment before swapchain injection.

- 2026-06-15 - Native tracker/controller/recoil boundary contract implemented and documented.
  - Recoil remains final feed-forward playback and must not consume target dx/dy, tracker state, target freshness, or controller correction errors.
  - Tracker receives component-aware final camera motion while manual/assist/dynamics/recoil/final components remain separately attributed.
  - Verification script: `scripts\verify\native_pipeline_contract.bat`.

- 2026-06-04 - Recoil despike and aim-assist dynamics direction accepted.
  - Decision: `decisions/DEC-2026-06-04-001-recoil-despike-and-assist-dynamics.md`.
  - Preserve tuned recoil strength/timing/feel while removing spikes/micro-jitter.
  - Smooth only AI assist delta in `AimAssistDynamicsPlugin`; do not delay manual input or recoil output.

- 2026-05-29 to 2026-05-30 - Weak association and authority gating baseline.
  - Decision: `decisions/DEC-2026-05-29-001-single-target-weak-association-authority-gating.md`.
  - Added weak/low-score continuation, cue hold, source-aware controller behavior, fire fail-closed authority, and lighter weak/cue bodylock.
  - User live-tested and accepted the version as a strong but usable baseline.

## Durable Working Rules

- Native C++ is the default live gamepad runtime; Python is fallback/debug/reference.
- Mouse and `kbm_to_gamepad` remain Python-side unless explicitly changed.
- C++ behavior edits should include focused native tests in the same change.
- Run native pipeline contract verification after tracker/controller/recoil boundary edits.
- Do not grant fire authority to cue-only, weak-only, or predicted-only targets.
- Keep recoil independent from tracker/controller target feedback.

## Archive Map

- `session-log-full.md`: full older project history and compact summaries through early June 2026.
- `archive/2026-06-24-audio-visual-fusion-plan.md`: detailed audio direction, fullscreen canvas, fusion process/IPC authority notes.
- `archive/2026-06-25-fusion-canvas.md`: detailed June 25 fusion canvas usability, idle behavior, and capture-feedback notes.
- `research-2026-06-24-visual-audio-fusion.md`: visual/audio fusion research checkpoint.
- `research-2026-06-24-fullscreen-canvas-fusion.md`: fullscreen canvas research and design direction.
- `research-2026-06-24-audio-direction-github-scan.md`: GitHub scan for audio direction references.

## Relevant Decision Records

- `decisions/DEC-2026-05-01-005-use-yellow-cue-as-short-continuation-hold.md`
- `decisions/DEC-2026-05-05-001-add-external-yellow-cue-input-and-sidecar-fallback.md`
- `decisions/DEC-2026-05-29-001-single-target-weak-association-authority-gating.md`
- `decisions/DEC-2026-06-04-001-recoil-despike-and-assist-dynamics.md`
- `decisions/DEC-2026-06-20-001-reject-cuda-array-preprocess.md`
- `decisions/DEC-2026-06-25-001-performance-first-fusion-canvas.md`
- `decisions/DEC-2026-07-06-001-intent-aware-selection-before-vision-cropping.md`

## Maintenance Notes

- Keep `handoff.md` short and current.
- Keep this index readable; move detailed history to archive files.
- Do not store secrets, tokens, cookies, keys, or unnecessary personal data.
- Do not use `.agent-context/` as a task ledger, scheduler, or external issue tracker.
