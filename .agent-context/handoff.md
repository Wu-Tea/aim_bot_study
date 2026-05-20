# Agent Handoff

Last updated: 2026-05-20T17:04:02+08:00
Updated by: Codex
Active scope: `recoil_app` record/replay reliability and gamepad recoil playback are implemented for offline/code verification. Runtime profile selection is now permissive for trial playback: a saved profile is used when `game`, `canonical_weapon_id`, `stance`, and `aim_mode` match.
Staleness: stale after a new live `recoil_app_start.bat` recording pass, calibration capture, `gamepad_start.bat` recoil smoke test, or further change to recoil profile/runtime/config semantics.

## Current Objective

Make recoil recording and playback usable end to end: record full-magazine evidence, audit profile quality, plot interpretable curves, dry-run replay output, and feed matching profiles into gamepad recoil compensation.

## Current State

- Branch/worktree: `dev` in `D:\work\AI\yolo-study-001`.
- The planned recoil reliability code path is implemented and covered by focused tests.
- Gamepad recoil runtime currently exposes three knobs:
  - `profile_amount`: normal recorded-profile strength for X and Y.
  - `profile_x_amount`: extra multiplier for recorded profile X deltas only.
  - `feedback_amount`: fixed fallback down-pull only when no matching profile is active.
- Runtime no longer blocks matching profiles because of support count, confidence, accepted episode count, vertical recovery/reversal, horizontal disagreement, missing calibration, or sidecar `degraded` status.
- Audit/status still reports those quality findings so bad recordings remain visible.
- Record mode keeps a separate clean-profile check so low-support profiles can still be supplemented by more recordings.
- Existing unrelated dirty workspace files remain outside the recoil scope; do not revert them without user approval.

## Key Files

- Runtime selection: `recoil_app/runtime.py`, `runtime/recoil_sidecar/service.py`, `controllers/gamepad_controller.py`
- Gamepad output: `controllers/gamepad/recoil_compensation.py`
- Config: `config/loader.py`
- Audit/readiness diagnostics: `vision/recoil_collection/audit.py`, `vision/recoil_collection/readiness.py`, `tools/audit_recoil_profiles.py`
- Dry-run replay: `tools/dry_run_recoil_playback.py`
- Validation docs: `docs/project/RECOIL_RECORD_REPLAY_VALIDATION.md`, `docs/project/GAMEPAD_OVERVIEW.md`
- Plan: `docs/superpowers/plans/2026-05-20-recoil-record-replay-reliability-plan.md`

## Verification Evidence

- Focused recoil/gamepad/config suite:
  - `py -3 -B -m unittest tests.recoil_collection.test_audit tests.recoil_collection.test_capture tests.recoil_collection.test_extraction tests.recoil_collection.test_readiness tests.recoil_collection.test_calibration tests.recoil_app.test_runtime tests.gamepad.test_gamepad_recoil_compensation tests.gamepad.test_gamepad_controller_host tests.runtime.test_recoil_sidecar_service tests.test_config_loader tests.recoil_collection.test_playback_dry_run -v`
  - Result: `132` tests OK.
- Syntax checks passed for touched recoil/runtime/gamepad/test modules.
- `py -3 -B tools\audit_recoil_profiles.py --profile-dir artifacts\recoil_profiles` runs and reports current local profiles as runtime-usable while preserving quality findings.
- `git diff --check` passed with LF/CRLF warnings only.

## Next Action

1. Restart `gamepad_start.bat` before live testing so the new runtime/config code is loaded.
2. Tune only `profile_amount`, `profile_x_amount`, and `feedback_amount`.
3. For trusted data, record three or more clean full magazines, run the audit tool, inspect `.recoil.png`, `.anti_recoil.png`, and `.timeline.png`, then add calibration if measured playback is needed.
4. Live smoke test should confirm logs select the matching profile instead of falling back for quality-warning reasons.

## Do Not Do

- Do not reintroduce `horizontal_profile_scale`, `feedback_x_amount`, `feedback_y_amount`, or gamepad recoil `amount` unless the user explicitly asks for a new design.
- Do not treat `feedback_amount` as active-profile X strength; it is fallback down-pull only.
- Do not reintroduce runtime profile-quality gates unless the user explicitly asks.
- Do not treat profiles with audit findings as clean recording evidence just because runtime trial playback allows them.
- Do not revert unrelated dirty workspace files.
