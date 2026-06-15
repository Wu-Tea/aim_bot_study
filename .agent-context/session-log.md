# Agent Session Log Index

Last updated: 2026-06-15T16:35:00+08:00
Updated by: Codex
Purpose: quick navigation for project continuity. The complete historical log is preserved in `session-log-full.md`.

## Reading Order

1. Read `handoff.md` for the current active objective and next action.
2. Read this index for recent context.
3. Open `decisions/` for durable architecture or scope decisions.
4. Open `session-log-full.md` only when deeper history is needed.

## Current Active Thread

- 2026-06-15T16:35:00+08:00 - Native tracker/controller/recoil boundary contract implemented and documented.
  - User goal: make `vision -> tracker -> controller -> recoil` directly verifiable so controller changes stop breaking recoil feel.
  - Recoil boundary: native recoil is now final feed-forward playback and must not consume target dx/dy, tracker state, target freshness, or controller correction errors.
  - Tracker boundary: tracker receives component-aware final camera motion for ego projection while manual/assist/dynamics/recoil/final output components remain separately attributed.
  - Removed the current runtime contract around recoil target-direction yield and old pre/post recoil tracker toggles; future changes must not reintroduce them without evidence and focused native contract tests.
  - Added/updated native tests for recoil input isolation, deterministic recoil playback without controller target state, component-aware final tracker motion, and recoil as final independent component.
  - Added `scripts\verify\native_pipeline_contract.bat` / `.ps1`; the script rejects known recoil/controller coupling patterns, builds native controller/runtime/benchmark targets, runs native controller tests, checks runtime `tracker_motion=component_aware_final`, and runs a short benchmark smoke.
  - `tools\check_native_cpp_gamepad_runtime.ps1` now calls the core pipeline contract by default; pass `-SkipPipelineContract` only when isolating launcher/scaffold checks.
  - Verification during this work: `scripts\verify\native_pipeline_contract.bat` PASS and `powershell -ExecutionPolicy Bypass -File tools\check_native_cpp_gamepad_runtime.ps1 -SkipPythonTests` PASS. Live gameplay recoil feel still requires user validation.
- 2026-06-04T23:42:57+08:00 - Accepted next controller-feel direction: recoil despike plus aim-assist dynamics.
  - User clarified that the shaky feel mainly appears on weapons with recoil enabled, and that current recoil parameters were tuned carefully; the goal is to remove curve spikes/micro-jitter without changing overall recoil strength, timing, or feel.
  - Accepted direction: at recoil profile read/activation time, generate a conservative despiked playback cache from profile deltas. Do not overwrite original recoil files, and do not apply broad low-pass smoothing that would soften the whole weapon curve.
  - Accepted direction: add an `AimAssistDynamicsPlugin` in the plugin list after `AIAimPlugin` and before recoil playback. It should smooth only AI assist delta (`output.right_stick - frame.manual_right_stick`) so manual input remains immediate and recoil output is not delayed.
  - Decision recorded: `decisions/DEC-2026-06-04-001-recoil-despike-and-assist-dynamics.md`.
- 2026-05-30T17:43:18+08:00 - Live-tested targeting/controller upgrade checkpointed as a version.
  - User live-tested the current weak-association/source-aware controller changes and reported the effect felt very strong, possibly too strong, but good enough to commit as a version.
  - Search path recorded: high-FPS detection was insufficient in practice; `deep-research-report (14).md` pointed toward detector-led short-horizon continuity; subagents split native selector, controller motion/projection, and contract/safety; GitHub/open-source comparison showed most FPS YOLO projects stay shallower; final direction became active-only weak association plus explicit target authority.
  - Added/verified source-aware controller behavior, auto-fire aim-readiness settling, weak/cue force scaling, native low-score continuation, yellow cue hold, and chest-biased `0.43` aim point.
  - Verification before commit: native build OK, 222 broader native/controller tests OK, 128 related targeting/controller/config tests OK, py_compile OK, `git diff --check` OK with LF/CRLF warnings only.
- 2026-05-29T15:25:24+08:00 - Single-target weak association and authority gating implemented in working tree.
  - Added native/Python authority fields, runner/controller fail-closed fire gates, and aim-authority target clearing.
  - Added native active-only low-score continuation with `associated_weak` output and no fire authority.
  - Kept yellow cue as auxiliary `cue_hold` continuation with no fire authority.
  - Made gamepad controller source-aware: weak/low-score does not refresh projection velocity; weak association cannot trigger ADS snap; weak/cue body-lock is lighter; predicted/no-authority targets stay manual.
  - Added config knobs for weak/cue body-lock force scale.
  - Verification: native build OK, 204 targeted regression tests OK, py_compile OK, `git diff --check` OK with LF/CRLF warnings only.
  - Worktree is dirty and not yet committed.

## Recent Implementation Baseline

- 2026-05-29 - `a196f92 Improve gamepad body-lock lateral hold`.
  - Added body-lock near-lock lateral motion assist for gamepad aiming.
  - Validation before commit: 209 gamepad tests OK, focused config/gamepad tests OK, py_compile OK, and benchmark deltas did not regress.
- 2026-05-29T14:50:45+08:00 - Targeting research and subagent review consolidated.
  - User provided `D:/Downloads/deep-research-report (14).md`.
  - Three subagents reviewed native selector, controller motion/projection, and contract/safety.
  - Decision recorded: `decisions/DEC-2026-05-29-001-single-target-weak-association-authority-gating.md`.
- 2026-05-23 - Recoil profile playback lead trial.
  - Added `[gamepad.recoil].profile_lead_ms` and dry-run/config/test coverage.
  - Live tuning of `profile_lead_ms` remained pending.
- 2026-05-19 - Native/gamepad contract hardening implemented.
  - Added atomic controller vision-state submission, source freshness, neutral gamepad shutdown, timing metrics, explicit native empty-gap clearing, config/docs cleanup, and benchmark config snapshots.
  - Split targeted verification passed; full unittest discovery timed out in this environment.

## Current Follow-Up

- Treat the committed native targeting/controller upgrade as the current live baseline; current target point is `body_lock_upper_body_ratio = 0.40`.
- Next implementation focus:
  - recoil playback despike at profile read/activation time, preserving tuned recoil feel
  - `AimAssistDynamicsPlugin` after `AIAimPlugin`, smoothing only AI assist delta and not manual input or recoil
- If targeting feels too strong later, tune weak/cue force scales and native weak association gates separately from recoil/assist smoothing.
- Longer next step after this commit: add richer replay/benchmark logging for source/tier, weak gate counts, cue age/score, fire request vs gate result, and controller final output.

- 2026-06-14T21:50:00+08:00 - User-confirmed C++ testing rule and next controller-mixing direction.
  - User requested recording in Agent Context Sync that C++ changes should include unit tests while modifying behavior.
  - Durable working rule: C++ controller/runtime behavior edits should add or update focused native unit tests in the same change.
  - User hypothesis for current ADS overpull: the issue is likely mixed user input arbitration, not simply ADS force being too high.
  - Next controller-feel direction: keep or increase ADS/body-lock correction strength where useful, but improve mixed-input adjudication so manual input that bends away from the AI correction is partially suppressed while helpful/aligned input is preserved.
- 2026-06-16T00:58:00+08:00 - Recoil playback feel restored after cumulative-Y regression.
  - Current structure still keeps recoil independent from tracker/controller target feedback; `NativeRecoilInput` must not grow target dx/dy/freshness fields.
  - Adapted the dev-branch recoil presentation by using per-sample Y profile delta for uncalibrated playback, instead of cumulative Y from fire start.
  - Restored live fallback feedback to a constant 30% down-pull in config.
  - Added native tests for recoil target-input exclusion, deterministic profile playback, constant fallback over 500ms+, and per-sample delta playback.
  - Verification: `cod_native_controller_tests`, `scripts\verify\native_pipeline_contract.bat`, and `git diff --check` passed.

## Full Archive Map

Use `session-log-full.md` for the full text or compact summaries of these ranges:

- 2026-05-29 - Targeting research/subagent consolidation, decision, and current weak-association implementation.
- 2026-05-20 to 2026-05-23 - Recoil runtime/profile playback follow-ups.
- 2026-05-19 - Native/gamepad contract hardening implementation and verification.
- 2026-05-18 - Multi-POV native/gamepad review and hardening plan.
- 2026-05-12 - Auto-fire manual takeover, commit discipline, native target freshness, and wide-low posture selector parity.
- 2026-05-11 - Gamepad release-tail, benchmark coverage, native/gamepad review findings, recoil app handoff.
- 2026-05-05 - Native-only vision scope, upper-body regression coverage, external cue bridge, sidecar fallback, ROI-only color copy, same-target auto-fire fix.
- 2026-05-01 - Native hotpath article reviews, rollback to native baseline, simplified native baseline, yellow-cue continuation hold.
- 2026-04-30 - Body-state v1, selector ego-warp continuity, native hotpath consolidation, COD22 yellow-dot mixed acquisition.
- 2026-04-22 to 2026-04-29 - Native vision migration chain, controller C++ rewrite deferral, and mouse live-control fixes.

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
- `decisions/DEC-2026-05-29-001-single-target-weak-association-authority-gating.md`

## Maintenance Notes

- Treat `session-log-full.md` as the append-only full archive.
- Keep this index short and current.
- Do not store secrets, tokens, cookies, keys, or unnecessary personal data.
- Do not use `.agent-context/` as a task ledger, scheduler, or external issue tracker.
