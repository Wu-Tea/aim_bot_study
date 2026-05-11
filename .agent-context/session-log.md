# Agent Session Log Index

Last updated: 2026-05-11T23:04:47+08:00
Updated by: Codex
Purpose: quick navigation for project continuity. The complete historical log is preserved in `session-log-full.md`.

## Reading Order

1. Read `handoff.md` for the current active objective and next action.
2. Read this index for the short map of recent context.
3. Open `session-log-full.md` only when detailed history is needed.
4. Open `decisions/` for durable architecture or scope decisions.

## Current Active Thread

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

- Run the controller benchmark again from commit `9537921` and compare against the current baseline plus recent local runs.
- Inspect coverage deltas before interpreting turn-recovery, decel-settle, or wrong-input-recovery changes as real improvements.
- Smoke-test `gamepad_start.bat` after config changes to confirm the default entry still loads the intended config path.
- If latency remains a concern, instrument the controller loop and native result handoff before considering any C++ controller migration.
- Keep high-feel gamepad tuning knobs easy to find near the top of config examples.

## Full Archive Map

Use `session-log-full.md` for the full text of these entries:

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
