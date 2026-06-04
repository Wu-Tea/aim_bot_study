# Agent Session Log Index

Last updated: 2026-06-04T23:42:57+08:00
Updated by: Codex
Purpose: quick navigation for project continuity. The complete historical log is preserved in `session-log-full.md`.

## Reading Order

1. Read `handoff.md` for the current active objective and next action.
2. Read this index for recent context.
3. Open `decisions/` for durable architecture or scope decisions.
4. Open `session-log-full.md` only when deeper history is needed.

## Current Active Thread

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
