# Agent Session Log Index

Last updated: 2026-05-19T21:01:32+08:00
Updated by: Codex
Purpose: quick navigation for project continuity. The complete historical log is preserved in `session-log-full.md`.

## Reading Order

1. Read `handoff.md` for the current active objective and next action.
2. Read this index for the short map of recent context.
3. Open `session-log-full.md` only when detailed history is needed.
4. Open `decisions/` for durable architecture or scope decisions.

## Current Active Thread

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
