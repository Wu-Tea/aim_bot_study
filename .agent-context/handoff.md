# Agent Handoff

Last updated: 2026-05-11T00:00:00+08:00
Updated by: Codex
Active scope: COD recoil_app stabilization and handoff
Staleness: stale after `recoil_app` is replaced by another recoil runtime, the user changes the capture/OCR strategy away from Y-switch text recognition, COD HUD coordinates are retuned with new real screenshots, or record/recoil mode semantics change.

## Current Objective

Stabilize and hand off the `recoil_app` path:

1. keep `recoil_app` as the primary recoil package with two modes: `record` and `recoil`
2. keep normal AI/gamepad runtime integration in-process through `GamepadRecoilBridge`; do not require a separate recoil sidecar process during normal play
3. keep `record` mode free of compensation output; it only recognizes weapons, learns curves, saves profiles, and writes debug plots
4. recognize weapons only from Y-switch OCR of the lower-right weapon-name text, not weapon images
5. prefer GPU-backed OCR and DXGI capture where available, but do not run heavy OCR/sample sweeps during handoff because the user's machine was being saturated

## Current State

The latest recoil direction is:

- `recoil_app_start.bat` is the user-facing launcher for standalone testing.
- `python -m recoil_app --game <cod20|cod21|cod22> --mode <record|recoil>` is the direct CLI.
- `recoil_app/runtime.py` owns the reusable runtime, identity store, memory-cached profile store, Y-switch OCR, record-mode learning, recoil-mode profile lookup, and the `GamepadRecoilBridge`.
- `controllers/gamepad_controller.py` imports the bridge only as a thin integration point; controller/gamepad code should not grow the OCR/learning logic further.
- New lightweight weapon identity files are written under `artifacts/recoil_app/weapons`.
- Recoil profiles remain under `artifacts/recoil_profiles`; plots go under `artifacts/recoil_plots`.
- `artifacts/weapon_examples/` contains user-provided screenshot fixtures for manual tuning and should not be committed.

Recent user-reported live issues and fixes:

- COD21 OCR was greedily merging ammo / numeric rows into weapon names, for example `点22塔恩托 40 999 ...`.
- COD20 OCR was reading ammo type instead of the weapon name, for example `9毫米鲁格手枪弹` and `7.62BLK`.
- `vision/weapon_identity/adapters.py` was retuned to move COD20/COD21 name ROIs upward and away from ammo rows.
- `recoil_app/runtime.py` now rejects obvious ammo labels and overlong cross-line OCR garbage when selecting weapon names.
- `vision/weapon_identity/text.py` now tries RapidOCR with CUDA by default, with `RECOIL_OCR_PROVIDER=dml|cpu` overrides.
- `recoil_app/runtime.py` now prefers DXGI capture and only falls back to `PIL.ImageGrab`.
- `recoil_app/console.py` now polls at 60 Hz instead of 200 Hz; `record` mode collector defaults to 60 FPS instead of 100 FPS.

## Verification

Because the user reported CPU saturation and Codex UI freezes from repeated local OCR sweeps, do not run heavy Python OCR/capture replay loops by default.

Last safe verification style for the next session:

- lightweight static commands are acceptable
- `python -m py_compile ...` may be acceptable if the user asks, but avoid OCR workloads unless explicitly approved
- do not run broad image OCR sweeps over `artifacts/weapon_examples` without user permission
- before any live retest, ensure no stale Python process is still running

The most recent in-conversation edits after the last heavy verification were not fully re-run by request. Treat the current state as implemented but needing a user live smoke test.

## Next Action

1. Ask the user to fully restart `recoil_app_start.bat` before retesting because old `switch_cache` values remain in process memory.
2. Retest COD21 Y-switch recognition and check whether names stay like `点22塔恩托` instead of absorbing numeric rows.
3. Retest COD20 Y-switch recognition and check whether names stay on weapon names instead of ammo labels.
4. If OCR is still wrong, add a cheap one-frame debug crop dump on Y press instead of running local OCR sweeps.
5. If CPU remains high, next cuts should be:
   - disable multi-pass OCR by default in live mode
   - reduce record-mode center capture size
   - only OCR one delayed frame unless the name is invalid
   - keep OCR provider on CUDA or DML and log the chosen provider

## Blockers

- No trusted live smoke result exists after the latest COD20/COD21 ROI and GPU/DXGI changes.
- The user's machine was being saturated by local Python OCR/screenshot tests; future agents must avoid repeating heavy sweeps without permission.
- Existing bad lightweight identity records may remain under `artifacts/recoil_app/weapons` from earlier misreads; if the same bad weapon appears from `switch_cache`, delete the bad identity file and restart.

## Files To Read First

- `recoil_app/runtime.py`
- `recoil_app/console.py`
- `recoil_app_start.bat`
- `vision/weapon_identity/adapters.py`
- `vision/weapon_identity/text.py`
- `controllers/gamepad_controller.py`
- `controllers/gamepad/physical_input.py`
- `docs/superpowers/specs/2026-05-06-recoil-app-autolearn-design.md`
- `docs/superpowers/plans/2026-05-06-recoil-app-modes-plan.md`
- `docs/superpowers/plans/2026-05-11-recoil-app-stabilization-handoff.md`

## Do Not Do

- Do not commit `artifacts/weapon_examples/`.
- Do not run local CPU-heavy OCR sweeps over all examples without explicit user permission.
- Do not revive the old manual-first `recoil_toolkit` workflow as the main path.
- Do not add more OCR/learning logic into `controllers/` or the main vision hot path; keep it in `recoil_app`.
- Do not apply compensation in `record` mode.

## Related Context

- Older native-vision context is still present in decision records and session log, but this handoff is now scoped to recoil_app.
- The native vision policy remains: normal vision work should stay native unless the user explicitly reopens Python vision parity.
