# Agent Session Log Index

Last updated: 2026-07-14T13:45:00+08:00
Updated by: Codex
Purpose: quick navigation for project continuity. Full older history is preserved in `session-log-full.md`; detailed recent fusion/canvas notes are archived under `archive/`.

## Reading Order

1. Read `handoff.md` for the current active objective and next action.
2. Read this index for recent milestones.
3. Open `decisions/` for durable architecture or scope decisions.
4. Open archive files only when deeper detail is needed.

## Current Active Thread

- 2026-07-14 - Live SDL input freeze and tracker-only ADS vertical jump reproduced.
  - Goal: explain repeated complete loss of control until process restart and ADS vertical jumps when the user sees no target.
  - Evidence: inspected all four same-day live telemetry files produced between 11:34 and 12:03.
  - Input failure pattern: each session contains a `531-775ms` sampling/runtime stall followed by zero physical-input changes until termination; frozen tails last `6.5s`, `50.8s`, `15.2s`, and `17.3s`.
  - Code trace: native runtime prefers SDL; `SdlGamepadReader` does not load/check `SDL_JoystickGetAttached`, unconditionally reports connected after `SDL_JoystickUpdate`, and has no reopen path. Restart works because it creates a new SDL handle.
  - Output-health gap: `VirtualGamepad::update` ignores `vigem_target_x360_update` return codes, so telemetry cannot distinguish successful output from a rejected ViGEm update.
  - ADS vertical-jump evidence: material no-production-target ADS Y episodes number `50`, `83`, `10`, and `3` across the four sessions; every frame is tracker `continuity` with reason `short_evidence_gap`.
  - Peak tracker-only ADS Y output: `-1.06677` and `+1.03637`; observation age reaches about `95.6ms`, matching the configured/default `96ms` projection window.
  - Component attribution: peak frames have zero dynamic adjustment, ADS brake, carry brake, and recoil. The source is full-scale ADS assist retained by tracker continuity.
  - User-confirmed action: record the findings and begin the repair.
  - AI-inferred item: the external detach trigger is probably USB/Bluetooth/SDL device loss, but the current logs do not record transport events or attachment state.
  - Context files updated: `handoff.md`, this session log, and proposed decision `DEC-2026-07-14-001-live-io-recovery-and-ads-continuity-boundary.md`.
  - Follow-up: add failing native tests, implement SDL and ViGEm health/recovery, split ADS continuity authority from bodylock continuity, then run full native benchmarks against the accepted baseline.

- 2026-07-07 - Proposed vision red-team stability decision before optimization.
  - User reframed native vision work as a vulnerability-finding effort: first prove where the vision module fails to provide stable compute or timely results, then optimize based on evidence.
  - Proposed decision: `decisions/DEC-2026-07-07-002-vision-red-team-stability-before-optimization.md`.
  - Next low-participation work should start with reusable log/stability analysis using existing `native_aim_perf_*.jsonl` files, then synthetic/replay benchmarks for active/inactive switching, DXGI no-update gaps, cold activation spikes, GPU wait, and contention behavior.
  - Do not treat model retraining, higher capture frequency, keep-warm, always-on inference, or runtime threading changes as accepted solutions until the stability failure profile is measurable.

- 2026-07-07 - ADS no-fresh acquisition cap accepted; crude suspicious-target gate rejected.
  - Implemented an ADS acquisition cap for cases where the controller is still acquiring in ADS but does not have fresh target evidence.
  - Kept benchmark artifact: `runs\native_perf\native_gamepad_benchmark_ads_authority_final_20260707.json`.
  - Recorded the tracker/controller refactor comparison scorecard in `docs/project/NATIVE_CONTROLLER_BENCHMARKS.md`.
  - Main improvement vs `runs\native_perf\native_gamepad_benchmark_metrics_expanded_20260707.json`:
    - `ads_diagonal_late_vision_fov_occlusion_50hz`: `final_error_px 27.120 -> 9.345`, `max_overshoot_px 66.928 -> 43.680`, `large50 2 -> 0`, `unreliable_no_fresh_target_high_output_frames 78 -> 0`.
    - `ads_diagonal_late_vision_fov_occlusion_50hz_dynamic_fire`: `final_error_px 17.196 -> 11.438`, `unreliable_no_fresh_target_high_output_frames 78 -> 0`, but `max_overshoot_px 49.261 -> 52.375`.
  - Bodylock moving/slide/jump metrics stayed identical to the vector-cap baseline, so this is currently the safe slice to keep.
  - Rejected a target-provider experiment that disabled projected candidate aim authority based on missing body-box evidence. It made some wrong-target metrics look better but regressed bodylock slide/jump continuity; do not revive it without an explicit target-authority state.
  - Verification:
    - `cod_native_controller_tests.exe` PASS.
    - `cod_native_gamepad_benchmark.exe --self-test` PASS.
    - `cod_native_benchmark_metrics_tests.exe` PASS.
    - `cod_native_output_validation_tests.exe` exit code 0.
    - `scripts\verify\native_pipeline_contract.bat` PASS after renaming the diagnostic snapshot fields from `pre_recoil_x/y` to `before_recoil_x/y`.
    - `git diff --check` passed for touched files with only CRLF warnings.

- 2026-07-07 - Accepted AI assist authority-boundary direction.
  - User confirmed the current direction should be recorded as a durable decision and remain fully reportable in later sessions.
  - Accepted decision: `decisions/DEC-2026-07-07-001-ai-assist-authority-boundaries.md`.
  - Core framing: the problem is no longer only ADS/bodylock strength tuning; it is deciding when AI may strongly help, weakly track, observe, yield, or release.
  - Pipeline direction:
    - vision candidate targets and evidence
    - userInput/UserIntent-aware selector
    - target authority and assist-permission decision
    - tracker memory with evidence/user-intent decay
    - separate ADS/bodylock control policies
    - manual/AI arbitration
    - component plus decision logging
    - benchmark scoring for hit, overshoot, smoothness, and anti-intervention
  - Implementation should start with benchmark and decision logging coverage, then selector/authority changes, then ADS near-target policy, then bodylock tracking policy.
  - Important boundary: do not solve this by adding more controller-only special cases, a universal ADS/bodylock brake, or hard userInput vision crop before selection/authority behavior is measurable.

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
  - Next target recorded: replace/augment perfect user intent with deterministic manual-input profiles.
    - Goal: benchmark should simulate real user input that can be delayed, slow, noisy, low-strength, overshoot, and briefly reverse during correction.
    - Proposed benchmark model: `ManualInputModel` emits both physical-style `manual_stick` and smoothed `UserAimIntent`.
    - Initial profiles:
      - `manual_clean`: short reaction delay, low noise, mostly correct direction.
      - `manual_slow`: longer reaction delay, slow strength ramp, late braking.
      - `manual_noisy_recover`: higher noise, overshoot, short reverse correction frames.
    - Scoring target:
      - userInput correct -> selector should select intended target faster.
      - userInput temporarily wrong/reversing -> selector should not be dragged into unstable target switches.
      - output should report intent/helpfulness/fight so the result is not just perfect-intent upper bound.

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

- 2026-07-07 - Adversarial controller benchmark and runtime log bridge.
  - Added controller-backed adversarial benchmark coverage for:
    - ADS manual carry-through / same-direction acceleration into overshoot risk.
    - Bodylock near-target high output without brake coverage.
    - Wrong target, user-vs-AI fight, invalid strong authority, stale high output, err target, and recovery windows.
  - Latest benchmark artifact: `runs\native_perf\native_gamepad_benchmark_adversarial_controller_20260707.json`.
  - Key run output:
    - `ads_manual_carry_through_100hz`: same-direction accel `38`, near-high `47`, brake-active `205`, max overshoot `2.1px`.
    - `ads_bodylock_near_high_output_100hz`: near-high `720`, brake gap `720`, chatter `4`, p95 turn `1.9deg`.
    - `adversarial_controller_authority_100hz`: wrong `140`, fight `433`, invalid strong `84`, stale-high `27`, err `99`, recovery `100`, p95 error `74.7px`.
  - Native aim perf JSON now includes bridge diagnostics while aiming:
    - manual/AI/final magnitude, target error, manual-AI fight, manual-final fight, near-high output, stale target, tracker projection, projected high output, authority without fire.
  - Added `tools\analyze_native_aim_diagnostics.py` to summarize benchmark JSON and one or more `native_aim_perf_*.jsonl` logs using the same diagnostic vocabulary.
  - Analyzer can derive counters from old logs without `diagnostic_*` fields, skips partial malformed JSONL rows, and ignores 0-byte logs when selecting `--latest`.
  - Verification:
    - `cod_native_benchmark_metrics_tests.exe` PASS
    - `cod_native_gamepad_benchmark.exe --self-test` PASS
    - `cod_native_gamepad_benchmark.exe --random-fov-ticks 0 --output runs\native_perf\native_gamepad_benchmark_adversarial_controller_20260707.json` PASS
    - `cod_native_runtime` build PASS
    - `python -m py_compile tools\analyze_native_aim_diagnostics.py` PASS
    - `git diff --check` PASS
  - Runtime short-run note: `cod_native_runtime.exe --perf-log --max-ticks 30` created an empty aim JSONL because aim perf JSONL writes only while `aiming=true`; live LT aiming sessions will populate the new fields.

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
