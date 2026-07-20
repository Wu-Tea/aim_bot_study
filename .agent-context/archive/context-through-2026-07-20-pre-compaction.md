# Agent Context Archive Through 2026-07-20 (Pre-Compaction)

This archive preserves the complete text for SyncSet sync-20260720-001. Source line endings are normalized to LF for repository consistency.

- handoff.md: 277 lines; normalized UTF-8 SHA-256 `15e08c68efa0a6093e3b8ec84ad70e29b9c4b00349021912521c0b5b3b1db527`
- session-log.md: 247 lines; normalized UTF-8 SHA-256 `3b4c11918ac30d6c38852dd388d040963c466f44d3765e5c360eb5a69efbcb2e`

## Original .agent-context/handoff.md

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
## 2026-07-16 Option B TargetCoordinator rewrite

- Active implementation branch/worktree: `codex/target-coordinator-rewrite` at
  `.worktrees/target-coordinator-rewrite`.
- Production controller is now:
  `IntentFilter + TargetCoordinator -> one TargetPlan -> ADS/BodyLock -> one shaper -> AutoFire -> Recoil`.
- The runtime target no longer links the old controller tracker, ADS completion/carry
  brake, old AI aim/dynamics, short-plan, authority/lifecycle, or validation stages.
- Left-stick `0.0118` drift, 100 Hz vision / 1000 Hz control, short occlusion hold,
  selected-target identity, manual opposition, BodyLock handoff, and unique-frame
  AutoFire readiness have focused tests.
- Final fixed seeds: gamepad `1337/1337/1337`, AimLab `12345`; gamepad self-test,
  full suite, left-stick `--require-fixed`, live failure, AutoFire, pipeline tests,
  log tests, and a real-model runtime smoke all pass.
- Acceptance/results: `docs/project/REFACTOR_B_ACCEPTANCE_20260716.md`.
- Final artifacts are `runs/native_perf/refactor_b_rewrite_{gamepad_final_seed1337,lstick_final,live_final}.json`
  in the main workspace.
- Do not tune gains before live validation. The accepted benchmark tradeoff removes
  BodyLock dropout/user fight/large overshoot but moving-chase mean error is higher.

## 2026-07-16 Controller config contract migration

- Implementation branch: `codex/controller-config-contract`, based on `dev` at
  `ec06d49`.
- Canonical target geometry is `[gamepad.tracker] aim_height_ratio = 0.365`.
  Deprecated `body_lock_upper_body_ratio` spellings translate once; canonical wins
  independent of file order and conflicts are diagnosed.
- Geometry is resolved once on fresh observation before `TargetCoordinator`. Upright
  and crouched boxes use the ratio; wide/low boxes preserve the Vision point clamped
  to the box; missing boxes preserve Vision. ADS, BodyLock, and coasting share the
  already-resolved target point.
- Profile-faithful benchmark mode is now the default and records effective
  Tracker/ADS/BodyLock configuration plus an empty override list. Historical artifacts
  without configuration provenance are `configuration-unproven`.
- Old anonymous scenario tuning is isolated behind stress-fixture mode. Stress mode
  fails closed until every override has field-level metadata; it cannot emit a
  misleading comparison artifact.
- Approved candidate config synchronized to the main workspace:
  ADS `1.54/1.26/150/160`, BodyLock `0.45/0.50/80/8`, manual escape `0.45/0.55`,
  tracker geometry `0.365`. Pre-change backup:
  `runs/config_backups/config.before-contract-migration-20260716.toml` (SHA-256 matched
  the source before editing).
- Trusted artifacts/seeds:
  - `runs/benchmarks/profile_faithful_seed1337.json`, seeds `1337/1337/1337`.
  - `runs/benchmarks/left_stick_fixed_seed1337.json`, five scenarios, defects `0`.
- A stale left-stick ADS handoff fixture was caught writing the retired BodyLock ratio;
  it produced `1e9` missing-transition sentinels. Switching the fixture to canonical
  tracker geometry changed the fixed gate from defects `1` to `0`.
- Focused Release tests passed for runtime config, target geometry, shared target
  pipeline, ADS, BodyLock, AutoFire, vertical BodyLock, and left-stick fixed gate.
- The trustworthy full suite exposes remaining behavior, not migration correctness:
  some diagonal cases have zero BodyLock frames under activation `80`, while moving
  chase cases still show BodyLock dropout/chatter and vertical manual opposition.
  Use the new artifact for the next tuning decision; do not compare raw scores against
  old silent-override files.

## 2026-07-17 Per-axis wrong-way intervention

- Implementation branch/worktree: `codex/ads-occlusion-benchmark` at
  `.worktrees/ads-occlusion-benchmark`.
- Normal/helpful/manual-escape behavior remains on the existing controller path.
  A small `AxisIntentArbiter` only removes manual-yield attenuation on the one axis
  confirmed wrong-way and worsening from stable Observed evidence.
- Confirmed evidence bridges at most 12 ms across ordinary inter-frame Coasting.
  Reacquiring/None, low reliability, target identity changes, size changes, and
  manual input at or above `0.45` clear or block intervention.
- The benchmark now separates stable-vision wrong-axis mistakes below escape from
  strong stale/crossing takeover during occlusion, records X/Y intervention frames,
  and no longer uses frame sequence as selected-target identity.
- Seed 1337 strict comparison: normal combat is bit-identical at `71.293475` with
  `0/0` intervention frames. Human-error score is `68.860482 -> 69.253609`;
  `wrong_x` mean error is `41.587511 -> 37.376675`, and `wrong_y` is
  `38.861727 -> 37.471628`. Stale/crossing cases are unchanged and all cases retain
  zero X/Y overshoot.
- Artifacts: `runs/benchmarks/axis_intervention_disabled_baseline_seed1337.json`
  and `runs/benchmarks/axis_intervention_accepted_seed1337.json`.
- Focused Intent/ADS/BodyLock/dynamics/arbiter/benchmark/controller tests pass;
  Release runtime builds at `D:/codex-build/pob/Release/cod_native_runtime.exe`.


## Original .agent-context/session-log.md

# Agent Session Log Index

Last updated: 2026-07-14T22:20:00+08:00
Updated by: Codex
Purpose: quick navigation for project continuity. Full older history is preserved in `session-log-full.md`; detailed recent fusion/canvas notes are archived under `archive/`.

## Reading Order

1. Read `handoff.md` for the current active objective and next action.
2. Read this index for recent milestones.
3. Open `decisions/` for durable architecture or scope decisions.
4. Open archive files only when deeper detail is needed.

## Current Active Thread

- 2026-07-14 - I/O/ADS repair completed; user-profile strong-AI trial; small/far-target jitter handed off.
  - Completed on local dev: d22400b adds SDL reconnect, checked ViGEm recovery, telemetry v4 I/O health, and an ADS current-observed-evidence boundary while preserving bodylock continuity.
  - Verification: 30/30 native tests passed; pipeline and live-failure benchmark passed; selector, ROI, and bodylock-continuity matched baseline; same-condition Vision p95 was +0.8% and scheduler lateness p99 +2.2%.
  - Profile: 416999 controller samples, 264696 active-manual samples, magnitude p50/p90/p99 0.306/0.790/1.034, direction preservation 97.53%, and 11339 bodylock samples.
  - Capability split: above 60px ADS error, manual/AI helpful rates were 63.7%/95.3%; AI was stronger-helpful in 77.8%. At 18-60px the rates were 56.1%/87.0%.
  - User selected aggressive trial: telemetry disabled; ADS horizontal/vertical 1.10/1.05; opposing-manual suppression 0.40; tracker/bodylock unchanged. Backup: runs/config_backups/config.before-strong-ai-profile-20260714-220245.toml.
  - Results: standard ADS final error 12.14 -> 8.70px and max overshoot 55.89 -> 44.74px. Tradeoffs: delayed-vision final error 6.95 -> 11.96px, adversarial fight 130 -> 138, near-high 380 -> 421, max final 0.809 -> 1.122.
  - New user-confirmed problem: over-far/visually small targets still generate AI input; with recoil feedback already stabilizing a low-recoil weapon and a small manual correction, AI can add lateral/upward jitter.
  - AI-inferred hypothesis: apparent target size/distance/evidence is insufficiently represented in authority. This is not benchmark-proven.
  - Follow-up: reproduce with a small/far-target plus recoil/manual benchmark; solve at authority while keeping recoil final and independent.
  - Context updated: handoff.md, session-log.md, and proposed decision DEC-2026-07-14-002-small-far-target-assist-authority.md.

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
  - Implemented: SDL attachment detection and bounded original-device reconnect; ViGEm checked delivery and bounded reconnect/retry; telemetry schema v4 I/O health; ADS current-production-evidence gate with bodylock continuity preserved.
  - Added benchmark: `cod_native_live_failure_benchmark` and artifact `runs/native_perf/native_live_failure_benchmark_20260714.json`; observed ADS assist remains active on evidence (`0.546667`) and becomes exactly zero for 50 tracker-only continuity frames while manual error remains zero.
  - Verification: 30 native test executables pass, native pipeline contract passes, gamepad/AimLab/selector/ROI/bodylock/telemetry/color/vision/scheduler benchmarks completed, and five-tick live runtime smoke passes with actual ViGEm initialization.
  - Baseline comparison: full gamepad key metrics unchanged; selector/ROI/bodylock scenario payloads identical; three-run same-condition vision and scheduler A/B stay within relative non-regression limits.
  - Known unrelated test debt: `tests/test_native_cpp_runtime_scaffold.py` has the same 8 stale source-text failures on clean `dev`; no regression was introduced.
  - Execution note: use the verified absolute tools `C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe` and `D:\env\python\python.exe`; bare `cmake` and `D:\env\python.exe` are invalid in the current PowerShell environment.
  - Follow-up: integrate the verified branch into local `dev`, rebuild the dev Release runtime, and retain live telemetry for a real physical SDL detach/reconnect confirmation when a controller is available.

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
