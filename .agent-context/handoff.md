# Agent Handoff

Last updated: 2026-07-14T22:20:00+08:00
Updated by: Codex
Active scope: Native C++ COD/FPS gamepad runtime, target selection, ADS/bodylock authority, tracker/controller feel, recoil isolation, native vision performance.
Staleness: stale after a runtime-entry change, detector/model baseline change, major native controller/selector behavior change, or live evidence that current native feel/perf regressed.

## Next Session Handoff (2026-07-14)

- Start here. The SDL freeze and tracker-only ADS jump are fixed on local dev at d22400b; do not reopen them without new evidence.
- New user-confirmed live problem: over-far or visually small targets still produce AI stick input. On weapons whose recoil is already mostly cancelled by recoil feedback and a small manual correction, the added AI contribution can cause lateral or upward jitter.
- Current hypothesis is AI-inferred, not proven: small/far detections receive more ADS authority than their visual evidence supports, and the assist becomes visible when combined with manual input plus final recoil feed-forward.
- Next action: add a deterministic small/far-target benchmark before changing controller code. Sweep apparent size, error, evidence, ADS state, manual input, and recoil feedback; score AI output, jitter, conflict, stability, and reacquisition.
- Preserve the boundary: recoil remains final feed-forward and must not consume vision/tracker state. Fix target/assist authority rather than feeding recoil state back into target control.
- Current aggressive config trial: telemetry disabled; ADS strength_scale=1.10, vertical_strength_scale=1.05, manual_opposition_suppression=0.40. Backup: runs/config_backups/config.before-strong-ai-profile-20260714-220245.toml.
- Tradeoff: standard ADS final error improved 12.14 -> 8.70px, but adversarial fight rose 130 -> 138, near-high output 380 -> 421, and max final output 0.809 -> 1.122. This is a live A/B trial, not an accepted baseline.
- Read next: decisions/DEC-2026-07-14-002-small-far-target-assist-authority.md, then the newest 2026-07-14 entry in session-log.md.

## Current Objective

Fix two reproduced live blockers in the native C++ runtime while preserving the accepted tracker/bodylock and ADS predictive-brake benchmark baseline:

1. SDL physical input freezes after a 0.53-0.77 second runtime stall and never recovers until restart.
2. ADS continues full-strength vertical snap during tracker-only `short_evidence_gap` continuity after production vision has lost the target.

The main architecture direction is now authority management: make vision provide candidates/evidence, make user input inform target selection and assist permission, keep tracker memory as continuity rather than truth, and separate ADS/bodylock control policies before further strength tuning.

## Immediate Live Blockers (2026-07-14)

- Four same-day telemetry sessions reproduce the input failure: after a `531-775ms` controller-sample gap, physical inputs never change again for the remaining `6.5s`, `50.8s`, `15.2s`, and `17.3s` of each session.
- The runtime prefers SDL input. `SdlGamepadReader::available()` only checks for a non-null handle, `read()` unconditionally reports `connected=true`, and the reader never checks `SDL_JoystickGetAttached` or reopens a detached device.
- ViGEm output health is also unobservable: `vigem_target_x360_update()` return codes are ignored and `output_sent_ns` means only that the call returned, not that the virtual device accepted the report.
- Four same-day telemetry sessions contain tracker-only ADS vertical output with no production vision target. Every material case is `assist_authority=continuity`, `assist_authority_reason=short_evidence_gap`; the largest observed `ai_y` is `-1.06677` / `+1.03637` with track age up to about `95.6ms`.
- Root policy issue: ADS and bodylock currently share the same full-scale `AimCoast` continuity decision. ADS must fail closed or stay tightly bounded without current observed evidence, while bodylock retains its separately benchmarked short-occlusion continuity.
- AI-inferred external trigger: a USB/Bluetooth/SDL detach likely causes the stale SDL handle. The logs prove the frozen handle behavior but do not identify the underlying transport event.
- Accepted decision: `.agent-context/decisions/DEC-2026-07-14-001-live-io-recovery-and-ads-continuity-boundary.md`.

## Immediate Live Blockers Resolution (2026-07-14)

- SDL now loads `SDL_JoystickGetAttached`; a detached handle returns a neutral disconnected state instead of replaying frozen axes. The runtime retries the original device name/control shape every 500 ms and never falls back to an arbitrary SDL device while recovering.
- ViGEm report return codes are checked. Failed delivery clears `output_sent_ns`, performs bounded backend recovery, retries the current report, and exposes delivery/error/reconnect health.
- Telemetry schema v4 records `physical_connected`, `current_observed_target_present`, `output_delivered`, output backend/error state, and input/output reconnect counts.
- ADS strong control now additionally requires the latest processed production frame to contain a target. A processed no-target frame keeps tracker identity/position continuity but emits no ADS AI output; bodylock/tracker projection ages are unchanged.
- New deterministic artifact: `runs/native_perf/native_live_failure_benchmark_20260714.json` (`observed_ai_y=0.546667`, 50 tracker-continuity frames, tracker-only AI Y peak `0`, manual Y error peak `0`, reconnect selection pass).
- Full gamepad artifact: `runs/native_perf/native_gamepad_benchmark_live_io_ads_continuity_recovery_20260714.json`. All compared key metrics equal the accepted ADS predictive-brake postedge baseline.
- Selector intent, ROI fallback, and bodylock continuity scenario payloads are byte-equivalent after JSON normalization to their postedge baseline payloads.
- Native verification: 30/30 `*tests.exe` passed; pipeline contract exited 0; runtime five-tick live smoke initialized current model, ViGEm output, vision service, and shutdown successfully.
- Vision same-condition three-run A/B: candidate GPU p95 median `15.480 ms`, old binary `15.356 ms` (+0.8%); current machine was much slower than the historical 2.16 ms run for both binaries, so historical-vs-current timing is not a valid code comparison.
- Scheduler same-condition three-run medians: candidate `999.214 Hz`, old binary `999.378 Hz`; p99 candidate `762.9 us`, old `746.4 us` (+2.2%). Both current runs show more OS jitter than the historical artifact, while the candidate stays within the relative 5% non-regression boundary.
- Python native scaffold has 8 pre-existing stale text-assertion failures on both clean `dev` and this branch; the other selected Python tests pass. Do not restore removed synchronous hot-loop behavior to satisfy those stale assertions.

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
- Native gamepad benchmark now includes adversarial controller diagnostics:
  - `ads_manual_carry_through_100hz`
  - `ads_bodylock_near_high_output_100hz`
  - `adversarial_controller_authority_100hz`
  - latest run: `runs\native_perf\native_gamepad_benchmark_adversarial_controller_20260707.json`
  - key defects from that run: wrong target `140`, user fight `433`, invalid strong `84`, stale high output `27`, err target `99`, recovery `100`, p95 error `74.7px`.
- Native gamepad benchmark now also reports ADS/bodylock split health metrics in `ads_manual_stress` and `ads_bodylock_near_high` JSON:
  - ADS unreliable acquisition: high output, same-direction manual/AI stacking, manual/AI fight, no-fresh-target high output, err-target high output, and mean/max final output while evidence is unreliable.
  - Bodylock close/centered tracking: close-assist samples and mean AI output, low-output close frames, dropout frames, centered samples, centered jitter frames, centered p95 output delta, and body-lock-only tracking/sustain metrics.
  - quick verification artifact: `runs\native_perf\native_gamepad_benchmark_metrics_expanded_20260707.json`.
- Native AimLab benchmark now has `cod_native_aimlab_benchmark` plus real selector-backed near-side-vs-far-front scenarios:
  - no intent: `final=0`, `wrong_ads=119`, `fight=119`
  - perfect lower-left intent: `final=100`, `wrong_ads=0`, `fight=0`
  - established manual clean/slow/noisy intent: `final=100`, `wrong_ads=0`, `fight=0`
  - late slow manual intent: `final=0`, `wrong_ads=67`, `fight=59`
- Live native runtime now builds `UserAimIntent` from physical right stick and passes it through `VisionEngine` into `VisionTargetSelector`; L3 no longer counts as aiming. LT now starts aim on a light press and exits aim as soon as the trigger shows a release drop, before it fully returns to zero.
- Native runtime perf logging now reports measured loop FPS from actual tick elapsed time instead of a hard-coded `1000`.
- Native aim perf JSON now includes explicit `vision_age_ms` and `output_age_ms` fields alongside legacy `age_ms`/`out_age_ms`.
- Native aim perf JSON now also writes controller bridge diagnostics while aiming:
  - manual/AI/final magnitudes
  - target error
  - manual-vs-AI fight and manual-vs-final fight
  - near-target high output
  - stale target
  - tracker projection and projected high output
  - aim authority without fire authority
  - component snapshots use `before_recoil_x/y` for the final controller stick before recoil feed-forward; avoid `pre_recoil` naming because pipeline contract treats that as a forbidden coupling pattern.
- Offline log bridge script: `tools\analyze_native_aim_diagnostics.py`.
  - Reads benchmark JSON plus one or more `native_aim_perf_*.jsonl` files.
  - Handles old logs by deriving counters from raw fields when `diagnostic_*` fields are absent.
  - Skips malformed JSONL rows and reports `invalid_rows`, because interrupted live logs may have partial rows.
- ADS resume now clears target/tracker state observed before the last ADS release, so the first resumed ADS frame waits for fresh vision instead of pulling an old target still inside TTL.
- ADS acquisition now applies a bounded output cap when the controller is in ADS acquisition without fresh target evidence:
  - kept artifact: `runs\native_perf\native_gamepad_benchmark_ads_authority_final_20260707.json`.
  - scorecard: `docs/project/NATIVE_CONTROLLER_BENCHMARKS.md`.
  - comparison against `runs\native_perf\native_gamepad_benchmark_metrics_expanded_20260707.json`:
    - `ads_diagonal_late_vision_fov_occlusion_50hz`: `final_error_px 27.120 -> 9.345`, `max_overshoot_px 66.928 -> 43.680`, `large50 2 -> 0`, `unreliable_no_fresh_target_high_output_frames 78 -> 0`.
    - `ads_diagonal_late_vision_fov_occlusion_50hz_dynamic_fire`: `final_error_px 17.196 -> 11.438`, `unreliable_no_fresh_target_high_output_frames 78 -> 0`; `max_overshoot_px 49.261 -> 52.375`, so this still needs a better fresh/wrong-target authority layer.
  - bodylock moving/slide/jump scenario metrics match the vector-cap artifact and baseline, so this slice did not change bodylock behavior.
  - verification on 2026-07-07: controller tests PASS, benchmark metrics tests PASS, gamepad benchmark self-test PASS, output validation tests exit 0, native pipeline contract PASS, and `git diff --check` has only CRLF warnings.
- Rejected trial: disabling `aim_authority` for projected suspicious ADS candidate states using crude body-box evidence improved some wrong-target numbers but regressed bodylock:
  - rejected artifact: `runs\native_perf\native_gamepad_benchmark_fresh_wrong_body_evidence_gate_20260707.json`.
  - `ads_bodylock_slide_visible_chase_100hz_dynamic`: `body_lock_frames 2406 -> 2226`, `body_lock_low_output_close_frames 251 -> 346`, `body_lock_dropout_frames 191 -> 286`, `max_overshoot_px 88.546 -> 99.824`.
  - Do not reintroduce this target-provider gate without a cleaner signal that separates ADS point-target authority from bodylock continuity.
- Durable decision accepted on 2026-07-07: future work should treat this as `candidate targets -> user intent selector -> target authority -> tracker memory decay -> separate ADS/bodylock policies -> manual/AI arbitration -> decision logging -> benchmark scoring`.

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
- Add an explicit authority/anti-intervention model before further controller tuning:
  - Do not let target selection collapse candidates too early.
  - User input should be a first-class intent signal, not only a stick value to mix with AI.
  - Tracker memory may smooth movement and short occlusion, but should decay when live evidence or user correction disagrees.
  - Strong ADS/bodylock authority should be evidence-gated by intent, cue/live/validity, target freshness, and corpse/stale-target risk.
  - Benchmarks and logs must measure both "helps well" and "does not help when it should not".

## Known Live Problems

- ADS snap consumes the selected strong target; it does not independently correct a wrong target choice.
- Multi-target cases can prefer a target that is more selector-friendly instead of matching user intent.
- Corpse/dead-target locking still needs stronger cue/validity authority handling.
- Tracker short memory is necessary for sliding, jumping, arc movement, and brief occlusion, but can become harmful if it overpowers live evidence or user correction.
- Most AimLab default scenarios still use fallback perfect scoring; expand them one by one before treating aggregate score as representative.
- Current near-side selector benchmark has both established manual profiles and a late slow profile. Treat the established profiles as proof that userInput can steer selector pickup; treat `near_side_vs_far_front_manual_slow_late` as evidence that late intent still leaves a sticky wrong-target risk.
- Current benchmark gap: it must score anti-intervention cases, including user correction, release intent, target switch intent, corpse/cue loss, stale tracker memory, near-target high output, and wrong-target fight.
- Current controller gap after the ADS no-fresh cap: err-target/fresh-but-wrong scenarios still show high output (`ads_diagonal_err_target_recovery_100hz_dynamic_fire` unchanged at `unreliable_err_target_high_output_frames=286`, `unreliable_no_fresh_target_high_output_frames=372`). Next fix should add explicit target-authority state instead of toggling raw `aim_authority` in `TargetSnapshotProvider`.
- Current benchmark/log bridge evidence from `native_aim_perf_20260707_010345_633.jsonl`:
  - rows `10491`, invalid rows `1`, target rows `2387`.
  - manual/AI fight `232`, manual/final fight `60`, near-high `436`, projected-high `215`, authority-without-fire `99`.
  - This was an older runtime log missing `diagnostic_*`, so the analyzer derived metrics from raw stick/target fields.

## Verification Rules

- For native controller/runtime behavior edits, add or update focused native unit tests in the same change.
- For tracker/controller/recoil boundary changes, run:
  - `scripts\verify\native_pipeline_contract.bat`
- For selector changes, run focused native selector tests and benchmark/self-test where relevant.
- For benchmark/log bridge changes, run:
  - `cod_native_benchmark_metrics_tests.exe`
  - `cod_native_gamepad_benchmark.exe --self-test`
  - `cod_native_gamepad_benchmark.exe --random-fov-ticks 0 --output runs\native_perf\native_gamepad_benchmark_adversarial_controller_20260707.json`
  - `python tools\analyze_native_aim_diagnostics.py --benchmark <benchmark.json> <native_aim_perf.jsonl>`
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
- Current accepted decision: `.agent-context/decisions/DEC-2026-07-07-001-ai-assist-authority-boundaries.md`
- Current benchmark spec: `docs/superpowers/specs/2026-07-06-native-aimlab-userinput-vision-benchmark-design.md`
- Native runtime docs: `docs/project/NATIVE_CPP_RUNTIME.md`
- Controller docs: `docs/project/GAMEPAD_OVERVIEW.md`, `docs/project/CONTROLLER_OVERVIEW.md`
- Native controller benchmark scorecard: `docs/project/NATIVE_CONTROLLER_BENCHMARKS.md`
