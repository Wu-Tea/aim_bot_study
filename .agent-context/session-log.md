# Agent Session Log Index

Last updated: 2026-05-20T17:04:02+08:00
Updated by: Codex
Purpose: quick navigation for project continuity. The complete historical log is preserved in `session-log-full.md`.

## Reading Order

1. Read `handoff.md` for the current active objective and next action.
2. Read this index for the short map of recent context.
3. Open `session-log-full.md` only when detailed history is needed.
4. Open `decisions/` for durable architecture or scope decisions.

## Current Active Thread

- 2026-05-20T17:04:02+08:00 - Cleaned recoil runtime follow-up for commit.
  - Removed a superseded same-day config diagnostics plan and trimmed `handoff.md` down to the current recoil runtime state, verification, and next live checks.
  - Cleaned current recoil playback naming so profile X output is no longer labeled as feedback in code; `feedback_amount` remains fallback-only.
  - Updated recoil docs to say fallback happens when no matching profile exists, not when profile quality checks fail.
  - Recoil commit scope should include only recoil/runtime/gamepad/config/docs/tests/context files; unrelated mouse/config/IDE dirty files remain outside scope.
- 2026-05-20T16:54:35+08:00 - Removed runtime profile-quality gates.
  - User requested that recoil runtime stop rejecting profiles for support/confidence/disagreement style quality checks and simply use matching weapon profiles.
  - Changed runtime profile selection so any profile matching `game`, `canonical_weapon_id`, `stance`, and `aim_mode` is selected for trial playback.
  - Removed profile-quality filtering from `RecoilProfileStore.get_best_profile()`, `profile_ids_for_weapon()`, sidecar `load_matching_profiles()`, and gamepad sidecar payload handling. A sidecar payload with a profile id is now usable even if its status is `degraded`.
  - Kept audit/status findings such as `accepted_episodes_below_min`, `support_below_min`, `vertical_direction_reversal`, `vertical_recovery_tail`, and `horizontal_episode_disagreement` as diagnostics only.
  - Kept record mode's clean-profile check separate through `get_best_quality_ready_profile()` so low-support profiles can still be supplemented by additional recordings.
  - Current artifacts audit as `runtime_ready=true` for all loaded profiles. `先驱` ADS is now ready for trial playback despite `accepted_episodes_below_min` and `support_below_min`; `小动脉` hipfire is ready despite `horizontal_episode_disagreement`.
  - Verification passed: focused recoil/gamepad/config suite (`132` tests OK), py_compile for touched runtime/sidecar/controller/audit/test modules, current profile audit, and profile-store status check.
  - Remaining risk: no live `gamepad_start.bat` smoke was run after removing runtime profile-quality gates; profiles with findings may feel bad and should be re-recorded for quality, but they now run.
- 2026-05-20T16:25:39+08:00 - Allowed vertical-recovery-tail profiles for runtime trial playback.
  - User showed live logs where `小动脉` fell back to `20%` because `vertical_recovery_tail` blocked the only ADS profile, making `profile_amount` and `profile_x_amount` appear unused.
  - Confirmed `config.toml` was being read: local values were `profile_amount = 0.4`, `profile_x_amount = 1.65`, and `feedback_amount = 0.2`, matching the observed `fallback=20%`.
  - Changed `vertical_recovery_tail` from runtime hard block to audit warning. Low accepted/support count, vertical direction reversal, and horizontal episode disagreement remain runtime blockers.
  - Audit still reports the warning, but now reports current `profile-cod22-小动脉-ads-standing-current` as `runtime_ready=true`.
  - Profile store check now resolves the current ADS profile as ready for trial playback.
  - Dry-run on current `小动脉` ADS profile reports `calibrated=false`, `profile_amount=0.4`, `profile_x_amount=1.65`, `feedback_amount=0.2`, peak `right_x=889`, peak `right_y=6899` in the first 12 frames.
  - Verification passed: focused recoil/gamepad/config suite (`132` tests OK), py_compile for touched readiness/audit/runtime tests, current profile audit, dry-run, profile-store check, and `git diff --check` with LF/CRLF warnings only.
  - Remaining risk: no live `gamepad_start.bat` smoke was run after allowing vertical-recovery-tail trial playback; the profile is usable for tuning but still not clean recording evidence.
- 2026-05-20T16:08:30+08:00 - Simplified gamepad recoil tuning to three knobs.
  - User asked to discard today's over-designed recoil runtime coefficients and keep only normal profile strength, X-axis enhancement, and feedback down-pull.
  - Updated `RecoilCompensationConfig`, config loading, dry-run CLI/output, gamepad host tests, current docs, and local ignored `config.toml`.
  - Current semantics: `profile_amount` scales recorded-profile X and Y; `profile_x_amount` adds extra multiplier only to recorded profile X deltas; `feedback_amount` is only the fixed fallback down-pull when no ready profile is active.
  - Removed current runtime/config/dry-run use of gamepad recoil `amount`, `feedback_x_amount`, `feedback_y_amount`, and `horizontal_profile_scale`.
  - Local ignored `config.toml` now uses `profile_amount = 0.25`, `profile_x_amount = 2.2`, `feedback_amount = 0.25`.
  - Dry-run on current `小动脉` ADS profile reports `calibrated=false`, `profile_amount=0.25`, `profile_x_amount=2.2`, `feedback_amount=0.25`, peak `right_x=741`, peak `right_y=4312` in the first 12 frames.
  - Verification passed: focused recoil/gamepad/config suite (`131` tests OK), py_compile for touched modules/tests, current profile audit, dry-run, and `git diff --check` with LF/CRLF warnings only.
  - Remaining risk: current `小动脉` ADS profile is still blocked by `vertical_recovery_tail`; no new clean live recording, calibration capture, or live `gamepad_start.bat` recoil smoke was run.
- 2026-05-20T15:45:50+08:00 - Added per-axis recoil feedback tuning and applied requested X/Y values.
  - User requested two changes from live impact feedback: increase X-axis coefficient and reduce vertical feedback coefficient.
  - Added `RecoilCompensationConfig.feedback_y_amount`, config loader support, active-profile log output, and dry-run JSON/CLI support.
  - `feedback_y_amount` scales only profile-driven vertical output; X feedback still uses `feedback_amount * feedback_x_amount`.
  - Local ignored `config.toml` now uses `amount = 0.30`, `horizontal_profile_scale = 0.0`, `feedback_amount = 2.00`, `feedback_x_amount = 1.35`, `feedback_y_amount = 0.80`.
  - Dry-run on current `小动脉` ADS profile reports `feedback_x_amount=1.35`, `feedback_y_amount=0.8`, peak `right_x=3637`, peak `right_y=5452` in the first 20 frames.
  - Verification passed: focused recoil/gamepad/config suite (`110` tests OK) and py_compile for touched gamepad/config/dry-run modules.
  - Remaining risk: current `小动脉` profile is still blocked by `vertical_recovery_tail`; live smoke should use a clean audit-passing profile.
- 2026-05-20T15:21:18+08:00 - Blocked current bad recoil profiles with recovery-tail and horizontal-disagreement gates.
  - User reported the latest X-axis change still did not solve live residual impacts and asked whether the recording or gamepad assist was actually wrong.
  - Offline artifact diagnosis showed current `小动脉` ADS/hipfire profiles are bad recordings/fits: both have large vertical recovery tails, and raw episodes disagree sharply in final X direction.
  - Added TDD coverage for `vertical_recovery_tail` and `horizontal_episode_disagreement` in readiness, audit, and magazine extraction fit summaries.
  - Updated `tools/audit_recoil_profiles.py` to output numeric diagnostics. Current audit now reports ADS and hipfire as `runtime_ready=false` with `vertical_recovery_tail`.
  - Reset local ignored `config.toml` to `horizontal_profile_scale = 0.0`; right-left-right X correction should use per-sample feedback rather than cumulative X playback.
  - Verification passed: focused recoil/gamepad/config suite (`109` tests OK), py_compile for touched recoil/config/tools modules, current profile audit, dry-run, and `git diff --check` with LF/CRLF warnings only.
  - Remaining risk: no new clean live recording, no measured calibration, and no live `gamepad_start.bat` smoke after the bad profiles were blocked.
- 2026-05-20T15:04:00+08:00 - Split recoil profile strength from X-axis feedback strength.
  - User clarified vertical recoil compensation is already acceptable and X-axis assistance is the remaining problem; previous `horizontal_profile_scale` amplified cumulative X and made one right-left-right segment worse.
  - Updated `RecoilCompensationConfig`: `amount` remains base recoil/profile/fallback strength, `horizontal_profile_scale` is now default `0.0`, and new `feedback_amount` plus `feedback_x_amount` drive per-sample X delta feedback.
  - Runtime log now reports `amount`, `x_scale`, `feedback`, and `feedback_x`.
  - Local ignored `config.toml` now uses `amount = 0.27`, `horizontal_profile_scale = 0.0`, `feedback_amount = 2.00`, `feedback_x_amount = 1.00`.
  - Dry-run on current `小动脉` ADS first 12 frames reports peak `right_x=2061`, `right_y=3568`; hipfire first 12 frames reports peak `right_x=906`, `right_y=1954`.
  - Verification passed: focused recoil/gamepad/config suite (`104` tests OK), py_compile for touched recoil/config/dry-run modules and tests, profile audit, and scoped `git diff --check` with LF/CRLF warnings only.
  - Remaining risk: no measured calibration file and no live `gamepad_start.bat` smoke after the feedback split.
- 2026-05-20T14:34:00+08:00 - Added offline recoil profile playback dry-run.
  - Completion audit found X-axis TDD/config/docs/context work was covered, but complete goal evidence still lacked calibration and a replay dry-run/live smoke artifact.
  - Added `tools/dry_run_recoil_playback.py`, which loads a recoil profile, optional config/calibration, and runs frames through the real `RecoilCompensationPlugin` to emit JSON `right_x`/`right_y` stick output.
  - Added `tests/recoil_collection/test_playback_dry_run.py` with red-green coverage for horizontal scaling and CLI JSON output.
  - Ran dry-run on current `小动脉` ADS and hipfire profiles. Both report `calibrated=false` and `horizontal_profile_scale=1.5`; first 12-frame ADS peak output is `right_x=3221`, `right_y=3568`; first 12-frame hipfire peak output is `right_x=858`, `right_y=1954`.
  - Verification passed: focused recoil/gamepad/config suite now `102` tests OK, py_compile for the dry-run tool and touched recoil/config modules, profile audit, and scoped `git diff --check` with LF/CRLF warnings only.
  - Remaining risk: no measured calibration file and no live `gamepad_start.bat` smoke after the X-axis scale change.
- 2026-05-20T14:29:00+08:00 - Recoil X-axis profile playback strengthened after live residual lateral jump report.
  - User showed bullet impacts still jumping left/right and reported X-axis recording/compensation felt too small.
  - Evidence: current `小动脉` ADS/hipfire profiles do contain X motion, but X peak is only about `17%` of Y peak; uncalibrated replay previously used only global `amount`, making lateral correction too conservative.
  - Added `RecoilCompensationConfig.horizontal_profile_scale` default `1.50`, config loader support for `[gamepad.recoil].horizontal_profile_scale`, active-profile log output `x_scale=...`, docs, plan follow-up checkboxes, and local ignored `config.toml` value `horizontal_profile_scale = 1.50`.
  - Verification passed: focused recoil/gamepad/config suite (`100` tests OK), py_compile for touched recoil/config/test modules, current profile audit, and scoped `git diff --check` with LF/CRLF warnings only.
  - Remaining risk: no live `gamepad_start.bat` smoke has been run after the X-axis scale change; calibration is still absent, so runtime remains an uncalibrated trial.
- 2026-05-20T00:00:00+08:00 - Recoil record/replay reliability plan implemented and verified.
  - Implemented offline profile audit tooling, episode diagnostics, central static-ROI motion estimation, strict magazine profile fit/readiness checks, calibration records, calibration-aware gamepad playback, runtime/sidecar calibration gating, timeline plots, and live validation docs.
  - Commits created on `dev`: `76bf13c`, `c8b28d1`, `111f843`, `a5b03da`, `5321bbb`, `d765781`, `4d13968`, `246c13c`, `300a9d3`.
  - Verification passed: focused recoil reliability unittest suite (`91` tests), py_compile for touched recoil modules/tools, `git diff --check` with LF/CRLF warnings only, and current artifact audit.
  - Current artifact audit intentionally reports existing local `小动脉` profiles as not runtime-ready: ADS has `accepted_episodes_below_min`, `support_below_min`, and `vertical_direction_reversal`; hipfire has `confidence_below_min` and `support_below_min`.
  - Live validation remains pending: record 3+ full magazines, inspect timeline plots, create matching calibration under `artifacts/recoil_calibration/`, rerun audit, then dry-run/live smoke `gamepad_start.bat`.
- 2026-05-19T21:01:32+08:00 - New draft input recorded: AI abstention for scalp-only targets and recoil recognition reliability.
  - User raised two trust-boundary problems: scalp/head-only positions may need AI to stop intervening and leave aiming fully manual; `recoil_app_start` OCR weapon-name recognition and recoil-curve recognition feel unreliable.
  - Draft plan written: `docs/superpowers/plans/2026-05-19-ai-abstention-and-recoil-recognition-draft.md`.
  - Initial code read found no first-class `head_only/scalp_only/abstain` state in the current target/controller contract.
  - Initial recoil read found OCR and profile confidence concepts already exist, but profile confidence is not yet a hard readiness gate and switch-name OCR can still be too trusting.
  - No behavior code changed for this draft; implementation should wait for user confirmation of the scalp-only intervention policy.
- 2026-05-19T20:53:26+08:00 - Native/gamepad contract hardening implemented and context synced.
  - Implemented atomic `ControllerVisionState` submission, auto-fire source freshness, neutral gamepad shutdown, end-to-end timing metrics, explicit native empty-gap clearing policy, config/docs cleanup, and benchmark config snapshots.
  - Native empty detection frames now clear target/fire state; Python fallback prediction remains an explicit policy difference rather than a parity requirement.
  - Native engine path honors `VISION_MODEL_PATH`; `config.toml.example` exposes model path, auto-fire, and recoil fallback knobs.
  - Native build passed. Split targeted verification passed across config/main/perf/native scaffold, native runner/parity/bridge, controller/auto-fire, benchmark runners, vision, gamepad, mouse, recoil, and weapon identity suites.
  - Full unittest discovery timed out in this environment; rely on the recorded split targeted suites unless a future run proves otherwise.
  - Live `gamepad_start.bat` gameplay smoke is still pending.
- 2026-05-18T21:26:02+08:00 - Multi-POV native/gamepad review recorded and hardening plan landed.
  - Review covered native vision latency/freshness, gamepad hand-feel/safety, and long-term architecture around the `native-vision` to `gamepad-controller` boundary.
  - Highest-priority issues: non-atomic vision/controller update, auto-fire freshness gap, incomplete age semantics, native capture-gap policy, exit neutralization, and config/doc drift.
  - Plan written: `docs/superpowers/plans/2026-05-18-native-gamepad-contract-hardening-plan.md`.
  - Review-time verification: `71` selected native/gamepad tests passed.
  - No business code changed in this record/plan pass.
- 2026-05-12T14:32:48+08:00 - Native vision freshness and wide-low posture selector committed.
  - Latest implementation commit: `c38e129 Improve vision target freshness and wide-low selection`.
  - `ControllerTarget.observed_at` now propagates native `age_ms` or Python capture time to gamepad and mouse controllers, so stale vision results are not restamped as fresh controller targets.
  - Native and Python selectors now share wide-low posture handling for prone/side-like boxes: lower aspect gate, target point at 0.50 height, and upper-in-box color/cue ROI.
  - Verification: native build passed; 160 selected tests passed; `git diff --check` passed with LF/CRLF warnings only.
  - Known residual: `tests.test_native_vision_synthetic_parity` still has the previous occlusion parity failure; live gameplay has not been smoke-tested.
- 2026-05-12T14:32:48+08:00 - Auto-fire manual takeover and commit-discipline cleanup committed.
  - `542c3de Fix manual auto-fire takeover window` holds fire output release, then suppresses auto-fire until release + delay totals 120ms.
  - `9e09a52 Add agent commit discipline guidance` adds `AGENT.md`; context-only progress should not be committed as checkpoint commits.
  - Verification before the auto-fire commit: 30 selected gamepad tests passed; `git diff --check` passed with LF/CRLF warnings only.
- 2026-05-11T23:04:47+08:00 - Context log split into index plus full archive.
  - `session-log.md` is now this lightweight index.
  - `session-log-full.md` preserves the previous complete log plus this maintenance note.
  - The active worktree context under `.worktrees/native-gamepad-cleanups/.agent-context/` should be kept aligned with the main workspace context while this branch is active.
- 2026-05-11T22:55:15+08:00 - Gamepad release-tail config and benchmark coverage committed.
  - Latest implementation commit: `9537921 Improve gamepad release tail and benchmark coverage`.
  - `body_lock_release_tail_scale` is exposed through `config.toml.example` and `config/loader.py`.
  - Benchmark/manual-mix aggregate reporting now includes coverage ratios for turn recovery, decel settle, and wrong-input recovery.
  - Verification before commit: `96` selected tests passed; `git diff --check` passed with LF/CRLF warnings only.
- 2026-05-11T20:30:40+08:00 - Native-vision and gamepad-controller review findings recorded.
  - Review found gamepad gain scaling ambiguity, no-target reset/recoil interaction risk, cue sidecar duplication, unused weapon-cache confidence, CUDA arch portability, CUDA event churn, stale docs, and source-label loss in a pybind helper.
  - Focused native/gamepad unittest suite passed `110` tests.
  - Broader offline checks exposed one gamepad hard-stop performance failure and one stale native/Python parity failure.

## Current Follow-Up

- Receive and evaluate the user's next idea against the new contract-hardened baseline.
- Run live `gamepad_start.bat` smoke when the user is ready: check neutral startup/exit, short target-loss no stale fire, semi-auto manual fire, and timing p95/max.
- Decide whether to stage/commit the current implementation and whether `.agent-context/` should be included or kept separate.
- Push/sync `dev` if remote state should include commits `542c3de`, `9e09a52`, `c38e129`, and any future hardening commit.
- Replay or capture crouch/prone/side benchmark material before expanding posture heuristics further.
- If latency still feels high, use the new source/native/handoff/consume/output metrics before discussing a controller-to-C++ rewrite.

## Full Archive Map

Use `session-log-full.md` for the full text of these entries:

- 2026-05-19 - Native/gamepad contract hardening implementation, verification, and context sync.
- 2026-05-18 - Multi-POV native/gamepad review and contract-hardening plan.
- 2026-05-12 - Auto-fire manual takeover; commit-discipline guidance; native vision target freshness; wide-low posture selector parity.
- 2026-05-11 - Gamepad release-tail, benchmark coverage, native/gamepad review findings, recoil_app handoff.
- 2026-05-05 - Native-only vision scope, upper-body regression coverage, external cue bridge, sidecar fallback, ROI-only color copy, same-target auto-fire fix.
- 2026-05-01 - Native hotpath review, article reviews, rollback to native pre-hotpath baseline, simplified native baseline, yellow-cue continuation hold.
- 2026-04-30 - Body-state v1, selector ego-warp continuity, native hotpath consolidation, COD22 yellow-dot mixed acquisition.
- 2026-04-29 - Runtime compatibility note and moving-POV research lead.
- 2026-04-28 - Mouse live-control fixes and tiered follow tuning.
- 2026-04-27 - Documentation consolidation and mouse startup-contract correction.
- 2026-04-22 - Native vision migration chain and controller C++ rewrite deferral.

## Relevant Decision Records

- `decisions/DEC-2026-04-22-001-native-vision-default-hybrid-runtime.md`
- `decisions/DEC-2026-04-22-002-defer-full-controller-cpp-rewrite.md`
- `decisions/DEC-2026-04-30-001-native-vision-dual-rate-warmscan-active-track.md`
- `decisions/DEC-2026-04-30-002-native-hotpath-consolidation-before-center-cue.md`
- `decisions/DEC-2026-04-30-003-cod22-yellow-dot-mixed-cue-acquisition.md`
- `decisions/DEC-2026-05-01-003-revert-default-native-runtime-to-708c253.md`
- `decisions/DEC-2026-05-01-004-simplify-native-baseline-remove-compensation-and-restore-gray-helpers.md`
- `decisions/DEC-2026-05-01-005-use-yellow-cue-as-short-continuation-hold.md`
- `decisions/DEC-2026-05-01-006-prioritize-native-hotpath-copy-reduction-over-full-controller-cpp-rewrite.md`
- `decisions/DEC-2026-05-05-001-add-external-yellow-cue-input-and-sidecar-fallback.md`
- `decisions/DEC-2026-05-05-002-scope-active-vision-work-to-native.md`

## Maintenance Notes

- Treat `session-log-full.md` as the append-only full archive.
- Keep this index short and current; update it when a new session materially changes the active objective, latest baseline, or next actions.
- Do not store secrets, tokens, cookies, keys, or personal data in either log.
- Do not use `.agent-context/` as a task ledger, scheduler, or external issue tracker.
